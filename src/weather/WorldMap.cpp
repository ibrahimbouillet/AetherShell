// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather/WorldMap.h"

#include "core/Strings.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <tuple>
#include <utility>

namespace aether::weather {
namespace {

constexpr std::size_t kHeaderBytes = 24;
constexpr std::size_t kCountryBytes = 60;
constexpr std::size_t kRingBytes = 8;
constexpr std::size_t kPointBytes = 4;
constexpr std::size_t kPlaceBytes = 16;
constexpr std::uint64_t kMaxCountries = 1'000;
constexpr std::uint64_t kMaxRings = 100'000;
constexpr std::uint64_t kMaxPoints = 2'000'000;
constexpr std::uint64_t kMaxPlaces = 100'000;
constexpr std::uint64_t kMaxStringBytes = 4u * 1024u * 1024u;

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}
    template <class T>
    T Read(std::size_t offset) const noexcept {
        T value{};
        std::memcpy(&value, bytes_.data() + offset, sizeof(T));  // x64 is little-endian like the file
        return value;
    }

private:
    std::span<const std::uint8_t> bytes_;
};

bool ValidLon(std::int32_t value) noexcept { return value >= -18000 && value <= 18000; }
bool ValidLat(std::int32_t value) noexcept { return value >= -9000 && value <= 9000; }
double Degrees(std::int16_t value) noexcept { return value / 100.0; }

Error Corrupt(const char* what) { return MakeError(ErrorCode::Integrity, std::string("world map data: ") + what); }

// Even-odd crossings of a horizontal ray from the position with one ring.
bool CrossesOdd(std::span<const MapVertex> ring, double latitude, double longitude) noexcept {
    bool inside = false;
    const std::size_t n = ring.size();
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const double yi = Degrees(ring[i].lat), yj = Degrees(ring[j].lat);
        const double xi = Degrees(ring[i].lon), xj = Degrees(ring[j].lon);
        if ((yi > latitude) != (yj > latitude)) {
            const double x = xi + (latitude - yi) * (xj - xi) / (yj - yi);
            if (longitude < x) inside = !inside;
        }
    }
    return inside;
}

// Planar distance in degrees with longitudes scaled at the latitude, for spacing only.
double SpacingDegrees(double latA, double lonA, double latB, double lonB, double scale) noexcept {
    const double dx = (lonA - lonB) * scale;
    const double dy = latA - latB;
    return std::sqrt(dx * dx + dy * dy);
}

// The longitude of a place shifted by a whole turn when that brings it into `view` (views can pass 180°).
std::optional<double> LongitudeIn(const GeoBounds& view, double latitude, double longitude) noexcept {
    for (const double shift : {0.0, 360.0, -360.0}) {
        if (view.Contains(latitude, longitude + shift)) return longitude + shift;
    }
    return std::nullopt;
}

}  // namespace

void GeoBounds::Include(const GeoBounds& other) noexcept {
    west = std::min(west, other.west);
    south = std::min(south, other.south);
    east = std::max(east, other.east);
    north = std::max(north, other.north);
}

double DistanceKilometres(double latitudeA, double longitudeA, double latitudeB, double longitudeB) noexcept {
    constexpr double kEarthRadius = 6371.0088;
    constexpr double kRadians = std::numbers::pi / 180.0;
    const double dLat = (latitudeB - latitudeA) * kRadians;
    const double dLon = (longitudeB - longitudeA) * kRadians;
    const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                     std::cos(latitudeA * kRadians) * std::cos(latitudeB * kRadians) * std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kEarthRadius * std::asin(std::min(1.0, std::sqrt(a)));
}

