// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/Strings.h"
#include "status/ClockFormatter.h"
#include "status/ClockTime.h"

#include <windows.h>

#include <cstring>

using namespace aether;
using namespace aether::status;

namespace {

// Saturday 17 October 2026, local wall-clock time.
ClockSnapshot FixedSnapshot(int hour, int minute, int second) {
    SYSTEMTIME local{};
    local.wYear = 2026;
    local.wMonth = 10;
    local.wDay = 17;
    local.wDayOfWeek = 6;
    local.wHour = static_cast<WORD>(hour);
    local.wMinute = static_cast<WORD>(minute);
    local.wSecond = static_cast<WORD>(second);
    SYSTEMTIME utcTime = local;  // the UTC instant is irrelevant to formatting
    FILETIME utc{};
    ::SystemTimeToFileTime(&utcTime, &utc);
    return MakeClockSnapshot(FileTimeToTicks(utc), local, TimeZoneDescription{L"Test Standard Time", false});
}

std::wstring Time(const ClockSnapshot& snapshot, TimeFormat format, bool seconds, const wchar_t* locale) {
    auto formatted = FormatTime(snapshot, ClockFormatOptions{format, seconds}, locale, LocaleOverrides::Ignore);
    if (!formatted) return L"<error: " + text::Widen(formatted.error().message) + L">";
    return formatted.value();
}

std::wstring Date(const ClockSnapshot& snapshot, DateStyle style, const wchar_t* locale) {
    auto formatted = FormatDate(snapshot, style, locale, LocaleOverrides::Ignore);
    if (!formatted) return L"<error: " + text::Widen(formatted.error().message) + L">";
    return formatted.value();
}

}  // namespace

// --- ClockTime ---------------------------------------------------------------------------------

