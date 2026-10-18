// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "platform/net/WinHttpError.h"

#include "core/Strings.h"

#include <windows.h>
#include <winhttp.h>

#include <array>

namespace aether::net {
namespace {

struct KnownError {
    DWORD code;
    ErrorCode category;
    std::string_view description;
};

constexpr std::array kKnownErrors{
    KnownError{ERROR_WINHTTP_TIMEOUT, ErrorCode::Timeout, "the operation timed out"},
    KnownError{ERROR_WINHTTP_OPERATION_CANCELLED, ErrorCode::Cancelled, "the operation was cancelled"},
    KnownError{ERROR_WINHTTP_NAME_NOT_RESOLVED, ErrorCode::Network, "the server name could not be resolved"},
    KnownError{ERROR_WINHTTP_CANNOT_CONNECT, ErrorCode::Network, "the connection to the server could not be established"},
    KnownError{ERROR_WINHTTP_CONNECTION_ERROR, ErrorCode::Network, "the connection was reset or closed unexpectedly"},
    KnownError{ERROR_WINHTTP_INVALID_SERVER_RESPONSE, ErrorCode::Network, "the server response could not be parsed"},
    KnownError{ERROR_WINHTTP_HEADER_SIZE_OVERFLOW, ErrorCode::Network, "the server response headers are too large"},
    KnownError{ERROR_WINHTTP_CHUNKED_ENCODING_HEADER_SIZE_OVERFLOW, ErrorCode::Network,
               "the server sent a malformed chunked body"},
    KnownError{ERROR_WINHTTP_RESEND_REQUEST, ErrorCode::Network, "the request had to be resent and was abandoned"},
    KnownError{ERROR_WINHTTP_REDIRECT_FAILED, ErrorCode::Network, "the redirect could not be followed"},
    KnownError{ERROR_WINHTTP_AUTO_PROXY_SERVICE_ERROR, ErrorCode::Network, "proxy auto-configuration failed"},
    KnownError{ERROR_WINHTTP_BAD_AUTO_PROXY_SCRIPT, ErrorCode::Network, "the proxy auto-configuration script is invalid"},
    KnownError{ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT, ErrorCode::Network,
               "the proxy auto-configuration script could not be downloaded"},
    KnownError{ERROR_WINHTTP_LOGIN_FAILURE, ErrorCode::Network, "proxy or server authentication failed"},
    KnownError{ERROR_WINHTTP_SHUTDOWN, ErrorCode::Unavailable, "WinHTTP is shutting down"},
    KnownError{ERROR_WINHTTP_INVALID_URL, ErrorCode::InvalidArgument, "the URL is invalid"},
    KnownError{ERROR_WINHTTP_UNRECOGNIZED_SCHEME, ErrorCode::InvalidArgument, "the URL scheme is not supported"},
    KnownError{ERROR_NOT_ENOUGH_MEMORY, ErrorCode::Internal, "not enough memory"},
    KnownError{ERROR_OUTOFMEMORY, ErrorCode::Internal, "not enough memory"},
    // TLS failures. In synchronous mode WinHTTP usually reports the generic ERROR_WINHTTP_SECURE_FAILURE and
    // delivers the precise reason through the secure-failure status callback (see DescribeSecureFailureFlags).
    KnownError{ERROR_WINHTTP_SECURE_FAILURE, ErrorCode::Network, "the secure connection could not be established"},
    KnownError{ERROR_WINHTTP_SECURE_FAILURE_PROXY, ErrorCode::Network,
               "the secure connection to the proxy could not be established"},
    KnownError{ERROR_WINHTTP_SECURE_CERT_DATE_INVALID, ErrorCode::Network,
               "the server certificate has expired or is not yet valid"},
    KnownError{ERROR_WINHTTP_SECURE_CERT_CN_INVALID, ErrorCode::Network,
               "the server certificate does not match the host name"},
    KnownError{ERROR_WINHTTP_SECURE_INVALID_CA, ErrorCode::Network,
               "the server certificate was not issued by a trusted authority"},
    KnownError{ERROR_WINHTTP_SECURE_CERT_REV_FAILED, ErrorCode::Network,
               "the revocation status of the server certificate could not be checked"},
    KnownError{ERROR_WINHTTP_SECURE_CERT_REVOKED, ErrorCode::Network, "the server certificate has been revoked"},
    KnownError{ERROR_WINHTTP_SECURE_INVALID_CERT, ErrorCode::Network, "the server certificate is invalid"},
    KnownError{ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE, ErrorCode::Network,
               "the server certificate is not valid for server authentication"},
    KnownError{ERROR_WINHTTP_SECURE_CHANNEL_ERROR, ErrorCode::Network,
               "the TLS channel failed (no common protocol version or cipher suite)"},
    KnownError{ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED, ErrorCode::Network, "the server requires a client certificate"},
    KnownError{ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED_PROXY, ErrorCode::Network,
               "the proxy requires a client certificate"},
};

struct SecureFlag {
    DWORD flag;
    std::string_view description;
};

constexpr std::array kSecureFlags{
    SecureFlag{WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID, "the server certificate has expired or is not yet valid"},
    SecureFlag{WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID, "the server certificate does not match the host name"},
    SecureFlag{WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA, "the server certificate was not issued by a trusted authority"},
    SecureFlag{WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED, "the server certificate has been revoked"},
    SecureFlag{WINHTTP_CALLBACK_STATUS_FLAG_CERT_REV_FAILED,
               "the revocation status of the server certificate could not be checked"},
    SecureFlag{WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CERT, "the server certificate is invalid"},
    SecureFlag{WINHTTP_CALLBACK_STATUS_FLAG_CERT_WRONG_USAGE,
               "the server certificate is not valid for server authentication"},
    SecureFlag{WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR,
               "the TLS channel failed (no common protocol version or cipher suite)"},
};

// Message text for codes outside the table. WinHTTP's messages live in winhttp.dll, not in the
// system message table, so that module is searched first.
std::string LookUpMessage(DWORD code) {
    const HMODULE winhttp = ::GetModuleHandleW(L"winhttp.dll");
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    if (winhttp) flags |= FORMAT_MESSAGE_FROM_HMODULE;
    wchar_t* buffer = nullptr;
    const DWORD length =
        ::FormatMessageW(flags, winhttp, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message;
    if (length > 0 && buffer) message = text::Narrow(text::Trim(std::wstring_view(buffer, length)));
    if (buffer) ::LocalFree(buffer);
    if (message.empty()) message = "error " + std::to_string(code);
    return message;
}

}  // namespace

bool IsTlsFailure(std::uint32_t code) noexcept {
    switch (code) {
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_FAILURE_PROXY:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_INVALID_CA:
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
        case ERROR_WINHTTP_SECURE_CERT_REVOKED:
        case ERROR_WINHTTP_SECURE_INVALID_CERT:
        case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE:
        case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
        case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED_PROXY: return true;
        default: return false;
    }
}

std::string DescribeSecureFailureFlags(std::uint32_t flags) {
    std::string description;
    for (const auto& entry : kSecureFlags) {
        if ((flags & entry.flag) == 0) continue;
        if (!description.empty()) description += "; ";
        description += entry.description;
    }
    return description;
}

Error MapWinHttpError(std::uint32_t code, std::string_view operation, std::uint32_t secureFailureFlags) {
    ErrorCode category = ErrorCode::Network;
    std::string description;
    for (const auto& entry : kKnownErrors) {
        if (entry.code == code) {
            category = entry.category;
            description = entry.description;
            break;
        }
    }
    if (description.empty()) description = LookUpMessage(code);

    std::string message(operation);
    message += ": ";
    if (IsTlsFailure(code)) {
        message += "TLS failure: ";
        const std::string specific = DescribeSecureFailureFlags(secureFailureFlags);
        message += specific.empty() ? description : specific;
    } else {
        message += description;
    }
    return MakeError(category, std::move(message), static_cast<std::int64_t>(code));
}

}  // namespace aether::net
