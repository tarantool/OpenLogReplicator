/* Thread reading Oracle Redo Logs using online with ASM
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

#include "DatabaseConnection.h"
#include "ReplicatorOnlineASM.h"

#include <unistd.h>
#include <utility>
#include <string>
#include "../reader/ReaderASM.h"
#include "../reader/ReaderUdev.h"
#include "../reader/ReaderFilesystem.h"
#include "../metadata/Metadata.h"
#include "../metadata/RedoLog.h"
#include "../metadata/Schema.h"
#include "DatabaseStatement.h"
#include "../common/exception/RuntimeException.h"

namespace OpenLogReplicator {
    ReplicatorOnlineASM::ReplicatorOnlineASM(Ctx *newCtx, void (*newArchGetLog)(Replicator *replicator),
                                             Builder *newBuilder, Metadata *newMetadata,
                                             TransactionBuffer *newTransactionBuffer, std::string newAlias,
                                             std::string newDatabase, std::string newUser,
                                             std::string newPassword, std::string newConnectString,
                                             bool newKeepConnection, std::string userASM, std::string passwdASM,
                                             std::string connectStringASM, bool getArchivelogFromFS, bool newUseUdev) :
    ReplicatorOnline(newCtx, newArchGetLog, newBuilder, newMetadata, newTransactionBuffer,
        std::move(newAlias), std::move(newDatabase), std::move(newUser),
        std::move(newPassword), std::move(newConnectString), newKeepConnection),
    useUdev(newUseUdev), getArchivelogFromFS(getArchivelogFromFS) {
        connASM = new DatabaseConnection(env, userASM, passwdASM, connectStringASM, true);
        connASMMeta = new DatabaseConnection(env, userASM, passwdASM, connectStringASM, true);
    }

    ReplicatorOnlineASM::~ReplicatorOnlineASM() {
        if (connASM != nullptr) {
            delete connASM;
            connASM = nullptr;
        }

        if (connASMMeta != nullptr) {
            delete connASMMeta;
            connASMMeta = nullptr;
        }
    }

    bool ReplicatorOnlineASM::checkConnection() {
        if (!ReplicatorOnline::checkConnection()) {
            return false;
        }

        if (!connASM->connected) {
            ctx->info(0, "connecting to the ASM instance of " + database + " to " + connASM->connectString);
        }

        while (!ctx->softShutdown) {
            // Connect main ASM connections if not connected
            if (!connASM->connected) {
                try {
                    connASM->connect();
                } catch (RuntimeException& ex) {
                    ctx->error(ex.code, ex.msg);
                }
            }

            // Connect metadata connection separately (for ReaderUdev)
            if (!connASMMeta->connected) {
                try {
                    connASMMeta->connect();
                } catch (RuntimeException& ex) {
                    ctx->error(ex.code, ex.msg);
                }
            }

            if (connASM->connected) {
                return true;
            }

            if (connASM->connected) {
                try {
                    DatabaseStatement stmt(conn);
                    if (unlikely(ctx->isTraceSet(Ctx::TRACE::SQL))) {
                        ctx->logTrace(Ctx::TRACE::SQL, std::string(SQL_CHECK_CONNECTION));
                    }

                    stmt.createStatement(SQL_CHECK_CONNECTION);
                    uint dummy;
                    stmt.defineUInt(1, dummy);
                    stmt.executeQuery();
                } catch (RuntimeException& ex) {
                    ctx->error(ex.code, ex.msg);
                    conn->disconnect();
                    contextSet(CONTEXT::SLEEP);
                    usleep(ctx->refreshIntervalUs);
                    contextSet(CONTEXT::CPU);
                    ctx->info(0, "reconnecting to the database instance of " + database + " to " + conn->connectString);
                    continue;
                }
                return true;
            }
            if (unlikely(ctx->isTraceSet(Ctx::TRACE::REDO))) {
                ctx->logTrace(Ctx::TRACE::REDO, "cannot connect to ASM, retry in " + std::to_string(ctx->refreshIntervalUs / 1000000) + " sec.");
            }

            contextSet(CONTEXT::SLEEP);
            usleep(ctx->refreshIntervalUs);
            contextSet(CONTEXT::CPU);
        }

        return false;
    }

    Reader* ReplicatorOnlineASM::readerCreate(const int group) {
        for (Reader* reader : readers) {
            if (reader->getGroup() == group) {
                return reader;
            }
        }

        Reader* reader;

        if (group == 0 && getArchivelogFromFS) {
            reader = new ReaderFilesystem(ctx, alias + "-reader-" + std::to_string(group), database, group,
                                   metadata->dbBlockChecksum != "OFF" && metadata->dbBlockChecksum != "FALSE");
        } else {
            if (useUdev) {
                reader = new ReaderUdev(ctx, alias + "-reader-" + std::to_string(group), this, database, group,
                                                   metadata->dbBlockChecksum != "OFF" && metadata->dbBlockChecksum != "FALSE");
            } else {
                reader = new ReaderASM(ctx, alias + "-reader-" + std::to_string(group), this, database, group,
                                                   metadata->dbBlockChecksum != "OFF" && metadata->dbBlockChecksum != "FALSE");
            }
        }

        readers.insert(reader);
        reader->initialize();

        ctx->spawnThread(reader);
        return reader;
    }

    std::string ReplicatorOnlineASM::getModeName() const {
        if (useUdev) {
            return {"ASM-UDEV"};
        }
        return {"ASM"};
    }
}
