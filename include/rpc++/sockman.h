/*-
 * Copyright (c) 2016-present Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

// -*- c++ -*-

#pragma once

#include <chrono>
#include <unordered_map>
#include <vector>

#include <sys/select.h>
#include <rpc++/timeout.h>

#ifdef __FreeBSD__
#define HAVE_KQUEUE
#endif

namespace oncrpc {

class Socket;

class SocketManager: public TimeoutManager,
                     public std::enable_shared_from_this<SocketManager>
{
public:
    SocketManager();
    ~SocketManager();

    void add(std::shared_ptr<Socket> conn);

    void remove(std::shared_ptr<Socket> conn);

    void changed(std::shared_ptr<Socket> conn);

    void run();

    void stop();

    auto idleTimeout() const { return idleTimeout_; }
    void setIdleTimeout(clock_type::duration d) { idleTimeout_ = d; }

    // TimeoutManager overrides
    task_type add(
        clock_type::time_point when, std::function<void()> what) override;

private:
    struct Entry {
        clock_type::time_point time;
        int fd;
    };

    std::mutex mutex_;
    bool running_ = false;
    bool stopping_ = false;
    std::unordered_map<
        std::shared_ptr<Socket>, Entry> sockets_;
    int pipefds_[2];
    clock_type::duration idleTimeout_;
#ifdef HAVE_KEVENT
    int kq_;
    std::vector<::kevent> changes_;
#else
    int maxfd_;
    fd_set rset_;
#endif
};

}
