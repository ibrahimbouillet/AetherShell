// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/SnapshotUtilities.h"

#include "core/Strings.h"

#include <format>

namespace aether::status {

bool SameContent(const ClockSnapshot& a, const ClockSnapshot& b) noexcept {
    return a.state == b.state && a.utc == b.utc && a.year == b.year && a.month == b.month && a.day == b.day &&
           a.dayOfWeek == b.dayOfWeek && a.hour == b.hour && a.minute == b.minute && a.second == b.second &&
           a.timeZoneName == b.timeZoneName && a.daylightSavingActive == b.daylightSavingActive;
}

bool SameContent(const BatterySnapshot& a, const BatterySnapshot& b) noexcept {
    return a.state == b.state && a.batteryPresent == b.batteryPresent && a.percent == b.percent &&
           a.charge == b.charge && a.source == b.source && a.remaining == b.remaining &&
           a.energySaverOn == b.energySaverOn && a.detail == b.detail;
}

bool SameContent(const KeyboardSnapshot& a, const KeyboardSnapshot& b) noexcept {
    return a.state == b.state && a.localeName == b.localeName && a.shortLabel == b.shortLabel &&
           a.languageName == b.languageName && a.layoutName == b.layoutName &&
           a.fromForegroundWindow == b.fromForegroundWindow && a.detail == b.detail;
}

bool SameContent(const NetworkSnapshot& a, const NetworkSnapshot& b) noexcept {
    return a.state == b.state && a.kind == b.kind && a.connectivity == b.connectivity &&
           a.signalBars == b.signalBars && a.profileName == b.profileName && a.detail == b.detail;
}

std::string_view ToString(ProviderState state) noexcept {
    switch (state) {
        case ProviderState::Initializing: return "Initializing";
        case ProviderState::Available: return "Available";
        case ProviderState::Unavailable: return "Unavailable";
        case ProviderState::Unsupported: return "Unsupported";
        case ProviderState::Error: return "Error";
    }
    return "?";
}

std::string_view ToString(ChargeState state) noexcept {
    switch (state) {
        case ChargeState::Unknown: return "Unknown";
        case ChargeState::Charging: return "Charging";
        case ChargeState::Discharging: return "Discharging";
        case ChargeState::Full: return "Full";
        case ChargeState::NotCharging: return "NotCharging";
    }
    return "?";
}

std::string_view ToString(PowerSource source) noexcept {
    switch (source) {
        case PowerSource::Unknown: return "Unknown";
        case PowerSource::External: return "External";
        case PowerSource::Battery: return "Battery";
    }
    return "?";
}

std::string_view ToString(ConnectionKind kind) noexcept {
    switch (kind) {
        case ConnectionKind::Unknown: return "Unknown";
        case ConnectionKind::None: return "None";
        case ConnectionKind::WiFi: return "WiFi";
        case ConnectionKind::Ethernet: return "Ethernet";
        case ConnectionKind::Cellular: return "Cellular";
        case ConnectionKind::Other: return "Other";
    }
    return "?";
}

std::string_view ToString(InternetConnectivity connectivity) noexcept {
    switch (connectivity) {
        case InternetConnectivity::Unknown: return "Unknown";
        case InternetConnectivity::None: return "None";
        case InternetConnectivity::LocalOnly: return "LocalOnly";
        case InternetConnectivity::Constrained: return "Constrained";
        case InternetConnectivity::Internet: return "Internet";
    }
    return "?";
}

namespace {
std::string WithDetail(std::string text, const std::string& detail) {
    if (!detail.empty()) text += std::format(" ({})", text::SanitizeForLog(detail, 200));
    return text;
}
}  // namespace

std::string Describe(const ClockSnapshot& s) {
    return std::format("{} {:04}-{:02}-{:02} {:02}:{:02}:{:02} dow={} zone='{}' dst={}", ToString(s.state), s.year,
                       s.month, s.day, s.hour, s.minute, s.second, s.dayOfWeek, text::Narrow(s.timeZoneName),
                       s.daylightSavingActive);
}

std::string Describe(const BatterySnapshot& s) {
    const std::string percent = s.percent ? std::format("{}%", *s.percent) : std::string("unknown%");
    const std::string remaining =
        s.remaining ? std::format("{} min", s.remaining->count() / 60) : std::string("unknown");
    return WithDetail(std::format("{} present={} {} charge={} source={} remaining={} energySaver={}",
                                  ToString(s.state), s.batteryPresent, percent, ToString(s.charge),
                                  ToString(s.source), remaining, s.energySaverOn),
                      s.detail);
}

std::string Describe(const KeyboardSnapshot& s) {
    return WithDetail(std::format("{} label='{}' locale='{}' language='{}' layout='{}' foreground={}",
                                  ToString(s.state), text::Narrow(s.shortLabel), text::Narrow(s.localeName),
                                  text::Narrow(s.languageName), text::Narrow(s.layoutName), s.fromForegroundWindow),
                      s.detail);
}

std::string Describe(const NetworkSnapshot& s) {
    const std::string bars = s.signalBars ? std::to_string(*s.signalBars) : std::string("n/a");
    return WithDetail(std::format("{} kind={} connectivity={} bars={}", ToString(s.state), ToString(s.kind),
                                  ToString(s.connectivity), bars),
                      s.detail);
}

}  // namespace aether::status
