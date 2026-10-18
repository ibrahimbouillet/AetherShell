// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "updates/UpdateTimer.h"

#include "platform/windows/Win32Error.h"

#include <algorithm>

namespace aether::updates {

Result<std::unique_ptr<ThreadpoolUpdateTimer>> ThreadpoolUpdateTimer::Create() {
    std::unique_ptr<ThreadpoolUpdateTimer> timer(new ThreadpoolUpdateTimer());
    timer->timer_ = ::CreateThreadpoolTimer(&ThreadpoolUpdateTimer::OnTimer, timer.get(), nullptr);
    if (timer->timer_ == nullptr) return platform::LastWin32Error("CreateThreadpoolTimer failed");
    return timer;
}

ThreadpoolUpdateTimer::~ThreadpoolUpdateTimer() {
    if (timer_ != nullptr) {
        Disarm();
        ::CloseThreadpoolTimer(timer_);
    }
}

void ThreadpoolUpdateTimer::Arm(std::chrono::milliseconds delay, std::function<void()> callback) {
    {
        std::lock_guard lock(mutex_);
        callback_ = std::move(callback);
    }
    // Negative FILETIME = relative time in 100 ns units (at least 1 unit so it is never "absolute 0").
    // The delay is bounded so the conversion to 100 ns units cannot overflow.
    constexpr std::chrono::milliseconds kMaxDelay = std::chrono::hours{24 * 31};
    const auto clamped = std::clamp(delay, std::chrono::milliseconds{0}, kMaxDelay);
    const auto ticks = std::max<long long>(clamped.count() * 10'000, 1);
    ULARGE_INTEGER due{};
    due.QuadPart = static_cast<ULONGLONG>(-ticks);
    FILETIME dueTime{due.LowPart, due.HighPart};
    // Allow Windows to coalesce the wake-up with others by up to 10 % of the delay (max 5 s).
    const auto window = static_cast<DWORD>(std::min<long long>(clamped.count() / 10, 5'000));
    ::SetThreadpoolTimer(timer_, &dueTime, 0, window);
}

void ThreadpoolUpdateTimer::Disarm() noexcept {
    ::SetThreadpoolTimer(timer_, nullptr, 0, 0);
    ::WaitForThreadpoolTimerCallbacks(timer_, TRUE);
    std::lock_guard lock(mutex_);
    callback_ = nullptr;
}

void CALLBACK ThreadpoolUpdateTimer::OnTimer(PTP_CALLBACK_INSTANCE, PVOID context, PTP_TIMER) {
    auto* self = static_cast<ThreadpoolUpdateTimer*>(context);
    std::function<void()> callback;
    {
        std::lock_guard lock(self->mutex_);
        callback = self->callback_;
    }
    if (!callback) return;
    try {
        callback();
    } catch (...) {
        // Exceptions must never reach the thread pool. The coordinator's callback only posts work.
    }
}

}  // namespace aether::updates
