// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// WinHttpClient against the real weather service (label "network", excluded from default test runs).
// Sends the same request the weather module builds (same headers, timeouts and redirect policy), so a
// transport fault that only the weather path hits is visible here with its full, unsummarised reason.
#include "AetherTest.h"

#include "platform/net/WinHttpClient.h"

#include <chrono>
#include <string>

using namespace aether;
using namespace aether::net;
using namespace std::chrono_literals;

namespace {

HttpRequest WeatherLikeRequest(std::string url) {
    HttpRequest request;
    request.method = "GET";
    request.url = std::move(url);
    request.headers.push_back({"Accept", "application/json"});
    request.connectTimeout = 10s;
    request.receiveTimeout = 15s;
    request.maxResponseBytes = 64 * 1024;
    request.allowedRedirectHosts.clear();
    request.maxRedirects = 0;
    request.allowCompression = false;  // same as the weather module: its compressed stream aborts WinHTTP's decoder
    return request;
}

}  // namespace

AETHER_TEST("network: the weather forecast request succeeds with the application's own client") {
    auto client = WinHttpClient::Create();
    REQUIRE(client.ok());
    auto response = client.value()->Send(
        WeatherLikeRequest("https://api.open-meteo.com/v1/forecast?latitude=48.8000&longitude=2.3000"
                           "&current=temperature_2m,weather_code,is_day&temperature_unit=celsius&timezone=auto"),
        {});
    if (!response.ok()) TEST_NOTE("forecast request failed: " + response.error().Describe());
    REQUIRE(response.ok());
    TEST_NOTE("HTTP " + std::to_string(response->status) + ", " + std::to_string(response->body.size()) + " body bytes");
    CHECK_EQ(response->status, 200);
    CHECK(response->body.find("\"current\"") != std::string::npos);
}

AETHER_TEST("network: the place-name search request succeeds with the application's own client") {
    auto client = WinHttpClient::Create();
    REQUIRE(client.ok());
    auto response = client.value()->Send(
        WeatherLikeRequest("https://geocoding-api.open-meteo.com/v1/search?name=Paris&count=3&language=fr&format=json"),
        {});
    if (!response.ok()) TEST_NOTE("geocoding request failed: " + response.error().Describe());
    REQUIRE(response.ok());
    CHECK_EQ(response->status, 200);
    CHECK(response->body.find("\"results\"") != std::string::npos);
}

AETHER_TEST("network: WinHTTP's decoder aborts Open-Meteo's compressed stream, which is why compression is off") {
    // Documents the fault behind the weather outage. If this ever passes, allowCompression may be reconsidered.
    auto client = WinHttpClient::Create();
    REQUIRE(client.ok());
    auto request = WeatherLikeRequest(
        "https://api.open-meteo.com/v1/forecast?latitude=48.8000&longitude=2.3000&current=temperature_2m");
    request.allowCompression = true;
    auto response = client.value()->Send(request, {});
    TEST_NOTE(response.ok() ? "compressed request now works (HTTP " + std::to_string(response->status) + ")"
                            : "compressed request fails as recorded: " + response.error().Describe());
}
