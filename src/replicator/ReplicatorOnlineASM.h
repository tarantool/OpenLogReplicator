/* Header for OracleAnalyzerOnlineASM class
   Copyright (C) 2018-2022 Adam Leszczynski (aleszczynski@bersler.com)

This file is part of OpenLogReplicator.

This program is free software: you can redistribute it and/or
modify it under the terms of the GNU Affero General Public License as
published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public
License along with this program; see the file LICENSE;
If not, see <http://www.gnu.org/licenses/>. */

#ifndef REPLICATOR_ONLINE_ASM_H_
#define REPLICATOR_ONLINE_ASM_H_

#include "ReplicatorOnline.h"
#include <mutex>

namespace OpenLogReplicator {
    class DatabaseEnvironment;

    class ReplicatorOnlineASM final : public ReplicatorOnline {
    protected:
        std::string getModeName() const override;

        bool checkConnection() override;

        Reader* readerCreate(int group) override;

        /**
         * @brief Flag to use direct block device access via ReaderASMBlockDevice instead of ReaderASM.
         *
         * When true, the replicator will create ReaderASMBlockDevice instances that bypass
         * the ASM instance and read directly from underlying block devices using
         * extent mapping. This can provide better performance and independence
         * from ASM instance load, but requires appropriate OS-level permissions.
         */
        bool useASMBlockDevice;

        bool getArchivelogFromFS;

    public:
        DatabaseConnection* connASM;

        /**
         * @brief Dedicated metadata connection to ASM instance.
         *
         * Separate connection used for querying ASM metadata views and tables
         * (v$asm_diskgroup, v$asm_disk, v$asm_file, x$kffxp, etc.). This allows
         * ReaderASMBlockDevice to query extent mappings without interfering with ongoing
         * I/O operations on the primary connection. Thread-safe access is
         * protected by connASMMetaMutex.
         */
        DatabaseConnection* connASMMeta;

        /**
         * @brief Mutex protecting thread-safe access to connASMMeta.
         *
         * OCI database connections are not thread-safe. This mutex ensures that
         * ReaderASMBlockDevice can safely execute metadata queries from its own thread
         * without conflicting with other operations.
         */
        std::mutex connASMMetaMutex;

        ReplicatorOnlineASM(Ctx* newCtx, void (*newArchGetLog)(Replicator* replicator), Builder* newBuilder,
                            Metadata* newMetadata, TransactionBuffer* newTransactionBuffer, std::string newAlias,
                            std::string newDatabase, std::string newUser, std::string newPassword,
                            std::string newConnectString, bool newKeepConnection, std::string userASM,
                            std::string passwdASM, std::string connectStringASM, bool getArchivelogFromFS,
                            bool newUseASMBlockDevice = false);


        ~ReplicatorOnlineASM() override;
    };
}

#endif
