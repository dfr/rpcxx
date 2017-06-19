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

#include <cassert>
#include <glog/logging.h>

#include <rpc++/socket.h>
#include <rpc++/sockman.h>

using namespace oncrpc;

SocketManager::SocketManager()
    : idleTimeout_(std::chrono::seconds(30))
{
    ::pipe(pipefds_);

#ifdef HAVE_KEVENT
    kq_ = ::kqueue();
    struct kevent kev;
    EV_SET(&kev, pipefds_[0], EVFILT_READ, EV_ADD, 0, 0, nullptr);
    changes_.push_back(kev);
#else
    maxfd_ = pipefds_[0];
    FD_ZERO(&rset_);
    FD_SET(pipefds_[0], &rset_);
#endif
}

SocketManager::~SocketManager()
{
#ifdef HAVE_KEVENT
    ::close(kq_);
#endif
    ::close(pipefds_[0]);
    ::close(pipefds_[1]);
}

void
SocketManager::add(std::shared_ptr<Socket> sock)
{
    VLOG(3) << "adding socket " << sock;
    std::unique_lock<std::mutex> lock(mutex_);
    assert(!sock->owner());
    int fd = sock->fd();
#ifdef HAVE_KEVENT
    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_ADD, 0, 0, sock.get());
    changes_.push_back(kev);
#else
    if (sock->fd() >= FD_SETSIZE) {
        LOG(FATAL) << "file descriptor too large for select: " << sock->fd();
    }
    if (fd > maxfd_)
        maxfd_ = fd;
    FD_SET(fd, &rset_);
#endif
    sockets_[sock] = {clock_type::now(), fd};
    sock->setOwner(shared_from_this());
}

void
SocketManager::remove(std::shared_ptr<Socket> sock)
{
    VLOG(3) << "removing socket " << sock;
    std::unique_lock<std::mutex> lock(mutex_);
    assert(sock->owner().get() == this);
    auto i = sockets_.find(sock);
    assert(i != sockets_.end());
    int fd = i->second.fd;
#ifdef HAVE_KEVENT
    struct kevent kev;
    EV_SET(&kev, fd, EVFILT_READ, EV_DELETE, 0, 0, sock.get());
    changes_.push_back(kev);
#else
    FD_CLR(fd, &rset_);
    sockets_.erase(i);
    sock->setOwner(nullptr);
    if (maxfd_ == fd) {
        int maxfd = -1;
        for (auto& entry: sockets_) {
            if (entry.second.fd > maxfd)
                maxfd = entry.second.fd;
        }
        maxfd_ = maxfd;
    }
#endif
}

void
SocketManager::changed(std::shared_ptr<Socket> sock)
{
    VLOG(3) << "socket descriptor changed " << sock;
    std::unique_lock<std::mutex> lock(mutex_);
    assert(sock->owner().get() == this);
    auto i = sockets_.find(sock);
    assert(i != sockets_.end());
    int oldfd = i->second.fd;
    int newfd = sock->fd();
    if (oldfd != newfd) {
#ifdef HAVE_KEVENT
        struct kevent kev;
        EV_SET(&kev, oldfd, EVFILT_READ, EV_DELETE, 0, 0, sock.get());
        changes_.push_back(kev);
        EV_SET(&kev, newfd, EVFILT_READ, EV_ADD, 0, 0, sock.get());
        changes_.push_back(kev);
#else
        FD_CLR(oldfd, &rset_);
        FD_SET(newfd, &rset_);
        i->second.fd = newfd;
        if (newfd > maxfd_)
            maxfd_ = newfd;
        if (maxfd_ == oldfd) {
            int maxfd = -1;
            for (auto& entry: sockets_) {
                if (entry.second.fd > maxfd)
                    maxfd = entry.second.fd;
            }
            maxfd_ = maxfd;
        }
#endif
    }
}

