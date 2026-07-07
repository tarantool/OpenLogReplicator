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

#include <string>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <array>
#include <dirent.h>
#include <cstdlib>
#include <memory>
#include <map>
#include <sstream>
#include <set>
#include <mutex>
#include <limits>

#ifdef __linux__
#ifndef O_DIRECT
#define O_DIRECT 040000
#endif
#endif

// macOS doesn't have O_DIRECT, use 0 instead
#ifdef __APPLE__
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#endif

// Solaris doesn't have O_DIRECT
#ifdef __sun
#ifndef O_DIRECT
#define O_DIRECT 0
#endif
#endif

#include "AsmFileName.h"
#include "ReaderASMBlockDevice.h"
#include "../replicator/ReplicatorOnlineASM.h"
#include "../common/exception/RuntimeException.h"
#include "../common/Clock.h"
#include "../common/Ctx.h"
#include "../common/metrics/Metrics.h"

namespace OpenLogReplicator {

    // SQL queries for ASM metadata
    // Returns all mirror copies (lxn_kffxp = 0 primary, 1..N mirrors); grouped by xnum_kffxp in loadExtentMap.
    static constexpr const char* SQL_QUERY_EXTENT_MAP =
        "SELECT x.xnum_kffxp, x.disk_kffxp, x.au_kffxp "
        "FROM x$kffxp x "
        "WHERE x.group_kffxp = :1 "
        "AND x.number_kffxp = :2 "
        "AND x.xnum_kffxp != 2147483648 "
        "ORDER BY x.xnum_kffxp, x.lxn_kffxp";

    static constexpr const char* SQL_QUERY_ASM_DISK =
        "SELECT disk_number, path FROM v$asm_disk WHERE group_number = :1";

    // Resolve by file number (unique within a disk group); used for fully-qualified system names.
    static constexpr const char* SQL_QUERY_ASM_METADATA_BY_FILENUM =
        "SELECT f.type, f.blocks, f.bytes, g.allocation_unit_size, g.group_number, f.file_number "
        "FROM v$asm_file f "
        "JOIN v$asm_diskgroup g ON f.group_number = g.group_number "
        "WHERE g.name = :1 AND f.file_number = :2";

    // Fallback for user aliases: match by name (only the last path component, may collide across dirs).
    static constexpr const char* SQL_QUERY_ASM_METADATA =
        "SELECT f.type, f.blocks, f.bytes, g.allocation_unit_size, g.group_number, a.file_number "
        "FROM v$asm_file f "
        "JOIN v$asm_diskgroup g ON f.group_number = g.group_number "
        "JOIN v$asm_alias a ON a.group_number = f.group_number AND a.file_number = f.file_number "
        "WHERE g.name = :1 AND a.name = :2 AND a.alias_directory = 'N'";

    // Helper function to trim whitespace from both ends of a string
    static void trim(std::string& str) {
        while (!str.empty() && (str.back() == ' ' || str.back() == '\t' || str.back() == '\n' || str.back() == '\r')) {
            str.pop_back();
        }
        while (!str.empty() && (str.front() == ' ' || str.front() == '\t')) {
            str.erase(0, 1);
        }
    }

    // Parse afdtool output line: "LABEL /dev/sdX" format
    static bool parseAfdLine(const std::string& line, std::string& label, std::string& path) {
        const size_t firstSpace = line.find(' ');
        if (firstSpace == std::string::npos) {
            return false;
        }
        label = line.substr(0, firstSpace);
        path = line.substr(firstSpace + 1);
        trim(label);
        trim(path);
        return !label.empty() && !path.empty();
    }

    ReaderASMBlockDevice::ReaderASMBlockDevice(Ctx* newCtx, std::string newAlias, Replicator* replicator,
                           std::string newDatabase, const int newGroup, const bool newConfiguredBlockSum) :
        Reader(newCtx, std::move(newAlias), std::move(newDatabase), newGroup, newConfiguredBlockSum),
        replicator(replicator),
        auSize(0),
        groupNumber(0),
        fileNumber(0),
        fileSizeRaw(0) {
    }

    ReaderASMBlockDevice::~ReaderASMBlockDevice() {
        ReaderASMBlockDevice::redoClose();
    }

    void ReaderASMBlockDevice::redoClose() {
        // Close all disk handles
        for (auto &[dksNumber, asmDisk] : diskHandles) {
            if (asmDisk.fd >= 0) {
                ::close(asmDisk.fd);
                asmDisk.fd = -1;
            }
        }
        diskHandles.clear();
        extentMap.clear();
        deadDisks.clear();
    }

