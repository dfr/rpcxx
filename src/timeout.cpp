/*-
 * Copyright (c) 2016 Doug Rabson
 * All rights reserved.
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
        auto task = std::move(queue_.front());
        std::pop_heap(queue_.begin(), queue_.end(), Comp());
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
