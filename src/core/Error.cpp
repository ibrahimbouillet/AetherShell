// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "core/Result.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace aether {

std::string_view ToString(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::Unknown: return "Unknown";
        case ErrorCode::InvalidArgument: return "InvalidArgument";
        case ErrorCode::InvalidState: return "InvalidState";
        case ErrorCode::NotFound: return "NotFound";
        case ErrorCode::AlreadyExists: return "AlreadyExists";
        case ErrorCode::Unsupported: return "Unsupported";
        case ErrorCode::Unavailable: return "Unavailable";
        case ErrorCode::AccessDenied: return "AccessDenied";
        case ErrorCode::Timeout: return "Timeout";
        case ErrorCode::Cancelled: return "Cancelled";
        case ErrorCode::Network: return "Network";
        case ErrorCode::Http: return "Http";
        case ErrorCode::RateLimited: return "RateLimited";
        case ErrorCode::Parse: return "Parse";
        case ErrorCode::Validation: return "Validation";
        case ErrorCode::Integrity: return "Integrity";
        case ErrorCode::Signature: return "Signature";
        case ErrorCode::InsufficientSpace: return "InsufficientSpace";
        case ErrorCode::Io: return "Io";
        case ErrorCode::Platform: return "Platform";
        case ErrorCode::Internal: return "Internal";
    }
    return "Unknown";
}

std::string Error::Describe() const {
    std::string text(ToString(code));
    if (!message.empty()) {
        text += ": ";
        text += message;
    }
    if (platformCode != 0) {
        char buffer[32];
        if (platformCode > 0 && platformCode < 1000) {
            std::snprintf(buffer, sizeof(buffer), " (%lld)", static_cast<long long>(platformCode));
        } else {
            std::snprintf(buffer, sizeof(buffer), " (0x%08X)", static_cast<unsigned>(platformCode & 0xFFFFFFFF));
        }
        text += buffer;
    }
    return text;
}

namespace detail {

void ResultAccessFailure(const Error* error) noexcept {
    std::string message = "Aether Shell: invalid Result access";
    if (error) {
        message += " (holds error: " + error->Describe() + ")";
    } else {
        message += " (error() called on a successful Result)";
    }
    message += "\n";
    ::OutputDebugStringA(message.c_str());
    std::fputs(message.c_str(), stderr);
    std::abort();
}

}  // namespace detail
}  // namespace aether
