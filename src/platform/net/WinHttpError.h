// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Translation of WinHTTP error codes into portable aether::Error values with messages a user or a
// log reader can act on. Pure apart from reading message tables, so it is unit tested.
#pragma once

#include "core/Result.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace aether::net {

// True for ERROR_WINHTTP_SECURE_* and the client-certificate errors: the TLS handshake or the server
// certificate validation failed.
[[nodiscard]] bool IsTlsFailure(std::uint32_t code) noexcept;

// Readable list of WINHTTP_CALLBACK_STATUS_FLAG_* bits ("the server certificate has expired or is not
// yet valid; ..."); empty when `flags` is 0.
[[nodiscard]] std::string DescribeSecureFailureFlags(std::uint32_t flags);

// Maps a WinHTTP or Win32 error raised while performing `operation` ("connecting to https://host"):
//   ERROR_WINHTTP_TIMEOUT                       -> ErrorCode::Timeout
//   ERROR_WINHTTP_OPERATION_CANCELLED           -> ErrorCode::Cancelled
//   name resolution, connect, reset, protocol   -> ErrorCode::Network
//   TLS / certificate failures                  -> ErrorCode::Network ("TLS failure: ...")
//   invalid URL or scheme                       -> ErrorCode::InvalidArgument
//   out of memory                               -> ErrorCode::Internal
//   anything else                               -> ErrorCode::Network
// `secureFailureFlags` are the bits reported by the WINHTTP_CALLBACK_STATUS_SECURE_FAILURE callback (0
// when unknown); they make certificate errors specific. platformCode carries the original code.
[[nodiscard]] Error MapWinHttpError(std::uint32_t code, std::string_view operation,
                                    std::uint32_t secureFailureFlags = 0);

}  // namespace aether::net
