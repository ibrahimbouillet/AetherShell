// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Thread-safe hand-off of work to the UI thread. Background services publish
// results through a dispatcher instead of touching UI state directly.
#pragma once

#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace aether {

class IUiDispatcher {
public:
    virtual ~IUiDispatcher() = default;

    // Queues `work` to run on the UI thread, in FIFO order. Safe to call from any thread.
    // Returns false and drops `work` once the dispatcher has shut down.
    virtual bool Post(std::function<void()> work) = 0;

    [[nodiscard]] virtual bool IsUiThread() const noexcept = 0;
};

// Deterministic dispatcher for tests: work runs only when RunPending() is called.
class ManualDispatcher final : public IUiDispatcher {
public:
    ManualDispatcher() : owner_(std::this_thread::get_id()) {}

    bool Post(std::function<void()> work) override {
        std::lock_guard lock(mutex_);
        if (closed_) return false;
        queue_.push_back(std::move(work));
        return true;
    }

    [[nodiscard]] bool IsUiThread() const noexcept override { return std::this_thread::get_id() == owner_; }

    // Runs queued work (including work queued while running) and returns how many items ran.
    std::size_t RunPending() {
        std::size_t count = 0;
        for (;;) {
            std::function<void()> work;
            {
                std::lock_guard lock(mutex_);
                if (queue_.empty()) return count;
                work = std::move(queue_.front());
                queue_.pop_front();
            }
            work();
            ++count;
        }
    }

    [[nodiscard]] std::size_t PendingCount() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    void Close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        queue_.clear();
    }

private:
    mutable std::mutex mutex_;
    std::deque<std::function<void()>> queue_;
    std::thread::id owner_;
    bool closed_ = false;
};

}  // namespace aether
