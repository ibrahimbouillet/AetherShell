// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The dedicated background thread every status provider runs on, plus the kernel events used to
// wake it. Providers do their blocking, COM and WinRT work on this thread, never on the UI thread.
#pragma once

#include "core/Result.h"
#include "platform/windows/Handles.h"

#include <windows.h>

#include <functional>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace aether::status {

// Auto-reset event that can be signalled from any thread, including WinRT event handlers that may
// still be running after their registration was revoked. Handlers capture a shared_ptr to it, so the
// handle stays valid for as long as any of them can call Signal().
class WakeSignal {
public:
    // Fails when the event cannot be created.
    [[nodiscard]] static Result<std::shared_ptr<WakeSignal>> Create();

    void Signal() const noexcept { ::SetEvent(event_.Get()); }
    [[nodiscard]] HANDLE Handle() const noexcept { return event_.Get(); }

private:
    platform::UniqueHandle event_;
};

class ProviderThread {
public:
    // Receives the manual-reset stop event; the body must return soon after it is signalled.
    using Body = std::function<void(HANDLE stopEvent)>;

    ProviderThread();
    ~ProviderThread();
    ProviderThread(const ProviderThread&) = delete;
    ProviderThread& operator=(const ProviderThread&) = delete;

    // Each instance is one session and starts at most once. The owning host serializes Start against
    // lifecycle publication; Stop may then run concurrently on any thread.
    [[nodiscard]] Result<void> Start(std::wstring name, Body body, std::function<void()> completed = {});

    // Signals immediately, then serializes external joins. Worker-side calls never acquire the join
    // mutex, so a listener may stop itself while another thread waits for it to finish.
    void Stop() noexcept;
    void RequestStop() noexcept;

    [[nodiscard]] bool IsCurrentThread() const noexcept;
    [[nodiscard]] bool IsFinished() const noexcept;

private:
    struct State;
    std::shared_ptr<State> state_;
    std::mutex joinMutex_;  // external callers only; never taken by the worker
    std::thread thread_;
};

// Waits on `handles` (at most MAXIMUM_WAIT_OBJECTS) and returns the index of the signalled handle,
// WAIT_TIMEOUT on timeout, or WAIT_FAILED. Thin wrapper that keeps call sites readable.
[[nodiscard]] DWORD WaitForAny(std::initializer_list<HANDLE> handles, DWORD timeoutMs) noexcept;

}  // namespace aether::status
