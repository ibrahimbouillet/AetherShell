// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Persistence of the last good weather reading (%LOCALAPPDATA%\AetherShell\cache\weather.json by
// convention; the path is injected). Format, schema version 1:
//
//   {"version":1,"locationKey":"48.8566,2.3522","unit":"celsius","temperature":15.5,
//    "weatherCode":2,"isDay":true,"observedAt":1790000000,"fetchedAt":1790000100}
//
// Times are Unix seconds (UTC). The file is replaced atomically, so a crash never leaves a
// half-written cache. Anything that does not validate is ignored, never partially used.
#pragma once

#include "core/Result.h"
#include "weather/WeatherTypes.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace aether::weather::cache {

inline constexpr int kSchemaVersion = 1;
inline constexpr std::uint64_t kMaxCacheFileBytes = 16u * 1024u;

// Cached data older than this (measured from fetchedAt) is never shown.
inline constexpr std::chrono::hours kMaxCacheAge{6};
// fetchedAt values further in the future than this (clock moved backwards) make the entry unusable.
inline constexpr std::chrono::minutes kMaxFutureSkew{5};

[[nodiscard]] std::string Serialize(const WeatherReading& reading);

// Strict validation: version, non-empty location key, known unit, plausible temperature,
// weather code 0..99, boolean isDay and positive integer timestamps.
[[nodiscard]] Result<WeatherReading> Parse(std::string_view text);

// nullopt when the file does not exist; an error when it exists but cannot be read or validated.
[[nodiscard]] Result<std::optional<WeatherReading>> Load(const std::filesystem::path& file);

[[nodiscard]] Result<void> Save(const std::filesystem::path& file, const WeatherReading& reading);

// True when `reading` may still be shown at `now` (age below kMaxCacheAge, not from the future).
[[nodiscard]] bool IsWithinMaxAge(const WeatherReading& reading, std::chrono::system_clock::time_point now) noexcept;

}  // namespace aether::weather::cache
