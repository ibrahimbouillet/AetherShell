// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "topbar/TopBarModel.h"

#include "core/Localization.h"
#include "core/Strings.h"
#include "status/ClockFormatter.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace aether::topbar {
namespace {

using i18n::Tr;
using i18n::TrFormat;
// D70: a line of a tooltip, translated whole, after a line break.
std::wstring Line(std::wstring_view english) { return L"\n" + std::wstring(Tr(english)); }
// A detail after the separator, translated.
std::wstring Detail(std::wstring_view english) { return L" \u00B7 " + std::wstring(Tr(english)); }

std::wstring StateText(status::ProviderState state) {
    switch (state) {
        case status::ProviderState::Initializing: return std::wstring(Tr(L"Initializing"));
        case status::ProviderState::Unavailable: return std::wstring(Tr(L"Unavailable"));
        case status::ProviderState::Unsupported: return std::wstring(Tr(L"Unsupported"));
        case status::ProviderState::Error: return std::wstring(Tr(L"Error"));
        case status::ProviderState::Available: return std::wstring(Tr(L"Available"));
    }
    return std::wstring(Tr(L"Unknown"));
}

DisplayItem Item(ItemKind kind, std::wstring text, std::wstring tooltip, bool actionable = false) {
    DisplayItem item;
    item.id = static_cast<int>(kind);
    item.kind = kind;
    item.text = std::move(text);
    item.accessibleName = tooltip;
    item.tooltip = std::move(tooltip);
    item.actionable = actionable;
    return item;
}

std::wstring Connectivity(status::InternetConnectivity value) {
    switch (value) {
        case status::InternetConnectivity::Internet: return std::wstring(Tr(L"Internet access"));
        case status::InternetConnectivity::LocalOnly: return std::wstring(Tr(L"Local network only"));
        case status::InternetConnectivity::Constrained: return std::wstring(Tr(L"Limited Internet access"));
        case status::InternetConnectivity::None: return std::wstring(Tr(L"No network connection"));
        case status::InternetConnectivity::Unknown: return std::wstring(Tr(L"Connectivity unknown"));
    }
    return std::wstring(Tr(L"Connectivity unknown"));
}

std::wstring KindText(status::ConnectionKind value) {
    switch (value) {
        case status::ConnectionKind::WiFi: return L"Wi-Fi";
        case status::ConnectionKind::Ethernet: return std::wstring(Tr(L"Ethernet"));
        case status::ConnectionKind::Cellular: return std::wstring(Tr(L"Cellular"));
        case status::ConnectionKind::Other: return std::wstring(Tr(L"Network"));
        case status::ConnectionKind::None: return std::wstring(Tr(L"Offline"));
        case status::ConnectionKind::Unknown: return std::wstring(Tr(L"Network status unavailable"));
    }
    return std::wstring(Tr(L"Network status unavailable"));
}

}  // namespace

std::optional<int> WiFiWaveCount(const DisplayItem& item) noexcept {
    if (item.connection != status::ConnectionKind::WiFi || !item.level || *item.level < 0 || *item.level > 5)
        return std::nullopt;
    return (*item.level * 3 + 4) / 5;
}

Result<std::vector<DisplayItem>> GroupQuickControlItems(std::vector<DisplayItem> items) {
    try {
        DisplayItem group; group.id=static_cast<int>(ItemKind::QuickControls);group.kind=ItemKind::QuickControls;
        group.actionable=true;group.tooltip=Tr(L"Quick controls");group.accessibleName=Tr(L"Quick controls");
        for (auto& item:items) if(item.kind==ItemKind::Network||item.kind==ItemKind::Volume||item.kind==ItemKind::Battery) {
            const auto line=item.tooltip.substr(0,item.tooltip.find(L'\n'));
            group.tooltip+=L"\n"+line;group.accessibleName+=L" · "+line;
            if(item.kind==ItemKind::Battery)group.text=item.text;
            group.components.push_back(std::move(item));
        }
        if(group.components.empty())return items;
        std::erase_if(items,[](const auto& item){return item.kind==ItemKind::Network||item.kind==ItemKind::Volume||item.kind==ItemKind::Battery;});
        const auto clock=std::find_if(items.begin(),items.end(),[](const auto& item){return item.kind==ItemKind::Clock;});
        items.insert(clock,std::move(group));return items;
    }catch(...){return MakeError(ErrorCode::Internal,"quick control status grouping failed");}
}

