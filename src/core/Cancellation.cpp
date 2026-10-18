// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Cancellation.h"

#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>

namespace aether {
namespace detail {

struct CancellationState {
    std::atomic<bool> cancelled{false};
    std::mutex mutex;
    std::condition_variable idle;
    std::map<std::uint64_t, std::function<void()>> callbacks;
    std::uint64_t nextId = 1;
    std::uint64_t executingId = 0;
    std::thread::id executingThread;
};

}  // namespace detail

bool CancellationToken::IsCancelled() const noexcept {
    return state_ && state_->cancelled.load(std::memory_order_acquire);
}

CancellationToken::Registration CancellationToken::OnCancel(std::function<void()> callback) const {
    if (!state_ || !callback) return {};
    {
        std::lock_guard lock(state_->mutex);
        if (!state_->cancelled.load(std::memory_order_acquire)) {
            const auto id = state_->nextId++;
            state_->callbacks.emplace(id, std::move(callback));
            return Registration(state_, id);
        }
    }
    callback();
    return {};
}

CancellationToken::Registration::Registration(std::shared_ptr<detail::CancellationState> state,
                                              std::uint64_t id) noexcept
    : state_(std::move(state)), id_(id) {}

CancellationToken::Registration::Registration(Registration&& other) noexcept
    : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)) {}

CancellationToken::Registration& CancellationToken::Registration::operator=(Registration&& other) noexcept {
    if (this != &other) {
        Reset();
        state_ = std::move(other.state_);
        id_ = std::exchange(other.id_, 0);
    }
    return *this;
}

CancellationToken::Registration::~Registration() { Reset(); }

void CancellationToken::Registration::Reset() noexcept {
    if (!state_ || id_ == 0) {
        state_.reset();
        return;
    }
    {
        std::unique_lock lock(state_->mutex);
        state_->callbacks.erase(id_);
        // Wait for an in-flight invocation of this callback on another thread.
        state_->idle.wait(lock, [&] {
            return state_->executingId != id_ || state_->executingThread == std::this_thread::get_id();
        });
    }
    state_.reset();
    id_ = 0;
}

CancellationSource::CancellationSource() : state_(std::make_shared<detail::CancellationState>()) {}

CancellationToken CancellationSource::Token() const noexcept { return CancellationToken(state_); }

bool CancellationSource::IsCancelled() const noexcept { return state_->cancelled.load(std::memory_order_acquire); }

void CancellationSource::Cancel() noexcept {
    if (state_->cancelled.exchange(true, std::memory_order_acq_rel)) return;
    std::unique_lock lock(state_->mutex);
    while (!state_->callbacks.empty()) {
        auto it = state_->callbacks.begin();
        const auto id = it->first;
        auto callback = std::move(it->second);
        state_->callbacks.erase(it);
        state_->executingId = id;
        state_->executingThread = std::this_thread::get_id();
        lock.unlock();
        try {
            callback();
        } catch (...) {
            // Cancellation callbacks must not throw; swallow to keep the source consistent.
        }
        lock.lock();
        state_->executingId = 0;
        state_->executingThread = {};
        state_->idle.notify_all();
    }
}

}  // namespace aether
