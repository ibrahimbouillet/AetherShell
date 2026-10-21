// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/SettingsSerialization.h"

#include "core/Strings.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <string_view>
#include <type_traits>

namespace aether::settings {
namespace {

using Warnings = std::vector<std::string>;

void Warn(Warnings& warnings, std::string_view field, std::string_view issue) {
    warnings.push_back(std::string(field) + ": " + std::string(issue));
}

template <class S, class F>
void Fields(S& s, F&& f) {
    f("dock", "iconSize", s.dock.iconSize);
    f("dock", "bottomMargin", s.dock.bottomMargin);
    f("dock", "sideMargin", s.dock.sideMargin);
    f("dock", "iconSpacing", s.dock.iconSpacing);
    f("dock", "opacity", s.dock.opacity);
    f("dock", "magnification", s.dock.magnification);
    f("dock", "showRunningApps", s.dock.showRunningApps);
    f("dock", "reserveSpace", s.dock.reserveSpace);
    f("dock", "pinned", s.dock.pinned);
    f("dock", "reserveAnimationSpace", s.dock.reserveAnimationSpace);
    f("dock", "metricsVersion", s.dock.metricsVersion);
    f("dock", "dismissedPins", s.dock.dismissedPins);
    f("dock", "explorerDefaultApplied", s.dock.explorerDefaultApplied);
    f("dock", "showStartButton", s.dock.showStartButton);
    f("dock", "showTaskViewButton", s.dock.showTaskViewButton);
    f("topBar", "topMargin", s.topBar.topMargin);
    f("topBar", "opacity", s.topBar.opacity);
    f("topBar", "clockFormat", s.topBar.clockFormat);
    f("topBar", "showSeconds", s.topBar.showSeconds);
    f("topBar", "dateStyle", s.topBar.dateStyle);
    f("topBar", "showDate", s.topBar.showDate);
    f("topBar", "showBattery", s.topBar.showBattery);
    f("topBar", "hideBatteryWhenAbsent", s.topBar.hideBatteryWhenAbsent);
    f("topBar", "showKeyboard", s.topBar.showKeyboard);
    f("topBar", "showNetwork", s.topBar.showNetwork);
    f("topBar", "showVolume", s.topBar.showVolume);
    f("topBar", "showMicrophone", s.topBar.showMicrophone);
    f("topBar", "showWeather", s.topBar.showWeather);
    f("topBar", "reserveSpace", s.topBar.reserveSpace);
    f("topBar", "height", s.topBar.height);
    f("topBar", "openWindowsFlyouts", s.topBar.openWindowsFlyouts);
    f("topBar", "weatherPlacement", s.topBar.weatherPlacement);
    f("topBar", "keyboardIndicator", s.topBar.keyboardIndicator);
    f("topBar", "showNotificationAreaButton", s.topBar.showNotificationAreaButton);
    f("topBar", "showBatteryPercent", s.topBar.showBatteryPercent);
    f("weather", "enabled", s.weather.enabled);
    f("weather", "locationName", s.weather.locationName);
    f("weather", "latitude", s.weather.latitude);
    f("weather", "longitude", s.weather.longitude);
    f("weather", "unit", s.weather.unit);
    f("weather", "refreshMinutes", s.weather.refreshMinutes);
    f("weather", "useWindowsLocation", s.weather.useWindowsLocation);
    f("weather", "windowsLocationConsent", s.weather.windowsLocationConsent);
    f("updates", "automaticChecks", s.updates.automaticChecks);
    f("updates", "checkIntervalHours", s.updates.checkIntervalHours);
    f("updates", "notifications", s.updates.notifications);
    f("updates", "channel", s.updates.channel);
    f("updates", "skippedVersion", s.updates.skippedVersion);
    f("general", "startWithWindows", s.general.startWithWindows);
    f("general", "replaceWindowsTaskbar", s.general.replaceWindowsTaskbar);
    f("general", "taskbarPinsImported", s.general.taskbarPinsImported);
    f("general", "motion", s.general.motion);
    f("general", "hotkeysEnabled", s.general.hotkeysEnabled);
    f("general", "searchHotkey", s.general.searchHotkey);
    f("general", "focusDockHotkey", s.general.focusDockHotkey);
    f("general", "logLevel", s.general.logLevel);
    f("general", "syncTaskbarPins", s.general.syncTaskbarPins);
    f("general", "themeMode", s.general.themeMode);
    f("general", "accentSource", s.general.accentSource);
    f("general", "animationSpeed", s.general.animationSpeed);
}

template <class E> struct EnumNames;
template <> struct EnumNames<MotionPreference> {
    static constexpr std::array values{std::pair{MotionPreference::System, "system"},
        std::pair{MotionPreference::Reduced, "reduced"}, std::pair{MotionPreference::Full, "full"}};
};
template <> struct EnumNames<ClockFormat> {
    static constexpr std::array values{std::pair{ClockFormat::System, "system"},
        std::pair{ClockFormat::Hours12, "hours12"}, std::pair{ClockFormat::Hours24, "hours24"}};
};
template <> struct EnumNames<DateStyle> {
    static constexpr std::array values{std::pair{DateStyle::WeekdayDayMonth, "weekday-day-month"},
        std::pair{DateStyle::Short, "short"}, std::pair{DateStyle::Long, "long"}};
};
template <> struct EnumNames<TemperatureUnit> {
    static constexpr std::array values{std::pair{TemperatureUnit::Celsius, "celsius"},
        std::pair{TemperatureUnit::Fahrenheit, "fahrenheit"}};
};
template <> struct EnumNames<UpdateChannel> {
    static constexpr std::array values{std::pair{UpdateChannel::Stable, "stable"},
        std::pair{UpdateChannel::Prerelease, "prerelease"}};
};
template <> struct EnumNames<ThemePreference> {
    static constexpr std::array values{std::pair{ThemePreference::System, "system"},
        std::pair{ThemePreference::Light, "light"}, std::pair{ThemePreference::Dark, "dark"}};
};
template <> struct EnumNames<AccentSource> {
    static constexpr std::array values{std::pair{AccentSource::Windows, "windows"},
        std::pair{AccentSource::Aether, "aether"}};
};
template <> struct EnumNames<WeatherPlacement> {
    static constexpr std::array values{std::pair{WeatherPlacement::Left, "left"},
        std::pair{WeatherPlacement::Right, "right"}};
};
// The first name of each list is the default: Validate() falls back to it for unknown values.
template <> struct EnumNames<KeyboardIndicatorStyle> {
    static constexpr std::array values{std::pair{KeyboardIndicatorStyle::TwoLine, "two-line"},
        std::pair{KeyboardIndicatorStyle::Compact, "compact"}};
};

// Version 1 (0.1) wrote the Dock size defaults 48/10 and no dock.metricsVersion.
constexpr int kLegacyDockIconSize = 48;
constexpr int kLegacyDockBottomMargin = 10;

void UnknownMembers(const json::Value& object, const std::set<std::string>& known, Warnings& warnings) {
    if (const auto* members = object.AsObject()) {
        for (const auto& member : *members) {
            if (!known.contains(member.key)) warnings.emplace_back("ignored an unknown settings member");
        }
    }
}

void Read(const json::Value& value, std::wstring& out, Warnings& warnings, std::string_view name) {
    if (const auto* text = value.AsString()) out = text::Widen(*text);
    else Warn(warnings, name, "expected a string; used default");
}

void Read(const json::Value& value, std::uint32_t& out, Warnings& warnings, std::string_view name) {
    const auto number = value.AsInt64();
    if (number && *number >= 0 && *number <= std::numeric_limits<std::uint32_t>::max()) {
        out = static_cast<std::uint32_t>(*number);
    } else Warn(warnings, name, "expected an unsigned integer; used default");
}

void Read(const json::Value& value, HotkeyChord& out, Warnings& warnings, std::string_view name) {
    if (!value.IsObject()) return Warn(warnings, name, "expected an object; used default");
    UnknownMembers(value, {"modifiers", "virtualKey"}, warnings);
    if (const auto* item = value.Find("modifiers")) Read(*item, out.modifiers, warnings, name);
    if (const auto* item = value.Find("virtualKey")) Read(*item, out.virtualKey, warnings, name);
}

void Read(const json::Value& value, std::vector<std::wstring>& out, Warnings& warnings, std::string_view name) {
    const auto* array = value.AsArray();
    if (!array) return Warn(warnings, name, "expected an array; used default");
    out.clear();
    for (const auto& item : *array) {
        if (const auto* text = item.AsString()) out.push_back(text::Widen(*text));
        else Warn(warnings, name, "ignored a non-string entry");
    }
}

void Read(const json::Value& value, std::vector<PinnedApp>& out, Warnings& warnings, std::string_view name) {
    const auto* array = value.AsArray();
    if (!array) return Warn(warnings, name, "expected an array; used default");
    out.clear();
    for (const auto& item : *array) {
        if (!item.IsObject()) { Warn(warnings, name, "ignored a non-object application"); continue; }
        UnknownMembers(item, {"id", "displayName", "parsingName", "appUserModelId", "executablePath", "shortcutPath", "arguments"}, warnings);
        PinnedApp pin;
        const auto field = [&](const char* key, std::wstring& text) {
            if (const auto* input = item.Find(key)) Read(*input, text, warnings, key);
        };
        field("id", pin.id);
        field("displayName", pin.displayName);
        field("parsingName", pin.parsingName);
        field("appUserModelId", pin.appUserModelId);
        field("executablePath", pin.executablePath);
        field("shortcutPath", pin.shortcutPath);
        field("arguments", pin.arguments);
        out.push_back(std::move(pin));
    }
}

template <class T>
void Read(const json::Value& value, T& out, Warnings& warnings, std::string_view name) {
    if constexpr (std::is_enum_v<T>) {
        if (const auto* text = value.AsString()) {
            for (const auto& [candidate, label] : EnumNames<T>::values) {
                if (*text == label) { out = candidate; return; }
            }
        }
        Warn(warnings, name, "unknown enum value; used default");
    } else if constexpr (std::is_same_v<T, bool>) {
        if (const auto boolean = value.AsBool()) out = *boolean;
        else Warn(warnings, name, "expected a boolean; used default");
    } else if constexpr (std::is_same_v<T, int>) {
        if (const auto number = value.AsInt64()) {
            out = static_cast<int>(std::clamp<std::int64_t>(*number, std::numeric_limits<int>::min(), std::numeric_limits<int>::max()));
        } else if (const auto wideNumber = value.AsDouble(); wideNumber && std::isfinite(*wideNumber) && std::floor(*wideNumber) == *wideNumber) {
            out = *wideNumber < 0 ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
            Warn(warnings, name, "integer exceeded supported range");
        } else Warn(warnings, name, "expected an integer; used default");
    } else if constexpr (std::is_same_v<T, double>) {
        if (const auto number = value.AsDouble()) out = *number;
        else Warn(warnings, name, "expected a number; used default");
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (const auto* text = value.AsString()) out = *text;
        else Warn(warnings, name, "expected a string; used default");
    } else if constexpr (std::is_same_v<T, std::optional<double>>) {
        if (value.IsNull()) out.reset();
        else if (const auto number = value.AsDouble()) out = *number;
        else Warn(warnings, name, "expected a number or null; used default");
    }
}

json::Value Write(const std::wstring& value) { return text::Narrow(value); }
json::Value Write(const HotkeyChord& value) {
    auto object = json::Value::MakeObject();
    object.Set("modifiers", value.modifiers);
    object.Set("virtualKey", value.virtualKey);
    return object;
}
json::Value Write(const std::vector<std::wstring>& values) {
    auto array = json::Value::MakeArray();
    for (const auto& value : values) array.Append(Write(value));
    return array;
}
json::Value Write(const std::vector<PinnedApp>& pins) {
    auto array = json::Value::MakeArray();
    for (const auto& pin : pins) {
        auto item = json::Value::MakeObject();
        item.Set("id", Write(pin.id));
        item.Set("displayName", Write(pin.displayName));
        item.Set("parsingName", Write(pin.parsingName));
        item.Set("appUserModelId", Write(pin.appUserModelId));
        item.Set("executablePath", Write(pin.executablePath));
        item.Set("shortcutPath", Write(pin.shortcutPath));
        item.Set("arguments", Write(pin.arguments));
        array.Append(std::move(item));
    }
    return array;
}
template <class T> json::Value Write(const T& value) {
    if constexpr (std::is_enum_v<T>) {
        for (const auto& [candidate, label] : EnumNames<T>::values) if (value == candidate) return label;
        return EnumNames<T>::values.front().second;
    } else if constexpr (std::is_same_v<T, std::optional<double>>) {
        return value ? json::Value(*value) : json::Value{};
    } else return json::Value(value);
}

template <class T>
void Clamp(T& value, T low, T high, T fallback, Warnings& warnings, const char* name) {
    if constexpr (std::is_floating_point_v<T>) {
        if (!std::isfinite(value)) {
            value = fallback;
            return Warn(warnings, name, "non-finite value; used default");
        }
    }
    const auto corrected = std::clamp(value, low, high);
    if (corrected != value) { value = corrected; Warn(warnings, name, "clamped to supported range"); }
}
void Limit(std::wstring& value, Warnings& warnings, const char* name) {
    if (value.size() <= 1024) return;
    value.resize(1024);
    if (value.back() >= 0xD800 && value.back() <= 0xDBFF) value.pop_back();
    Warn(warnings, name, "limited to 1024 UTF-16 code units");
}
// Moves the untouched version-1 size defaults to the compact defaults once. Customized sizes are kept,
// and the marker then records the current version, so a later explicit 48/10 choice is respected.
void MigrateDockMetrics(DockSettings& dock, Warnings& warnings) {
    if (dock.metricsVersion >= kCurrentDockMetricsVersion) return;
    if (dock.iconSize == kLegacyDockIconSize && dock.bottomMargin == kLegacyDockBottomMargin) {
        const DockSettings defaults;
        dock.iconSize = defaults.iconSize;
        dock.bottomMargin = defaults.bottomMargin;
        warnings.emplace_back("dock: moved the previous default size to the compact default");
    }
    dock.metricsVersion = kCurrentDockMetricsVersion;
}
bool ValidChord(const HotkeyChord& chord) {
    return chord.modifiers != 0 && (chord.modifiers & ~0x0Fu) == 0 && chord.virtualKey >= 0x20 &&
        chord.virtualKey <= 0xFE && chord.virtualKey != 0x5B && chord.virtualKey != 0x5C &&
        !(chord.virtualKey >= 0xA0 && chord.virtualKey <= 0xA5) && chord.virtualKey != 0xE7;
}

}  // namespace

Result<std::vector<std::string>> Validate(Settings& s) {
    try {
        Warnings warnings;
        s.schemaVersion = kCurrentSchemaVersion;
        const Settings defaults;
        Clamp(s.dock.iconSize, 32, 96, defaults.dock.iconSize, warnings, "iconSize");
        Clamp(s.dock.bottomMargin, 0, 200, defaults.dock.bottomMargin, warnings, "bottomMargin");
        Clamp(s.dock.sideMargin, 0, 400, defaults.dock.sideMargin, warnings, "sideMargin");
        Clamp(s.dock.iconSpacing, 0, 32, defaults.dock.iconSpacing, warnings, "iconSpacing");
        Clamp(s.dock.opacity, 0.30, 1.00, defaults.dock.opacity, warnings, "dock.opacity");
        Clamp(s.dock.magnification, 1.00, 2.00, defaults.dock.magnification, warnings, "magnification");
        Clamp(s.dock.metricsVersion, 1, kCurrentDockMetricsVersion, defaults.dock.metricsVersion, warnings, "metricsVersion");
        Clamp(s.topBar.topMargin, 0, 100, defaults.topBar.topMargin, warnings, "topMargin");
        Clamp(s.topBar.opacity, 0.30, 1.00, defaults.topBar.opacity, warnings, "topBar.opacity");
        Clamp(s.topBar.height, 24, 48, defaults.topBar.height, warnings, "topBar.height");
        Clamp(s.weather.refreshMinutes, 15, 180, defaults.weather.refreshMinutes, warnings, "refreshMinutes");
        Clamp(s.updates.checkIntervalHours, 6, 168, defaults.updates.checkIntervalHours, warnings, "checkIntervalHours");
        Clamp(s.general.animationSpeed, 0.5, 1.5, defaults.general.animationSpeed, warnings, "animationSpeed");
        if (s.weather.latitude.has_value() != s.weather.longitude.has_value() ||
            (s.weather.latitude && (!std::isfinite(*s.weather.latitude) || !std::isfinite(*s.weather.longitude)))) {
            s.weather.latitude.reset(); s.weather.longitude.reset();
            Warn(warnings, "weather", "invalid coordinate pair; cleared location coordinates");
        }
        if (s.weather.latitude) {
            Clamp(*s.weather.latitude, -90.0, 90.0, 0.0, warnings, "latitude");
            Clamp(*s.weather.longitude, -180.0, 180.0, 0.0, warnings, "longitude");
        }
        Limit(s.weather.locationName, warnings, "locationName");
        Fields(s, [&](const char*, const char* name, auto& value) {
            using T = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_enum_v<T>) {
                bool found = false;
                for (const auto& [candidate, label] : EnumNames<T>::values) {
                    (void)label;
                    if (candidate == value) found = true;
                }
                if (!found) { value = EnumNames<T>::values.front().first; Warn(warnings, name, "unknown enum; used default"); }
            }
        });
        // Identities compare case-insensitively, matching the Dock model.
        std::set<std::wstring> ids;
        std::vector<PinnedApp> pins;
        for (auto& pin : s.dock.pinned) {
            // Truncating a command line can change its meaning. Reject malformed metadata instead.
            if (pin.arguments.size() > 32767 || pin.arguments.find(L'\0') != std::wstring::npos) {
                Warn(warnings, "pinned", "ignored invalid application arguments");
                continue;
            }
            Limit(pin.id, warnings, "pinned.id"); Limit(pin.displayName, warnings, "pinned.displayName");
            Limit(pin.parsingName, warnings, "pinned.parsingName"); Limit(pin.appUserModelId, warnings, "pinned.appUserModelId");
            Limit(pin.executablePath, warnings, "pinned.executablePath"); Limit(pin.shortcutPath, warnings, "pinned.shortcutPath");
            if (pin.id.empty() || !ids.insert(text::ToLower(pin.id)).second || pins.size() >= 64) {
                Warn(warnings, "pinned", "ignored missing identity, duplicate or excess application");
                continue;
            }
            pins.push_back(std::move(pin));
        }
        s.dock.pinned = std::move(pins);
        std::set<std::wstring> dismissedIds;
        std::vector<std::wstring> dismissed;
        for (auto& id : s.dock.dismissedPins) {
            Limit(id, warnings, "dismissedPins");
            if (id.empty() || !dismissedIds.insert(text::ToLower(id)).second || dismissed.size() >= 128) {
                Warn(warnings, "dismissedPins", "ignored an empty, duplicate or excess application id");
                continue;
            }
            dismissed.push_back(std::move(id));
        }
        s.dock.dismissedPins = std::move(dismissed);
        if (!ValidChord(s.general.searchHotkey)) {
            s.general.searchHotkey = defaults.general.searchHotkey;
            Warn(warnings, "searchHotkey", "invalid chord; used default");
        }
        if (!ValidChord(s.general.focusDockHotkey)) {
            s.general.focusDockHotkey = defaults.general.focusDockHotkey;
            Warn(warnings, "focusDockHotkey", "invalid chord; used default");
        }
        if (s.general.searchHotkey == s.general.focusDockHotkey) {
            s.general.focusDockHotkey = s.general.searchHotkey == defaults.general.focusDockHotkey
                ? defaults.general.searchHotkey : defaults.general.focusDockHotkey;
            Warn(warnings, "focusDockHotkey", "duplicate chord; used an unused default");
        }
        constexpr std::array levels{"trace", "debug", "info", "warning", "error"};
        if (std::find(levels.begin(), levels.end(), s.general.logLevel) == levels.end()) {
            s.general.logLevel = "info";
            Warn(warnings, "logLevel", "unknown level; used info");
        }
        if (s.updates.skippedVersion.size() > 128) {
            s.updates.skippedVersion.resize(128);
            while (!s.updates.skippedVersion.empty() && !text::IsValidUtf8(s.updates.skippedVersion)) s.updates.skippedVersion.pop_back();
            Warn(warnings, "skippedVersion", "limited to 128 bytes");
        }
        return warnings;
    } catch (...) { return MakeError(ErrorCode::Internal, "settings validation failed"); }
}