Result<std::vector<DisplayItem>> BuildDisplayItems(const TopBarSnapshots& s,
    const settings::TopBarSettings& config, bool weatherEnabled, std::wstring_view locale,
    TaskbarReplacementState taskbar) {
    try {
        std::vector<DisplayItem> result;
        result.push_back(Item(ItemKind::Logo, L"", std::wstring(Tr(L"Aether Shell menu")), true));
        if (config.showWeather && weatherEnabled) {
            const auto& snapshot = s.weather.snapshot;
            const bool usable = (snapshot.state == status::WeatherState::Available ||
                                 snapshot.state == status::WeatherState::Cached) && snapshot.temperature &&
                                 std::isfinite(*snapshot.temperature);
            auto item = Item(ItemKind::Weather, L"—", std::wstring(Tr(L"Weather status unavailable")), true);
            item.unknown = !usable;
            if (usable) {
                item.text = std::format(L"{:.0f}°{}", *snapshot.temperature,
                    snapshot.unit == status::TemperatureUnit::Fahrenheit ? L"F" : L"C");
                item.cached = snapshot.state == status::WeatherState::Cached;
                item.weatherGlyph = s.weather.glyph;
                item.isDay = s.weather.isDay;
                item.tooltip = snapshot.locationName + L" · " + std::wstring(Tr(snapshot.conditionText)) + L" · " + item.text;
                if (item.cached) item.tooltip += Line(L"Cached weather");
                if (snapshot.observedAt)
                    item.tooltip += L"\n" + TrFormat(L"Observation (UTC): {}",
                        std::format(L"{:%F %R}", std::chrono::floor<std::chrono::seconds>(*snapshot.observedAt)));
                if (!snapshot.attribution.empty()) item.tooltip += L"\n" + snapshot.attribution;
            } else {
                item.tooltip = TrFormat(L"Weather: {}", std::wstring(Tr(text::Widen(weather::ToString(snapshot.state)))));
            }
            if (!snapshot.detail.empty()) item.tooltip += L"\n" + text::Widen(snapshot.detail);
            item.tooltip += Line(L"Click to open weather settings");
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        if (config.showHiddenIconsButton) {
            // D45: Windows' own hidden notification-area icons (background apps), not notifications and
            // not a second Dock. The host opens Explorer's overflow; nothing here lists or imitates icons.
            auto item = Item(ItemKind::NotificationArea, L"", std::wstring(Tr(L"Show hidden icons")), true);
            item.tooltip += Line(L"Opens Windows' notification-area overflow");
            result.push_back(std::move(item));
        }
        if (config.showKeyboard) {
            const bool available = s.keyboard.state == status::ProviderState::Available && !s.keyboard.shortLabel.empty();
            auto item = Item(ItemKind::Keyboard, available ? s.keyboard.shortLabel : L"—",
                available ? s.keyboard.languageName + L" · " + s.keyboard.layoutName
                          : TrFormat(L"Input language: {}", StateText(s.keyboard.state)), true);
            item.unknown = !available;
            if (available && config.height >= 30 && config.keyboardIndicator == settings::KeyboardIndicatorStyle::TwoLine)
                item.secondaryText = s.keyboard.layoutName;
            if (available && !s.keyboard.fromForegroundWindow) item.tooltip += Line(L"Fallback input layout");
            item.tooltip += Line(L"Click to switch input language (Win+Space)");
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        if (config.showNetwork) {
            const bool available = s.network.state == status::ProviderState::Available;
            auto item = Item(ItemKind::Network, L"", available ? KindText(s.network.kind) + L" · " +
                Connectivity(s.network.connectivity) : TrFormat(L"Network: {}", StateText(s.network.state)), true);
            item.connection = available ? s.network.kind : status::ConnectionKind::Unknown;
            item.connectivity = available ? s.network.connectivity : status::InternetConnectivity::Unknown;
            item.unknown = !available || item.connection == status::ConnectionKind::Unknown;
            if (available && s.network.signalBars && *s.network.signalBars <= 5) item.level = *s.network.signalBars;
            if ((item.connection == status::ConnectionKind::WiFi || item.connection == status::ConnectionKind::Cellular) && !item.level)
                item.tooltip += Detail(L"Signal strength unavailable");
            item.tooltip += Line(L"Click to open Windows network settings");
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        if (config.showVolume) {
            const bool available = s.audio.state == status::ProviderState::Available && s.audio.deviceAvailable;
            auto item = Item(ItemKind::Volume, L"", available ? std::wstring(Tr(L"Audio output")) : TrFormat(L"Audio: {}", StateText(s.audio.state)), true);
            item.unknown = !available || !s.audio.muted || !s.audio.volume || !std::isfinite(*s.audio.volume);
            if (available && s.audio.volume && std::isfinite(*s.audio.volume)) {
                item.level = static_cast<int>(std::round(std::clamp(*s.audio.volume, 0.0f, 1.0f) * 100.0f));
                item.tooltip += L" · " + std::to_wstring(*item.level) + L"%";
            } else item.tooltip += Detail(L"volume unknown");
            if (available && s.audio.muted) {
                item.muted = *s.audio.muted;
                item.tooltip += Detail(item.muted ? L"Muted" : L"Sound on");
            }
            if (available && !s.audio.deviceName.empty()) item.tooltip += L"\n" + s.audio.deviceName;
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        if (config.showMicrophone) {
            const bool available = s.microphone.state == status::ProviderState::Available && s.microphone.deviceAvailable;
            auto item = Item(ItemKind::Microphone, L"", TrFormat(L"Microphone: {}", StateText(s.microphone.state)), true);
            item.unknown = !available || !s.microphone.muted;
            if (available && s.microphone.muted) {
                item.muted = *s.microphone.muted;
                item.tooltip += Detail(item.muted ? L"Muted" : L"Endpoint unmuted");
            }
            item.tooltip += Line(L"Recording detection unsupported");
            if (available && !s.microphone.deviceName.empty()) item.tooltip += L"\n" + s.microphone.deviceName;
            item.tooltip += Line(L"Click to toggle mute; right-click for Windows voice typing and microphone privacy settings");
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        const bool knownAbsent = !s.battery.batteryPresent &&
            (s.battery.state == status::ProviderState::Available || s.battery.state == status::ProviderState::Unavailable);
        if (config.showBattery && !(config.hideBatteryWhenAbsent && knownAbsent)) {
            const bool available = s.battery.state == status::ProviderState::Available && s.battery.batteryPresent;
            auto item = Item(ItemKind::Battery, L"—", TrFormat(L"Battery: {}", StateText(s.battery.state)), true);
            item.unknown = !available || !s.battery.percent || *s.battery.percent < 0 || *s.battery.percent > 100;
            const bool powerReported = s.battery.state == status::ProviderState::Available ||
                s.battery.state == status::ProviderState::Unavailable;
            item.powerSource = powerReported ? s.battery.source : status::PowerSource::Unknown;
            item.charge = available ? s.battery.charge : status::ChargeState::Unknown;
            item.lowOrCritical = available ? s.battery.lowOrCritical : std::nullopt;
            if (available && s.battery.percent && *s.battery.percent >= 0 && *s.battery.percent <= 100) {
                item.level = *s.battery.percent;
                item.text = std::to_wstring(*item.level) + L"%";
                item.tooltip = TrFormat(L"Battery {}", item.text);
            } else item.tooltip = Tr(L"Battery status unavailable");
            if (available && s.battery.charge == status::ChargeState::Charging) item.tooltip += Detail(L"Charging");
            if (item.powerSource == status::PowerSource::External) item.tooltip += Detail(L"External power");
            if (available && s.battery.remaining && s.battery.remaining->count() >= 0) {
                const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(*s.battery.remaining).count();
                item.tooltip += L"\n" + TrFormat(L"Estimated remaining: {} h {} min", minutes / 60, minutes % 60);
            }
            if (!config.showBatteryPercent) item.text.clear();
            item.tooltip += Line(L"Click to open power settings");
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        auto clock = Item(ItemKind::Clock, L"—", std::wstring(Tr(L"Clock status unavailable")), true);
        status::ClockFormatOptions options;
        options.format = static_cast<status::TimeFormat>(config.clockFormat);
        options.showSeconds = config.showSeconds;
        auto time = status::FormatTime(s.clock, options, locale);
        if (time) {
            clock.text = time.value();
            auto date = status::FormatDate(s.clock, static_cast<status::DateStyle>(config.dateStyle), locale);
            if (config.showDate && date) clock.text = date.value() + L"   " + clock.text;
            auto longDate = status::FormatDate(s.clock, status::DateStyle::Long, locale);
            clock.tooltip = longDate ? longDate.value() : time.value();
            if (!s.clock.timeZoneName.empty()) clock.tooltip += L"\n" + s.clock.timeZoneName;
        } else clock.unknown = true;
        clock.tooltip += Line(L"Click to open calendar and notifications");
        clock.accessibleName = clock.tooltip;
        result.push_back(std::move(clock));
        // D27 removed the trailing native-taskbar arrow; recovery stays in Settings, the logo menu and the CLI.
        // D45's hidden-icons chevron above is a different item that opens only the overflow.
        (void)taskbar; // kept in the API for existing callers and future recovery status.
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "top bar display model failed"); }
}

std::vector<PlacedItem> LayoutItems(const std::vector<DisplayItem>& items, const std::vector<float>& widths,
                                  float widthDip, float heightDip, settings::WeatherPlacement weatherPlacement) {
    std::vector<PlacedItem> result;
    if (!std::isfinite(widthDip) || !std::isfinite(heightDip) || widthDip <= 0 || heightDip <= 0 || items.empty()) return result;
    const float padding = std::min(10.0f, widthDip / 8.0f);
    const float logoWidth = std::min(36.0f, std::max(0.0f, widthDip - padding * 2));
    const auto logo = std::find_if(items.begin(), items.end(), [](const auto& item) { return item.kind == ItemKind::Logo; });
    if (logo != items.end()) result.push_back({*logo, {padding, 0, padding + logoWidth, heightDip}});
    const float available = std::max(0.0f, widthDip - padding * 2 - logoWidth - 12.0f);
    std::vector<std::pair<DisplayItem, float>> active;
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (items[index].kind == ItemKind::Logo) continue;
        const float requested = index < widths.size() && std::isfinite(widths[index]) ? widths[index] : 40.0f;
        active.emplace_back(items[index], std::clamp(requested, 28.0f, 400.0f));
    }
    const auto total = [&] { float value = 0; for (const auto& item : active) value += item.second; return value; };
    // The hidden-icons chevron gives way before the input language: both stay reachable from Windows.
    for (const auto kind : {ItemKind::Weather, ItemKind::NotificationArea, ItemKind::Keyboard}) {
        if (total() <= available) break;
        std::erase_if(active, [kind](const auto& item) { return item.first.kind == kind; });
    }
    // At extreme widths, retain the clock before decorative/status glyphs. Never overlap targets.
    for (const auto kind : {ItemKind::Network, ItemKind::Microphone, ItemKind::Volume, ItemKind::Battery, ItemKind::QuickControls}) {
        if (total() <= available) break;
        std::erase_if(active, [kind](const auto& item) { return item.first.kind == kind; });
    }
    const float sum = total();
    const float scale = sum > available && sum > 0 ? available / sum : 1.0f;
    float leftWeather = 0;
    if (weatherPlacement == settings::WeatherPlacement::Left) {
        const auto weather = std::find_if(active.begin(), active.end(), [](const auto& item) { return item.first.kind == ItemKind::Weather; });
        if (weather != active.end()) {
            leftWeather = weather->second * scale;
            const float left = padding + logoWidth + 12.0f;
            result.push_back({weather->first, {left, 0, left + leftWeather, heightDip}});
            active.erase(weather);
        }
    }
    float x = widthDip - padding - (sum * scale - leftWeather);
    for (const auto& [item, width] : active) {
        const float actual = width * scale;
        if (actual > 0) result.push_back({item, {x, 0, x + actual, heightDip}});
        x += actual;
    }
    return result;
}

Result<TopBarPlacement> ComputeTopBarPlacement(const RECT& area, UINT dpi, int heightDip,
                                              int topMarginDip, float shadowDip) {
    if (dpi < 48 || dpi > 768 || heightDip < 24 || heightDip > 48 || topMarginDip < 0 || topMarginDip > 100 ||
        !std::isfinite(shadowDip) || shadowDip < 0 || shadowDip > 128 || area.right <= area.left) {
        return MakeError(ErrorCode::InvalidArgument, "invalid Top Bar geometry");
    }
    const float scale = static_cast<float>(dpi) / 96.0f;
    const int side = static_cast<int>(std::lround(12.0f * scale));
    const int height = static_cast<int>(std::lround(static_cast<float>(heightDip) * scale));
    const int top = static_cast<int>(std::lround(static_cast<float>(topMarginDip) * scale));
    const int shadow = static_cast<int>(std::ceil(shadowDip * scale));
    const int width = area.right - area.left - side * 2;
    if (width <= 0) return MakeError(ErrorCode::Unavailable, "Top Bar monitor is too narrow");
    TopBarPlacement placed;
    placed.windowBoundsPx = {area.left + side - shadow, area.top + top - shadow,
        area.right - side + shadow, area.top + top + height + shadow};
    placed.panelBoundsPx = {shadow, shadow, shadow + width, shadow + height};
    placed.reservationHeightPx = top + height + shadow;
    return placed;
}

std::optional<int> HitTest(const std::vector<PlacedItem>& items, D2D1_POINT_2F point) noexcept {
    for (const auto& item : items) if (point.x >= item.rect.left && point.x < item.rect.right &&
        point.y >= item.rect.top && point.y < item.rect.bottom) return item.item.id;
    return std::nullopt;
}

Result<float> WheelVolumeTarget(std::optional<float> observed, std::optional<float> pending, float notches) {
    const auto baseline = pending ? pending : observed;
    if (!baseline || !std::isfinite(*baseline) || !std::isfinite(notches))
        return MakeError(ErrorCode::Unavailable, "volume level is unknown");
    return std::clamp(*baseline + notches * 0.02f, 0.0f, 1.0f);
}

}  // namespace aether::topbar