    bool ReaderASMBlockDevice::getAfdDeviceMapping(std::map<std::string, std::string>& afdMap) {
        const std::unique_ptr<FILE, decltype(&pclose)> pipe(popen("afdtool -getdevlist 2>/dev/null", "r"), pclose);
        if (!pipe) {
            return false;
        }

        afdMap.clear();
        int lineCount = 0;
        while (true) {
            std::array<char, AFD_TOOL_BUFFER_SIZE> buffer{};
            if (fgets(buffer.data(), buffer.size(), pipe.get()) == nullptr) {
                break;
            }
            lineCount++;
            if (lineCount <= 3) {
                continue; // Skip header lines
            }

            std::string line = buffer.data();
            trim(line);
            if (line.empty()) {
                continue;
            }

            // Parse line: typically "LABEL /dev/sdX" format
            std::string label, path;
            if (parseAfdLine(line, label, path)) {
                afdMap[label] = path;
            }
        }
        return !afdMap.empty();
    }

    bool ReaderASMBlockDevice::loadExtentMap() {
        try {
            const auto* replicatorOnlineAsm = dynamic_cast<ReplicatorOnlineASM *>(replicator);

            ctx->info(0, "[ReaderASMBlockDevice] Loading extent map for group_number=" + std::to_string(groupNumber) +
                     ", file_number=" + std::to_string(fileNumber));

            {
                DatabaseStatement stmt(replicatorOnlineAsm->connASMMeta);
                stmt.createStatement(SQL_QUERY_EXTENT_MAP);
                stmt.bindInt(1, groupNumber);
                stmt.bindInt(2, fileNumber);

                uint32_t xnum = 0;
                uint16_t diskNum = 0;
                uint32_t auNum = 0;
                stmt.defineUInt(1, xnum);
                stmt.defineUInt(2, diskNum);
                stmt.defineUInt(3, auNum);

                extentMap.clear();
                uint32_t prevXnum = std::numeric_limits<uint32_t>::max();
                int ret = stmt.executeQuery();
                while (ret == 1) {
                    if (xnum != prevXnum) {
                        extentMap.emplace_back();
                        prevXnum = xnum;
                    }
                    extentMap.back().push_back({diskNum, auNum});
                    ret = stmt.next();
                }
            }

            ctx->info(0, "Loaded " + std::to_string(extentMap.size()) + " extents for " + fileName);

            if (extentMap.empty()) {
                ctx->error(0, "No extents found for file: " + fileName);
                return false;
            }

            return true;

        } catch (RuntimeException& ex) {
            ctx->error(ex.code, "Failed to load extent map: " + ex.msg);
            return false;
        }
    }

