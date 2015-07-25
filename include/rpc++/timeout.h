// -*- c++ -*-

#pragma once

#include <chrono>
#include <deque>
#include <functional>
#include <queue>
#include <thread>

namespace oncrpc {

class TimeoutManager
{
public:
    typedef std::chrono::system_clock clock_type;
    typedef int task_type;

    virtual task_type add(
        clock_type::time_point when, std::function<void()> what);
    void update(clock_type::time_point now);
    void cancel(task_type tid);
    clock_type::time_point next() const;

protected:
    struct Task
    {
        task_type tid;
        std::chrono::system_clock::time_point when;
        std::function<void()> what;
    };

    struct Comp
    {
        int operator()(const Task& t1, const Task& t2)
        {
            return t1.when > t2.when;
        }
    };

    mutable std::mutex mutex_;
    task_type nextTid_ = 1;
    std::deque<Task> queue_;
};

}
