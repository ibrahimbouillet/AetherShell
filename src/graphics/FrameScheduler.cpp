// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/FrameScheduler.h"

#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"

#include <dwmapi.h>

#include <algorithm>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace aether::graphics {

FrameScheduler::~FrameScheduler() { Shutdown(); }

double FrameScheduler::Now() noexcept {
    static const double frequency = [] {
        LARGE_INTEGER f{};
        ::QueryPerformanceFrequency(&f);
        return static_cast<double>(f.QuadPart);
    }();
    LARGE_INTEGER counter{};
    ::QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / frequency;
}

Result<void> FrameScheduler::Initialize(platform::MessageLoop& loop) {
    if (timer_) return Ok();
    // High-resolution timers (Windows 10 1803+) give sub-millisecond precision without raising the
    // global timer resolution; older systems fall back to a regular waitable timer.
    timer_ = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer_) timer_ = ::CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
    if (!timer_) return platform::LastWin32Error("CreateWaitableTimerEx(frame)");
    loop_ = &loop;
    waitId_ = loop.AddWaitHandle(timer_, [this] { Tick(); });
    if (waitId_ == 0) return MakeError(ErrorCode::Internal, "message loop rejected the frame timer");
    UpdateRefreshInterval();
    return Ok();
}

void FrameScheduler::Shutdown() noexcept {
    if (loop_ && waitId_) loop_->RemoveWaitHandle(waitId_);
    waitId_ = 0;
    loop_ = nullptr;
    if (timer_) {
        ::CancelWaitableTimer(timer_);
        ::CloseHandle(timer_);
        timer_ = nullptr;
    }
    pending_.clear();
    running_.clear();
    armed_ = false;
}

void FrameScheduler::UpdateRefreshInterval() noexcept {
    DWM_TIMING_INFO info{};
    info.cbSize = sizeof(info);
    if (SUCCEEDED(::DwmGetCompositionTimingInfo(nullptr, &info)) && info.qpcRefreshPeriod > 0) {
        LARGE_INTEGER frequency{};
        ::QueryPerformanceFrequency(&frequency);
        const double interval = static_cast<double>(info.qpcRefreshPeriod) / static_cast<double>(frequency.QuadPart);
        // Ignore implausible values (outside 24..360 Hz).
        if (interval > 1.0 / 361.0 && interval < 1.0 / 23.0) refreshInterval_ = interval;
    }
    lastRefreshQuery_ = Now();
}

void FrameScheduler::RequestFrame(IFrameClient* client) {
    if (!client || !timer_) return;
    if (std::find(pending_.begin(), pending_.end(), client) == pending_.end()) pending_.push_back(client);
    Arm();
}

void FrameScheduler::Cancel(IFrameClient* client) noexcept {
    std::erase(pending_, client);
    std::replace(running_.begin(), running_.end(), client, static_cast<IFrameClient*>(nullptr));
}

void FrameScheduler::Arm() {
    if (armed_ || !timer_) return;
    const double now = Now();
    if (now - lastRefreshQuery_ > 5.0) UpdateRefreshInterval();
    // Continuous animation keeps one refresh interval between frame starts: a frame's own work is
    // not added on top (a 10 ms frame would otherwise run every 27 ms). After a pause, or when a
    // frame overran, the next one is a full interval away / almost immediate respectively.
    double delay = refreshInterval_;
    if (lastTick_ > 0.0 && now - lastTick_ < refreshInterval_ * 1.5) delay = std::max(0.001, lastTick_ + refreshInterval_ - now);
    // Relative due time in 100 ns units (negative = relative).
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(delay * 10'000'000.0);
    if (::SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0)) {
        armed_ = true;
    } else {
        AETHER_LOG_ERROR("graphics", "SetWaitableTimerEx failed ({})", ::GetLastError());
    }
}

void FrameScheduler::Tick() {
    armed_ = false;
    running_.swap(pending_);
    pending_.clear();
    const double now = Now();
    lastTick_ = now;
    for (std::size_t i = 0; i < running_.size(); ++i) {
        if (IFrameClient* client = running_[i]) client->OnFrame(now);
    }
    running_.clear();
}

}  // namespace aether::graphics
