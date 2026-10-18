// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/ClockProvider.h"

#include "diagnostics/Logger.h"
#include "platform/windows/Handles.h"
#include "platform/windows/Win32Error.h"
#include "status/ProviderHost.h"

#include <algorithm>
#include <atomic>

namespace aether::status {
namespace {

constexpr std::int64_t kTicksPerMs = 10'000;
// The watchdog wait ends this long after the expected boundary. It only matters when the system
// clock was set backwards: the absolute timer then fires late by the size of the jump.
constexpr std::int64_t kWatchdogSlackMs = 2'000;
// A boundary observed this late means the machine slept or the clock moved forward.
constexpr std::int64_t kLateThresholdTicks = 2 * kFileTimeTicksPerSecond;

// Coalescing tolerance handed to the kernel: invisible on a clock display, kinder to the battery.
ULONG TolerableDelayMs(ClockResolution resolution) noexcept {
    return resolution == ClockResolution::Seconds ? 8 : 50;
}

std::int64_t NowTicks() noexcept {
    FILETIME now{};
    ::GetSystemTimePreciseAsFileTime(&now);
    return FileTimeToTicks(now);
}

std::string_view ToString(ClockResolution resolution) noexcept {
    return resolution == ClockResolution::Seconds ? "seconds" : "minutes";
}

enum class WakeReason { Stop, Requested, Boundary, ClockJumped };

}  // namespace

struct ClockProvider::Impl {
    explicit Impl(ClockResolution initial) : resolution(initial) {}

    void Run(HANDLE stopEvent, HANDLE wakeEvent);
    ClockSnapshot Read(std::int64_t utcTicks, bool forceZoneRefresh);
    WakeReason WaitForBoundary(HANDLE stopEvent, HANDLE wakeEvent, HANDLE timer, std::int64_t target,
                               ClockResolution activeResolution);

    std::atomic<ClockResolution> resolution;
    // Owned by the provider thread.
    TimeZoneDescription zone;
    int lastMinute = -1;
    bool zoneErrorLogged = false;
    bool armErrorLogged = false;