    bool ReaderASMBlockDevice::openDisks() {
        try {
            auto* replicatorOnlineAsm = dynamic_cast<ReplicatorOnlineASM *>(replicator);

            ctx->info(0, "[ReaderASMBlockDevice] openDisks() starting for group_number=" + std::to_string(groupNumber));

            // Get disk paths from v$asm_disk
            std::map<uint16_t, std::string> diskPaths;
            {
                ctx->info(0, "[ReaderASMBlockDevice] Querying v$asm_disk for group_number=" + std::to_string(groupNumber));
                DatabaseStatement stmt(replicatorOnlineAsm->connASMMeta);
                stmt.createStatement(SQL_QUERY_ASM_DISK);
                stmt.bindInt(1, groupNumber);

                uint16_t diskNum = 0;
                char pathBuf[512];
                stmt.defineUInt(1, diskNum);
                stmt.defineString(2, pathBuf, sizeof(pathBuf));

                ctx->info(0, "[ReaderASMBlockDevice] Executing v$asm_disk query...");
                int ret = stmt.executeQuery();
                ctx->info(0, "[ReaderASMBlockDevice] v$asm_disk query executed, loading results...");
                while (ret == 1) {
                    diskPaths[diskNum] = std::string(pathBuf);
                    ret = stmt.next();
                }
                ctx->info(0, "[ReaderASMBlockDevice] v$asm_disk loaded " + std::to_string(diskPaths.size()) + " disks");
            }

            // Collect unique disk numbers used by this file.
            // extentMap contains one entry per AU (thousands of extents), but we only need
            // unique disk numbers to avoid opening the same disk multiple times.
            // diskPaths contains ALL disks in the diskgroup (could be 10-50), but we only
            // want to open disks that actually store data for this specific file.
            std::set<uint16_t> usedDisks;
            for (const auto& copies : extentMap) {
                for (const auto& extent : copies) {
                    usedDisks.insert(extent.diskNumber);
                }
            }

            // Handle AFD path resolution if any disk path starts with "AFD:"
            std::map<std::string, std::string> afdMap;
            bool hasAfdPath = false;
            for (const auto& pair : diskPaths) {
                if (pair.second.rfind("AFD:", 0) == 0) {
                    hasAfdPath = true;
                    break;
                }
            }

            if (hasAfdPath) {
                if (!getAfdDeviceMapping(afdMap)) {
                    ctx->warning(0, "Failed to get AFD device mapping, AFD disks may not be accessible");
                }
            }

            for (uint16_t diskNum : usedDisks) {
                auto it = diskPaths.find(diskNum);
                if (it == diskPaths.end()) {
                    ctx->error(0, "Disk " + std::to_string(diskNum) + " not found in v$asm_disk");
                    return false;
                }

                std::string path = it->second;

                // Handle AFD paths (AFD:diskname)
                if (path.rfind("AFD:", 0) == 0) {
                    std::string afdName = path.substr(4);
                    // Trim whitespace
                    while (!afdName.empty() && afdName.back() == ' ') afdName.pop_back();

                    // Look up the real device path using afdtool mapping
                    if (auto afdIt = afdMap.find(afdName);
                        afdIt != afdMap.end()) {
                        path = afdIt->second;
                    } else {
                        // Fallback: try to construct path assuming simple AFD naming
                        path = "/dev/oracleafd/disks/" + afdName;
                        ctx->warning(0, "AFD disk " + afdName + " not in mapping, trying fallback path: " + path);
                    }
                }

                // Try to open the block device with O_DIRECT
                int fd = open(path.c_str(), O_RDONLY | O_DIRECT);
                if (fd < 0) {
                    // Try without O_DIRECT
                    fd = open(path.c_str(), O_RDONLY);
                    if (fd < 0) {
                        ctx->error(0, "Failed to open disk " + path + ": " + strerror(errno));
                        return false;
                    }
                    ctx->warning(0, "Opened disk " + path + " without O_DIRECT");
                }

                diskHandles[diskNum] = {path, fd};
                ctx->info(0, "Opened disk " + std::to_string(diskNum) + " at " + path);
            }

            return true;

        } catch (RuntimeException& ex) {
            ctx->error(ex.code, "Failed to open disks: " + ex.msg);
            return false;
        }
    }

