#pragma once

#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <vector>

// Unbounded blocking FIFO between two pipeline filters. push never blocks, so an HTTP
// thread handing off a scan never waits on analytics; pop blocks until a value or close.
template <class T>
class Pipe {
public:
    void push(T value) {
        {
            std::lock_guard lock(mu_);
            queue_.push_back(std::move(value));
        }
        ready_.notify_one();
    }

    // nullopt only once the pipe is closed AND drained, so no value is lost on shutdown.
    std::optional<T> pop() {
        std::unique_lock lock(mu_);
        ready_.wait(lock, [&] { return !queue_.empty() || closed_; });
        if (queue_.empty()) return std::nullopt;
        T value = std::move(queue_.front());
        queue_.pop_front();
        return value;
    }

    // Everything queued, in order, as one batch; waits for at least one value.
    // Empty only once the pipe is closed and drained.
    std::vector<T> popAll() {
        std::unique_lock lock(mu_);
        ready_.wait(lock, [&] { return !queue_.empty() || closed_; });
        std::vector<T> batch(std::make_move_iterator(queue_.begin()), std::make_move_iterator(queue_.end()));
        queue_.clear();
        return batch;
    }

    void close() {
        {
            std::lock_guard lock(mu_);
            closed_ = true;
        }
        ready_.notify_all();
    }
private:
    std::mutex mu_;
    std::condition_variable ready_;
    std::deque<T> queue_;
    bool closed_ = false;
};