void
SocketManager::run()
{
    std::unique_lock<std::mutex> lock(mutex_);
    running_ = true;
    stopping_ = false;
    while (!stopping_) {
        auto idleLimit = clock_type::now() - idleTimeout_;
        std::vector<std::shared_ptr<Socket>> idle;
        for (const auto& i: sockets_) {
            if (i.first->closeOnIdle() &&
                i.second.time < idleLimit) {
                VLOG(3) << "idle timeout for socket " << i.second.fd;
                idle.push_back(i.first);
                continue;
            }
        }

        if (idle.size() > 0) {
            lock.unlock();
            for (auto sock: idle) {
                remove(sock);
            }
            idle.clear();
            lock.lock();
        }

#ifdef HAVE_KEVENT
        std::vector<::kevent> changes(std::move(changes_));
#else
        fd_set rset = rset_;
        int maxfd = maxfd_;
#endif
        lock.unlock();

        auto stopTime = next();
        auto now = clock_type::now();
        auto timeout =
            std::chrono::duration_cast<std::chrono::system_clock::duration>(
                std::chrono::duration_cast<std::chrono::microseconds>(
                    stopTime - now));
        if (timeout > idleTimeout_)
            timeout = idleTimeout_;
        if (timeout.count() < 0)
            timeout = std::chrono::seconds(0);
        auto sec = timeout.count() / 1000000;
        auto usec = timeout.count() % 1000000;
        if (sec > 999999)
            sec = 999999; //std::numeric_limits<int>::max();
        VLOG(3) << "sleeping for " << sec << "." << usec << "s";
#ifdef HAVE_KEVENT
        std::vector<::kevent> events(256);
        ::timespec ts { int(sec), int(usec * 1000) };
        auto rv = ::kevent(
            kq_, changes.data(), changes.size(),
            events.data(), events.size(), &ts);
#else
        ::timeval tv { int(sec), int(usec) };
        auto rv = ::select(maxfd + 1, &rset, nullptr, nullptr, &tv);
#endif
        if (rv < 0) {
            if (errno == EBADF || errno == EINTR) {
                lock.lock();
                continue;
            }
            throw std::system_error(errno, std::system_category());
        }

        // Execute timeouts, if any
        now = clock_type::now();
        update(now);

        if (rv == 0) {
            lock.lock();
            continue;
        }

#ifdef HAVE_KEVENT
        for (int i = 0; i < rv; i++) {
            auto& ev = events[i];
            // The notification pipe entry has udata set to nullptr
            if (ev.udata == nullptr) {
                char ch;
                ::read(pipefds_[0], &ch, 1);
                lock.lock();
                continue;
            }
            else {
                auto sock = static_cast<Socket*>(ev.udata);
                if (!sock->onReadable(this))
                    remove(sock);
            }
        }
        lock.lock();
#else
        if (FD_ISSET(pipefds_[0], &rset)) {
            char ch;
            ::read(pipefds_[0], &ch, 1);
            lock.lock();
            continue;
        }

        lock.lock();
        std::vector<std::shared_ptr<Socket>> ready;
        for (auto& i: sockets_) {
            if (FD_ISSET(i.second.fd, &rset)) {
                i.second.time = now;
                ready.push_back(i.first);
            }
        }
        lock.unlock();
        for (auto sock: ready) {
            if (!sock->onReadable(this))
                remove(sock);
        }
        lock.lock();
#endif
    }
    running_ = false;
}

void
SocketManager::stop()
{
    std::unique_lock<std::mutex> lock(mutex_);
    stopping_ = true;
    char ch = 0;
    ::write(pipefds_[1], &ch, 1);
}

TimeoutManager::task_type
SocketManager::add(
    clock_type::time_point when, std::function<void()> what)
{
    auto tid = TimeoutManager::add(when, what);
    std::unique_lock<std::mutex> lock(mutex_);
    if (running_) {
        char ch = 0;
        ::write(pipefds_[1], &ch, 1);
    }
    return tid;
}