Result<WorldMap> WorldMap::Parse(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderBytes || std::memcmp(bytes.data(), "AWM1", 4) != 0) return Corrupt("not an AWM1 file");
    const Reader in(bytes);
    const std::uint64_t countryCount = in.Read<std::uint32_t>(4);
    const std::uint64_t ringCount = in.Read<std::uint32_t>(8);
    const std::uint64_t pointCount = in.Read<std::uint32_t>(12);
    const std::uint64_t placeCount = in.Read<std::uint32_t>(16);
    const std::uint64_t stringBytes = in.Read<std::uint32_t>(20);
    if (countryCount == 0 || countryCount > kMaxCountries || ringCount > kMaxRings || pointCount > kMaxPoints ||
        placeCount > kMaxPlaces || stringBytes == 0 || stringBytes > kMaxStringBytes) {
        return Corrupt("counts are out of range");
    }
    const std::uint64_t countriesAt = kHeaderBytes;
    const std::uint64_t ringsAt = countriesAt + countryCount * kCountryBytes;
    const std::uint64_t pointsAt = ringsAt + ringCount * kRingBytes;
    const std::uint64_t placesAt = pointsAt + pointCount * kPointBytes;
    const std::uint64_t stringsAt = placesAt + placeCount * kPlaceBytes;
    if (stringsAt + stringBytes != bytes.size()) return Corrupt("size does not match its counts");

    WorldMap map;
    map.strings_.assign(reinterpret_cast<const char*>(bytes.data() + stringsAt), static_cast<std::size_t>(stringBytes));
    if (map.strings_.back() != '\0') return Corrupt("strings are not terminated");
    const auto validString = [&](std::uint32_t offset) { return offset < stringBytes; };

    map.points_.resize(static_cast<std::size_t>(pointCount));
    for (std::size_t i = 0; i < map.points_.size(); ++i) {
        const std::size_t at = static_cast<std::size_t>(pointsAt + i * kPointBytes);
        MapVertex vertex{in.Read<std::int16_t>(at), in.Read<std::int16_t>(at + 2)};
        if (!ValidLon(vertex.lon) || !ValidLat(vertex.lat)) return Corrupt("a point is outside the globe");
        map.points_[i] = vertex;
    }

    map.rings_.resize(static_cast<std::size_t>(ringCount));
    for (std::size_t i = 0; i < map.rings_.size(); ++i) {
        const std::size_t at = static_cast<std::size_t>(ringsAt + i * kRingBytes);
        Ring& ring = map.rings_[i];
        ring.firstPoint = in.Read<std::uint32_t>(at);
        ring.pointCount = in.Read<std::uint32_t>(at + 4);
        if (ring.pointCount < 3 || std::uint64_t{ring.firstPoint} + ring.pointCount > pointCount) {
            return Corrupt("a ring is outside the point table");
        }
        ring.bounds = {180.0, 90.0, -180.0, -90.0};
        for (const auto& vertex : map.RingPoints(i)) {
            ring.bounds.Include({Degrees(vertex.lon), Degrees(vertex.lat), Degrees(vertex.lon), Degrees(vertex.lat)});
        }
    }

    map.countries_.resize(static_cast<std::size_t>(countryCount));
    for (std::size_t i = 0; i < map.countries_.size(); ++i) {
        const std::size_t at = static_cast<std::size_t>(countriesAt + i * kCountryBytes);
        Country& country = map.countries_[i];
        std::memcpy(country.code.data(), bytes.data() + at, 4);
        if (country.code[3] != '\0') return Corrupt("a country code is not terminated");
        for (std::size_t language = 0; language < kWorldMapLanguages; ++language) {
            country.names[language] = in.Read<std::uint32_t>(at + 4 + language * 4);
            if (!validString(country.names[language])) return Corrupt("a country name is outside the strings");
        }
        const std::int16_t west = in.Read<std::int16_t>(at + 44), south = in.Read<std::int16_t>(at + 46);
        const std::int16_t east = in.Read<std::int16_t>(at + 48), north = in.Read<std::int16_t>(at + 50);
        if (!ValidLon(west) || !ValidLon(east) || !ValidLat(south) || !ValidLat(north) || west > east || south > north) {
            return Corrupt("a country box is invalid");
        }
        country.bounds = {Degrees(west), Degrees(south), Degrees(east), Degrees(north)};
        country.firstRing = in.Read<std::uint32_t>(at + 52);
        country.ringCount = in.Read<std::uint32_t>(at + 56);
        if (country.ringCount == 0 || std::uint64_t{country.firstRing} + country.ringCount > ringCount) {
            return Corrupt("a country is outside the ring table");
        }
        for (std::uint32_t r = 0; r < country.ringCount; ++r) map.rings_[country.firstRing + r].country = static_cast<std::uint32_t>(i);
    }

    map.places_.resize(static_cast<std::size_t>(placeCount));
    for (std::size_t i = 0; i < map.places_.size(); ++i) {
        const std::size_t at = static_cast<std::size_t>(placesAt + i * kPlaceBytes);
        const auto lon = in.Read<std::int16_t>(at);
        const auto lat = in.Read<std::int16_t>(at + 2);
        Place& place = map.places_[i];
        place.longitude = Degrees(lon);
        place.latitude = Degrees(lat);
        place.name = in.Read<std::uint32_t>(at + 4);
        place.population = in.Read<std::uint32_t>(at + 8);
        place.country = in.Read<std::uint16_t>(at + 12);
        place.rank = in.Read<std::uint8_t>(at + 14);
        if (!ValidLon(lon) || !ValidLat(lat) || !validString(place.name) ||
            (place.country != 0xFFFF && place.country >= countryCount)) {
            return Corrupt("a place is invalid");
        }
    }
    return map;
}

