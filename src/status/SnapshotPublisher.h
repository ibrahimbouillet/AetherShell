// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Delivery half of the IStatusProvider contract, shared by every provider in this module:
//  * the listener is never invoked concurrently with itself,
//  * once Close() has returned the listener is never invoked again (Close() waits for a call that is
//    in progress on another thread),
//  * Current() is a thread-safe copy of the last published snapshot,
//  * identical snapshots can be suppressed so consumers only hear about real changes.
//
// Close() may be called from inside the listener (for example a consumer that stops the provider in
// reaction to a snapshot); it then marks the publisher closed without waiting for itself.
#pragma once

#include "diagnostics/Logger.h"

#include <atomic>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace aether::status {

template <class Snapshot>
class SnapshotPublisher {
public:
    using Listener = std::function<void(const Snapshot&)>;
    using Equivalent = bool (*)(const Snapshot&, const Snapshot&);

    SnapshotPublisher() = default;
    SnapshotPublisher(const SnapshotPublisher&) = delete;
    SnapshotPublisher& operator=(const SnapshotPublisher&) = delete;

    // Starts delivering to `listener` and resets Current() to a default (Initializing) snapshot.
    void Open(Listener listener) {
        std::lock_guard callLock(callMutex_);
        listener_ = std::move(listener);
        open_ = true;
        hasDelivered_ = false;
        std::lock_guard stateLock(stateMutex_);
        current_ = Snapshot{};
    }

    // Records `snapshot` as current and hands it to the listener. When `equivalent` is given and the
    // snapshot matches the last delivered one, nothing happens. Returns true when the listener ran.
    bool Publish(const Snapshot& snapshot, Equivalent equivalent = nullptr) {
        std::lock_guard callLock(callMutex_);
        if (!open_) return false;
        if (equivalent != nullptr && hasDelivered_ && equivalent(lastDelivered_, snapshot)) return false;
        lastDelivered_ = snapshot;
        hasDelivered_ = true;
        {
            std::lock_guard stateLock(stateMutex_);
            current_ = snapshot;
        }
        callingThread_.store(std::this_thread::get_id());
        try {
            if (listener_) listener_(snapshot);
        } catch (const std::exception&) {
            AETHER_LOG_ERROR("status", "status listener threw an exception");
        } catch (...) {
            AETHER_LOG_ERROR("status", "status listener threw a non-standard exception");
        }
        callingThread_.store(std::thread::id{});
        // Close() was called from inside the listener: release it now that it has returned.
        if (!open_) listener_ = nullptr;
        return true;
    }

    // After this returns the listener is not running on another thread and will not run again.
    void Close() noexcept {
        if (callingThread_.load() == std::this_thread::get_id()) {
            // Re-entrant call from the listener: this thread already holds callMutex_.
            open_ = false;
            return;
        }
        std::lock_guard callLock(callMutex_);
        open_ = false;
        listener_ = nullptr;
    }

    [[nodiscard]] bool IsOpen() const {
        std::lock_guard callLock(callMutex_);
        return open_;
    }

    [[nodiscard]] Snapshot Current() const {
        std::lock_guard stateLock(stateMutex_);
        return current_;
    }

private:
    mutable std::mutex callMutex_;   // serializes listener calls; guards listener_, open_, lastDelivered_
    mutable std::mutex stateMutex_;  // guards current_ only, so Current() never waits for a listener
    Listener listener_;
    bool open_ = false;
    bool hasDelivered_ = false;
    Snapshot lastDelivered_{};
    Snapshot current_{};
    std::atomic<std::thread::id> callingThread_{};
};

}  // namespace aether::status
