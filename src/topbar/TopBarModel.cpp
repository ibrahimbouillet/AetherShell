// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "topbar/TopBarModel.h"

#include "core/Strings.h"
#include "status/ClockFormatter.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>

namespace aether::topbar {
namespace {

std::wstring StateText(status::ProviderState state) {
    switch (state) {
        case status::ProviderState::Initializing: return L"Initializing";
        case status::ProviderState::Unavailable: return L"Unavailable";
        case status::ProviderState::Unsupported: return L"Unsupported";
        case status::ProviderState::Error: return L"Error";
        case status::ProviderState::Available: return L"Available";
    }
    return L"Unknown";
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
        case status::InternetConnectivity::Internet: return L"Internet access";
        case status::InternetConnectivity::LocalOnly: return L"Local network only";
        case status::InternetConnectivity::Constrained: return L"Limited Internet access";
        case status::InternetConnectivity::None: return L"No network connection";
        case status::InternetConnectivity::Unknown: return L"Connectivity unknown";
    }
    return L"Connectivity unknown";
}

std::wstring KindText(status::ConnectionKind value) {
    switch (value) {
        case status::ConnectionKind::WiFi: return L"Wi-Fi";
        case status::ConnectionKind::Ethernet: return L"Ethernet";
        case status::ConnectionKind::Cellular: return L"Cellular";
        case status::ConnectionKind::Other: return L"Network";
        case status::ConnectionKind::None: return L"Offline";
        case status::ConnectionKind::Unknown: return L"Network status unavailable";
    }
    return L"Network status unavailable";
}

}  // namespace