Result<WorldMap> LoadEmbeddedWorldMap() {
    HMODULE module = ::GetModuleHandleW(nullptr);
    HRSRC resource = ::FindResourceW(module, L"AETHER_WORLD_MAP", RT_RCDATA);
    if (!resource) return MakeError(ErrorCode::NotFound, "the world map is not embedded in this program");
    const DWORD size = ::SizeofResource(module, resource);
    HGLOBAL loaded = ::LoadResource(module, resource);
    const void* data = loaded ? ::LockResource(loaded) : nullptr;
    if (!data || size == 0) return MakeError(ErrorCode::Unavailable, "the world map resource could not be read");
    return WorldMap::Parse(std::span<const std::uint8_t>(static_cast<const std::uint8_t*>(data), size));
}

std::span<const MapVertex> WorldMap::RingPoints(std::size_t ring) const noexcept {
    if (ring >= rings_.size()) return {};
    return std::span<const MapVertex>(points_).subspan(rings_[ring].firstPoint, rings_[ring].pointCount);
}

std::wstring WorldMap::CountryName(std::size_t country, std::size_t language) const {
    if (country >= countries_.size()) return {};
    if (language >= kWorldMapLanguages) language = 0;
    return text::Widen(std::string_view(strings_.c_str() + countries_[country].names[language]));
}

std::wstring WorldMap::PlaceName(std::size_t place) const {
    if (place >= places_.size()) return {};
    return text::Widen(std::string_view(strings_.c_str() + places_[place].name));
}

std::optional<std::size_t> WorldMap::FindCountry(std::string_view code) const noexcept {
    for (std::size_t i = 0; i < countries_.size(); ++i) {
        if (code == std::string_view(countries_[i].code.data())) return i;
    }
    return std::nullopt;
}

std::optional<std::size_t> WorldMap::CountryAt(double latitude, double longitude) const noexcept {
    if (!std::isfinite(latitude) || !std::isfinite(longitude)) return std::nullopt;
    for (std::size_t c = 0; c < countries_.size(); ++c) {
        const Country& country = countries_[c];
        if (!country.bounds.Contains(latitude, longitude)) continue;
        bool inside = false;
        for (std::uint32_t r = 0; r < country.ringCount; ++r) {
            const std::size_t ring = country.firstRing + r;
            if (rings_[ring].bounds.Contains(latitude, longitude) && CrossesOdd(RingPoints(ring), latitude, longitude)) {
                inside = !inside;
            }
        }
        if (inside) return c;
    }
    return std::nullopt;
}

std::optional<std::size_t> WorldMap::CountryFor(double latitude, double longitude) const noexcept {
    if (auto inside = CountryAt(latitude, longitude)) return inside;
    const auto place = NearestPlace(latitude, longitude, 40.0);
    if (!place || places_[*place].country == 0xFFFF) return std::nullopt;
    return places_[*place].country;
}

