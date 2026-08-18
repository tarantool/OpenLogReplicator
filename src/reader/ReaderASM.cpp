/* Base class for reading redo from ASM
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

#include <string>
#include "ReaderASM.h"
#include "../replicator/ReplicatorOnlineASM.h"
#include "../common/exception/RuntimeException.h"

#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../common/Clock.h"
#include "../common/Ctx.h"
#include "../common/metrics/Metrics.h"
#include "ReaderFilesystem.h"


namespace OpenLogReplicator {

    ReaderASM::ReaderASM(Ctx* newCtx, std::string newAlias, Replicator* replicator, std::string newDatabase, const int newGroup, const bool newConfiguredBlockSum) :
        Reader(newCtx, std::move(newAlias), std::move(newDatabase), newGroup, newConfiguredBlockSum),
        replicator(replicator){}

    ReaderASM::~ReaderASM() {
        ReaderASM::redoClose();
    }

    void ReaderASM::redoClose() {
        if (stmtRead != nullptr) {
            delete stmtRead;
            stmtRead = nullptr;
        }

        if (fileDes == -1) {
            return;
        }

        try {
            const ReplicatorOnlineASM* replicatorOnlineAsm = dynamic_cast<ReplicatorOnlineASM*>(replicator);

            DatabaseStatement stmt(replicatorOnlineAsm->connASM);
            ctx->logTrace(Ctx::TRACE::SQL, std::string(SQL_ASM_CLOSE));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM1: " + std::to_string(fileDes));
            stmt.createStatement(SQL_ASM_CLOSE);
            stmt.bindInt(1, fileDes);
            stmt.executeQuery();
            fileDes = -1;
        } catch (RuntimeException& ex) {
            // TODO if nothing is done, then there may be a memory leak in ASM. Need to investigate correct behavior when errors occur
            // TODO There is no handling in case a different type of replicator is passed instead of ReplicatorOnlineASM*
            ctx->error(10051, "OCI: closing ASM reader ended with an error. Error: " + ex.msg);
        }
    }

    Reader::REDO_CODE ReaderASM::redoOpen() {
        try {
            uint64_t fileType = -1;
            blockSize = 0;
            const ReplicatorOnlineASM* replicatorOnlineAsm = dynamic_cast<ReplicatorOnlineASM*>(replicator);
            DatabaseStatement stmt(replicatorOnlineAsm->connASM);
            ctx->logTrace(Ctx::TRACE::SQL, std::string(SQL_ASM_GETFILEATR));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM1: " + fileName);
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM2: " + std::to_string(fileType));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM3: " + std::to_string(fileSize));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM4: " + std::to_string(blockSize));
            stmt.createStatement(SQL_ASM_GETFILEATR);
            stmt.bindString(1, fileName);
            stmt.bindUInt(2, fileType);
            stmt.bindUInt(3, fileSize);
            stmt.bindUInt(4, blockSize);
            stmt.executeQuery();

            physicalBlockSize = -1;
            ctx->logTrace(Ctx::TRACE::SQL, std::string(SQL_ASM_OPEN));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM1: " + fileName);
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM2: " + std::to_string(fileType));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM3: " + std::to_string(blockSize));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM4: " + std::to_string(fileDes));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM5: " + std::to_string(physicalBlockSize));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM6: " + std::to_string(fileSize));
            stmt.createStatement(SQL_ASM_OPEN);
            stmt.bindString(1, fileName);
            stmt.bindUInt(2, fileType);
            stmt.bindUInt(3, blockSize);
            stmt.bindInt(4, fileDes);
            stmt.bindUInt(5, physicalBlockSize);
            stmt.bindUInt(6, fileSize);
            stmt.executeQuery();

            // Размер файла в байтах fileSize (в блоках) × blockSize (в байтах)
            fileSize *= blockSize;
        } catch (RuntimeException& ex) {
            ctx->error(10051, "OCI: An error occurred while opening redo logs. Error: " + ex.msg);
            return REDO_CODE::ERROR;
        }

        return REDO_CODE::OK;
    }

    int ReaderASM::redoRead(uint8_t* buf, uint64_t offset, uint size) {
        if (fileDes == -1) {
            return -1;
        }

        uint64_t startTime = 0;
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::PERFORMANCE))) {
            startTime = ctx->clock->getTimeUt();
        }

        offset /= blockSize;
        try {
            if (stmtRead == nullptr) {
                const ReplicatorOnlineASM* replicatorOnlineAsm = dynamic_cast<ReplicatorOnlineASM*>(replicator);
                stmtRead = new DatabaseStatement(replicatorOnlineAsm->connASM);
                stmtRead->createStatement(SQL_ASM_READ);
            } else {
                stmtRead->unbindAll();
            }

            ctx->logTrace(Ctx::TRACE::SQL, std::string(SQL_ASM_READ));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM1: " + std::to_string(fileDes));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM1(addition): " + fileName);
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM2: " + std::to_string(offset));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM3: " + std::to_string(size));
            ctx->logTrace(Ctx::TRACE::SQL, "PARAM4: " + std::to_string(*buf));

            stmtRead->bindInt(1, fileDes);
            stmtRead->bindUInt(2, offset);
            stmtRead->bindUInt(3, size);
            stmtRead->bindBinary(4, buf, size);
            stmtRead->executeQuery();
        } catch (RuntimeException& ex) {
            ctx->error(10051, "OCI: An error occurred while reading redo logs from ASM. Error: " + ex.msg);
            return -1;
        }

        if (unlikely(ctx->isTraceSet(Ctx::TRACE::PERFORMANCE))) {
            if (size > 0) {
                sumRead += size;
            }
            sumTime += ctx->clock->getTimeUt() - startTime;
        }

        return size;
    }

    uint ReaderASM::readSize(uint prevRead) {
        if (prevRead < blockSize) {
            return blockSize;
        }

        //TODO что за магическое число 16384?
        prevRead = std::min<uint64_t>(16384, Ctx::MEMORY_CHUNK_SIZE);

        return prevRead;
    }

    Reader::REDO_CODE ReaderASM::reloadHeaderRead() {
        if (const int64_t bytes = redoRead(headerBuffer + blockSize, blockSize, blockSize);
            bytes != blockSize) {
            ctx->error(46666, "unable to read file " + fileName);
            return REDO_CODE::ERROR;
        }
        if (ctx->metrics != nullptr)
            ctx->metrics->emitBytesRead(blockSize);
        return REDO_CODE::OK;
    }

    void ReaderASM::showHint(Thread* t, std::string origPath, std::string mappedPath) const {
        bool first = true;
        uid_t uid = geteuid();
        gid_t gid = getegid();

        ctx->hint("check mapping, failed to read: " + origPath + " mapped to: " + mappedPath + " run as uid: " +
            std::to_string(uid) + " gid: " + std::to_string(gid));

        while (!mappedPath.empty()) {
            std::string partialFileName;
            if (!first) {
                size_t found = mappedPath.find_last_of("/\\");
                partialFileName = mappedPath.substr(found + 1);
                mappedPath.resize(found);
            }
            if (mappedPath.empty()) {
                break;
            }

            struct stat fileStat{};
            t->contextSet(CONTEXT::OS, REASON::OS);
            const int statRet = stat(mappedPath.c_str(), &fileStat);
            t->contextSet(CONTEXT::CPU);

            // try with stat
            if (statRet != 0) {
                ctx->hint("- path: " + mappedPath + " - get metadata returned: " + strerror(errno));
                first = false;
                continue;
            }

            std::string fileType;
            switch (fileStat.st_mode & S_IFMT) {
                case S_IFBLK:
                    fileType = "block device";
                    break;
                case S_IFCHR:
                    fileType = "character device";
                    break;
                case S_IFDIR:
                    fileType = "directory";
                    break;
                case S_IFIFO:
                    fileType = "FIFO/pipe";
                    break;
                case S_IFLNK:
                    fileType = "symlink";
                    break;
                case S_IFREG:
                    fileType = "regular file";
                    break;
                case S_IFSOCK:
                    fileType = "socket";
                    break;
                default:
                    fileType = "unknown?";
            }

            std::stringstream permissions;
            permissions << std::oct << fileStat.st_mode;

            ctx->hint("- path: " + mappedPath + " - type: " + fileType + " permissions: " + permissions.str() +
                      " uid: " + std::to_string(fileStat.st_uid) + " gid: " + std::to_string(fileStat.st_gid));

            DIR* dir = opendir(mappedPath.c_str());
            if (dir == nullptr) {
                ctx->hint("- path: " + mappedPath + " - get metadata returned: " + strerror(errno));
                first = false;
                continue;
            }

            bool found = false;
            const dirent* ent;
            while ((ent = readdir(dir)) != nullptr) {
                if (const std::string dName(ent->d_name); dName == "." || dName == "..") {
                    continue;
                }

                const std::string localFileName(ent->d_name);
                if (partialFileName != localFileName) {
                    continue;
                }

                found = true;
                break;
            }
            closedir(dir);

            if (!found) {
                ctx->hint("- path: " + mappedPath + " - can be listed but does not contain: " + partialFileName);
            }

            first = false;
        }
    }
}
