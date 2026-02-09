/* Header for OracleAnalyzerOnlineASM class
   Copyright (C) 2018-2022 Adam Leszczynski (aleszczynski@bersler.com)

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

#ifndef REPLICATOR_ONLINE_ASM_H_
#define REPLICATOR_ONLINE_ASM_H_

#include "ReplicatorOnline.h"

namespace OpenLogReplicator {
    class ReplicatorOnlineASM final : public ReplicatorOnline {
    protected:
        std::string getModeName() const override;

        bool checkConnection() override;

        Reader* readerCreate(int group) override;

    public:
        DatabaseConnection* connASM;

        ReplicatorOnlineASM(Ctx* newCtx, void (*newArchGetLog)(Replicator* replicator), Builder* newBuilder,
                            Metadata* newMetadata,
                            TransactionBuffer* newTransactionBuffer, std::string newAlias, std::string newDatabase,
                            std::string newUser,
                            std::string newPassword, std::string newConnectString, bool newKeepConnection,
                            std::string userASM, std::string passwdASM, std::string connectStringASM);

        ~ReplicatorOnlineASM() override;
    };
}

#endif
