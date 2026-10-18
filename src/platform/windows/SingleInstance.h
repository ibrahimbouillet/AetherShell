// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Per-user single-instance enforcement plus a tiny command channel so a
// second launch (or the updater) can ask the running instance to act.
#pragma once

#include "core/Result.h"
#include "platform/windows/Handles.h"

#include <windows.h>

#include <functional>
#include <string>
#include <string_view>

namespace aether::platform {

// String form of the current user's SID ("S-1-5-21-...").
[[nodiscard]] Result<std::wstring> CurrentUserSid();

class SingleInstanceLock {
public:
    SingleInstanceLock() = default;
    // Creates the named mutex "Local\<name>.<user SID>". The Local\ namespace scopes it to the
    // current logon session; the SID scopes it to the user.
    [[nodiscard]] static Result<SingleInstanceLock> Acquire(std::wstring_view name);
    [[nodiscard]] bool IsPrimary() const noexcept { return primary_; }

private:
    UniqueHandle mutex_;
    bool primary_ = false;
};

// Commands are short ASCII words chosen by the application ("activate", "settings", "shutdown").
// The receiver must validate them against an allow-list; payloads are never executed.
class InstanceChannel {
public:
    using Handler = std::function<void(std::wstring_view command)>;

    InstanceChannel() = default;
    ~InstanceChannel();
    InstanceChannel(const InstanceChannel&) = delete;
    InstanceChannel& operator=(const InstanceChannel&) = delete;

    // Primary instance, UI thread. `handler` runs on the UI thread.
    [[nodiscard]] Result<void> Listen(std::wstring_view channelName, Handler handler);
    void Close() noexcept;

    // Secondary instance or helper process. Grants the receiver permission to take the foreground
    // (AllowSetForegroundWindow) before sending, so it can bring a window forward.
    [[nodiscard]] static Result<void> Send(std::wstring_view channelName, std::wstring_view command,
                                           DWORD timeoutMs = 3000);

    // Process id of the listening instance, or 0 when none is found.
    [[nodiscard]] static DWORD FindListenerProcessId(std::wstring_view channelName);

private:
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    HWND hwnd_ = nullptr;
    std::wstring className_;
    Handler handler_;
};

}  // namespace aether::platform
