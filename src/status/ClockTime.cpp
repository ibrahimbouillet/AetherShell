// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/ClockTime.h"

namespace aether::status {
namespace {
// FILETIME ticks between 1601-01-01 and the Unix epoch (system_clock's epoch).
constexpr std::int64_t kUnixEpochTicks = 116'444'736'000'000'000;
using Ticks = std::chrono::duration<std::int64_t, std::ratio<1, kFileTimeTicksPerSecond>>;

std::wstring BoundedName(const wchar_t (&name)[32]) {
    std::size_t length = 0;
    while (length < 32 && name[length] != L'\0') ++length;
    return std::wstring(name, length);
}
}  // namespace

std::int64_t ResolutionTicks(ClockResolution resolution) noexcept {
    return resolution == ClockResolution::Seconds ? kFileTimeTicksPerSecond : 60 * kFileTimeTicksPerSecond;
}

std::int64_t NextBoundaryTicks(std::int64_t utcTicks, ClockResolution resolution) noexcept {
    const std::int64_t period = ResolutionTicks(resolution);
    return (utcTicks / period + 1) * period;
}

std::int64_t FileTimeToTicks(const FILETIME& fileTime) noexcept {
    ULARGE_INTEGER value{};
    value.LowPart = fileTime.dwLowDateTime;
    value.HighPart = fileTime.dwHighDateTime;
    return static_cast<std::int64_t>(value.QuadPart);
}

FILETIME TicksToFileTime(std::int64_t ticks) noexcept {
    ULARGE_INTEGER value{};
    value.QuadPart = static_cast<ULONGLONG>(ticks);
    return FILETIME{value.LowPart, value.HighPart};
}

std::chrono::system_clock::time_point TicksToTimePoint(std::int64_t ticks) noexcept {
    return std::chrono::system_clock::time_point(
        std::chrono::duration_cast<std::chrono::system_clock::duration>(Ticks(ticks - kUnixEpochTicks)));
}

std::int64_t TimePointToTicks(std::chrono::system_clock::time_point timePoint) noexcept {
    return std::chrono::duration_cast<Ticks>(timePoint.time_since_epoch()).count() + kUnixEpochTicks;
}

std::optional<TimeZoneDescription> DescribeTimeZone(DWORD zoneId, const DYNAMIC_TIME_ZONE_INFORMATION& info) {
    switch (zoneId) {
        case TIME_ZONE_ID_DAYLIGHT: return TimeZoneDescription{BoundedName(info.DaylightName), true};
        case TIME_ZONE_ID_STANDARD:
        case TIME_ZONE_ID_UNKNOWN:  // the zone has no daylight saving transitions
            return TimeZoneDescription{BoundedName(info.StandardName), false};
        default: return std::nullopt;
    }
}

ClockSnapshot MakeClockSnapshot(std::int64_t utcTicks, const SYSTEMTIME& local, const TimeZoneDescription& zone) {
    ClockSnapshot snapshot;
    snapshot.state = ProviderState::Available;
    snapshot.utc = TicksToTimePoint(utcTicks);
    snapshot.year = local.wYear;
    snapshot.month = local.wMonth;
    snapshot.day = local.wDay;
    snapshot.dayOfWeek = local.wDayOfWeek;  // 0 = Sunday, as in SYSTEMTIME
    snapshot.hour = local.wHour;
    snapshot.minute = local.wMinute;
    snapshot.second = local.wSecond;
    snapshot.timeZoneName = zone.name;
    snapshot.daylightSavingActive = zone.daylightSavingActive;
    return snapshot;
}

SYSTEMTIME ToSystemTime(const ClockSnapshot& snapshot) noexcept {
    SYSTEMTIME time{};
    time.wYear = static_cast<WORD>(snapshot.year);
    time.wMonth = static_cast<WORD>(snapshot.month);
    time.wDay = static_cast<WORD>(snapshot.day);
    time.wDayOfWeek = static_cast<WORD>(snapshot.dayOfWeek);
    time.wHour = static_cast<WORD>(snapshot.hour);
    time.wMinute = static_cast<WORD>(snapshot.minute);
    time.wSecond = static_cast<WORD>(snapshot.second);
    return time;
}

}  // namespace aether::status
