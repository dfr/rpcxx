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

#include <glog/logging.h>

#include <rpc++/timeout.h>

using namespace oncrpc;

TimeoutManager::task_type
TimeoutManager::add(
    clock_type::time_point when,
    std::function<void()> what)
{
    std::unique_lock<std::mutex> lock(mutex_);
    auto tid = nextTid_++;
    queue_.push_back(Task{tid, when, what});
    std::push_heap(queue_.begin(), queue_.end(), Comp());
    return tid;
}

void
TimeoutManager::update(clock_type::time_point now)
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (queue_.size() > 0 && now >= queue_.front().when) {
        std::pop_heap(queue_.begin(), queue_.end(), Comp());
        auto task = std::move(queue_.back());
        queue_.pop_back();
        lock.unlock();
        VLOG(3) << "calling timeout function";
        task.what();
        lock.lock();
    }
}

void
TimeoutManager::cancel(task_type tid)
{
    std::unique_lock<std::mutex> lock(mutex_);
    for (auto i = queue_.begin(); i != queue_.end(); ++i) {
        if (i->tid == tid) {
            queue_.erase(i);
            std::make_heap(queue_.begin(), queue_.end(), Comp());
            return;
        }
    }
}

TimeoutManager::clock_type::time_point
TimeoutManager::next() const
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() > 0)
        return queue_.front().when;
    else
        return clock_type::time_point::max();
}
