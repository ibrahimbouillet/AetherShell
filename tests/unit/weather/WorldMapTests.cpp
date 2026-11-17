// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: the weather window's offline map, read from the real data embedded in this test program.
#include "AetherTest.h"

#include "weather/WorldMap.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::weather;

namespace {

const WorldMap& Map() {
    static const WorldMap map = [] {
        auto loaded = LoadEmbeddedWorldMap();
        if (!loaded) throw std::runtime_error("world map did not load: " + loaded.error().message);
        return std::move(loaded).value();
    }();
    return map;
}

std::string CodeAt(double latitude, double longitude) {
    const auto country = Map().CountryAt(latitude, longitude);
    return country ? std::string(Map().Countries()[*country].code.data()) : std::string("sea");
}

}  // namespace

AETHER_TEST("world map: the embedded data loads and is complete") {
    const auto& map = Map();
    CHECK(map.Countries().size() >= 230);
    CHECK(map.Places().size() >= 7000);
    CHECK(map.Rings().size() >= map.Countries().size());
    const auto france = map.FindCountry("FRA");
    REQUIRE(france.has_value());
    CHECK_EQ(map.CountryName(*france, 0), std::wstring(L"France"));
    const auto china = map.FindCountry("CHN");
    REQUIRE(china.has_value());
    CHECK_EQ(map.CountryName(*china, 0), std::wstring(L"China"));
    CHECK_EQ(map.CountryName(*china, 3), std::wstring(L"中华人民共和国"));
    const auto germany = map.FindCountry("DEU");
    REQUIRE(germany.has_value());
    CHECK_EQ(map.CountryName(*germany, 1), std::wstring(L"Allemagne"));
    CHECK_EQ(map.CountryName(*germany, 99), std::wstring(L"Germany"));  // out of range: English
}

AETHER_TEST("world map: positions fall in the right country, the sea in none") {
    CHECK_EQ(CodeAt(48.8566, 2.3522), std::string("FRA"));    // Paris
    CHECK_EQ(CodeAt(52.52, 13.405), std::string("DEU"));      // Berlin
    CHECK_EQ(CodeAt(40.0, -100.0), std::string("USA"));       // Kansas
    // Lower Manhattan is water at this scale: the nearest town settles it.
    const auto newYork = Map().CountryFor(40.7128, -74.006);
    REQUIRE(newYork.has_value());
    CHECK_EQ(std::string(Map().Countries()[*newYork].code.data()), std::string("USA"));
    CHECK(!Map().CountryFor(45.0, -30.0).has_value());
    CHECK_EQ(CodeAt(-33.8688, 151.2093), std::string("AUS"));  // Sydney
    CHECK_EQ(CodeAt(35.6762, 139.6503), std::string("JPN"));  // Tokyo
    CHECK_EQ(CodeAt(-1.2921, 36.8219), std::string("KEN"));   // Nairobi
    CHECK_EQ(CodeAt(45.0, -30.0), std::string("sea"));        // mid-Atlantic
    CHECK_EQ(CodeAt(41.8719, 12.5674), std::string("ITA"));   // Rome
}

AETHER_TEST("world map: the nearest place names a position, preferring its own country") {
    const auto& map = Map();
    const auto france = map.FindCountry("FRA");
    const auto paris = map.NearestPlace(48.85, 2.35, 50.0, france);
    REQUIRE(paris.has_value());
    CHECK_EQ(map.PlaceName(*paris), std::wstring(L"Paris"));
    CHECK(!map.NearestPlace(0.0, -140.0, 50.0).has_value());  // open Pacific
    CHECK_NEAR(DistanceKilometres(48.8566, 2.3522, 45.764, 4.8357), 392.0, 5.0);  // Paris - Lyon
}

AETHER_TEST("world map: the focus holds mainland France without its overseas parts") {
    const auto& map = Map();
    const auto france = map.CountryAt(45.764, 4.8357);  // Lyon
    REQUIRE(france.has_value());
    const auto view = map.FocusBounds(45.764, 4.8357, france);
    CHECK(view.Contains(48.8566, 2.3522));    // Paris
    CHECK(view.Contains(43.2965, 5.3698));    // Marseille
    CHECK(view.Contains(42.15, 9.1));         // Corsica
    CHECK(view.Width() < 20.0);               // not French Guiana or Réunion
    CHECK(view.Height() < 15.0);

    const auto russia = map.FindCountry("RUS");
    const auto moscow = map.FocusBounds(55.7558, 37.6173, russia);
    CHECK(moscow.Contains(55.7558, 37.6173));
    CHECK(moscow.Width() <= 22.0 * 1.2 + 0.6);  // a region, not the whole country

    const auto sea = map.FocusBounds(45.0, -30.0, std::nullopt);
    CHECK(sea.Contains(45.0, -30.0));
    CHECK_NEAR(sea.Width(), 8.0, 1e-9);
}

AETHER_TEST("world map: sampled places spread over the country and stay in view") {
    const auto& map = Map();
    const auto france = map.FindCountry("FRA");
    const auto view = map.FocusBounds(45.764, 4.8357, france);
    const auto places = map.SamplePlaces(view, france, 45.764, 4.8357, 14);
    CHECK(places.size() >= 10);
    CHECK(places.size() <= 14);
    std::size_t inFrance = 0;
    for (const auto index : places) {
        const auto& place = map.Places()[index];
        CHECK(view.Contains(place.latitude, place.longitude));
        if (place.country == *france) ++inFrance;
        CHECK(DistanceKilometres(place.latitude, place.longitude, 45.764, 4.8357) > 30.0);  // apart from the position
    }
    CHECK(inFrance >= places.size() / 2);
    const bool hasParis = std::any_of(places.begin(), places.end(), [&](std::size_t i) { return map.PlaceName(i) == L"Paris"; });
    CHECK(hasParis);
}

AETHER_TEST("world map: damaged data is refused") {
    std::vector<std::uint8_t> bytes(24, 0);
    std::memcpy(bytes.data(), "AWM1", 4);
    CHECK(!WorldMap::Parse(bytes).ok());
    bytes[0] = 'X';
    CHECK(!WorldMap::Parse(bytes).ok());
    CHECK(!WorldMap::Parse({}).ok());

    // A real file cut short.
    HMODULE module = ::GetModuleHandleW(nullptr);
    HRSRC resource = ::FindResourceW(module, L"AETHER_WORLD_MAP", RT_RCDATA);
    REQUIRE(resource != nullptr);
    const auto* data = static_cast<const std::uint8_t*>(::LockResource(::LoadResource(module, resource)));
    const DWORD size = ::SizeofResource(module, resource);
    std::vector<std::uint8_t> copy(data, data + size);
    auto whole = WorldMap::Parse(copy);
    CHECK(whole.ok());
    copy.pop_back();
    auto cut = WorldMap::Parse(copy);
    REQUIRE(!cut.ok());
    CHECK(cut.error().code == ErrorCode::Integrity);
}
