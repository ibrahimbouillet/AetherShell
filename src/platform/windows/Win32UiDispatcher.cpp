// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/Win32UiDispatcher.h"

#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"

#include <exception>

namespace aether::platform {
namespace {

constexpr wchar_t kClassName[] = L"AetherShell.UiDispatcher";
constexpr UINT kDrainMessage = WM_APP + 0x101;

}  // namespace

Win32UiDispatcher::~Win32UiDispatcher() { Shutdown(); }

Result<void> Win32UiDispatcher::Initialize() {
    if (hwnd_) return Ok();
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &Win32UiDispatcher::WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kClassName;
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return LastWin32Error("RegisterClassEx(UiDispatcher)");
    }
    hwnd_ = ::CreateWindowExW(0, kClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (!hwnd_) return LastWin32Error("CreateWindowEx(UiDispatcher)");
    threadId_ = ::GetCurrentThreadId();
    std::lock_guard lock(mutex_);
    open_ = true;
    return Ok();
}

void Win32UiDispatcher::Shutdown() noexcept {
    {
        std::lock_guard lock(mutex_);
        open_ = false;
        queue_.clear();
        messagePending_ = false;
    }
    if (hwnd_) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

bool Win32UiDispatcher::Post(std::function<void()> work) {
    if (!work) return false;
    std::lock_guard lock(mutex_);
    if (!open_) return false;
    queue_.push_back(std::move(work));
    if (!messagePending_) {
        if (!::PostMessageW(hwnd_, kDrainMessage, 0, 0)) {
            queue_.pop_back();
            return false;
        }
        messagePending_ = true;
    }
    return true;
}

bool Win32UiDispatcher::IsUiThread() const noexcept { return ::GetCurrentThreadId() == threadId_; }

void Win32UiDispatcher::Drain() {
    std::deque<std::function<void()>> batch;
    {
        std::lock_guard lock(mutex_);
        batch.swap(queue_);
        messagePending_ = false;
    }
    for (auto& work : batch) {
        try {
            work();
        } catch (const std::exception&) {
            AETHER_LOG_ERROR("dispatcher", "UI work item threw an exception");
        } catch (...) {
            AETHER_LOG_ERROR("dispatcher", "UI work item threw a non-standard exception");
        }
    }
}

LRESULT CALLBACK Win32UiDispatcher::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<Win32UiDispatcher*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == kDrainMessage && self) {
        self->Drain();
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace aether::platform