std::optional<std::size_t> WorldMap::NearestPlace(double latitude, double longitude, double maxKilometres,
                                                  std::optional<std::size_t> country) const noexcept {
    const auto search = [&](bool sameCountryOnly) -> std::optional<std::size_t> {
        std::optional<std::size_t> best;
        double bestDistance = maxKilometres;
        const double latitudeWindow = maxKilometres / 111.0 + 0.01;
        for (std::size_t i = 0; i < places_.size(); ++i) {
            const Place& place = places_[i];
            if (sameCountryOnly && place.country != *country) continue;
            if (std::abs(place.latitude - latitude) > latitudeWindow) continue;
            const double distance = DistanceKilometres(latitude, longitude, place.latitude, place.longitude);
            if (distance <= bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    };
    if (country && *country < countries_.size()) {
        if (auto own = search(true)) return own;
    }
    return search(false);
}

GeoBounds WorldMap::FocusBounds(double latitude, double longitude, std::optional<std::size_t> country, double maxSpan,
                                double fallbackSpan) const noexcept {
    const auto around = [&](double width, double height) {
        return GeoBounds{longitude - width / 2, std::max(-85.0, latitude - height / 2), longitude + width / 2,
                         std::min(85.0, latitude + height / 2)};
    };
    if (!country || *country >= countries_.size()) return around(fallbackSpan, fallbackSpan * 0.7);

    const Country& shape = countries_[*country];
    // The part that holds the position, or failing that (a coast lost to simplification) the nearest part.
    std::optional<std::size_t> anchor;
    double nearest = 1e9;
    for (std::uint32_t r = 0; r < shape.ringCount; ++r) {
        const std::size_t ring = shape.firstRing + r;
        const GeoBounds& box = rings_[ring].bounds;
        if (box.Contains(latitude, longitude) && CrossesOdd(RingPoints(ring), latitude, longitude)) {
            anchor = ring;
            break;
        }
        const double dx = std::max({box.west - longitude, 0.0, longitude - box.east});
        const double dy = std::max({box.south - latitude, 0.0, latitude - box.north});
        if (dx * dx + dy * dy < nearest) {
            nearest = dx * dx + dy * dy;
            anchor = ring;
        }
    }
    GeoBounds view = rings_[*anchor].bounds;
    const double margin = std::max(1.0, 0.35 * std::max(view.Width(), view.Height()));
    const GeoBounds reach{view.west - margin, view.south - margin, view.east + margin, view.north + margin};
    for (std::uint32_t r = 0; r < shape.ringCount; ++r) {
        const GeoBounds& box = rings_[shape.firstRing + r].bounds;
        if (box.Intersects(reach)) view.Include(box);
    }
    view.Include({longitude, latitude, longitude, latitude});

    // Very large countries: the region around the position, kept within the country's extent.
    const double maxHeight = maxSpan * 0.72;
    const auto limit = [](double low, double high, double centre, double span) {
        if (high - low <= span) return std::pair{low, high};
        double from = std::clamp(centre - span / 2, low, high - span);
        return std::pair{from, from + span};
    };
    std::tie(view.west, view.east) = limit(view.west, view.east, longitude, maxSpan);
    std::tie(view.south, view.north) = limit(view.south, view.north, latitude, maxHeight);

    // A little room around the shape, and a readable minimum for very small countries.
    const double padX = std::max(0.25, view.Width() * 0.06);
    const double padY = std::max(0.2, view.Height() * 0.08);
    view = {view.west - padX, view.south - padY, view.east + padX, view.north + padY};
    const double minimum = 1.6;
    if (view.Width() < minimum) {
        const double centre = (view.west + view.east) / 2;
        view.west = centre - minimum / 2;
        view.east = centre + minimum / 2;
    }
    if (view.Height() < minimum * 0.7) {
        const double centre = (view.south + view.north) / 2;
        view.south = centre - minimum * 0.35;
        view.north = centre + minimum * 0.35;
    }
    view.south = std::max(-85.0, view.south);
    view.north = std::min(85.0, view.north);
    return view;
}

std::vector<std::size_t> WorldMap::SamplePlaces(const GeoBounds& view, std::optional<std::size_t> country, double latitude,
                                                double longitude, std::size_t count) const {
    std::vector<std::size_t> chosen;
    if (count == 0) return chosen;
    const double scale = std::cos(std::clamp((view.south + view.north) / 2, -80.0, 80.0) * std::numbers::pi / 180.0);
    const double extent = std::max(view.Width() * scale, view.Height());
    const double separation = extent / (std::sqrt(static_cast<double>(count)) + 1.5);
    std::vector<std::pair<double, double>> taken{{latitude, longitude}};  // keep clear of the position's own label

    const auto pass = [&](bool sameCountryOnly, double spacing) {
        for (std::size_t i = 0; i < places_.size() && chosen.size() < count; ++i) {
            const Place& place = places_[i];
            if (sameCountryOnly && place.country != *country) continue;
            const auto lon = LongitudeIn(view, place.latitude, place.longitude);
            if (!lon) continue;
            if (std::find(chosen.begin(), chosen.end(), i) != chosen.end()) continue;
            const bool apart = std::all_of(taken.begin(), taken.end(), [&](const auto& other) {
                return SpacingDegrees(place.latitude, *lon, other.first, other.second, scale) >= spacing;
            });
            if (!apart) continue;
            chosen.push_back(i);
            taken.emplace_back(place.latitude, *lon);
        }
    };
    const bool hasCountry = country && *country < countries_.size();
    if (hasCountry) {
        pass(true, separation);
        if (chosen.size() < count / 2) pass(true, separation / 2);
    }
    pass(false, separation);  // neighbours fill what the country leaves
    return chosen;
}

}  // namespace aether::weather