    Reader::REDO_CODE ReaderASMBlockDevice::redoOpen() {
        try {
            blockSize = 0;

            // Parse filename: +DGNAME/path/to/file
            const size_t firstSlash = fileName.find('/', 1);
            if (firstSlash == std::string::npos) {
                ctx->error(0, "Invalid ASM filename format: " + fileName);
                return REDO_CODE::ERROR;
            }

            const std::string diskGroupName = fileName.substr(1, firstSlash - 1);
            const std::string filePath = fileName.substr(firstSlash + 1);

            const size_t lastSlash = filePath.rfind('/');
            const std::string fileNameOnly = (lastSlash == std::string::npos) ? filePath : filePath.substr(lastSlash + 1);

            // Prefer resolving by embedded file# (unambiguous); aliases fall back to name matching below.
            uint32_t parsedFileNumber = 0;
            const bool haveFileNumber = parseAsmFileNumber(fileNameOnly, parsedFileNumber);

            ctx->info(0, "[ReaderASMBlockDevice] Parsed diskGroupName='" + diskGroupName + "', filePath='" + filePath +
                     "', fileNameOnly='" + fileNameOnly + "', fileNumber=" +
                     (haveFileNumber ? std::to_string(parsedFileNumber) : std::string("(alias)")));

            auto* replicatorOnlineAsm = dynamic_cast<ReplicatorOnlineASM *>(replicator);

            // Lock mutex for thread-safe access to connASMMeta (OCI is not thread-safe)
            std::lock_guard lock(replicatorOnlineAsm->connASMMetaMutex);

            // Get file attributes, group_number and file_number in one query
            {
                ctx->info(0, "[ReaderASMBlockDevice] Querying ASM metadata for: " + fileName);
                DatabaseStatement stmt(replicatorOnlineAsm->connASMMeta);
                if (haveFileNumber) {
                    stmt.createStatement(SQL_QUERY_ASM_METADATA_BY_FILENUM);
                    stmt.bindString(1, diskGroupName);
                    stmt.bindUInt(2, parsedFileNumber);
                } else {
                    stmt.createStatement(SQL_QUERY_ASM_METADATA);
                    stmt.bindString(1, diskGroupName);
                    stmt.bindString(2, fileNameOnly);
                }
                char fileTypeBuf[32];
                stmt.defineString(1, fileTypeBuf, sizeof(fileTypeBuf));
                stmt.defineUInt(2, fileSizeRaw);  // blocks
                stmt.defineUInt(3, fileSize);     // bytes
                stmt.defineUInt(4, auSize);
                stmt.defineUInt(5, groupNumber);
                stmt.defineUInt(6, fileNumber);
                if (stmt.executeQuery() == 0) {
                    ctx->error(0, "File not found in ASM: " + fileName);
                    return REDO_CODE::ERROR;
                }
                // A second row means the alias name is ambiguous across directories.
                if (!haveFileNumber && stmt.next() != 0) {
                    ctx->error(0, "Ambiguous ASM alias '" + fileNameOnly + "' in disk group '" + diskGroupName +
                               "' matches multiple files; cannot resolve " + fileName);
                    return REDO_CODE::ERROR;
                }
                fileType = std::string(fileTypeBuf);
                ctx->info(0, "[ReaderASMBlockDevice] ASM metadata query OK, type=" + fileType +
                         ", blocks=" + std::to_string(fileSizeRaw) +
                         ", au_size=" + std::to_string(auSize) +
                         ", group_number=" + std::to_string(groupNumber) +
                         ", file_number=" + std::to_string(fileNumber));
            }

            // Load extent map for direct I/O (now that we have group_number and file_number)
            if (!loadExtentMap()) {
                ctx->error(0, "Failed to load extent map for: " + fileName);
                return REDO_CODE::ERROR;
            }

            if (!openDisks()) {
                ctx->error(0, "Failed to open ASM disks for: " + fileName);
                extentMap.clear();
                for (auto& [diskNum, disk] : diskHandles) {
                    if (disk.fd >= 0) {
                        ::close(disk.fd);
                    }
                }
                diskHandles.clear();
                return REDO_CODE::ERROR;
            }

            // Detect block size by reading the Oracle file header.
            if (Reader::reloadHeaderRead() != REDO_CODE::OK)
                return REDO_CODE::ERROR;

            ctx->info(0, "[ReaderASMBlockDevice] redoOpen " + fileName + " complete: fileSize=" + std::to_string(fileSize) +
                     " (" + std::to_string(fileSize / 1024 / 1024) + " MB), blockSize=" + std::to_string(blockSize));

        } catch (RuntimeException& ex) {
            ctx->error(ex.code, "Failed to open file: " + ex.msg);
            return REDO_CODE::ERROR;
        }

        return REDO_CODE::OK;
    }

    void ReaderASMBlockDevice::fixHeaderBlock(uint8_t* buf, const uint64_t offset, const uint bytesRead) const {
        // Only fix first block of the file (offset 0 and buffer contains first 512 bytes)
        if (offset != 0 || bytesRead < 512) {
            return;
        }

        // Fix file header for ONLINELOG, DATAFILE, TEMPFILE, CONTROLFILE, ARCHIVELOG
        // This is needed because ASM stores files with different header format

        // Read current checksum at offset 0x10-0x13 (little-endian)
        uint32_t checksum;
        memcpy(&checksum, buf + 0x10, 4);

        // XOR with magic constant to fix checksum
        checksum ^= MAGIC_XOR;
        memcpy(buf + 0x10, &checksum, 4);

        // Write magic constant at offset 0x20-0x23
        memcpy(buf + 0x20, &MAGIC_XOR, 4);

        ctx->info(0, "[ReaderASMBlockDevice] Fixed header block for file type: " + fileType);
    }

