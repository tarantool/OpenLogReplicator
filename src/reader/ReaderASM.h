/* Header for ReaderASM class
Copyright (C) 2018-2025 Adam Leszczynski (aleszczynski@bersler.com)

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

#ifndef READERASM_H_
#define READERASM_H_

#include "Reader.h"
#include "../replicator/DatabaseConnection.h"
#include "../replicator/DatabaseEnvironment.h"
#include "../replicator/DatabaseStatement.h"
#include "../replicator/Replicator.h"


namespace OpenLogReplicator {
    class ReaderASM final : public Reader {
    protected:
        /**
         * \brief Procedure closes a file by the passed descriptor. i - number
         */
        static constexpr std::string_view SQL_ASM_CLOSE{"BEGIN dbms_diskgroup.close(:i); END;"};

        /**
         * \brief Procedure gets attributes of a file by the passed filename.
         *
         * Parameters:
         * - i - filename in ASM notation (IN)
         * - j - file type (here for redolog it will return a specific value, number) (OUT)
         * - k - size of the file in blocks (OUT)
         * - l - block size in bytes (OUT)
         */
        static constexpr std::string_view SQL_ASM_GETFILEATR{"BEGIN dbms_diskgroup.getfileattr(:i, :j, :k, :l); END;"};

        /**
         * \brief Procedure opens a file for reading by the passed filename in ASM notation.
         *
         * Parameters:
         * - i - filename (IN)ч
         * - j - file type (OUT)
         * - k - logical block size in bytes (OUT)
         * - l - file descriptor (OUT)
         * - m - physical block size in bytes (OUT)
         * - n - file size in blocks (OUT)
         */
        static constexpr std::string_view SQL_ASM_OPEN{"BEGIN dbms_diskgroup.open(:i, 'r', :j, :k, :l, :m, :n); END;"};

        /**
         * \brief Procedure reads raw bytes from a file by the passed descriptor.
         *
         * Parameters:
         * - i - file descriptor (IN)
         * - j - offset in block (IN)
         * - k - count of bytes to read (IN)
         * - l - pointer to byte array where data is written
         */
        static constexpr std::string_view SQL_ASM_READ{"BEGIN dbms_diskgroup.read(:i, :j, :k, :l); END;"};

        /**
         * \brief Replicator that owns the current Reader.
         */
        Replicator* replicator;

        /**
         * \brief File descriptor returned by OCI after calling the SQL_ASM_OPEN procedure.
         */
        int32_t fileDes{-1};

        /**
         * \brief Physical block size in bytes
         *
         * \todo consider whether it's necessary to keep it as a field. It's not used globally anywhere. Need to understand
         * what differentiates it from the logical block value
         */
        uint64_t physicalBlockSize{0};

        /**
         * \brief Expression for calling the SQL_ASM_READ procedure. Located here to reduce the amount of dynamic
         * memory allocation on each read. Used only for reading! Created during the first call to redoOpen().
         */
        DatabaseStatement* stmtRead = nullptr;

        void redoClose() override;

        REDO_CODE redoOpen() override;

        int redoRead(uint8_t* buf, uint64_t offset, uint size) override;

        uint readSize(uint prevRead) override;

        REDO_CODE reloadHeaderRead() override;

    public:
        ReaderASM(Ctx* newCtx, std::string newAlias, Replicator* replicator, std::string newDatabase, int newGroup,
                  bool newConfiguredBlockSum);

        ~ReaderASM() override;

        void showHint(Thread* t, std::string origPath, std::string mappedPath) const override;
    };
}

#endif
