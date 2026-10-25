// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// UI-thread message loop that also waits on kernel handles (for example the
// frame timer) without polling: the thread sleeps in MsgWaitForMultipleObjectsEx
// until a message arrives or a registered handle is signalled.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace aether::platform {

class MessageLoop {
public:
    using WaitCallback = std::function<void()>;
    // Return true when the message was fully handled (it is then not translated/dispatched).
    using MessageFilter = std::function<bool(MSG& message)>;

    // Runs until WM_QUIT and returns its exit code.
    int Run();

    // A nested modal loop on the same thread (glass menus, confirmations). The registered wait
    // handles keep being serviced, so frame timers and dispatcher work continue while it runs.
    // `intercept` (optional) sees every retrieved message first and returns true when it consumed
    // it; otherwise the message filters run when `runFilters` is set, then it is translated and
    // dispatched as in Run(). `done` is checked before every wait and after every message. Returns
    // true when `done` ended the loop; false after WM_QUIT, which is re-posted for the outer loop.
    bool RunNested(const std::function<bool()>& done, const MessageFilter& intercept = {}, bool runFilters = true);

    // At most MAXIMUM_WAIT_OBJECTS - 1 handles. Auto-reset objects are expected; a handle that stays
    // signalled would make its callback run on every iteration. Callbacks run on the UI thread.
    std::uint64_t AddWaitHandle(HANDLE handle, WaitCallback callback);
    void RemoveWaitHandle(std::uint64_t id);

    // Filters run in registration order before TranslateMessage (modeless dialogs use IsDialogMessage).
    std::uint64_t AddMessageFilter(MessageFilter filter);
    void RemoveMessageFilter(std::uint64_t id);

private:
    // Waits for input or one registered handle and runs that handle's callback.
    void WaitOnce();
    struct WaitEntry {
        std::uint64_t id;
        HANDLE handle;
        WaitCallback callback;
    };
    struct FilterEntry {
        std::uint64_t id;
        MessageFilter filter;
    };
    std::vector<WaitEntry> waits_;
    std::vector<FilterEntry> filters_;
    std::uint64_t nextId_ = 1;
};

}  // namespace aether::platform
