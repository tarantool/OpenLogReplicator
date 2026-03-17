/* Base class for streaming using network sockets
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

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netdb.h>
#include <unistd.h>

#include "../common/Ctx.h"
#include "../common/Thread.h"
#include "../common/exception/ConfigurationException.h"
#include "../common/exception/NetworkException.h"
#include "../common/exception/RuntimeException.h"
#include "StreamNetwork.h"

namespace OpenLogReplicator {
    StreamNetwork::StreamNetwork(Ctx* newCtx, std::string newUri):
            Stream(newCtx, std::move(newUri)) {
        readBuffer[0] = 0;
    }

    StreamNetwork::~StreamNetwork() {
        if (res != nullptr) {
            freeaddrinfo(res);
            res = nullptr;
        }

        if (socketFD != -1) {
            close(socketFD);
            socketFD = -1;
        }

        if (serverFD != -1) {
            close(serverFD);
            serverFD = -1;
        }
    }

    void StreamNetwork::initialize() {
        // Colon
        const auto uriIt = uri.find(':');
        if (uriIt == std::string::npos)
            throw ConfigurationException(30008, "uri is missing ':' in parameter: " + uri);

        host = uri.substr(0, uriIt);
        port = uri.substr(uriIt + 1, uri.length() - 1);
    }

    std::string StreamNetwork::getName() const {
        return "Network:" + uri;
    }

    void StreamNetwork::initializeClient() {
        sockaddr_in addressC{};
        memset(&addressC, 0, sizeof(addressC));
        addressC.sin_family = AF_INET;
        addressC.sin_port = htons(atoi(port.c_str()));

        if ((socketFD = socket(AF_INET, SOCK_STREAM, 0)) == 0)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (1)");

        const hostent* server = gethostbyname(host.c_str());
        if (server == nullptr)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (2)");

        memcpy(&addressC.sin_addr.s_addr, server->h_addr, server->h_length);
        if (connect(socketFD, reinterpret_cast<sockaddr*>(&addressC), sizeof(addressC)) < 0)
            throw NetworkException(10062, "connection to " + uri + " failed, errno: " + std::to_string(errno) + ", message: " +
                                   strerror(errno));
    }

    void StreamNetwork::initializeServer() {
        addrinfo hints{};
        memset(&hints, 0, sizeof hints);
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_PASSIVE;

        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || res == nullptr)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (3)");

        serverFD = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (serverFD == 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (4)");

        const int flags = fcntl(serverFD, F_GETFL);
        if (flags < 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (5)");
        if (fcntl(serverFD, F_SETFL, flags | O_NONBLOCK) < 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (6)");

        int opt = 1;
        if (setsockopt(serverFD, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) != 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (7)");

#ifdef SO_REUSEPORT
        if (setsockopt(serverFD, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) != 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (21)");
#endif

        if (bind(serverFD, res->ai_addr, res->ai_addrlen) < 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (8)");
        if (listen(serverFD, 1) < 0)
            throw RuntimeException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (9)");

        freeaddrinfo(res);
        res = nullptr;
    }

    void StreamNetwork::sendMessage(const void* msg, uint64_t length) {
        uint8_t lengthBuf[8];
        uint64_t sent = 0;

        if (socketFD == -1)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (10)");

        fd_set wset;
        fd_set w;
        FD_ZERO(&wset);
        FD_SET(socketFD, &wset);

        // Header content - always little endian for debezium client compatibility
        //
        // Example: mem address =  0x05, 0x06, 0x07, 0x08, ...
        // byte[] msg = { 0x04, 0x00, 0x00, 0x00, ... } from Little Endian system means 4 number.
        // Big Endian platform interprets *static_cast<const uint32_t*>(msg) like 0x04000000.
        // It takes first four bytes and MSB (most significant byte) on lowest address and that
        // will be int 0x04000000 (in decimal is 67108864).
        // It should be interpreted as LSB (least significant byte) on lowest address, that
        // will be int 0x00000004 (in decimal is 4).
        if (length < MAX_LENGTH) {
            // 32-bit length
            Ctx::write32Little(lengthBuf, static_cast<uint32_t>(length));
            while (sent < sizeof(uint32_t)) {
                if (ctx->softShutdown)
                    return;

                w = wset;
                // Blocking select
                select(socketFD + 1, nullptr, &w, nullptr, nullptr);
                ssize_t r = write(socketFD, lengthBuf + sent, sizeof(uint32_t) - sent);
                if (r <= 0) {
                    if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                        r = 0;
                    else {
                        close(socketFD);
                        socketFD = -1;
                        throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                               strerror(errno) + " (11)");
                    }
                }
                sent += r;
            }
        } else {
            // 64-bit length
            Ctx::write32Little(lengthBuf, MAX_LENGTH);
            while (sent < sizeof(uint32_t)) {
                if (ctx->softShutdown)
                    return;

                w = wset;
                // Blocking select
                select(socketFD + 1, nullptr, &w, nullptr, nullptr);
                ssize_t r = write(socketFD, lengthBuf + sent, sizeof(uint32_t) - sent);
                if (r <= 0) {
                    if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                        r = 0;
                    else {
                        close(socketFD);
                        socketFD = -1;
                        throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                               strerror(errno) + " (12)");
                    }
                }
                sent += r;
            }

            Ctx::write64Little(lengthBuf, length);
            sent = 0;
            while (sent < sizeof(uint64_t)) {
                if (ctx->softShutdown)
                    return;

                w = wset;
                // Blocking select
                select(socketFD + 1, nullptr, &w, nullptr, nullptr);
                ssize_t r = write(socketFD, lengthBuf + sent, sizeof(uint64_t) - sent);
                if (r <= 0) {
                    if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                        r = 0;
                    else {
                        close(socketFD);
                        socketFD = -1;
                        throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                               strerror(errno) + " (13)");
                    }
                }
                sent += r;
            }
        }

        sent = 0;
        // Message content
        while (sent < length) {
            if (ctx->softShutdown)
                return;

            w = wset;
            // Blocking select
            select(socketFD + 1, nullptr, &w, nullptr, nullptr);
            ssize_t r = write(socketFD, static_cast<const char*>(msg) + sent, length - sent);
            if (r <= 0) {
                if (r < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
                    r = 0;
                else {
                    close(socketFD);
                    socketFD = -1;
                    throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                           strerror(errno) + " (14)");
                }
            }
            sent += r;
        }
    }

    uint64_t StreamNetwork::receiveMessage(void* msg, uint64_t bufferSize) {
        uint64_t recvd = 0;

        // Read message length
        while (recvd < sizeof(uint32_t)) {
            if (ctx->softShutdown)
                return 0;

            const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, sizeof(uint32_t) - recvd);

            if (bytes > 0)
                recvd += bytes;
            else if (bytes == 0) {
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10056, "host disconnected");
            } else {
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (15)");
            }
        }

        // Read length as little endian (little endian client compatibility)
        uint64_t length = Ctx::read32Little(static_cast<const uint8_t*>(msg));
        if (length < MAX_LENGTH) {
            // 32-bit message length
            if (bufferSize < length)
                throw NetworkException(10055, "message from client exceeds buffer size (length: " + std::to_string(bufferSize) +
                                       ", buffer size: " + std::to_string(length) + ")");
            recvd = 0;
        } else {
            // 64-bit message length
            recvd = 0;

            while (recvd < sizeof(uint64_t)) {
                if (ctx->softShutdown)
                    return 0;

                const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, sizeof(uint64_t) - recvd);

                if (bytes > 0)
                    recvd += bytes;
                else if (bytes == 0) {
                    close(socketFD);
                    socketFD = -1;
                    throw NetworkException(10056, "host disconnected");
                } else {
                    close(socketFD);
                    socketFD = -1;
                    throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " +
                                           strerror(errno) + " (16)");
                }
            }

            length = Ctx::read64Little(static_cast<const uint8_t*>(msg));
            if (bufferSize < length)
                throw NetworkException(10055, "message from client exceeds buffer size (length: " + std::to_string(bufferSize) +
                                       ", buffer size: " + std::to_string(length) + ")");
            recvd = 0;
        }

        while (recvd < length) {
            if (ctx->softShutdown)
                return 0;

            const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, length - recvd);

            if (bytes > 0)
                recvd += bytes;
            else if (bytes == 0) {
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10056, "host disconnected");
            } else {
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (17)");
            }
        }

        return recvd;
    }

    uint64_t StreamNetwork::receiveMessageNB(void* msg, uint64_t bufferSize) {
        uint64_t recvd = 0;

        // Read message length
        while (recvd < sizeof(uint32_t)) {
            if (ctx->softShutdown)
                return 0;

            const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, sizeof(uint32_t) - recvd);

            if (bytes > 0)
                recvd += bytes;
            else if (bytes == 0) {
                // Client disconnected
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10056, "host disconnected");
            } else {
                if (recvd == 0)
                    return 0;

                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    ctx->writerThread->contextSet(Thread::CONTEXT::SLEEP);
                    ctx->usleepInt(ctx->pollIntervalUs);
                    ctx->writerThread->contextSet(Thread::CONTEXT::CPU);
                } else
                    return 0;
            }
        }

        uint64_t length = Ctx::read32Little(static_cast<const uint8_t*>(msg));
        if (length < MAX_LENGTH) {
            // 32-bit message length
            if (bufferSize < length)
                throw NetworkException(10055, "message from client exceeds buffer size (length: " + std::to_string(bufferSize) +
                                       ", buffer size: " + std::to_string(length) + ")");
            recvd = 0;
        } else {
            // 64-bit message length
            recvd = 0;

            while (recvd < sizeof(uint64_t)) {
                if (ctx->softShutdown)
                    return 0;

                const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, sizeof(uint64_t) - recvd);

                if (bytes > 0)
                    recvd += bytes;
                else if (bytes == 0) {
                    // Client disconnected
                    close(socketFD);
                    socketFD = -1;
                    throw NetworkException(10056, "host disconnected");
                } else {
                    if (recvd == 0)
                        return 0;

                    if (errno == EWOULDBLOCK || errno == EAGAIN) {
                        ctx->writerThread->contextSet(Thread::CONTEXT::SLEEP);
                        ctx->usleepInt(ctx->pollIntervalUs);
                        ctx->writerThread->contextSet(Thread::CONTEXT::CPU);
                    } else
                        return 0;
                }
            }

            length = Ctx::read64Little(static_cast<const uint8_t*>(msg));
            if (bufferSize < length)
                throw NetworkException(10055, "message from client exceeds buffer size (length: " + std::to_string(bufferSize) +
                                       ", buffer size: " + std::to_string(length) + ")");
            recvd = 0;
        }

        while (recvd < length) {
            const int64_t bytes = read(socketFD, static_cast<char*>(msg) + recvd, length - recvd);

            if (bytes > 0)
                recvd += bytes;
            else if (bytes == 0) {
                // Client disconnected
                close(socketFD);
                socketFD = -1;
                throw NetworkException(10056, "host disconnected");
            } else {
                if (errno == EWOULDBLOCK || errno == EAGAIN) {
                    ctx->writerThread->contextSet(Thread::CONTEXT::SLEEP);
                    ctx->usleepInt(ctx->pollIntervalUs);
                    ctx->writerThread->contextSet(Thread::CONTEXT::CPU);
                } else
                    return 0;
            }
        }

        return recvd;
    }

    bool StreamNetwork::isConnected() {
        if (socketFD != -1)
            return true;

        int64_t addrlen = sizeof(address);
        socketFD = accept(serverFD, reinterpret_cast<sockaddr*>(&address), reinterpret_cast<socklen_t*>(&addrlen));
        if (socketFD < 0) {
            if (errno == EWOULDBLOCK)
                return false;

            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (18)");
        }

        const int flags = fcntl(socketFD, F_GETFL);
        if (flags < 0)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (19)");
        if (fcntl(socketFD, F_SETFL, flags | O_NONBLOCK) < 0)
            throw NetworkException(10061, "network error, errno: " + std::to_string(errno) + ", message: " + strerror(errno) + " (20)");

        if (socketFD != -1) {
            char clientHost[NI_MAXHOST];
            char clientService[NI_MAXSERV];

            if (getnameinfo(reinterpret_cast<sockaddr*>(&address), addrlen, clientHost, sizeof(clientHost), clientService, sizeof(clientService),
                            NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
                ctx->info(0, "new client connection: " + std::string(clientHost) + ":" + clientService);
            }
            return true;
        }

        return false;
    }
}
