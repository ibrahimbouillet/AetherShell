// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Holds the latest snapshot of a provider and forwards changes to its listener.
//
//  * Sessions: Open() starts a session and returns its id; Publish() calls carrying another id
//    (left over from a previous Start/Stop cycle) are ignored, and Close() invalidates the session.
//  * Deduplication: the first snapshot of a session is always delivered, later ones only when they
//    differ from the last delivered snapshot (SameSnapshot).
//  * The listener runs outside the lock on the publishing thread. Callers guarantee that Publish()
//    is never invoked concurrently with itself (the providers publish only from their COM thread).
//  * Exceptions thrown by the listener are caught and logged; they never reach the COM thread.
#pragma once

#include "diagnostics/Logger.h"
#include "status/audio/AudioControl.h"
#include "status/audio/EndpointReading.h"

#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace aether::status::audio {

template <class Snapshot>
class SnapshotPublisher {
public:
    using Listener = std::function<void(const Snapshot&, ChangeOrigin)>;

    SnapshotPublisher() = default;
    SnapshotPublisher(const SnapshotPublisher&) = delete;
    SnapshotPublisher& operator=(const SnapshotPublisher&) = delete;

    // Starts a new session. Current() goes back to a default (Initializing) snapshot until the
    // session's first Publish().
    [[nodiscard]] std::uint64_t Open(Listener listener) {
        auto shared = std::make_shared<const Listener>(std::move(listener));
        std::lock_guard lock(mutex_);
        listener_ = std::move(shared);
        published_.reset();
        return ++session_;
    }

    // Ends the session. Current() keeps returning the last delivered snapshot.
    void Close() noexcept {
        std::shared_ptr<const Listener> released;
        {
            std::lock_guard lock(mutex_);
            released = std::move(listener_);
            ++session_;
        }
        // `released` (and whatever the listener captured) is destroyed here, outside the lock.
    }

    // Returns true when the snapshot was delivered to the listener.
    bool Publish(std::uint64_t session, const Snapshot& snapshot, ChangeOrigin origin) {
        std::shared_ptr<const Listener> listener;
        {
            std::lock_guard lock(mutex_);
            if (session != session_ || !listener_) return false;
            if (published_ && SameSnapshot(*published_, snapshot)) return false;
            published_ = snapshot;
            listener = listener_;
        }
        try {
            (*listener)(snapshot, origin);
        } catch (const std::exception&) {
            AETHER_LOG_ERROR("status.audio", "status listener threw an exception");
        } catch (...) {
            AETHER_LOG_ERROR("status.audio", "status listener threw a non-standard exception");
        }
        return true;
    }

    [[nodiscard]] Snapshot Current() const {
        std::lock_guard lock(mutex_);
        return published_ ? *published_ : Snapshot{};
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const Listener> listener_;
    std::optional<Snapshot> published_;
    std::uint64_t session_ = 0;
};

}  // namespace aether::status::audio