    ProviderHost<ClockSnapshot> host;  // last: joined before the members above are destroyed
};

ClockSnapshot ClockProvider::Impl::Read(std::int64_t utcTicks, bool forceZoneRefresh) {
    // Local time is derived from the same UTC reading (FileTimeToLocalFileTime applies the current bias,
    // exactly like GetLocalTime) so `utc` and the local fields always describe the same instant.
    const FILETIME utc = TicksToFileTime(utcTicks);
    FILETIME localFileTime{};
    SYSTEMTIME local{};
    if (!::FileTimeToLocalFileTime(&utc, &localFileTime) || !::FileTimeToSystemTime(&localFileTime, &local)) {
        const auto error = platform::LastWin32Error("FileTimeToLocalFileTime");
        AETHER_LOG_ERROR("status.clock", "cannot convert the system time: {}", error.Describe());
        ClockSnapshot failed;
        failed.state = ProviderState::Error;
        failed.utc = TicksToTimePoint(utcTicks);
        return failed;
    }

    // The zone (and whether daylight saving time is in effect) can only change at a minute boundary,
    // or through a time/zone change the application reports with NotifyTimeChanged().
    if (forceZoneRefresh || local.wMinute != lastMinute) {
        DYNAMIC_TIME_ZONE_INFORMATION info{};
        const DWORD zoneId = ::GetDynamicTimeZoneInformation(&info);
        if (auto described = DescribeTimeZone(zoneId, info)) {
            zone = std::move(*described);
            zoneErrorLogged = false;
        } else {
            zone = {};  // unknown: no name rather than a stale one
            if (!zoneErrorLogged) {
                AETHER_LOG_WARN("status.clock", "GetDynamicTimeZoneInformation failed: {}",
                                platform::LastWin32Error("time zone").Describe());
                zoneErrorLogged = true;
            }
        }
    }
    lastMinute = local.wMinute;
    return MakeClockSnapshot(utcTicks, local, zone);
}

WakeReason ClockProvider::Impl::WaitForBoundary(HANDLE stopEvent, HANDLE wakeEvent, HANDLE timer,
                                                std::int64_t target, ClockResolution activeResolution) {
    for (;;) {
        const std::int64_t now = NowTicks();
        const std::int64_t remainingMs = std::max<std::int64_t>(0, (target - now + kTicksPerMs - 1) / kTicksPerMs);

        LARGE_INTEGER due{};
        due.QuadPart = target;  // positive: absolute UTC time, re-evaluated by the kernel if the clock changes
        const ULONG tolerance = TolerableDelayMs(activeResolution);
        const bool armed = ::SetWaitableTimerEx(timer, &due, 0, nullptr, nullptr, nullptr, tolerance) != FALSE;
        if (!armed && !armErrorLogged) {
            AETHER_LOG_ERROR("status.clock", "SetWaitableTimerEx failed, using timed waits: {}",
                             platform::LastWin32Error("SetWaitableTimerEx").Describe());
            armErrorLogged = true;
        }

        const DWORD result = armed ? WaitForAny({stopEvent, wakeEvent, timer},
                                                static_cast<DWORD>(remainingMs + kWatchdogSlackMs))
                                   : WaitForAny({stopEvent, wakeEvent}, static_cast<DWORD>(remainingMs));
        if (result == 0) return WakeReason::Stop;
        if (result == 1) return WakeReason::Requested;
        if (result == WAIT_FAILED) {
            AETHER_LOG_ERROR("status.clock", "wait failed: {}",
                             platform::LastWin32Error("WaitForMultipleObjects").Describe());
            // Avoid a hot loop; still honour Stop() promptly.
            if (::WaitForSingleObject(stopEvent, 1'000) == WAIT_OBJECT_0) return WakeReason::Stop;
            return WakeReason::ClockJumped;
        }
        if (result == WAIT_TIMEOUT && armed) {
            AETHER_LOG_INFO("status.clock", "boundary not reached in time; the system clock moved backwards");
            return WakeReason::ClockJumped;
        }

        // The timer fired (or the fallback wait elapsed).
        const std::int64_t after = NowTicks();
        if (after < target) {
            if (target - after > kFileTimeTicksPerSecond) return WakeReason::ClockJumped;
            continue;  // woke a hair early (timer granularity): wait for the same boundary again
        }
        if (after - target > kLateThresholdTicks) {
            AETHER_LOG_INFO("status.clock", "boundary observed {} s late (resume from sleep or clock moved forward)",
                            (after - target) / kFileTimeTicksPerSecond);
            return WakeReason::ClockJumped;
        }
        return WakeReason::Boundary;
    }
}

void ClockProvider::Impl::Run(HANDLE stopEvent, HANDLE wakeEvent) {
    platform::UniqueHandle timer(::CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_MODIFY_STATE | SYNCHRONIZE));
    if (!timer) {
        const auto error = platform::LastWin32Error("CreateWaitableTimerEx");
        AETHER_LOG_ERROR("status.clock", "{}", error.Describe());
        ClockSnapshot failed;
        failed.state = ProviderState::Error;
        host.Publish(failed);
        ::WaitForSingleObject(stopEvent, INFINITE);
        return;
    }

    zone = {};
    lastMinute = -1;
    bool forceZoneRefresh = true;
    for (;;) {
        const ClockResolution current = resolution.load();
        const std::int64_t now = NowTicks();
        host.Publish(Read(now, forceZoneRefresh));
        forceZoneRefresh = false;

        switch (WaitForBoundary(stopEvent, wakeEvent, timer.Get(), NextBoundaryTicks(now, current), current)) {
            case WakeReason::Stop: ::CancelWaitableTimer(timer.Get()); return;
            case WakeReason::Requested:
            case WakeReason::ClockJumped: forceZoneRefresh = true; break;
            case WakeReason::Boundary: break;
        }
    }
}

ClockProvider::ClockProvider(ClockResolution resolution) : impl_(std::make_shared<Impl>(resolution)) {}

ClockProvider::~ClockProvider() { Stop(); }

Result<void> ClockProvider::Start(Listener listener) {
    const auto impl = impl_;
    auto started = impl->host.Start(std::move(listener), L"Aether status: clock",
                                    [impl](HANDLE stop, const std::shared_ptr<WakeSignal>& wake) {
                                        impl->Run(stop, wake->Handle());
                                    });
    if (started) AETHER_LOG_INFO("status.clock", "clock provider started ({})", ToString(impl->resolution.load()));
    return started;
}

void ClockProvider::Stop() noexcept { impl_->host.Stop(); }

ClockSnapshot ClockProvider::Current() const { return impl_->host.Current(); }

void ClockProvider::SetResolution(ClockResolution resolution) noexcept {
    if (impl_->resolution.exchange(resolution) != resolution) impl_->host.Wake();
}

ClockResolution ClockProvider::Resolution() const noexcept { return impl_->resolution.load(); }

void ClockProvider::NotifyTimeChanged() noexcept { impl_->host.Wake(); }

}  // namespace aether::status
