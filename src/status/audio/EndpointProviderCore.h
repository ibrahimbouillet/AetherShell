// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Lifecycle shared by AudioProvider and MicrophoneProvider: one EndpointMonitor
// per Start()/Stop() cycle, a SnapshotPublisher that turns its readings into
// deduplicated snapshots, and the control calls forwarded to the running
// monitor. Internal to the module.
//
// Guarantees (IStatusProvider contract):
//  * Start() never waits for Core Audio; the first snapshot arrives on the COM thread.
//  * The listener runs on the COM thread, never concurrently with itself, and never after Stop()
//    returns: Stop() closes the publisher, then joins the COM thread, which waits for a listener
//    call already in progress.
//  * Stop() may be called from the listener. It then cannot join its own thread: it closes the
//    publisher, asks the monitor to stop, and keeps it until the next Start(), Stop() or the
//    destructor runs on another thread and joins it. Start() from the listener is refused, and
//    destroying the provider from its own listener is not supported.
//  * No lock is held while joining, so a listener may call Current(), Stop() or the control
//    functions without deadlocking.
#pragma once

#include "core/Result.h"
#include "status/audio/AudioControl.h"
#include "status/audio/EndpointMonitor.h"
#include "status/audio/EndpointReading.h"
#include "status/audio/SnapshotPublisher.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace aether::status::audio {

template <class Snapshot>
class EndpointProviderCore {
public:
    using OriginListener = std::function<void(const Snapshot&, ChangeOrigin)>;
    using SnapshotFactory = Snapshot (*)(const EndpointReading&);

    EndpointProviderCore(EndpointMonitorOptions options, SnapshotFactory factory)
        : options_(std::move(options)), factory_(factory) {}

    ~EndpointProviderCore() {
        Stop();
        std::shared_ptr<EndpointMonitor> retired;
        {
            std::lock_guard lock(mutex_);
            retired = std::move(retired_);
        }
        // `retired` joins here when the provider was last stopped from its own listener.
    }

    EndpointProviderCore(const EndpointProviderCore&) = delete;
    EndpointProviderCore& operator=(const EndpointProviderCore&) = delete;

    Result<void> Start(OriginListener listener) {
        if (!listener) return MakeError(ErrorCode::InvalidArgument, "the status listener must not be empty");
        try {
            std::shared_ptr<EndpointMonitor> retired;
            {
                std::lock_guard lock(mutex_);
                if (monitor_) return MakeError(ErrorCode::InvalidState, "the audio provider is already running");
                if (retired_ && retired_->IsMonitorThread()) {
                    return MakeError(ErrorCode::InvalidState,
                                     "the audio provider cannot be restarted from its own listener");
                }
                retired = retired_;
            }
            if (retired) retired->Stop();  // join outside the lock, including concurrent Stop() calls

            auto monitor = std::make_shared<EndpointMonitor>(options_);
            std::lock_guard lock(mutex_);
            if (monitor_) return MakeError(ErrorCode::InvalidState, "the audio provider is already running");
            if (retired_ == retired) retired_.reset();
            const std::uint64_t session = publisher_.Open(std::move(listener));
            // Runs on the COM thread. A late reading of an older session is ignored by the publisher.
            auto started = monitor->Start([this, session](const EndpointReading& reading, ChangeOrigin origin) {
                publisher_.Publish(session, factory_(reading), origin);
            });
            if (!started.ok()) {
                publisher_.Close();
                return started.error();
            }
            monitor_ = std::move(monitor);
            return Ok();
        } catch (const std::exception& ex) {
            return MakeError(ErrorCode::Internal, std::string("could not start the audio provider: ") + ex.what());
        }
    }

    void Stop() noexcept {
        std::shared_ptr<EndpointMonitor> monitor;
        {
            std::lock_guard lock(mutex_);
            if (monitor_) {
                retired_ = std::move(monitor_);
                publisher_.Close();  // no delivery starts after this point
            }
            monitor = retired_;
        }
        // Keep the retired monitor discoverable until every concurrent Stop() can join it. Never
        // hold mutex_ here: discarded control requests may invoke completions that call us again.
        if (monitor) monitor->Stop();
    }

    [[nodiscard]] Snapshot Current() const { return publisher_.Current(); }

    // Arguments are validated before the running state, so an invalid level is always reported as
    // InvalidArgument and never reaches Core Audio.
    Result<void> SetVolume(float level, std::chrono::milliseconds timeout) {
        auto normalized = NormalizeRequestedVolume(level);
        if (!normalized.ok()) return normalized.error();
        auto monitor = Running();
        if (!monitor) return NotRunning();
        return monitor->SetVolume(normalized.value(), timeout);
    }
    Result<void> SetMuted(bool muted, std::chrono::milliseconds timeout) {
        auto monitor = Running();
        if (!monitor) return NotRunning();
        return monitor->SetMuted(muted, timeout);
    }
    Result<void> ToggleMute(std::chrono::milliseconds timeout) {
        auto monitor = Running();
        if (!monitor) return NotRunning();
        return monitor->ToggleMute(timeout);
    }

    void SetVolumeAsync(float level, ControlCompletion completion) {
        auto normalized = NormalizeRequestedVolume(level);
        if (!normalized.ok()) return CompletionOnce(std::move(completion)).Complete(normalized.error());
        if (auto monitor = Running()) return monitor->SetVolumeAsync(normalized.value(), std::move(completion));
        CompletionOnce(std::move(completion)).Complete(NotRunning());
    }
    void SetMutedAsync(bool muted, ControlCompletion completion) {
        if (auto monitor = Running()) return monitor->SetMutedAsync(muted, std::move(completion));
        CompletionOnce(std::move(completion)).Complete(NotRunning());
    }
    void ToggleMuteAsync(ControlCompletion completion) {
        if (auto monitor = Running()) return monitor->ToggleMuteAsync(std::move(completion));
        CompletionOnce(std::move(completion)).Complete(NotRunning());
    }

private:
    [[nodiscard]] std::shared_ptr<EndpointMonitor> Running() const {
        std::lock_guard lock(mutex_);
        return monitor_;
    }

    [[nodiscard]] Error NotRunning() const {
        return MakeError(ErrorCode::InvalidState, "the audio " + options_.label + " provider is not running");
    }

    const EndpointMonitorOptions options_;
    const SnapshotFactory factory_;
    SnapshotPublisher<Snapshot> publisher_;
    mutable std::mutex mutex_;                   // guards monitor_ and retired_; never held while joining
    std::shared_ptr<EndpointMonitor> monitor_;   // the running monitor
    std::shared_ptr<EndpointMonitor> retired_;   // stopped from its own listener, not joined yet
};

}  // namespace aether::status::audio
