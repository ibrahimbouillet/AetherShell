// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// GeocodingClient against an in-memory HTTP client: URL encoding, limits and error mapping.
#include "AetherTest.h"
#include "WeatherTestSupport.h"

#include "weather/GeocodingClient.h"

#include <stdexcept>
#include <string>

using namespace aether;
using namespace aether::weather;
using namespace std::chrono_literals;
using aether::weather::testing::FakeHttpClient;

namespace {

constexpr std::string_view kZurichResults = R"({"results":[
    {"id":2657896,"name":"Zürich","latitude":47.36667,"longitude":8.55,"admin1":"Zürich","country":"Schweiz"},
    {"id":2657895,"name":"Zürich","latitude":47.4,"longitude":8.6,"admin1":"Zürich","country":"Schweiz"}
]})";

ErrorCode SearchError(GeocodingClient& client, std::wstring_view query) {
    auto result = client.Search(query, L"en-US", CancellationToken{});
    return result.ok() ? ErrorCode::Unknown : result.error().code;
}

}  // namespace

AETHER_TEST("geocoding: non-ASCII queries are percent-encoded as UTF-8") {
    FakeHttpClient http;
    http.RespondWith(200, std::string(kZurichResults));
    GeocodingClient client(http);

    auto result = client.Search(L"  Zürich  ", L"de-CH", CancellationToken{});
    REQUIRE(result.ok());
    REQUIRE_EQ(result.value().size(), std::size_t{2});
    CHECK_EQ(result.value()[0].name, std::wstring(L"Zürich"));
    CHECK_EQ(result.value()[0].country, std::wstring(L"Schweiz"));
    CHECK_NEAR(result.value()[0].latitude, 47.36667, 1e-9);

    auto tokyo = client.Search(L"東京", L"ja", CancellationToken{});
    REQUIRE(tokyo.ok());
    auto injection = client.Search(L"Paris&count=100#x y", L"fr-FR", CancellationToken{});
    REQUIRE(injection.ok());

    const auto requests = http.Requests();
    REQUIRE_EQ(requests.size(), std::size_t{3});
    CHECK_EQ(requests[0].url, std::string("https://geocoding-api.open-meteo.com/v1/search?name=Z%C3%BCrich&count=5"
                                          "&language=de&format=json"));
    CHECK_EQ(requests[1].url, std::string("https://geocoding-api.open-meteo.com/v1/search?name=%E6%9D%B1%E4%BA%AC"
                                          "&count=5&language=ja&format=json"));
    CHECK_EQ(requests[2].url, std::string("https://geocoding-api.open-meteo.com/v1/search?name=Paris%26count%3D100%23x"
                                          "%20y&count=5&language=fr&format=json"));
    CHECK_EQ(requests[0].method, std::string("GET"));
    CHECK(requests[0].connectTimeout == 10s);
    CHECK(requests[0].receiveTimeout == 15s);
    CHECK_EQ(requests[0].maxResponseBytes, static_cast<std::uint64_t>(256 * 1024));
    CHECK(requests[0].allowedRedirectHosts.empty());
}

AETHER_TEST("geocoding: too short or too long queries send nothing") {
    FakeHttpClient http;
    http.RespondWith(200, std::string(kZurichResults));
    GeocodingClient client(http);

    auto empty = client.Search(L"   ", L"en", CancellationToken{});
    REQUIRE(empty.ok());
    CHECK(empty.value().empty());
    auto single = client.Search(L"Z", L"en", CancellationToken{});
    REQUIRE(single.ok());
    CHECK(single.value().empty());
    // One astral character (a surrogate pair) is still a single character.
    auto astral = client.Search(L"\U0001F30D", L"en", CancellationToken{});
    REQUIRE(astral.ok());
    CHECK(astral.value().empty());

    CHECK_EQ(SearchError(client, std::wstring(201, L'a')), ErrorCode::InvalidArgument);
    CHECK_EQ(http.RequestCount(), std::size_t{0});

    auto twoLetters = client.Search(L"Ay", L"en", CancellationToken{});
    CHECK(twoLetters.ok());
    CHECK_EQ(http.RequestCount(), std::size_t{1});
}

AETHER_TEST("geocoding: no match returns an empty list") {
    FakeHttpClient http;
    http.RespondWith(200, R"({"generationtime_ms":0.31})");
    GeocodingClient client(http);
    auto result = client.Search(L"Xyzzyville", L"en", CancellationToken{});
    REQUIRE(result.ok());
    CHECK(result.value().empty());
}

AETHER_TEST("geocoding: HTTP and transport failures map to explicit errors") {
    FakeHttpClient http;
    GeocodingClient client(http);

    http.RespondWith(429, "");
    CHECK_EQ(SearchError(client, L"Paris"), ErrorCode::RateLimited);
    http.RespondWith(503, "Service Unavailable");
    CHECK_EQ(SearchError(client, L"Paris"), ErrorCode::Unavailable);

    http.RespondWith(400, R"({"error":true,"reason":"Parameter count must be between 1 and 100."})");
    auto rejected = client.Search(L"Paris", L"en", CancellationToken{});
    REQUIRE_FALSE(rejected.ok());
    CHECK_EQ(rejected.error().code, ErrorCode::Http);
    CHECK_EQ(rejected.error().platformCode, std::int64_t{400});
    CHECK(rejected.error().message.find("Parameter count must be between 1 and 100.") != std::string::npos);

    http.RespondWith(200, "{\"results\": [");
    CHECK_EQ(SearchError(client, L"Paris"), ErrorCode::Parse);

    http.FailWith(ErrorCode::Timeout);
    CHECK_EQ(SearchError(client, L"Paris"), ErrorCode::Timeout);
    http.FailWith(ErrorCode::Network);
    CHECK_EQ(SearchError(client, L"Paris"), ErrorCode::Network);
}

AETHER_TEST("geocoding: cancellation and exceptions stay inside the module") {
    FakeHttpClient http;
    http.RespondWith(200, std::string(kZurichResults));
    GeocodingClient client(http);

    CancellationSource source;
    source.Cancel();
    auto cancelled = client.Search(L"Paris", L"en", source.Token());
    REQUIRE_FALSE(cancelled.ok());
    CHECK_EQ(cancelled.error().code, ErrorCode::Cancelled);
    CHECK_EQ(http.RequestCount(), std::size_t{0});

    http.SetHandler([](const net::HttpRequest&, const CancellationToken&) -> Result<net::HttpResponse> {
        throw std::runtime_error("transport bug");
    });
    auto thrown = client.Search(L"Paris", L"en", CancellationToken{});
    REQUIRE_FALSE(thrown.ok());
    CHECK_EQ(thrown.error().code, ErrorCode::Internal);
}

AETHER_TEST("geocoding: cancellation wins when the transport returns a completed response") {
    FakeHttpClient http;
    GeocodingClient client(http);
    for (const int status : {200, 500}) {
        CancellationSource source;
        http.SetHandler([&](const net::HttpRequest&, const CancellationToken&) -> Result<net::HttpResponse> {
            source.Cancel();
            net::HttpResponse response;
            response.status = status;
            response.body = std::string(kZurichResults);
            return response;
        });
        auto result = client.Search(L"Zürich", L"de-CH", source.Token());
        REQUIRE_FALSE(result.ok());
        CHECK_EQ(result.error().code, ErrorCode::Cancelled);
    }
    CHECK_EQ(http.RequestCount(), std::size_t{2});
}
