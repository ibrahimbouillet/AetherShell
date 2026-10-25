// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/MessageLoop.h"

#include "diagnostics/Logger.h"

#include <algorithm>
#include <exception>

namespace aether::platform {

std::uint64_t MessageLoop::AddWaitHandle(HANDLE handle, WaitCallback callback) {
    if (!handle || !callback || waits_.size() >= MAXIMUM_WAIT_OBJECTS - 1) return 0;
    const auto id = nextId_++;
    waits_.push_back(WaitEntry{id, handle, std::move(callback)});
    return id;
}

void MessageLoop::RemoveWaitHandle(std::uint64_t id) {
    std::erase_if(waits_, [id](const WaitEntry& entry) { return entry.id == id; });
}

std::uint64_t MessageLoop::AddMessageFilter(MessageFilter filter) {
    if (!filter) return 0;
    const auto id = nextId_++;
    filters_.push_back(FilterEntry{id, std::move(filter)});
    return id;
}

void MessageLoop::RemoveMessageFilter(std::uint64_t id) {
    std::erase_if(filters_, [id](const FilterEntry& entry) { return entry.id == id; });
}

void MessageLoop::WaitOnce() {
    std::vector<HANDLE> handles;
    std::vector<std::uint64_t> ids;
    handles.reserve(waits_.size());
    ids.reserve(waits_.size());
    for (const auto& wait : waits_) {
        handles.push_back(wait.handle);
        ids.push_back(wait.id);
    }
    const DWORD count = static_cast<DWORD>(handles.size());
    const DWORD result = ::MsgWaitForMultipleObjectsEx(count, count ? handles.data() : nullptr, INFINITE,
                                                       QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (result < WAIT_OBJECT_0 + count) {
        const auto id = ids[result - WAIT_OBJECT_0];
        // Copy the callback: it may remove its own registration.
        auto it = std::find_if(waits_.begin(), waits_.end(), [id](const WaitEntry& e) { return e.id == id; });
        if (it != waits_.end()) {
            auto callback = it->callback;
            try {
                callback();
            } catch (const std::exception&) {
                AETHER_LOG_ERROR("loop", "wait callback threw an exception");
            } catch (...) {
                AETHER_LOG_ERROR("loop", "wait callback threw a non-standard exception");
            }
        }
    } else if (result == WAIT_FAILED) {
        AETHER_LOG_ERROR("loop", "MsgWaitForMultipleObjectsEx failed ({}); dropping wait handles",
                         ::GetLastError());
        waits_.clear();
    }
}

int MessageLoop::Run() {
    for (;;) {
        WaitOnce();
        MSG message;
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) return static_cast<int>(message.wParam);
            bool handled = false;
            for (std::size_t i = 0; i < filters_.size() && !handled; ++i) {
                auto filter = filters_[i].filter;
                handled = filter(message);
            }
            if (!handled) {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
        }
    }
}

bool MessageLoop::RunNested(const std::function<bool()>& done, const MessageFilter& intercept, bool runFilters) {
    const auto finished = [&done] {
        try {
            return !done || done();
        } catch (...) {
            AETHER_LOG_ERROR("loop", "nested loop condition threw; ending the nested loop");
            return true;
        }
    };
    while (!finished()) {
        WaitOnce();
        MSG message;
        while (!finished() && ::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                ::PostQuitMessage(static_cast<int>(message.wParam));
                return false;
            }
            bool handled = false;
            if (intercept) {
                try {
                    handled = intercept(message);
                } catch (...) {
                    AETHER_LOG_ERROR("loop", "nested loop message interceptor threw");
                }
            }
            for (std::size_t i = 0; runFilters && i < filters_.size() && !handled; ++i) {
                auto filter = filters_[i].filter;
                handled = filter(message);
            }
            if (!handled) {
                ::TranslateMessage(&message);
                ::DispatchMessageW(&message);
            }
        }
    }
    return true;
}

}  // namespace aether::platform
