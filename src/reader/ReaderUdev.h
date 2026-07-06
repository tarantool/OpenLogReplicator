/* Reader for direct block device access to ASM files
   Copyright (C) 2025 OpenLogReplicator

This file is part of OpenLogReplicator.

OpenLogReplicator is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as published
by the Free Software Foundation; either version 3, or (at your option)
any later version.

OpenLogReplicator is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenLogReplicator; see the file LICENSE;  If not see
<http://www.gnu.org/licenses/>.  */

#ifndef READERUDEV_H_
#define READERUDEV_H_

#include "Reader.h"
#include "../replicator/Replicator.h"
#include "../replicator/DatabaseConnection.h"
#include "../replicator/DatabaseEnvironment.h"
#include "../replicator/DatabaseStatement.h"

#include <vector>
#include <map>
#include <set>
#include <string>
#include <fcntl.h>

namespace OpenLogReplicator {

    /**
    * @brief Single extent entry mapping logical file extent to physical disk location.
    *
    * ASM stores files in Allocation Units (AU) that are striped across multiple disks.
    * This structure represents a single extent: which disk contains the data (diskNumber)
    * and at which AU offset on that disk (auNumber).
    */
    // Single extent entry: (disk_number, au_number_on_disk)
    struct AsmExtent {
        uint16_t diskNumber;
        uint32_t auNumber;  // Physical AU number on this disk (from x$kffxp.au_kffxp)
    };

    /**
     * @brief Represents opened disk handle for direct block device access.
     *
     * Contains the file path to the block device and the file descriptor
     * used for pread operations.
     */
    struct AsmDisk {
        std::string path;
        int fd;
    };

    /**
     * @brief Reader implementation for direct block device access to Oracle ASM files.
     *
     * ReaderUdev provides an alternative to ReaderASM for reading Oracle redo log files stored in ASM (Automatic
     * Storage Management). Instead of using the Oracle Call Interface (OCI) and dbms_diskgroup package, this reader
     * accesses ASM disks directly at the block device level using standard POSIX file operations with O_DIRECT flag.
     *
     * This approach offers several advantages:
     * - Bypasses Oracle's ASM instance for read operations, potentially improving performance
     * - Eliminates dependency on dbms_diskgroup package availability
     * - Provides direct control over I/O operations and buffering
     * - Enables reading ASM files even when ASM instance is under heavy load
     *
     * The reader works by:
     * 1. Querying ASM metadata views (v$asm_diskgroup, v$asm_disk, v$asm_file, v$asm_alias) to identify the disk
     *    group, file number, and allocation unit size
     * 2. Loading the extent map from x$kffxp to determine physical disk locations for each logical extent of the file
     * 3. Opening the underlying block devices (/dev/sdX, etc.) with O_DIRECT flag for
     *    unbuffered I/O
     * 4. Translating file offsets to physical disk locations using the extent map and reading data via pread system
     *    calls
     *
     * ASM File Layout:
     * Files in ASM are divided into Allocation Units (AU), typically 1MB or 4MB in size. These AUs are distributed
     * across disks in the diskgroup. The extentMap stores the physical location of each logical AU:
     * - Logical extent N at file offset (N * auSize) maps to extentMap[N]
     * - extentMap[N].diskNumber identifies which disk contains this AU
     * - extentMap[N].auNumber is the physical AU number on that disk
     * - Physical byte offset = auNumber * auSize + offset_within_AU
     *
     * Supported disk path formats:
     * - Standard block devices: /dev/sdb1, /dev/oracleasm/disks/DATA01, etc.
     * - AFD (ASM Filter Driver) paths: AFD:LABEL is resolved via afdtool or fallback to /dev/oracleafd/disks/LABEL
     *
     * Usage context:
     * ReaderUdev is instantiated by ReplicatorOnlineASM when the useUdev flag is set to true. This is typically
     * configured when the user wants to bypass OCI-based ASM access in favor of direct block device reads. The reader
     * is created in ReplicatorOnlineASM::readerCreate() and managed by the replicator's threading system.
     *
     * Thread safety:
     * The class relies on connASMMetaMutex in ReplicatorOnlineASM for thread-safe access to the metadata database
     * connection, as OCI connections are not thread-safe by default.
     *
     * Header fixing:
     * For certain file types (ONLINELOG, DATAFILE, TEMPFILE, CONTROLFILE, ARCHIVELOG), the reader performs header
     * block checksum correction. ASM stores files with a different header format, so the fixHeaderBlock method applies
     * a magic XOR value to make the header compatible with standard Oracle redo log processing.
     */
    class ReaderUdev final : public Reader {
    protected:
        /**
          * @brief Replicator that owns this reader and provides database connections.
          */
        Replicator* replicator;

        /**
         * @brief ASM file type as returned from v$asm_file.type (ONLINELOG, DATAFILE, etc.)
         */
        std::string fileType;

        /**
         * @brief ASM extent map: for each logical extent, the list of mirror copies (primary first).
         *
         * extentMap[i] holds all copies of logical extent i, ordered by lxn_kffxp (index 0 = primary).
         * readDirect() tries copies in order and falls back to the next one on pread error, so a single
         * failed disk does not stop replication as long as at least one mirror copy is reachable.
         * For EXTERNAL redundancy diskgroups each inner vector has exactly one entry.
         */
        std::vector<std::vector<AsmExtent>> extentMap;

