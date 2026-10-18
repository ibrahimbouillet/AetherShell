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

    // At most MAXIMUM_WAIT_OBJECTS - 1 handles. Auto-reset objects are expected; a handle that stays
    // signalled would make its callback run on every iteration. Callbacks run on the UI thread.
    std::uint64_t AddWaitHandle(HANDLE handle, WaitCallback callback);
    void RemoveWaitHandle(std::uint64_t id);

    // Filters run in registration order before TranslateMessage (modeless dialogs use IsDialogMessage).
    std::uint64_t AddMessageFilter(MessageFilter filter);
    void RemoveMessageFilter(std::uint64_t id);

private:
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
