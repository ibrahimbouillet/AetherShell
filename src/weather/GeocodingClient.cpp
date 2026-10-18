// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/GeocodingClient.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "weather/OpenMeteo.h"

#include <exception>
#include <string>

namespace aether::weather {
namespace {

constexpr std::string_view kLogComponent = "weather";

// Number of Unicode code points in UTF-16 text (surrogate pairs count once).
std::size_t CodePointCount(std::wstring_view text) noexcept {
    std::size_t count = 0;
    for (const wchar_t c : text) {
        if (c < 0xDC00 || c > 0xDFFF) ++count;  // skip low surrogates
    }
    return count;
}

Result<std::vector<LocationCandidate>> SearchImpl(net::IHttpClient& http, std::wstring_view query,
                                                  std::wstring_view languageTag, const CancellationToken& cancel) {
    const auto trimmed = text::Trim(query);
    if (CodePointCount(trimmed) < 2) return std::vector<LocationCandidate>{};
    const std::string utf8 = text::Narrow(trimmed);
    if (utf8.size() > openmeteo::kMaxQueryBytes) {
        return MakeError(ErrorCode::InvalidArgument, "location search text is too long");
    }
    if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "location search cancelled");

    const auto request = openmeteo::MakeRequest(openmeteo::BuildGeocodingUrl(
        utf8, openmeteo::NormalizeLanguage(languageTag), openmeteo::kMaxGeocodingResults));
    auto response = http.Send(request, cancel);
    // A transport may finish at the same instant cancellation arrives. Never return candidates for
    // a superseded search, even when that transport returned a successful response.
    if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "location search cancelled");
    if (!response) {
        // The search text is user data: it is never logged.
        AETHER_LOG_WARN(kLogComponent, "location search failed: {}", ToString(response.error().code));
        return response.error();
    }

    const auto& reply = response.value();
    if (reply.status != 200) {
        // The provider reason may echo request parameters (the user's search text), so it goes to the
        // caller only, not to the log.
        const auto reason = openmeteo::ProviderErrorReason(reply.body);
        std::string message = "location search failed with HTTP " + std::to_string(reply.status);
        if (reason) message += ": " + *reason;
        AETHER_LOG_WARN(kLogComponent, "location search failed with HTTP {}", reply.status);
        ErrorCode code = ErrorCode::Http;
        if (reply.status == 429) {
            code = ErrorCode::RateLimited;
        } else if (reply.status >= 500 && reply.status <= 599) {
            code = ErrorCode::Unavailable;
        }
        return MakeError(code, std::move(message), reply.status);
    }

    auto candidates = openmeteo::ParseGeocoding(reply.body, openmeteo::kMaxGeocodingResults);
    if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "location search cancelled");
    if (!candidates) {
        // Only the category: the message may carry a provider reason that echoes the search text.
        AETHER_LOG_WARN(kLogComponent, "invalid location search response ({})", ToString(candidates.error().code));
    }
    return candidates;
}

}  // namespace

Result<std::vector<LocationCandidate>> GeocodingClient::Search(std::wstring_view query, std::wstring_view languageTag,
                                                               const CancellationToken& cancel) const {
    try {
        return SearchImpl(http_, query, languageTag, cancel);
    } catch (const std::exception&) {
        // Transport exception text can contain a URL and the user's location query.
        AETHER_LOG_ERROR(kLogComponent, "location search threw an exception");
        return MakeError(ErrorCode::Internal, "location search failed unexpectedly");
    } catch (...) {
        AETHER_LOG_ERROR(kLogComponent, "location search threw a non-standard exception");
        return MakeError(ErrorCode::Internal, "location search failed unexpectedly");
    }
}

}  // namespace aether::weather
