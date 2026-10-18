// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure time arithmetic used by ClockProvider, kept separate so it can be unit tested with fixed
// inputs. Times are FILETIME ticks: 100 ns intervals since 1601-01-01 00:00 UTC.
#pragma once

#include "status/StatusTypes.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace aether::status {

enum class ClockResolution {
    Minutes,  // one snapshot per minute boundary (default; what a status bar without seconds needs)
    Seconds,  // one snapshot per second boundary
};

inline constexpr std::int64_t kFileTimeTicksPerSecond = 10'000'000;

[[nodiscard]] std::int64_t ResolutionTicks(ClockResolution resolution) noexcept;

// First boundary of `resolution` strictly after `utcTicks`. Every time zone offset is a whole number
// of minutes, so UTC minute boundaries coincide with local minute boundaries.
[[nodiscard]] std::int64_t NextBoundaryTicks(std::int64_t utcTicks, ClockResolution resolution) noexcept;

[[nodiscard]] std::int64_t FileTimeToTicks(const FILETIME& fileTime) noexcept;
[[nodiscard]] FILETIME TicksToFileTime(std::int64_t ticks) noexcept;
[[nodiscard]] std::chrono::system_clock::time_point TicksToTimePoint(std::int64_t ticks) noexcept;
[[nodiscard]] std::int64_t TimePointToTicks(std::chrono::system_clock::time_point timePoint) noexcept;

struct TimeZoneDescription {
    std::wstring name;  // StandardName or DaylightName, whichever is in effect
    bool daylightSavingActive = false;
};

// Interprets the result of GetDynamicTimeZoneInformation. std::nullopt for TIME_ZONE_ID_INVALID.
[[nodiscard]] std::optional<TimeZoneDescription> DescribeTimeZone(DWORD zoneId,
                                                                  const DYNAMIC_TIME_ZONE_INFORMATION& info);

// Builds an Available snapshot from a UTC instant and the local wall-clock time of the same instant.
[[nodiscard]] ClockSnapshot MakeClockSnapshot(std::int64_t utcTicks, const SYSTEMTIME& local,
                                              const TimeZoneDescription& zone);

// The SYSTEMTIME a snapshot describes (local wall-clock time), for the NLS formatting functions.
[[nodiscard]] SYSTEMTIME ToSystemTime(const ClockSnapshot& snapshot) noexcept;

}  // namespace aether::status
