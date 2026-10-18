// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Location search for the Settings UI through the Open-Meteo geocoding API. The query typed by the
// user is sent to geocoding-api.open-meteo.com only when Search() is called; nothing is cached.
#pragma once

#include "core/Cancellation.h"
#include "core/Http.h"
#include "core/Result.h"
#include "weather/WeatherTypes.h"

#include <string_view>
#include <vector>

namespace aether::weather {

class GeocodingClient {
public:
    // `http` must outlive the client.
    explicit GeocodingClient(net::IHttpClient& http) noexcept : http_(http) {}

    // Blocking: call on a worker thread, never on the UI thread.
    //
    // `query` is trimmed; fewer than two characters return an empty list without a request (the
    // provider returns nothing for them), more than openmeteo::kMaxQueryBytes UTF-8 bytes fail with
    // InvalidArgument. `languageTag` is a BCP-47 tag such as "fr-FR"; results are localized when the
    // provider has a translation. At most openmeteo::kMaxGeocodingResults candidates are returned.
    //
    // Errors: Network/Timeout/Cancelled from the transport, RateLimited (HTTP 429), Unavailable (5xx),
    // Http (other statuses, message includes the provider reason), Parse/Integrity (bad response).
    [[nodiscard]] Result<std::vector<LocationCandidate>> Search(std::wstring_view query, std::wstring_view languageTag,
                                                                const CancellationToken& cancel) const;

private:
    net::IHttpClient& http_;
};

}  // namespace aether::weather