        /**
         * @brief Map of opened disk handles, where keys are disk_number and values are AsmDisk mapping.
         *
         * Only disks that actually contain data for this file are opened. The map is populated during redoOpen()
         * based on unique disk numbers found in extentMap, and disk paths are obtained from v$asm_disk.
         */
        std::map<uint16_t, AsmDisk> diskHandles;

        // Disks that returned an I/O error; skipped in readDirect so we don't retry a known-bad copy every read.
        std::set<uint16_t> deadDisks;

        /**
         * @brief Allocation Unit size in bytes (got from v$asm_diskgroup).
         *
         * Typical values are 1MiB (1048576) or 4MiB (4194304). This determines how file offsets are translated to
         * physical disk locations.
         */
        uint32_t auSize;

        /**
         * @brief ASM disk group number containing the file.
         */
        uint32_t groupNumber;

        /**
         * @brief ASM file number within the disk group.
         */
        uint32_t fileNumber;

        /**
         * @brief File size in blocks (from v$asm_file.blocks).
         */
        uint64_t fileSizeRaw;

        /**
         * @brief Maximum read size in bytes (4MB).
         *
         * Used for limiting read operations to improve performance and memory usage.
         */
        static constexpr uint64_t MAX_READ_SIZE = 4 * 1024 * 1024;

        /**
         * @brief Buffer size for reading afdtool output.
         *
         * Must be large enough to hold one line of afdtool -getdevlist output,
         * which includes label and device path. 512 bytes is sufficient for
         * typical paths like "/dev/rdsk/c1d208s0" even with longer labels.
         */
        static constexpr size_t AFD_TOOL_BUFFER_SIZE = 512;

        /**
         * @brief Magic XOR constant for fixing ASM file header checksum.
         *
         * ASM stores certain file types with different header format. This constant
         * is used to fix the checksum and magic bytes for compatibility with
         * standard Oracle redo log processing.
         */
        static constexpr uint32_t MAGIC_XOR = 0x000081a0;

        /**
         * @brief Closes all open disk file descriptors and clears extent map.
         */
        void redoClose() override;

        /**
         * @brief Opens the ASM file by loading metadata and opening disk devices.
         *
         * Parses the ASM filename (+DGNAME/path/to/file), queries metadata from
         * v$asm_diskgroup, v$asm_file, and v$asm_alias, loads the extent map from
         * x$kffxp, and opens the underlying block devices for direct I/O.
         */
        REDO_CODE redoOpen() override;

        /**
         * @brief Reads data from the ASM file at the specified offset.
         *
         * Translates the logical file offset to physical disk location using
         * extentMap, then reads data via pread from the appropriate disk.
         * Handles cross-AU reads that may span multiple physical disks.
         */
        int redoRead(uint8_t* buf, uint64_t offset, uint size) override;

        /**
         * @brief Calculates the next read size based on previous read amount.
         *
         * Implements exponential growth of read size up to 4MB maximum for
         * better sequential read performance.
         */
        uint readSize(uint prevRead) override;
        REDO_CODE reloadHeaderRead() override;

        // Helper methods

        /**
         * @brief Loads the extent map from x$kffxp table.
         *
         * Primary extents only (lxn_kffxp = 0); if a primary disk is unreachable, reads fail —
         * restart from checkpoint or fall back to ReaderASM.
         */
        bool loadExtentMap();

        /**
         * @brief Opens block devices for all disks used by this file.
         *
         * Queries v$asm_disk to get disk paths, resolves AFD paths if needed,
         * and opens each disk with O_DIRECT flag for unbuffered I/O.
         */
        bool openDisks();

        /**
         * @brief Performs direct I/O read from ASM disks.
         *
         * Core reading implementation that translates file offsets to physical
         * disk locations using extentMap and reads data using pread system calls.
         */
        int readDirect(uint8_t* buf, uint64_t offset, uint size);

        /**
         * @brief Fixes ASM file header checksum for Oracle compatibility.
         *
         * ASM stores certain file types with different header format. This method
         * applies a magic XOR constant to fix the checksum and magic bytes so
         * the header is compatible with standard Oracle redo log processing.
         */
        void fixHeaderBlock(uint8_t* buf, uint64_t offset, uint bytesRead) const;

        /**
         * @brief Resolves AFD (ASM Filter Driver) device names to system paths.
         *
         * Executes afdtool to get the mapping between AFD labels and actual
         * block device paths. Used when v$asm_disk returns paths starting with "AFD:".
         */
        static bool getAfdDeviceMapping(std::map<std::string, std::string>& afdMap);

    public:
        ReaderUdev(Ctx* newCtx, std::string newAlias, Replicator* replicator,
                   std::string newDatabase, int newGroup, bool newConfiguredBlockSum);
        ~ReaderUdev() override;

        /**
         * @brief Displays a hint message when disk access fails.
         *
         * Provides guidance to check ASM disk access permissions when the reader
         * encounters errors reading from block devices.
         */
        void showHint(Thread* t, std::string origPath, std::string mappedPath) const override;
    };
}

#endif
