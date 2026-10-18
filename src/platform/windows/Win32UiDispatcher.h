// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Dispatcher.h"
#include "core/Result.h"

#include <windows.h>

#include <deque>
#include <functional>
#include <mutex>

namespace aether::platform {

// IUiDispatcher backed by a message-only window on the UI thread. Work posted from any thread is
// executed by the UI thread's message loop in FIFO order. Batches of posts cost one PostMessage.
class Win32UiDispatcher final : public IUiDispatcher {
public:
    Win32UiDispatcher() = default;
    ~Win32UiDispatcher() override;
    Win32UiDispatcher(const Win32UiDispatcher&) = delete;
    Win32UiDispatcher& operator=(const Win32UiDispatcher&) = delete;

    // Must be called on the UI thread before Post() is used.
    [[nodiscard]] Result<void> Initialize();
    // UI thread. Further posts fail; pending work is discarded.
    void Shutdown() noexcept;

    bool Post(std::function<void()> work) override;
    [[nodiscard]] bool IsUiThread() const noexcept override;

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    void Drain();

    mutable std::mutex mutex_;
    std::deque<std::function<void()>> queue_;
    bool messagePending_ = false;
    bool open_ = false;
    HWND hwnd_ = nullptr;
    DWORD threadId_ = 0;
};

}  // namespace aether::platform