Result<std::vector<DisplayItem>> BuildDisplayItems(const TopBarSnapshots& s,
    const settings::TopBarSettings& config, bool weatherEnabled, std::wstring_view locale) {
    try {
        std::vector<DisplayItem> result;
        result.push_back(Item(ItemKind::Logo, L"", L"Aether Shell menu", true));
        if (config.showWeather && weatherEnabled) {
            const auto& snapshot = s.weather.snapshot;
            const bool usable = (snapshot.state == status::WeatherState::Available ||
                                 snapshot.state == status::WeatherState::Cached) && snapshot.temperature &&
                                 std::isfinite(*snapshot.temperature);
            auto item = Item(ItemKind::Weather, L"—", L"Weather status unavailable");
            item.unknown = !usable;
            if (usable) {
                item.text = std::format(L"{:.0f}°{}", *snapshot.temperature,
                    snapshot.unit == status::TemperatureUnit::Fahrenheit ? L"F" : L"C");
                item.cached = snapshot.state == status::WeatherState::Cached;
                item.weatherGlyph = s.weather.glyph;
                item.isDay = s.weather.isDay;
                item.tooltip = snapshot.locationName + L" · " + snapshot.conditionText + L" · " + item.text;
                if (item.cached) item.tooltip += L"\nCached weather";
                if (snapshot.observedAt)
                    item.tooltip += std::format(L"\nObservation (UTC): {:%F %R}",
                        std::chrono::floor<std::chrono::seconds>(*snapshot.observedAt));
                if (!snapshot.attribution.empty()) item.tooltip += L"\n" + snapshot.attribution;
            } else {
                item.tooltip = L"Weather: " + text::Widen(weather::ToString(snapshot.state));
            }
            if (!snapshot.detail.empty()) item.tooltip += L"\n" + text::Widen(snapshot.detail);
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        if (config.showKeyboard) {
            const bool available = s.keyboard.state == status::ProviderState::Available && !s.keyboard.shortLabel.empty();
            auto item = Item(ItemKind::Keyboard, available ? s.keyboard.shortLabel : L"—",
                available ? s.keyboard.languageName + L" · " + s.keyboard.layoutName
                          : L"Input language: " + StateText(s.keyboard.state));
            item.unknown = !available;
            if (available && !s.keyboard.fromForegroundWindow) item.tooltip += L"\nFallback input layout";
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        if (config.showNetwork) {
            const bool available = s.network.state == status::ProviderState::Available;
            auto item = Item(ItemKind::Network, L"", available ? KindText(s.network.kind) + L" · " +
                Connectivity(s.network.connectivity) : L"Network: " + StateText(s.network.state));
            item.connection = available ? s.network.kind : status::ConnectionKind::Unknown;
            item.connectivity = available ? s.network.connectivity : status::InternetConnectivity::Unknown;
            item.unknown = !available || item.connection == status::ConnectionKind::Unknown;
            if (available && s.network.signalBars && *s.network.signalBars <= 5) item.level = *s.network.signalBars;
            if ((item.connection == status::ConnectionKind::WiFi || item.connection == status::ConnectionKind::Cellular) && !item.level)
                item.tooltip += L" · Signal strength unavailable";
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        if (config.showVolume) {
            const bool available = s.audio.state == status::ProviderState::Available && s.audio.deviceAvailable;
            auto item = Item(ItemKind::Volume, L"", available ? L"Audio output" : L"Audio: " + StateText(s.audio.state), true);
            item.unknown = !available || !s.audio.muted || !s.audio.volume || !std::isfinite(*s.audio.volume);
            if (available && s.audio.volume && std::isfinite(*s.audio.volume)) {
                item.level = static_cast<int>(std::round(std::clamp(*s.audio.volume, 0.0f, 1.0f) * 100.0f));
                item.tooltip += L" · " + std::to_wstring(*item.level) + L"%";
            } else item.tooltip += L" · volume unknown";
            if (available && s.audio.muted) {
                item.muted = *s.audio.muted;
                item.tooltip += item.muted ? L" · Muted" : L" · Sound on";
            }
            if (available && !s.audio.deviceName.empty()) item.tooltip += L"\n" + s.audio.deviceName;
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        if (config.showMicrophone) {
            const bool available = s.microphone.state == status::ProviderState::Available && s.microphone.deviceAvailable;
            auto item = Item(ItemKind::Microphone, L"", L"Microphone: " + StateText(s.microphone.state), true);
            item.unknown = !available || !s.microphone.muted;
            if (available && s.microphone.muted) {
                item.muted = *s.microphone.muted;
                item.tooltip += item.muted ? L" · Muted" : L" · Endpoint unmuted";
            }
            item.tooltip += L"\nRecording detection unsupported";
            if (available && !s.microphone.deviceName.empty()) item.tooltip += L"\n" + s.microphone.deviceName;
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        const bool knownAbsent = !s.battery.batteryPresent &&
            (s.battery.state == status::ProviderState::Available || s.battery.state == status::ProviderState::Unavailable);
        if (config.showBattery && !(config.hideBatteryWhenAbsent && knownAbsent)) {
            const bool available = s.battery.state == status::ProviderState::Available && s.battery.batteryPresent;
            auto item = Item(ItemKind::Battery, L"—", L"Battery: " + StateText(s.battery.state));
            item.unknown = !available || !s.battery.percent || *s.battery.percent < 0 || *s.battery.percent > 100;
            const bool powerReported = s.battery.state == status::ProviderState::Available ||
                s.battery.state == status::ProviderState::Unavailable;
            item.powerSource = powerReported ? s.battery.source : status::PowerSource::Unknown;
            item.charge = available ? s.battery.charge : status::ChargeState::Unknown;
            if (available && s.battery.percent && *s.battery.percent >= 0 && *s.battery.percent <= 100) {
                item.level = *s.battery.percent;
                item.text = std::to_wstring(*item.level) + L"%";
                item.tooltip = L"Battery " + item.text;
            } else item.tooltip = L"Battery status unavailable";
            if (available && s.battery.charge == status::ChargeState::Charging) item.tooltip += L" · Charging";
            if (item.powerSource == status::PowerSource::External) item.tooltip += L" · External power";
            if (available && s.battery.remaining && s.battery.remaining->count() >= 0) {
                const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(*s.battery.remaining).count();
                item.tooltip += std::format(L"\nEstimated remaining: {} h {} min", minutes / 60, minutes % 60);
            }
            item.accessibleName = item.tooltip;
            result.push_back(std::move(item));
        }
        auto clock = Item(ItemKind::Clock, L"—", L"Clock status unavailable");
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
        clock.accessibleName = clock.tooltip;
        result.push_back(std::move(clock));
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "top bar display model failed"); }
}

std::vector<PlacedItem> LayoutItems(const std::vector<DisplayItem>& items, const std::vector<float>& widths,
                                  float widthDip, float heightDip) {
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
    for (const auto kind : {ItemKind::Weather, ItemKind::Keyboard}) {
        if (total() <= available) break;
        std::erase_if(active, [kind](const auto& item) { return item.first.kind == kind; });
    }
    // At extreme widths, retain the clock before decorative/status glyphs. Never overlap targets.
    for (const auto kind : {ItemKind::Network, ItemKind::Microphone, ItemKind::Volume, ItemKind::Battery}) {
        if (total() <= available) break;
        std::erase_if(active, [kind](const auto& item) { return item.first.kind == kind; });
    }
    const float sum = total();
    const float scale = sum > available && sum > 0 ? available / sum : 1.0f;
    float x = widthDip - padding - sum * scale;
    for (const auto& [item, width] : active) {
        const float actual = width * scale;
        if (actual > 0) result.push_back({item, {x, 0, x + actual, heightDip}});
        x += actual;
    }
    return result;
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
