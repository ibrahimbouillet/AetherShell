// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// One-shot timer abstraction used by UpdateCoordinator for scheduling, so tests
// can drive scheduling deterministically with a fake. The production
// implementation uses a Win32 thread-pool timer (CreateThreadpoolTimer), which
// costs no thread while waiting and lets Windows coalesce wake-ups (a tolerance
// window of a few seconds is requested to save power).
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace aether::updates {

class IUpdateTimer {
public:
    virtual ~IUpdateTimer() = default;

    // Arms the timer, replacing any pending deadline: `callback` runs once, `delay` from now, on an
    // arbitrary thread. The callback must be short (post work elsewhere) and must not call Disarm().
    virtual void Arm(std::chrono::milliseconds delay, std::function<void()> callback) = 0;

    // Cancels the pending deadline. When it returns, no callback is running and none will run.
    // Must not be called from inside the callback.
    virtual void Disarm() noexcept = 0;
};

class ThreadpoolUpdateTimer final : public IUpdateTimer {
public:
    [[nodiscard]] static Result<std::unique_ptr<ThreadpoolUpdateTimer>> Create();
    ~ThreadpoolUpdateTimer() override;

    ThreadpoolUpdateTimer(const ThreadpoolUpdateTimer&) = delete;
    ThreadpoolUpdateTimer& operator=(const ThreadpoolUpdateTimer&) = delete;

    void Arm(std::chrono::milliseconds delay, std::function<void()> callback) override;
    void Disarm() noexcept override;

private:
    ThreadpoolUpdateTimer() = default;
    static void CALLBACK OnTimer(PTP_CALLBACK_INSTANCE instance, PVOID context, PTP_TIMER timer);

    PTP_TIMER timer_ = nullptr;
    std::mutex mutex_;
    std::function<void()> callback_;
};

}  // namespace aether::updates
