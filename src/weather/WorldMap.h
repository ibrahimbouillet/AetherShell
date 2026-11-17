// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: the weather window's offline map: country shapes, their names in the ten interface languages
// and populated places, from public-domain Natural Earth data converted by
// tools/world-map/make_world_map.py into resources/weather/world-map.bin (format described there).
// Nothing here touches the network: finding the country of a position and the nearest town is local.
//
// Coordinates are degrees; shapes are stored to a hundredth of a degree (about 1.1 km).
#pragma once

#include "core/Result.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::weather {

inline constexpr std::size_t kWorldMapLanguages = 10;  // i18n::Language order: en fr es zh hi pt ru ar bn ur

struct GeoBounds {
    double west = 0.0;
    double south = 0.0;
    double east = 0.0;
    double north = 0.0;

    [[nodiscard]] double Width() const noexcept { return east - west; }
    [[nodiscard]] double Height() const noexcept { return north - south; }
    [[nodiscard]] bool Contains(double latitude, double longitude) const noexcept {
        return longitude >= west && longitude <= east && latitude >= south && latitude <= north;
    }
    [[nodiscard]] bool Intersects(const GeoBounds& other) const noexcept {
        return west <= other.east && other.west <= east && south <= other.north && other.south <= north;
    }
    void Include(const GeoBounds& other) noexcept;
};

struct MapVertex {
    std::int16_t lon = 0;  // hundredths of a degree
    std::int16_t lat = 0;
};

class WorldMap {
public:
    struct Country {
        std::array<char, 4> code{};                         // ISO-like ADM0_A3, NUL terminated ("FRA")
        std::array<std::uint32_t, kWorldMapLanguages> names{};  // string offsets
        GeoBounds bounds;
        std::uint32_t firstRing = 0;
        std::uint32_t ringCount = 0;
    };
    struct Ring {
        std::uint32_t firstPoint = 0;
        std::uint32_t pointCount = 0;
        std::uint32_t country = 0;
        GeoBounds bounds;
    };
    struct Place {
        double latitude = 0.0;
        double longitude = 0.0;
        std::uint32_t name = 0;
        std::uint32_t population = 0;
        std::uint16_t country = 0xFFFF;  // 0xFFFF: none
        std::uint8_t rank = 0;           // Natural Earth scalerank: 0 is the most prominent
    };

    // Validates every count, offset and coordinate; anything inconsistent is Integrity.
    [[nodiscard]] static Result<WorldMap> Parse(std::span<const std::uint8_t> bytes);

    [[nodiscard]] std::span<const Country> Countries() const noexcept { return countries_; }
    [[nodiscard]] std::span<const Ring> Rings() const noexcept { return rings_; }
    [[nodiscard]] std::span<const Place> Places() const noexcept { return places_; }
    [[nodiscard]] std::span<const MapVertex> RingPoints(std::size_t ring) const noexcept;

    // UTF-16 name; `language` is an i18n::Language value (English when out of range).
    [[nodiscard]] std::wstring CountryName(std::size_t country, std::size_t language) const;
    [[nodiscard]] std::wstring PlaceName(std::size_t place) const;
    [[nodiscard]] std::optional<std::size_t> FindCountry(std::string_view code) const noexcept;

    // The country whose shape contains the position (even-odd over its rings); nullopt at sea.
    [[nodiscard]] std::optional<std::size_t> CountryAt(double latitude, double longitude) const noexcept;
    // CountryAt, or on a coast the simplified shapes miss (Manhattan), the country of the nearest place
    // within 40 km.
    [[nodiscard]] std::optional<std::size_t> CountryFor(double latitude, double longitude) const noexcept;
    // The nearest place within `maxKilometres` (great-circle), preferring the position's own country.
    [[nodiscard]] std::optional<std::size_t> NearestPlace(double latitude, double longitude, double maxKilometres,
                                                          std::optional<std::size_t> country = std::nullopt) const noexcept;

    // What the map shows around a position: the country's part that holds it with its near islands,
    // limited to `maxSpan` degrees around the position for very large countries, and a window of
    // `fallbackSpan` degrees when the position is at sea or outside any country.
    [[nodiscard]] GeoBounds FocusBounds(double latitude, double longitude, std::optional<std::size_t> country,
                                        double maxSpan = 22.0, double fallbackSpan = 8.0) const noexcept;

    // Up to `count` places inside `view` (and inside `country` when given), the most prominent first and
    // kept apart from each other and from the position so that their temperatures cover the map.
    [[nodiscard]] std::vector<std::size_t> SamplePlaces(const GeoBounds& view, std::optional<std::size_t> country,
                                                        double latitude, double longitude, std::size_t count) const;

private:
    std::vector<Country> countries_;
    std::vector<Ring> rings_;
    std::vector<MapVertex> points_;
    std::vector<Place> places_;
    std::string strings_;
};

// The map embedded in the running executable (RCDATA AETHER_WORLD_MAP, see
// aether_add_world_map_resource). Blocking work of a few milliseconds: call it on a worker.
[[nodiscard]] Result<WorldMap> LoadEmbeddedWorldMap();

// Great-circle distance in kilometres.
[[nodiscard]] double DistanceKilometres(double latitudeA, double longitudeA, double latitudeB, double longitudeB) noexcept;

}  // namespace aether::weather
