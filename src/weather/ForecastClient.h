// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: the weather window's requests to Open-Meteo: the detailed forecast of one place and the
// current conditions of the map's places. Nothing is cached or sent in the background: the window
// asks while it is open. Coordinates are never logged.
#pragma once

#include "core/Cancellation.h"
#include "core/Http.h"
#include "core/Result.h"
#include "status/StatusTypes.h"
#include "weather/Forecast.h"

#include <vector>

namespace aether::weather {

class ForecastClient {
public:
    // `http` must outlive the client.
    explicit ForecastClient(net::IHttpClient& http) noexcept : http_(http) {}

    // Blocking: call on a worker thread, never on the UI thread. InvalidArgument for coordinates out
    // of range; Network/Timeout/Cancelled from the transport, RateLimited (HTTP 429), Unavailable (5xx),
    // Http (other statuses), Parse/Validation/Integrity (bad response).
    [[nodiscard]] Result<DetailedForecast> Fetch(double latitude, double longitude, status::TemperatureUnit unit,
                                                 const CancellationToken& cancel) const;

    // Blocking. 1..kMaxForecastPoints places; readings keep their request index.
    [[nodiscard]] Result<std::vector<PointReading>> FetchPoints(const std::vector<GeoPoint>& points,
                                                                status::TemperatureUnit unit,
                                                                const CancellationToken& cancel) const;

private:
    net::IHttpClient& http_;
};

}  // namespace aether::weather