Result<json::Value> ToJson(const Settings& settings) {
    try {
        Settings validated = settings;
        if (auto checked = Validate(validated); !checked) return checked.error();
        auto root = json::Value::MakeObject();
        root.Set("schemaVersion", kCurrentSchemaVersion);
        for (const char* name : {"dock", "topBar", "weather", "updates", "general"}) root.Set(name, json::Value::MakeObject());
        Fields(validated, [&](const char* group, const char* key, const auto& value) { root.Find(group)->Set(key, Write(value)); });
        return root;
    } catch (...) { return MakeError(ErrorCode::Internal, "settings serialization failed"); }
}

Result<ParsedSettings> FromJson(const json::Value& root) {
    try {
        if (!root.IsObject()) return MakeError(ErrorCode::Parse, "settings must be a JSON object");
        ParsedSettings parsed;
        UnknownMembers(root, {"schemaVersion", "dock", "topBar", "weather", "updates", "general"}, parsed.warnings);
        std::set<std::string> badGroups;
        for (const char* name : {"dock", "topBar", "weather", "updates", "general"}) {
            if (const auto* group = root.Find(name); group && !group->IsObject()) {
                Warn(parsed.warnings, name, "expected an object; used defaults");
                badGroups.insert(name);
            }
        }
        // A missing marker means the file predates it (version 1). There an absent iconSize or
        // bottomMargin meant the version-1 defaults, so those are seeded before the members are read.
        const auto* dock = badGroups.contains("dock") ? nullptr : root.Find("dock");
        if (!dock || !dock->Find("metricsVersion")) {
            parsed.value.dock.iconSize = kLegacyDockIconSize;
            parsed.value.dock.bottomMargin = kLegacyDockBottomMargin;
            parsed.value.dock.metricsVersion = 1;
        }
        Fields(parsed.value, [&](const char* group, const char* key, auto& value) {
            if (badGroups.contains(group)) return;
            if (const auto* object = root.Find(group)) if (const auto* field = object->Find(key)) Read(*field, value, parsed.warnings, key);
        });
        // Existing chosen coordinates remain manual; a legacy opt-out stays disabled. Never
        // manufacture a previous Windows permission grant from an older settings file.
        const auto* weather = badGroups.contains("weather") ? nullptr : root.Find("weather");
        if(weather&&!weather->Find("useWindowsLocation")&&parsed.value.weather.latitude)
            parsed.value.weather.useWindowsLocation=false;
        MigrateDockMetrics(parsed.value.dock, parsed.warnings);
        for (const char* group : {"dock", "topBar", "weather", "updates", "general"}) {
            std::set<std::string> known;
            Fields(parsed.value, [&](const char* name, const char* key, auto&) { if (std::string_view(group) == name) known.insert(key); });
            if (const auto* object = root.Find(group)) UnknownMembers(*object, known, parsed.warnings);
        }
        auto validated = Validate(parsed.value);
        if (!validated) return validated.error();
        parsed.warnings.insert(parsed.warnings.end(), validated->begin(), validated->end());
        return parsed;
    } catch (...) { return MakeError(ErrorCode::Internal, "settings deserialization failed"); }
}

}  // namespace aether::settings
