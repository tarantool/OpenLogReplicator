/* Header for Replicator class
   Copyright (C) 2018-2026 Adam Leszczynski (aleszczynski@bersler.com)

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

#ifndef REPLICATOR_H_
#define REPLICATOR_H_

#include <queue>
#include <set>
#include <vector>

#include "../common/Ctx.h"
#include "../common/Thread.h"
#include "../common/types/Seq.h"

namespace OpenLogReplicator {
    class Parser;
    class Builder;
    class Metadata;
    class Reader;
    class RedoLogRecord;
    class State;
    class Transaction;
    class TransactionBuffer;

    struct parserCompare {
        bool operator()(const Parser* p1, const Parser* p2) const;
    };

    class Replicator : public Thread {
    protected:
        void (*archGetLog)(Replicator* replicator);
        Builder* builder;
        Metadata* metadata;
        TransactionBuffer* transactionBuffer;
        std::string database;
        std::string redoCopyPath;
        // Redo log files
        Reader* archReader{nullptr};
        std::string lastCheckedDay;
        std::priority_queue<Parser*, std::vector<Parser*>, parserCompare> archiveRedoQueue;
        std::set<Parser*> onlineRedoSet;
        std::set<Reader*> readers;
        std::vector<std::string> pathMapping;
        std::vector<std::string> redoLogsBatch;

        void cleanArchList();
        void updateOnlineLogs() const;
        void readerDropAll();
        static Seq getSequenceFromFileName(const Replicator* replicator, const std::string& file);
        virtual std::string getModeName() const;
        virtual bool checkConnection();
        virtual bool continueWithOnline();
        virtual void verifySchema(Scn currentScn);
        virtual void createSchema();
        virtual void updateOnlineRedoLogData();

    public:
        Replicator(Ctx* newCtx, void (*newArchGetLog)(Replicator* replicator), Builder* newBuilder, Metadata* newMetadata,
                   TransactionBuffer* newTransactionBuffer, std::string newAlias, std::string newDatabase);
        ~Replicator() override;

        virtual void initialize();
        virtual void positionReader();
        virtual void loadDatabaseMetadata();
        void run() override;
        virtual Reader* readerCreate(int group);
        void checkOnlineRedoLogs();
        virtual void goStandby();
        void addPathMapping(std::string source, std::string target);
        void addRedoLogsBatch(std::string path);
        static void archGetLogPath(Replicator* replicator);
        static void archGetLogList(Replicator* replicator);
        void applyMapping(std::string& path) const;
        void updateResetlogs();
        void wakeUp() override;
        void printStartMsg() const;
        bool processArchivedRedoLogs();
        bool processOnlineRedoLogs();

        friend class OpenLogReplicator;
        friend class ReplicatorOnline;

        std::string getName() const override {
            return {"Replicator: " + alias};
        }
    };
}

#endif
