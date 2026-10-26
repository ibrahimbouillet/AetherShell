// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Event-driven frame pacing. Windows request a frame only when something
// changed or an animation is running; the scheduler arms a high-resolution
// waitable timer for the next display refresh and calls every client that
// asked. With no requests the timer stays disarmed and the UI thread sleeps,
// so an idle desktop costs no CPU.
#pragma once

#include "core/Result.h"
#include "platform/windows/MessageLoop.h"

#include <windows.h>

#include <cstdint>
#include <vector>

namespace aether::graphics {

class IFrameClient {
public:
    virtual ~IFrameClient() = default;
    // Called on the UI thread. `now` is FrameScheduler::Now().
    virtual void OnFrame(double now) = 0;
};

class FrameScheduler {
public:
    FrameScheduler() = default;
    ~FrameScheduler();
    FrameScheduler(const FrameScheduler&) = delete;
    FrameScheduler& operator=(const FrameScheduler&) = delete;

    [[nodiscard]] Result<void> Initialize(platform::MessageLoop& loop);
    void Shutdown() noexcept;

    // Schedules `client` for the next frame (deduplicated). UI thread only.
    void RequestFrame(IFrameClient* client);
    // Must be called before a client is destroyed.
    void Cancel(IFrameClient* client) noexcept;

    // Monotonic seconds (QueryPerformanceCounter).
    [[nodiscard]] static double Now() noexcept;
    // Display refresh interval reported by DWM (falls back to 60 Hz).
    [[nodiscard]] double RefreshInterval() const noexcept { return refreshInterval_; }
    // The loop servicing the frame timer (null before Initialize). Nested modal loops (glass menus)
    // use MessageLoop::RunNested on it so frames keep ticking while they run.
    [[nodiscard]] platform::MessageLoop* Loop() const noexcept { return loop_; }

private:
    void Arm();
    void Tick();
    void UpdateRefreshInterval() noexcept;

    platform::MessageLoop* loop_ = nullptr;
    HANDLE timer_ = nullptr;
    std::uint64_t waitId_ = 0;
    bool armed_ = false;
    double refreshInterval_ = 1.0 / 60.0;
    double lastRefreshQuery_ = 0.0;
    double lastTick_ = 0.0;  // start of the most recent frame (steady cadence while animating)
    std::vector<IFrameClient*> pending_;
    std::vector<IFrameClient*> running_;
};

}  // namespace aether::graphics
