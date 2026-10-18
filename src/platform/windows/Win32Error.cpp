// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/windows/Win32Error.h"

#include "core/Strings.h"

namespace aether::platform {

std::string FormatSystemMessage(DWORD code) {
    wchar_t* buffer = nullptr;
    const DWORD length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code,
        0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message;
    if (length > 0 && buffer) {
        message = text::Narrow(text::Trim(std::wstring_view(buffer, length)));
    }
    if (buffer) ::LocalFree(buffer);
    return message;
}

ErrorCode CategorizeWin32(DWORD code) noexcept {
    switch (code) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_MOD_NOT_FOUND:
        case ERROR_NOT_FOUND: return ErrorCode::NotFound;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_ELEVATION_REQUIRED:
        case ERROR_PRIVILEGE_NOT_HELD: return ErrorCode::AccessDenied;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS: return ErrorCode::AlreadyExists;
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL: return ErrorCode::InsufficientSpace;
        case ERROR_CANCELLED:
        case ERROR_OPERATION_ABORTED: return ErrorCode::Cancelled;
        case WAIT_TIMEOUT:
        case ERROR_TIMEOUT: return ErrorCode::Timeout;
        case ERROR_INVALID_PARAMETER: return ErrorCode::InvalidArgument;
        case ERROR_NOT_SUPPORTED:
        case ERROR_CALL_NOT_IMPLEMENTED: return ErrorCode::Unsupported;
        default: return ErrorCode::Platform;
    }
}

Error ErrorFromWin32(DWORD code, std::string_view context) {
    std::string message(context);
    const std::string system = FormatSystemMessage(code);
    if (!system.empty()) {
        message += ": ";
        message += system;
    }
    return MakeError(CategorizeWin32(code), std::move(message), static_cast<std::int64_t>(code));
}

Error ErrorFromHResult(HRESULT hr, std::string_view context) {
    std::string message(context);
    const std::string system = FormatSystemMessage(static_cast<DWORD>(hr));
    if (!system.empty()) {
        message += ": ";
        message += system;
    }
    ErrorCode category = ErrorCode::Platform;
    if (HRESULT_FACILITY(hr) == FACILITY_WIN32) {
        category = CategorizeWin32(static_cast<DWORD>(HRESULT_CODE(hr)));
    } else if (hr == E_INVALIDARG) {
        category = ErrorCode::InvalidArgument;
    } else if (hr == E_NOTIMPL) {
        category = ErrorCode::Unsupported;
    } else if (hr == E_ACCESSDENIED) {
        category = ErrorCode::AccessDenied;
    } else if (hr == E_ABORT) {
        category = ErrorCode::Cancelled;
    }
    return MakeError(category, std::move(message), static_cast<std::int64_t>(static_cast<std::uint32_t>(hr)));
}

Error LastWin32Error(std::string_view context) { return ErrorFromWin32(::GetLastError(), context); }

}  // namespace aether::platform
