// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Locale-aware text for clock snapshots, built on GetTimeFormatEx/GetDateFormatEx. Pure functions of
// their inputs and the Windows locale data, so they are unit tested with fixed snapshots and locales.
#pragma once

#include "core/Result.h"
#include "status/StatusTypes.h"

#include <string>
#include <string_view>

namespace aether::status {

enum class TimeFormat {
    System,   // the locale's own time format (the user's Region settings for the default locale)
    Hours12,  // force a 12-hour clock, keeping the locale's separators and AM/PM designators
    Hours24,  // force a 24-hour clock, keeping the locale's separators
};

struct ClockFormatOptions {
    TimeFormat format = TimeFormat::System;
    bool showSeconds = false;
};

enum class DateStyle {
    WeekdayDayMonth,  // short localized form, picture "ddd d MMM" (e.g. "Sat 17 Oct", "sam. 17 oct.")
    Short,            // the locale's short date (DATE_SHORTDATE)
    Long,             // the locale's long date (DATE_LONGDATE)
};

// Whether the user's Region customizations apply when `localeName` is the user's locale. Ignore gives
// the stock locale data and is what tests use for deterministic output.
enum class LocaleOverrides { Apply, Ignore };

// `localeName` empty means LOCALE_NAME_USER_DEFAULT. Fails with InvalidState when the snapshot is not
// Available, and with the NLS error for an unknown locale or an invalid date.
[[nodiscard]] Result<std::wstring> FormatTime(const ClockSnapshot& snapshot, const ClockFormatOptions& options,
                                              std::wstring_view localeName = {},
                                              LocaleOverrides overrides = LocaleOverrides::Apply);

[[nodiscard]] Result<std::wstring> FormatDate(const ClockSnapshot& snapshot, DateStyle style,
                                              std::wstring_view localeName = {},
                                              LocaleOverrides overrides = LocaleOverrides::Apply);

enum class TimeMarkerPosition { Suffix, Prefix };  // LOCALE_ITIMEMARKPOSN 0 / 1

// Rewrites a GetTimeFormatEx picture for a forced 12- or 24-hour clock. Quoted literals are kept
// verbatim. Hours24: h/hh become HH and the AM/PM marker (with the space next to it) is removed; a
// picture that is already 24-hour is returned unchanged. Hours12: H/HH become h and, when the picture
// has no marker, `marker` (a picture fragment, "tt" by default) is added at `position`; a picture that
// is already 12-hour is returned unchanged. System returns the picture unchanged.
[[nodiscard]] std::wstring ConvertTimePicture(std::wstring_view picture, TimeFormat format,
                                              TimeMarkerPosition position, std::wstring_view marker = L"tt");

}  // namespace aether::status
