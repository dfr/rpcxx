/*-
 * Copyright (c) 2016-2017 Doug Rabson
 * All rights reserved.
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
