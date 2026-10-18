// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Mapping of WinHTTP error codes to portable error categories (platform/net/WinHttpError.h).
#include "AetherTest.h"

#include "platform/net/WinHttpError.h"

#include <windows.h>
#include <winhttp.h>

#include <string>

using namespace aether;
using namespace aether::net;

namespace {

bool Contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

}  // namespace

AETHER_TEST("winhttp errors: transport failures map to Timeout, Network and Cancelled") {
    const auto timeout = MapWinHttpError(ERROR_WINHTTP_TIMEOUT, "waiting for https://example.com");
    CHECK_EQ(timeout.code, ErrorCode::Timeout);
    CHECK_EQ(timeout.platformCode, std::int64_t{ERROR_WINHTTP_TIMEOUT});
    CHECK(timeout.message.starts_with("waiting for https://example.com: "));
    CHECK(Contains(timeout.message, "timed out"));

    const auto dns = MapWinHttpError(ERROR_WINHTTP_NAME_NOT_RESOLVED, "op");
    CHECK_EQ(dns.code, ErrorCode::Network);
    CHECK(Contains(dns.message, "could not be resolved"));

    CHECK_EQ(MapWinHttpError(ERROR_WINHTTP_CANNOT_CONNECT, "op").code, ErrorCode::Network);
    CHECK_EQ(MapWinHttpError(ERROR_WINHTTP_CONNECTION_ERROR, "op").code, ErrorCode::Network);
    CHECK_EQ(MapWinHttpError(ERROR_WINHTTP_INVALID_SERVER_RESPONSE, "op").code, ErrorCode::Network);
    CHECK_EQ(MapWinHttpError(ERROR_WINHTTP_OPERATION_CANCELLED, "op").code, ErrorCode::Cancelled);
    CHECK_EQ(MapWinHttpError(ERROR_WINHTTP_INVALID_URL, "op").code, ErrorCode::InvalidArgument);
    CHECK_EQ(MapWinHttpError(ERROR_WINHTTP_UNRECOGNIZED_SCHEME, "op").code, ErrorCode::InvalidArgument);
    CHECK_EQ(MapWinHttpError(ERROR_NOT_ENOUGH_MEMORY, "op").code, ErrorCode::Internal);

    // Unknown codes still produce a Network error with some description.
    const auto unknown = MapWinHttpError(12999, "op");
    CHECK_EQ(unknown.code, ErrorCode::Network);
    CHECK_EQ(unknown.platformCode, std::int64_t{12999});
    CHECK(unknown.message.size() > std::string("op: ").size());
}

AETHER_TEST("winhttp errors: TLS failures are Network errors with a clear reason") {
    CHECK(IsTlsFailure(ERROR_WINHTTP_SECURE_FAILURE));
    CHECK(IsTlsFailure(ERROR_WINHTTP_SECURE_INVALID_CA));
    CHECK(IsTlsFailure(ERROR_WINHTTP_SECURE_CERT_DATE_INVALID));
    CHECK(IsTlsFailure(ERROR_WINHTTP_SECURE_CHANNEL_ERROR));
    CHECK(IsTlsFailure(ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED));
    CHECK_FALSE(IsTlsFailure(ERROR_WINHTTP_TIMEOUT));
    CHECK_FALSE(IsTlsFailure(ERROR_WINHTTP_CANNOT_CONNECT));

    const auto generic = MapWinHttpError(ERROR_WINHTTP_SECURE_FAILURE, "sending the request to https://x.example");
    CHECK_EQ(generic.code, ErrorCode::Network);
    CHECK(Contains(generic.message, "TLS failure: "));
    CHECK(Contains(generic.message, "secure connection could not be established"));

    const auto specific =
        MapWinHttpError(ERROR_WINHTTP_SECURE_FAILURE, "op",
                        WINHTTP_CALLBACK_STATUS_FLAG_INVALID_CA | WINHTTP_CALLBACK_STATUS_FLAG_CERT_CN_INVALID);
    CHECK_EQ(specific.code, ErrorCode::Network);
    CHECK(Contains(specific.message, "TLS failure: "));
    CHECK(Contains(specific.message, "trusted authority"));
    CHECK(Contains(specific.message, "host name"));

    const auto expired = MapWinHttpError(ERROR_WINHTTP_SECURE_CERT_DATE_INVALID, "op");
    CHECK_EQ(expired.code, ErrorCode::Network);
    CHECK(Contains(expired.message, "TLS failure: "));
    CHECK(Contains(expired.message, "expired"));
}

AETHER_TEST("winhttp errors: secure failure flags are described individually") {
    CHECK(DescribeSecureFailureFlags(0).empty());
    CHECK(Contains(DescribeSecureFailureFlags(WINHTTP_CALLBACK_STATUS_FLAG_CERT_REVOKED), "revoked"));
    const auto both = DescribeSecureFailureFlags(WINHTTP_CALLBACK_STATUS_FLAG_CERT_DATE_INVALID |
                                                 WINHTTP_CALLBACK_STATUS_FLAG_SECURITY_CHANNEL_ERROR);
    CHECK(Contains(both, "expired"));
    CHECK(Contains(both, "; "));
    CHECK(Contains(both, "TLS channel"));
}
