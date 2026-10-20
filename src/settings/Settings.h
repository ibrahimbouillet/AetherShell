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
enum class ThemePreference { System, Light, Dark };        // "system" follows the Windows light/dark mode
enum class AccentSource { Windows, Aether };              // Windows accent color or the Aether brand palette
enum class WeatherPlacement { Left, Right };              // next to the logo, or in the status cluster
enum class KeyboardIndicatorStyle { Compact, TwoLine };   // "FR" or a two-line "FRA / FR" indicator

// Version of the Dock size defaults. 1 is 0.1's 48/10; 2 is the compact 40/4 introduced in 0.2.
inline constexpr int kCurrentDockMetricsVersion = 2;

struct PinnedApp {
    std::wstring id;  // aether::apps identity ("aumid:...", "exe:...", "shell:...")
    std::wstring displayName;
    std::wstring parsingName;
    std::wstring appUserModelId;
    std::wstring executablePath;
    std::wstring shortcutPath;
    std::wstring arguments;
};

struct DockSettings {
    int iconSize = 40;             // DIPs, 32..96 (presets: Small 32, Medium 40, Large 48)
    int bottomMargin = 4;          // DIPs above the work-area bottom, 0..200
    int sideMargin = 16;           // minimum DIPs kept from the work-area sides, 0..400
    int iconSpacing = 8;           // DIPs, 0..32
    double opacity = 0.80;         // glass tint opacity, 0.30..1.00
    double magnification = 1.45;   // hover scale factor, 1.00 (off)..2.00
    bool showRunningApps = true;   // show unpinned running applications
    bool reserveSpace = false;     // register as an AppBar and reserve work area
    std::vector<PinnedApp> pinned; // in display order; at most 64, ids unique ignoring case
    // Replacement mode: true also reserves hover magnification, launch bounce and focus headroom
    // (ADR 0009 item 5); false reserves only the visible panel and the bottom gap.
    bool reserveAnimationSpace = false;
    // Internal migration marker, 1..2, not shown in the UI. A file without it was written by 0.1:
    // its untouched 48/10 size defaults move once to 40/4 while customized values are kept. In such a
    // file an absent iconSize or bottomMargin still means 48 or 10.
    int metricsVersion = kCurrentDockMetricsVersion;
    // Application ids the user removed; the automatic taskbar-pin sync never re-adds them.
    // At most 128 ids, each at most 1024 UTF-16 code units, unique ignoring case.
    std::vector<std::wstring> dismissedPins;
    bool explorerDefaultApplied = false;  // the one-time default File Explorer pin was offered
    bool showStartButton = true;          // Start entry that opens the Windows Start menu
    bool showTaskViewButton = true;       // Task View entry
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
    int height = 32;               // bar height in DIPs, 24..48; glyphs, text and reservation follow it
    // Persisted preference for future flyout/settings routing. Current status actions use their
    // documented fixed routes; this switch is not yet consumed by the app (see KNOWN_LIMITATIONS).
    bool openWindowsFlyouts = true;
    WeatherPlacement weatherPlacement = WeatherPlacement::Left;
    // TwoLine needs a height of at least 30 DIPs; the Top Bar falls back to Compact below that.
    KeyboardIndicatorStyle keyboardIndicator = KeyboardIndicatorStyle::TwoLine;
    bool showNotificationAreaButton = true; // effective only while taskbar replacement is active
    bool showBatteryPercent = true;
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
    // D23: the Dock replaces the Windows taskbar while Aether Shell runs (reversible). An absent or
    // non-boolean value means true; false is stored only after an explicit choice (Settings >
    // Desktop, --restore-taskbar), and ResetToDefaults keeps the current value.
    bool replaceWindowsTaskbar = true;
    bool taskbarPinsImported = false; // one automatic best-effort import when replacement is selected
    bool syncTaskbarPins = true;    // additively sync Windows taskbar pins while replacement is on
    MotionPreference motion = MotionPreference::System;
    ThemePreference themeMode = ThemePreference::System;  // high contrast always takes precedence
    AccentSource accentSource = AccentSource::Windows;
    double animationSpeed = 1.0;    // MotionPolicy duration scale, 0.5 (faster)..1.5 (slower)
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
