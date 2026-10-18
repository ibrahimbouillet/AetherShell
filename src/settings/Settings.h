// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// User configuration model (schema version 1). Persisted as JSON in
// %LOCALAPPDATA%\AetherShell\config\settings.json by SettingsService.
// Ranges listed here are enforced by Validate(); out-of-range values are
// clamped and reported as warnings, never silently discarded.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace aether::settings {

inline constexpr int kCurrentSchemaVersion = 1;

enum class MotionPreference { System, Reduced, Full };     // "system" follows "Show animations in Windows"
enum class ClockFormat { System, Hours12, Hours24 };       // "system" follows the user locale
enum class DateStyle { WeekdayDayMonth, Short, Long };    // "Sat 17 Oct" / locale short / locale long
enum class TemperatureUnit { Celsius, Fahrenheit };
enum class UpdateChannel { Stable, Prerelease };

struct PinnedApp {
    std::wstring id;  // aether::apps identity ("aumid:...", "exe:...", "shell:...")
    std::wstring displayName;
    std::wstring parsingName;
    std::wstring appUserModelId;
    std::wstring executablePath;
    std::wstring shortcutPath;
};

struct DockSettings {
    int iconSize = 48;             // DIPs, 32..96
    int bottomMargin = 10;         // DIPs above the work-area bottom, 0..200
    int sideMargin = 16;           // minimum DIPs kept from the work-area sides, 0..400
    int iconSpacing = 8;           // DIPs, 0..32
    double opacity = 0.80;         // glass tint opacity, 0.30..1.00
    double magnification = 1.45;   // hover scale factor, 1.00 (off)..2.00
    bool showRunningApps = true;   // show unpinned running applications
    bool reserveSpace = false;     // register as an AppBar and reserve work area
    std::vector<PinnedApp> pinned; // in display order
};

struct TopBarSettings {
    int topMargin = 6;             // DIPs below the work-area top, 0..100
    double opacity = 0.80;         // 0.30..1.00
    ClockFormat clockFormat = ClockFormat::System;
    bool showSeconds = false;
    DateStyle dateStyle = DateStyle::WeekdayDayMonth;
    bool showDate = true;
    bool showBattery = true;
    bool hideBatteryWhenAbsent = true;
    bool showKeyboard = true;
    bool showNetwork = true;
    bool showVolume = true;
    bool showMicrophone = true;
    bool showWeather = true;       // only effective when weather.enabled
    bool reserveSpace = true;      // register as a top AppBar so maximized windows stay below it
};

struct WeatherSettings {
    bool enabled = false;          // off by default: weather needs network requests (privacy)
    std::wstring locationName;     // display name chosen by the user, e.g. "Paris, France"
    std::optional<double> latitude;   // -90..90
    std::optional<double> longitude;  // -180..180
    TemperatureUnit unit = TemperatureUnit::Celsius;
    int refreshMinutes = 30;       // 15..180
};

struct UpdateSettings {
    bool automaticChecks = true;
    int checkIntervalHours = 24;   // 6..168
    bool notifications = true;
    UpdateChannel channel = UpdateChannel::Stable;
    std::string skippedVersion;    // "Postpone this version": not announced again
};

// RegisterHotKey-compatible modifier bits: Alt=0x1, Control=0x2, Shift=0x4, Win=0x8.
// MOD_NOREPEAT is a registration policy, not part of the persisted user combination.
struct HotkeyChord {
    std::uint32_t modifiers = 0x0003;
    std::uint32_t virtualKey = 0x20;
    bool operator==(const HotkeyChord&) const = default;
};

struct GeneralSettings {
    bool startWithWindows = false;  // only ever set after explicit user consent
    MotionPreference motion = MotionPreference::System;
    bool hotkeysEnabled = true;     // Ctrl+Alt+Space (search), Ctrl+Alt+D (focus Dock)
    HotkeyChord searchHotkey{};
    HotkeyChord focusDockHotkey{0x0003, 0x44};
    std::string logLevel = "info";  // trace|debug|info|warning|error
};

struct Settings {
    int schemaVersion = kCurrentSchemaVersion;
    DockSettings dock;
    TopBarSettings topBar;
    WeatherSettings weather;
    UpdateSettings updates;
    GeneralSettings general;
};

}  // namespace aether::settings
