// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/SingleInstance.h"

#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"

#include <sddl.h>

#include <memory>
#include <vector>

namespace aether::platform {
namespace {

constexpr ULONG_PTR kCommandSignature = 0x41455448;  // 'AETH'
constexpr size_t kMaxCommandChars = 64;

std::wstring ChannelClassName(std::wstring_view channelName) {
    std::wstring name = L"AetherShell.Channel.";
    name += channelName;
    if (auto sid = CurrentUserSid()) {
        name += L".";
        name += sid.value();
    }
    // Window class names are limited to 256 characters.
    if (name.size() > 250) name.resize(250);
    return name;
}

}  // namespace

Result<std::wstring> CurrentUserSid() {
    UniqueHandle token;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, token.Put())) {
        return LastWin32Error("OpenProcessToken");
    }
    DWORD size = 0;
    ::GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &size);
    if (size == 0) return LastWin32Error("GetTokenInformation(size)");
    std::vector<std::byte> buffer(size);
    if (!::GetTokenInformation(token.Get(), TokenUser, buffer.data(), size, &size)) {
        return LastWin32Error("GetTokenInformation");
    }
    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    wchar_t* sidText = nullptr;
    if (!::ConvertSidToStringSidW(user->User.Sid, &sidText)) return LastWin32Error("ConvertSidToStringSid");
    std::wstring result(sidText);
    ::LocalFree(sidText);
    return result;
}

Result<SingleInstanceLock> SingleInstanceLock::Acquire(std::wstring_view name) {
    auto sid = CurrentUserSid();
    if (!sid) return sid.error();
    std::wstring mutexName = L"Local\\";
    mutexName += name;
    mutexName += L".";
    mutexName += sid.value();
    SingleInstanceLock lock;
    lock.mutex_.Reset(::CreateMutexW(nullptr, FALSE, mutexName.c_str()));
    if (!lock.mutex_) return LastWin32Error("CreateMutex(single instance)");
    lock.primary_ = ::GetLastError() != ERROR_ALREADY_EXISTS;
    return lock;
}

InstanceChannel::~InstanceChannel() { Close(); }

Result<void> InstanceChannel::Listen(std::wstring_view channelName, Handler handler) {
    Close();
    className_ = ChannelClassName(channelName);
    handler_ = std::move(handler);
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &InstanceChannel::WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className_.c_str();
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return LastWin32Error("RegisterClassEx(InstanceChannel)");
    }
    hwnd_ = ::CreateWindowExW(0, className_.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);
    if (!hwnd_) return LastWin32Error("CreateWindowEx(InstanceChannel)");
    return Ok();
}

void InstanceChannel::Close() noexcept {
    if (hwnd_) {
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    handler_ = nullptr;
}

DWORD InstanceChannel::FindListenerProcessId(std::wstring_view channelName) {
    const std::wstring className = ChannelClassName(channelName);
    HWND target = ::FindWindowExW(HWND_MESSAGE, nullptr, className.c_str(), nullptr);
    if (!target) return 0;
    DWORD pid = 0;
    ::GetWindowThreadProcessId(target, &pid);
    return pid;
}

Result<void> InstanceChannel::Send(std::wstring_view channelName, std::wstring_view command, DWORD timeoutMs) {
    if (command.empty() || command.size() > kMaxCommandChars) {
        return MakeError(ErrorCode::InvalidArgument, "instance command is empty or too long");
    }
    const std::wstring className = ChannelClassName(channelName);
    HWND target = ::FindWindowExW(HWND_MESSAGE, nullptr, className.c_str(), nullptr);
    if (!target) return MakeError(ErrorCode::NotFound, "no running instance is listening");

    DWORD pid = 0;
    ::GetWindowThreadProcessId(target, &pid);
    if (pid) ::AllowSetForegroundWindow(pid);

    const std::wstring payload(command);
    COPYDATASTRUCT data{};
    data.dwData = kCommandSignature;
    data.cbData = static_cast<DWORD>(payload.size() * sizeof(wchar_t));
    data.lpData = const_cast<wchar_t*>(payload.data());
    DWORD_PTR result = 0;
    if (!::SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                               SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, timeoutMs, &result)) {
        return LastWin32Error("SendMessageTimeout(instance command)");
    }
    if (result != TRUE) return MakeError(ErrorCode::InvalidState, "running instance rejected the command");
    return Ok();
}

LRESULT CALLBACK InstanceChannel::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* self = reinterpret_cast<InstanceChannel*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_COPYDATA && self) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
        if (!data || data->dwData != kCommandSignature || data->cbData == 0 ||
            data->cbData % sizeof(wchar_t) != 0 || data->cbData > kMaxCommandChars * sizeof(wchar_t)) {
            return FALSE;
        }
        std::wstring command(static_cast<const wchar_t*>(data->lpData), data->cbData / sizeof(wchar_t));
        for (wchar_t c : command) {
            if (!((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-')) return FALSE;
        }
        if (self->handler_) {
            try {
                self->handler_(command);
            } catch (...) {
                AETHER_LOG_ERROR("instance", "instance command handler threw");
            }
        }
        return TRUE;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace aether::platform