AETHER_TEST("clock time: next boundary is strictly after the instant") {
    const std::int64_t second = kFileTimeTicksPerSecond;
    const std::int64_t minute = 60 * second;
    const std::int64_t base = (133'000'000'000'000'000 / minute) * minute;  // a whole UTC minute
    REQUIRE_EQ(base % minute, std::int64_t{0});

    CHECK_EQ(NextBoundaryTicks(base, ClockResolution::Seconds), base + second);
    CHECK_EQ(NextBoundaryTicks(base, ClockResolution::Minutes), base + minute);
    CHECK_EQ(NextBoundaryTicks(base + 1, ClockResolution::Seconds), base + second);
    CHECK_EQ(NextBoundaryTicks(base + second - 1, ClockResolution::Seconds), base + second);
    CHECK_EQ(NextBoundaryTicks(base + 59 * second + 5, ClockResolution::Minutes), base + minute);
    CHECK_EQ(NextBoundaryTicks(base + 30 * second, ClockResolution::Minutes), base + minute);
    CHECK_EQ(ResolutionTicks(ClockResolution::Seconds), second);
    CHECK_EQ(ResolutionTicks(ClockResolution::Minutes), minute);
}

AETHER_TEST("clock time: FILETIME and system_clock conversions round-trip") {
    const std::int64_t ticks = 134'000'000'123'456'789;
    const FILETIME fileTime = TicksToFileTime(ticks);
    CHECK_EQ(FileTimeToTicks(fileTime), ticks);

    // 1970-01-01 is the system_clock epoch.
    CHECK(TicksToTimePoint(116'444'736'000'000'000) == std::chrono::system_clock::time_point{});
    CHECK_EQ(TimePointToTicks(std::chrono::system_clock::time_point{}), 116'444'736'000'000'000);
    CHECK_EQ(TimePointToTicks(TicksToTimePoint(ticks)), ticks);
    CHECK_EQ(TimePointToTicks(TicksToTimePoint(ticks) + std::chrono::seconds(1)), ticks + kFileTimeTicksPerSecond);
}

AETHER_TEST("clock time: time zone description follows the zone id") {
    DYNAMIC_TIME_ZONE_INFORMATION info{};
    std::memcpy(info.StandardName, L"Romance Standard Time", sizeof(L"Romance Standard Time"));
    std::memcpy(info.DaylightName, L"Romance Daylight Time", sizeof(L"Romance Daylight Time"));

    auto daylight = DescribeTimeZone(TIME_ZONE_ID_DAYLIGHT, info);
    REQUIRE(daylight.has_value());
    CHECK_EQ(daylight->name, std::wstring(L"Romance Daylight Time"));
    CHECK(daylight->daylightSavingActive);

    auto standard = DescribeTimeZone(TIME_ZONE_ID_STANDARD, info);
    REQUIRE(standard.has_value());
    CHECK_EQ(standard->name, std::wstring(L"Romance Standard Time"));
    CHECK_FALSE(standard->daylightSavingActive);

    // A zone without daylight saving transitions.
    auto noDst = DescribeTimeZone(TIME_ZONE_ID_UNKNOWN, info);
    REQUIRE(noDst.has_value());
    CHECK_EQ(noDst->name, std::wstring(L"Romance Standard Time"));
    CHECK_FALSE(noDst->daylightSavingActive);

    CHECK_FALSE(DescribeTimeZone(TIME_ZONE_ID_INVALID, info).has_value());

    // A name filling the whole 32-character field has no terminator; it must not be over-read.
    for (auto& c : info.StandardName) c = L'X';
    auto full = DescribeTimeZone(TIME_ZONE_ID_STANDARD, info);
    REQUIRE(full.has_value());
    CHECK_EQ(full->name.size(), 32u);
}

AETHER_TEST("clock time: snapshot carries the local fields and round-trips to SYSTEMTIME") {
    const ClockSnapshot snapshot = FixedSnapshot(14, 5, 9);
    CHECK_EQ(snapshot.state, ProviderState::Available);
    CHECK_EQ(snapshot.year, 2026);
    CHECK_EQ(snapshot.month, 10);
    CHECK_EQ(snapshot.day, 17);
    CHECK_EQ(snapshot.dayOfWeek, 6);
    CHECK_EQ(snapshot.hour, 14);
    CHECK_EQ(snapshot.minute, 5);
    CHECK_EQ(snapshot.second, 9);
    CHECK_EQ(snapshot.timeZoneName, std::wstring(L"Test Standard Time"));
    CHECK_FALSE(snapshot.daylightSavingActive);

    const SYSTEMTIME back = ToSystemTime(snapshot);
    CHECK_EQ(back.wYear, 2026);
    CHECK_EQ(back.wMonth, 10);
    CHECK_EQ(back.wDay, 17);
    CHECK_EQ(back.wDayOfWeek, 6);
    CHECK_EQ(back.wHour, 14);
    CHECK_EQ(back.wMinute, 5);
    CHECK_EQ(back.wSecond, 9);
    CHECK_EQ(back.wMilliseconds, 0);
}

// --- ConvertTimePicture ------------------------------------------------------------------------

AETHER_TEST("clock format: 24-hour conversion drops the marker and its space") {
    const auto suffix = TimeMarkerPosition::Suffix;
    const auto prefix = TimeMarkerPosition::Prefix;
    CHECK_EQ(ConvertTimePicture(L"h:mm tt", TimeFormat::Hours24, suffix), std::wstring(L"HH:mm"));
    CHECK_EQ(ConvertTimePicture(L"hh:mm:ss tt", TimeFormat::Hours24, suffix), std::wstring(L"HH:mm:ss"));
    CHECK_EQ(ConvertTimePicture(L"tt h:mm", TimeFormat::Hours24, prefix), std::wstring(L"HH:mm"));
    CHECK_EQ(ConvertTimePicture(L"h:mm tt", TimeFormat::Hours24, suffix), std::wstring(L"HH:mm"));
    // Already 24-hour: unchanged, including the locale's own separators and literals.
    CHECK_EQ(ConvertTimePicture(L"HH:mm", TimeFormat::Hours24, suffix), std::wstring(L"HH:mm"));
    CHECK_EQ(ConvertTimePicture(L"H.mm", TimeFormat::Hours24, suffix), std::wstring(L"H.mm"));
    CHECK_EQ(ConvertTimePicture(L"HH'h'mm", TimeFormat::Hours24, suffix), std::wstring(L"HH'h'mm"));
}

AETHER_TEST("clock format: 12-hour conversion adds the marker at the locale position") {
    const auto suffix = TimeMarkerPosition::Suffix;
    const auto prefix = TimeMarkerPosition::Prefix;
    CHECK_EQ(ConvertTimePicture(L"HH:mm", TimeFormat::Hours12, suffix), std::wstring(L"h:mm tt"));
    CHECK_EQ(ConvertTimePicture(L"HH:mm:ss", TimeFormat::Hours12, suffix), std::wstring(L"h:mm:ss tt"));
    CHECK_EQ(ConvertTimePicture(L"H:mm", TimeFormat::Hours12, prefix), std::wstring(L"tt h:mm"));
    CHECK_EQ(ConvertTimePicture(L"HH.mm", TimeFormat::Hours12, suffix), std::wstring(L"h.mm tt"));
    // Quoted literals are never rewritten, even when they contain picture letters.
    CHECK_EQ(ConvertTimePicture(L"HH'h'mm", TimeFormat::Hours12, suffix), std::wstring(L"h'h'mm tt"));
    CHECK_EQ(ConvertTimePicture(L"'Hh tt' HH:mm", TimeFormat::Hours12, suffix), std::wstring(L"'Hh tt' h:mm tt"));
    // A custom marker fragment (quoted designator for locales without one).
    CHECK_EQ(ConvertTimePicture(L"HH:mm", TimeFormat::Hours12, suffix, L"'PM'"), std::wstring(L"h:mm 'PM'"));
    // Already 12-hour: unchanged.
    CHECK_EQ(ConvertTimePicture(L"h:mm tt", TimeFormat::Hours12, suffix), std::wstring(L"h:mm tt"));
    CHECK_EQ(ConvertTimePicture(L"tt h:mm", TimeFormat::Hours12, prefix), std::wstring(L"tt h:mm"));
    // System never rewrites.
    CHECK_EQ(ConvertTimePicture(L"HH:mm", TimeFormat::System, suffix), std::wstring(L"HH:mm"));
}

// --- FormatTime / FormatDate with fixed locales (stock locale data, no user overrides) -----------

AETHER_TEST("clock format: en-US time in every mode") {
    const auto afternoon = FixedSnapshot(14, 5, 9);
    CHECK_EQ(Time(afternoon, TimeFormat::System, false, L"en-US"), std::wstring(L"2:05 PM"));
    CHECK_EQ(Time(afternoon, TimeFormat::System, true, L"en-US"), std::wstring(L"2:05:09 PM"));
    CHECK_EQ(Time(afternoon, TimeFormat::Hours12, false, L"en-US"), std::wstring(L"2:05 PM"));
    CHECK_EQ(Time(afternoon, TimeFormat::Hours24, false, L"en-US"), std::wstring(L"14:05"));
    CHECK_EQ(Time(afternoon, TimeFormat::Hours24, true, L"en-US"), std::wstring(L"14:05:09"));

    const auto morning = FixedSnapshot(9, 5, 0);
    CHECK_EQ(Time(morning, TimeFormat::System, false, L"en-US"), std::wstring(L"9:05 AM"));
    CHECK_EQ(Time(morning, TimeFormat::Hours24, false, L"en-US"), std::wstring(L"09:05"));

    const auto midnight = FixedSnapshot(0, 30, 0);
    CHECK_EQ(Time(midnight, TimeFormat::System, false, L"en-US"), std::wstring(L"12:30 AM"));
    CHECK_EQ(Time(midnight, TimeFormat::Hours24, false, L"en-US"), std::wstring(L"00:30"));
}

AETHER_TEST("clock format: fr-FR and de-DE time in every mode") {
    const auto afternoon = FixedSnapshot(14, 5, 9);
    for (const wchar_t* locale : {L"fr-FR", L"de-DE"}) {
        TEST_NOTE(test::NarrowForDisplay(locale));
        CHECK_EQ(Time(afternoon, TimeFormat::System, false, locale), std::wstring(L"14:05"));
        CHECK_EQ(Time(afternoon, TimeFormat::System, true, locale), std::wstring(L"14:05:09"));
        CHECK_EQ(Time(afternoon, TimeFormat::Hours24, false, locale), std::wstring(L"14:05"));
        // These locales define no AM/PM designators; the invariant ones keep a 12-hour time unambiguous.
        CHECK_EQ(Time(afternoon, TimeFormat::Hours12, false, locale), std::wstring(L"2:05 PM"));
        CHECK_EQ(Time(afternoon, TimeFormat::Hours12, true, locale), std::wstring(L"2:05:09 PM"));
        CHECK_EQ(Time(FixedSnapshot(9, 5, 0), TimeFormat::Hours12, false, locale), std::wstring(L"9:05 AM"));
        CHECK_EQ(Time(FixedSnapshot(0, 30, 0), TimeFormat::Hours12, false, locale), std::wstring(L"12:30 AM"));
    }
}

AETHER_TEST("clock format: locale designators and marker position are kept") {
    const auto afternoon = FixedSnapshot(14, 5, 9);
    // ko-KR puts its own designator first ("tt h:mm").
    CHECK_EQ(Time(afternoon, TimeFormat::System, false, L"ko-KR"), std::wstring(L"오후 2:05"));
    CHECK_EQ(Time(afternoon, TimeFormat::Hours24, false, L"ko-KR"), std::wstring(L"14:05"));
    // ja-JP is 24-hour by default but defines designators: the 12-hour form uses them.
    CHECK_EQ(Time(afternoon, TimeFormat::System, false, L"ja-JP"), std::wstring(L"14:05"));
    CHECK_EQ(Time(afternoon, TimeFormat::Hours12, false, L"ja-JP"), std::wstring(L"2:05 午後"));
}

AETHER_TEST("clock format: dates in en-US, fr-FR and de-DE") {
    const auto snapshot = FixedSnapshot(14, 5, 9);
    CHECK_EQ(Date(snapshot, DateStyle::WeekdayDayMonth, L"en-US"), std::wstring(L"Sat 17 Oct"));
    CHECK_EQ(Date(snapshot, DateStyle::WeekdayDayMonth, L"fr-FR"), std::wstring(L"sam. 17 oct."));
    CHECK_EQ(Date(snapshot, DateStyle::WeekdayDayMonth, L"de-DE"), std::wstring(L"Sa 17 Okt"));

    CHECK_EQ(Date(snapshot, DateStyle::Short, L"en-US"), std::wstring(L"10/17/2026"));
    CHECK_EQ(Date(snapshot, DateStyle::Short, L"fr-FR"), std::wstring(L"17/10/2026"));
    CHECK_EQ(Date(snapshot, DateStyle::Short, L"de-DE"), std::wstring(L"17.10.2026"));

    CHECK_EQ(Date(snapshot, DateStyle::Long, L"en-US"), std::wstring(L"Saturday, October 17, 2026"));
    CHECK_EQ(Date(snapshot, DateStyle::Long, L"fr-FR"), std::wstring(L"samedi 17 octobre 2026"));
    CHECK_EQ(Date(snapshot, DateStyle::Long, L"de-DE"), std::wstring(L"Samstag, 17. Oktober 2026"));
}

AETHER_TEST("clock format: user default locale produces text") {
    const auto snapshot = FixedSnapshot(14, 5, 9);
    auto time = FormatTime(snapshot, ClockFormatOptions{});
    REQUIRE(time.ok());
    CHECK_FALSE(time.value().empty());
    auto date = FormatDate(snapshot, DateStyle::WeekdayDayMonth);
    REQUIRE(date.ok());
    CHECK(date.value().find(L"17") != std::wstring::npos);
    TEST_NOTE("user default: " + test::NarrowForDisplay(time.value()) + " / " + test::NarrowForDisplay(date.value()));
}

AETHER_TEST("clock format: refuses snapshots without a time and unknown locales") {
    ClockSnapshot initializing;
    auto time = FormatTime(initializing, ClockFormatOptions{}, L"en-US");
    REQUIRE_FALSE(time.ok());
    CHECK_EQ(time.error().code, ErrorCode::InvalidState);
    auto date = FormatDate(initializing, DateStyle::Short, L"en-US");
    REQUIRE_FALSE(date.ok());
    CHECK_EQ(date.error().code, ErrorCode::InvalidState);

    ClockSnapshot failed;
    failed.state = ProviderState::Error;
    CHECK_FALSE(FormatTime(failed, ClockFormatOptions{}, L"en-US").ok());

    const auto snapshot = FixedSnapshot(14, 5, 9);
    CHECK_FALSE(FormatTime(snapshot, ClockFormatOptions{}, L"not a locale!").ok());
    CHECK_FALSE(FormatDate(snapshot, DateStyle::Long, L"not a locale!").ok());
}
