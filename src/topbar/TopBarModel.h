// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Result.h"
#include "settings/Settings.h"
#include "status/StatusTypes.h"
#include "weather/WeatherTypes.h"

#include <d2d1.h>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aether::topbar {

enum class ItemKind { Logo = 1, Weather, Keyboard, Network, Volume, Microphone, Battery, Clock, NotificationArea, QuickControls };
struct TaskbarReplacementState {
    bool available = false;
    bool active = false;
    bool suspended = false;
    bool operator==(const TaskbarReplacementState&) const = default;
};
struct TopBarSnapshots {
    status::ClockSnapshot clock;
    status::BatterySnapshot battery;
    status::KeyboardSnapshot keyboard;
    status::NetworkSnapshot network;
    status::AudioSnapshot audio;
    status::MicrophoneSnapshot microphone;
    weather::WeatherReport weather;
};
struct DisplayItem {
    int id = 0;
    ItemKind kind = ItemKind::Logo;
    std::wstring text;
    std::wstring secondaryText;
    std::wstring tooltip;
    std::wstring accessibleName;
    bool actionable = false;
    bool unknown = false;
    bool muted = false;
    bool cached = false;
    std::optional<int> level;  // Percent for battery/audio, real bars for wireless.
    status::ConnectionKind connection = status::ConnectionKind::Unknown;
    status::InternetConnectivity connectivity = status::InternetConnectivity::Unknown;
    status::PowerSource powerSource = status::PowerSource::Unknown;
    status::ChargeState charge = status::ChargeState::Unknown;
    weather::WeatherGlyph weatherGlyph = weather::WeatherGlyph::Unknown;
    std::optional<bool> isDay;
    std::optional<bool> lowOrCritical;
    std::vector<DisplayItem> components; // grouped real network/audio/battery status, no synthetic readings
    bool operator==(const DisplayItem&) const = default;
};
struct PlacedItem { DisplayItem item; D2D1_RECT_F rect{}; };

// Windows reports 0..5 wireless bars. Wi-Fi uses three curved wave levels, cellular retains
// five vertical bars; missing strength never becomes a fabricated full-strength reading.
[[nodiscard]] std::optional<int> WiFiWaveCount(const DisplayItem& item) noexcept;
[[nodiscard]] Result<std::vector<DisplayItem>> GroupQuickControlItems(std::vector<DisplayItem> items);

[[nodiscard]] Result<std::vector<DisplayItem>> BuildDisplayItems(const TopBarSnapshots& snapshots,
    const settings::TopBarSettings& settings, bool weatherEnabled, std::wstring_view locale = {},
    TaskbarReplacementState taskbar = {});
// Widths correspond to items and include glyph/text padding. Low-priority weather then keyboard
// disappear first; remaining items shrink/clamp without overlapping the logo.
[[nodiscard]] std::vector<PlacedItem> LayoutItems(const std::vector<DisplayItem>& items,
    const std::vector<float>& widths, float widthDip, float heightDip = 32.0f,
    settings::WeatherPlacement weatherPlacement = settings::WeatherPlacement::Right);
struct TopBarPlacement {
    RECT windowBoundsPx{};
    RECT panelBoundsPx{};  // relative to window, excludes the exterior shadow
    int reservationHeightPx = 0;
};
// Pure geometry; reserve the complete visible footprint, including the exterior shadow. `sideDip` is the space kept
// free at each end of the bar: none for the Text presentation (D71), which spans the screen.
[[nodiscard]] Result<TopBarPlacement> ComputeTopBarPlacement(const RECT& area, UINT dpi, int heightDip,
                                                           int topMarginDip, float shadowDip, float sideDip = 12.0f);

// D71: the Text presentation writes the items straight over the desktop picture. What lies behind them is measured
// once per picture: the relative luminance (WCAG 2, 0 black to 1 white) of the strip the bar occupies, in equal
// columns across the screen. `low` and `high` are the darkest and the lightest parts of a column, `mean` its average.
struct BackdropColumn {
    float low = 0.0f, mean = 0.0f, high = 0.0f;
    bool operator==(const BackdropColumn&) const = default;
};
struct BackdropProfile {
    std::vector<BackdropColumn> columns;
    bool operator==(const BackdropProfile&) const = default;
};
// Measures `strip` (pixels of the picture, clipped to it) of top-down BGRA pixels in `columns` equal columns. No
// column when there is nothing to measure.
[[nodiscard]] BackdropProfile MeasureBackdrop(std::span<const std::uint8_t> bgra, int width, int height, const RECT& strip,
                                              int columns);
// The ink that reads over a backdrop: light or dark, and how strong a soft halo of the opposite tone lies under it
// (0 to 1) so that it keeps 4.5:1 against the lightest or darkest part behind it.
struct TextInk {
    bool dark = false;
    float halo = 0.0f;
    bool operator==(const TextInk&) const = default;
};
// For items lying between `left` and `right` (0 to 1 of the profile's width). Light ink is preferred, as on a
// picture; dark ink where the whole backdrop behind them is light. Without a profile nothing is known of the
// backdrop: the theme decides (`lightTheme`: dark ink), under a strong halo.
[[nodiscard]] TextInk ChooseTextInk(const BackdropProfile* profile, float left, float right, bool lightTheme) noexcept;
[[nodiscard]] std::optional<int> HitTest(const std::vector<PlacedItem>& items, D2D1_POINT_2F point) noexcept;
[[nodiscard]] Result<float> WheelVolumeTarget(std::optional<float> observed, std::optional<float> pending,
                                             float notches);

}  // namespace aether::topbar