    int ReaderASMBlockDevice::readDirect(uint8_t* buf, const uint64_t offset, uint size) {
        // Check bounds - don't read beyond file size
        if (offset >= fileSize) {
            return 0;  // EOF
        }

        // Adjust size if reading beyond EOF
        if (offset + size > fileSize) {
            size = static_cast<uint>(fileSize - offset);
        }

        // Convert file offset to AU (allocation unit) coordinates:
        // - auIndex: which AU contains this offset (file offset / AU size)
        // - offsetInAu: position within that AU (file offset % AU size)
        // Example: auSize=1MB, offset=2.5MB -> auIndex=2, offsetInAu=512KB
        uint64_t auIndex = offset / auSize;
        uint32_t offsetInAu = offset % auSize;

        ssize_t bytesRead = 0;
        uint32_t remaining = size;

        // Extent mapping explanation:
        // Files in ASM are striped across disks in Allocation Units (AU).
        // extentMap stores for each logical extent: {diskNumber, physical AU number on that disk}.
        //
        // Example: 4MB file with 1MB AU on 2 disks:
        //   Logical extent (auIndex):   0        1        2        3
        //   extentMap:              [{0,5},   {1,7},   {0,12},  {1,20}]
        //                            disk0    disk1    disk0    disk1
        //                            AU#5     AU#7     AU#12    AU#20
        //
        // To read offset=2.5MB:
        //   auIndex=2 (3rd extent), offsetInAu=512KB
        //   extentMap[2] = {diskNumber=0, auNumber=12}
        //   physicalOffset = 12 * 1MB + 512KB = 12.5MB on disk 0

        while (remaining > 0) {
            if (auIndex >= extentMap.size()) {
                ctx->error(0, "Read beyond extent map: auIndex=" + std::to_string(auIndex) +
                         " >= extentMap.size()=" + std::to_string(extentMap.size()) +
                         ", offset=" + std::to_string(offset) + ", size=" + std::to_string(size));
                return -1;
            }

            // Try each mirror copy in order (primary first) until one succeeds.
            // Known-bad disks are skipped so we don't spam a dead fd on every read.
            const auto& copies = extentMap[auIndex];
            const uint32_t toRead = std::min<uint32_t>(remaining, auSize - offsetInAu);
            ssize_t n = -1;
            for (const auto& [diskNumber, auNumber] : copies) {
                if (deadDisks.count(diskNumber) > 0)
                    continue;
                auto diskIt = diskHandles.find(diskNumber);
                if (diskIt == diskHandles.end()) {
                    ctx->warning(0, "Disk handle not found for disk " + std::to_string(diskNumber) + ", marking dead");
                    deadDisks.insert(diskNumber);
                    continue;
                }
                const uint64_t physicalOffset = static_cast<uint64_t>(auNumber) * auSize + offsetInAu;
                n = pread(diskIt->second.fd, buf + bytesRead, toRead, static_cast<off_t>(physicalOffset));
                if (n > 0)
                    break;
                ctx->warning(0, "pread failed on disk " + diskIt->second.path +
                         " offset=" + std::to_string(physicalOffset) +
                         (n < 0 ? " err=" + std::string(strerror(errno)) : " (0 bytes)") +
                         ", marking dead and trying next copy");
                deadDisks.insert(diskNumber);
            }
            if (n <= 0) {
                ctx->error(0, "All " + std::to_string(copies.size()) + " mirror copies failed at auIndex=" + std::to_string(auIndex));
                return -1;
            }

            bytesRead += n;
            remaining -= n;
            auIndex++;
            offsetInAu = 0;
        }

        // Fix header block if this is the first read (offset 0)
        if (offset == 0 && bytesRead >= 512) {
            fixHeaderBlock(buf, offset, static_cast<uint>(bytesRead));
        }

        return static_cast<int>(bytesRead);
    }

    int ReaderASMBlockDevice::redoRead(uint8_t* buf, const uint64_t offset, const uint size) {
        uint64_t startTime = 0;
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::PERFORMANCE)))
            startTime = ctx->clock->getTimeUt();

        const int result = readDirect(buf, offset, size);

        if (unlikely(ctx->isTraceSet(Ctx::TRACE::PERFORMANCE))) {
            if (size > 0 && result > 0)
                sumRead += result;
            sumTime += ctx->clock->getTimeUt() - startTime;
        }

        return result;
    }

    uint ReaderASMBlockDevice::readSize(uint prevRead) {
        if (prevRead < blockSize)
            return blockSize;

        prevRead *= 2;
        // Read up to 4MB at a time for better performance
        prevRead = std::min<uint64_t>(prevRead, MAX_READ_SIZE);

        // Align to block size
        if (prevRead % blockSize != 0) {
            prevRead = ((prevRead / blockSize) + 1) * blockSize;
        }

        return prevRead;
    }

    Reader::REDO_CODE ReaderASMBlockDevice::reloadHeaderRead() {
        const int64_t bytes = redoRead(headerBuffer + blockSize, blockSize, blockSize);
        if (bytes != blockSize) {
            ctx->error(46666, "unable to read file " + fileName + " (got " + std::to_string(bytes) + " bytes, expected " + std::to_string(blockSize) + ")");
            return REDO_CODE::ERROR;
        }
        if (ctx->metrics != nullptr)
            ctx->metrics->emitBytesRead(bytes);
        return REDO_CODE::OK;
    }

    void ReaderASMBlockDevice::showHint(Thread* t, std::string origPath, std::string mappedPath) const {
        ctx->hint("check ASM disk access, failed to read: " + origPath +
                  " - ensure OpenLogReplicator has access to ASM disks");
    }

}
