/* Base class for source and target thread
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

#include "Ctx.h"
#include "Thread.h"

#include <pthread.h>
#include <utility>
#include "exception/RuntimeException.h"

namespace OpenLogReplicator {
    Thread::Thread(Ctx* newCtx, std::string newAlias):
            ctx(newCtx),
            alias(std::move(newAlias)) {}

    void Thread::wakeUp() {
        if (unlikely(ctx->isTraceSet(Ctx::TRACE::THREADS))) {
            std::ostringstream ss;
            ss << std::this_thread::get_id();
            ctx->logTrace(Ctx::TRACE::THREADS, "thread (" + ss.str() + ") wake up");
        }
    }

    void* Thread::runStatic(void* voidThread) {
        auto* thread = static_cast<Thread*>(voidThread);
        // Set thread name (max 15 characters + null terminator)
        std::string name = thread->getName();
        if (name.length() > 15)
            name = name.substr(0, 15);
#if __linux__
        pthread_setname_np(thread->pthread, name.c_str());
#endif
#if __APPLE__
        pthread_setname_np(name.c_str());
#endif

        thread->contextRun();
        thread->finished = true;
        return nullptr;
    }
}
