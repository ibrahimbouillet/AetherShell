// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Additive schema-v1 fields of 0.2: defaults, JSON names, ranges, the one-time Dock size migration
// and case-insensitive identity lists.
#include "AetherTest.h"

#include "core/Json.h"
#include "settings/SettingsSerialization.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>

using namespace aether;
using namespace aether::settings;

namespace {

ParsedSettings ParseText(const char* text) {
    auto document = json::Parse(text);
    REQUIRE(document.ok());
    auto parsed = FromJson(*document);
    REQUIRE(parsed.ok());
    return std::move(*parsed);
}

ParsedSettings RoundTrip(const Settings& settings) {
    auto document = ToJson(settings);
    REQUIRE(document.ok());
    auto reparsed = json::Parse(json::Serialize(*document));
    REQUIRE(reparsed.ok());
    auto parsed = FromJson(*reparsed);
    REQUIRE(parsed.ok());
    return std::move(*parsed);
}

std::string Member(const Settings& settings, const char* group, const char* key) {
    auto document = ToJson(settings);
    if (!document) return {};
    const auto* object = document->Find(group);
    const auto* value = object ? object->Find(key) : nullptr;
    const auto* text = value ? value->AsString() : nullptr;
    return text ? *text : std::string();
}

}  // namespace

AETHER_TEST("settings schema: new fields have the documented defaults") {
    const Settings defaults;
    CHECK_EQ(defaults.dock.iconSize, 40);
    CHECK_EQ(defaults.dock.bottomMargin, 4);
    CHECK_FALSE(defaults.dock.reserveAnimationSpace);
    CHECK_EQ(defaults.dock.metricsVersion, kCurrentDockMetricsVersion);
    CHECK_EQ(kCurrentDockMetricsVersion, 2);
    CHECK(defaults.dock.dismissedPins.empty());
    CHECK_FALSE(defaults.dock.explorerDefaultApplied);
    CHECK(defaults.dock.showStartButton);
    CHECK(defaults.dock.showTaskViewButton);
    CHECK_EQ(defaults.topBar.height, 32);
    CHECK(defaults.topBar.openWindowsFlyouts);
    CHECK_EQ(defaults.topBar.weatherPlacement, WeatherPlacement::Left);
    CHECK_EQ(defaults.topBar.keyboardIndicator, KeyboardIndicatorStyle::TwoLine);
    CHECK_FALSE(defaults.topBar.showNotificationAreaButton);  // D27 removed the trailing native-taskbar arrow
    CHECK(defaults.topBar.showBatteryPercent);
    CHECK(defaults.general.syncTaskbarPins);
    CHECK_EQ(defaults.general.themeMode, ThemePreference::System);
    CHECK_EQ(defaults.general.accentSource, AccentSource::Windows);
    CHECK_EQ(defaults.general.animationSpeed, 1.0);
    // D23: on by default, flipped together with the lifecycle test guards (nativeTaskbarAccess).
    CHECK(defaults.general.replaceWindowsTaskbar);
    Settings validated;
    auto warnings = Validate(validated);
    REQUIRE(warnings.ok());
    CHECK(warnings->empty());
}

AETHER_TEST("settings schema: every new field survives a JSON text round trip") {
    Settings settings;
    settings.dock.reserveAnimationSpace = true;
    settings.dock.dismissedPins = {L"exe:synthetic-dismissed", L"aumid:Synthetic.App!Main"};
    settings.dock.explorerDefaultApplied = true;
    settings.dock.showStartButton = false;
    settings.dock.showTaskViewButton = false;
    settings.topBar.height = 44;
    settings.topBar.openWindowsFlyouts = false;
    settings.topBar.weatherPlacement = WeatherPlacement::Right;
    settings.topBar.keyboardIndicator = KeyboardIndicatorStyle::Compact;
    settings.topBar.showNotificationAreaButton = true;
    settings.topBar.showBatteryPercent = false;
    settings.general.syncTaskbarPins = false;
    settings.general.themeMode = ThemePreference::Dark;
    settings.general.accentSource = AccentSource::Aether;
    settings.general.animationSpeed = 1.25;
    const auto parsed = RoundTrip(settings);
    CHECK(parsed.warnings.empty());
    const auto& value = parsed.value;
    CHECK(value.dock.reserveAnimationSpace);
    CHECK_EQ(value.dock.metricsVersion, 2);
    REQUIRE_EQ(value.dock.dismissedPins.size(), 2u);
    CHECK_EQ(value.dock.dismissedPins[0], std::wstring(L"exe:synthetic-dismissed"));
    CHECK_EQ(value.dock.dismissedPins[1], std::wstring(L"aumid:Synthetic.App!Main"));
    CHECK(value.dock.explorerDefaultApplied);
    CHECK_FALSE(value.dock.showStartButton);
    CHECK_FALSE(value.dock.showTaskViewButton);
    CHECK_EQ(value.topBar.height, 44);
    CHECK_FALSE(value.topBar.openWindowsFlyouts);
    CHECK_EQ(value.topBar.weatherPlacement, WeatherPlacement::Right);
    CHECK_EQ(value.topBar.keyboardIndicator, KeyboardIndicatorStyle::Compact);
    CHECK(value.topBar.showNotificationAreaButton);
    CHECK_FALSE(value.topBar.showBatteryPercent);
    CHECK_FALSE(value.general.syncTaskbarPins);
    CHECK_EQ(value.general.themeMode, ThemePreference::Dark);
    CHECK_EQ(value.general.accentSource, AccentSource::Aether);
    CHECK_EQ(value.general.animationSpeed, 1.25);
    CHECK_EQ(*ToJson(value), *ToJson(settings));
}

AETHER_TEST("settings schema: new enums use stable lower-case JSON names") {
    Settings settings;
    CHECK_EQ(Member(settings, "general", "themeMode"), std::string("system"));
    CHECK_EQ(Member(settings, "general", "accentSource"), std::string("windows"));
    CHECK_EQ(Member(settings, "topBar", "weatherPlacement"), std::string("left"));
    CHECK_EQ(Member(settings, "topBar", "keyboardIndicator"), std::string("two-line"));
    settings.general.themeMode = ThemePreference::Light;
    settings.general.accentSource = AccentSource::Aether;
    settings.topBar.weatherPlacement = WeatherPlacement::Right;
    settings.topBar.keyboardIndicator = KeyboardIndicatorStyle::Compact;
    CHECK_EQ(Member(settings, "general", "themeMode"), std::string("light"));
    CHECK_EQ(Member(settings, "general", "accentSource"), std::string("aether"));
    CHECK_EQ(Member(settings, "topBar", "weatherPlacement"), std::string("right"));
    CHECK_EQ(Member(settings, "topBar", "keyboardIndicator"), std::string("compact"));
    settings.general.themeMode = ThemePreference::Dark;
    CHECK_EQ(Member(settings, "general", "themeMode"), std::string("dark"));

    const auto parsed = ParseText(R"({"dock":{"metricsVersion":2},"general":{"themeMode":"dark","accentSource":"aether"},"topBar":{"weatherPlacement":"right","keyboardIndicator":"compact"}})");
    CHECK(parsed.warnings.empty());
    CHECK_EQ(parsed.value.general.themeMode, ThemePreference::Dark);
    CHECK_EQ(parsed.value.general.accentSource, AccentSource::Aether);
    CHECK_EQ(parsed.value.topBar.weatherPlacement, WeatherPlacement::Right);
    CHECK_EQ(parsed.value.topBar.keyboardIndicator, KeyboardIndicatorStyle::Compact);
}

AETHER_TEST("settings schema: new boolean and marker fields use their documented JSON names") {
    // ToJson and FromJson share one name table, so a round trip cannot catch a misspelled key.
    const auto parsed = ParseText(R"({"dock":{"metricsVersion":2,"reserveAnimationSpace":true,"explorerDefaultApplied":true,"showStartButton":false,"showTaskViewButton":false},"topBar":{"openWindowsFlyouts":false,"showNotificationAreaButton":true,"showBatteryPercent":false},"general":{"syncTaskbarPins":false}})");
    CHECK(parsed.warnings.empty());
    CHECK_EQ(parsed.value.dock.metricsVersion, 2);
    CHECK(parsed.value.dock.reserveAnimationSpace);
    CHECK(parsed.value.dock.explorerDefaultApplied);
    CHECK_FALSE(parsed.value.dock.showStartButton);
    CHECK_FALSE(parsed.value.dock.showTaskViewButton);
    CHECK_FALSE(parsed.value.topBar.openWindowsFlyouts);
    CHECK(parsed.value.topBar.showNotificationAreaButton);
    CHECK_FALSE(parsed.value.topBar.showBatteryPercent);
    CHECK_FALSE(parsed.value.general.syncTaskbarPins);

    auto document = ToJson(Settings{});
    REQUIRE(document.ok());
    const std::pair<const char*, const char*> names[] = {
        {"dock", "reserveAnimationSpace"}, {"dock", "metricsVersion"}, {"dock", "dismissedPins"},
        {"dock", "explorerDefaultApplied"}, {"dock", "showStartButton"}, {"dock", "showTaskViewButton"},
        {"topBar", "height"}, {"topBar", "openWindowsFlyouts"}, {"topBar", "weatherPlacement"},
        {"topBar", "keyboardIndicator"}, {"topBar", "showNotificationAreaButton"}, {"topBar", "showBatteryPercent"},
        {"general", "syncTaskbarPins"}, {"general", "themeMode"}, {"general", "accentSource"},
        {"general", "animationSpeed"}};
    for (const auto& [group, key] : names) {
        const auto* object = document->Find(group);
        REQUIRE(object != nullptr);
        CHECK(object->Find(key) != nullptr);
    }
}

AETHER_TEST("settings schema: unknown enum names and wrong types fall back to defaults with warnings") {
    const auto parsed = ParseText(R"({"dock":{"metricsVersion":2,"showStartButton":"yes","dismissedPins":{}},"general":{"themeMode":"Dark","accentSource":"synthetic-accent","syncTaskbarPins":1,"animationSpeed":"fast"},"topBar":{"weatherPlacement":"center","keyboardIndicator":3,"height":"tall"}})");
    CHECK(parsed.warnings.size() >= 9u);
    CHECK_EQ(parsed.value.general.themeMode, ThemePreference::System);
    CHECK_EQ(parsed.value.general.accentSource, AccentSource::Windows);
    CHECK(parsed.value.general.syncTaskbarPins);
    CHECK_EQ(parsed.value.general.animationSpeed, 1.0);
    CHECK_EQ(parsed.value.topBar.weatherPlacement, WeatherPlacement::Left);
    CHECK_EQ(parsed.value.topBar.keyboardIndicator, KeyboardIndicatorStyle::TwoLine);
    CHECK_EQ(parsed.value.topBar.height, 32);
    CHECK(parsed.value.dock.showStartButton);
    CHECK(parsed.value.dock.dismissedPins.empty());
    for (const auto& warning : parsed.warnings) CHECK(warning.find("synthetic") == std::string::npos);

    Settings invalid;
    invalid.general.themeMode = static_cast<ThemePreference>(42);
    invalid.general.accentSource = static_cast<AccentSource>(-1);
    invalid.topBar.weatherPlacement = static_cast<WeatherPlacement>(7);
    invalid.topBar.keyboardIndicator = static_cast<KeyboardIndicatorStyle>(9);
    auto warnings = Validate(invalid);
    REQUIRE(warnings.ok());
    CHECK_EQ(warnings->size(), 4u);
    CHECK_EQ(invalid.general.themeMode, ThemePreference::System);
    CHECK_EQ(invalid.general.accentSource, AccentSource::Windows);
    CHECK_EQ(invalid.topBar.weatherPlacement, WeatherPlacement::Left);
    CHECK_EQ(invalid.topBar.keyboardIndicator, KeyboardIndicatorStyle::TwoLine);
}

AETHER_TEST("settings schema: new numeric fields clamp to their ranges and reject non-finite speeds") {
    Settings settings;
    settings.topBar.height = 23; settings.general.animationSpeed = 0.1; settings.dock.metricsVersion = 0;
    auto low = Validate(settings);
    REQUIRE(low.ok()); CHECK_EQ(low->size(), 3u);
    CHECK_EQ(settings.topBar.height, 24); CHECK_EQ(settings.general.animationSpeed, 0.5);
    CHECK_EQ(settings.dock.metricsVersion, 1);
    settings.topBar.height = 49; settings.general.animationSpeed = 9; settings.dock.metricsVersion = 7;
    auto high = Validate(settings);
    REQUIRE(high.ok()); CHECK_EQ(high->size(), 3u);
    CHECK_EQ(settings.topBar.height, 48); CHECK_EQ(settings.general.animationSpeed, 1.5);
    CHECK_EQ(settings.dock.metricsVersion, 2);
    settings.topBar.height = 24; settings.general.animationSpeed = 0.5;
    auto edge = Validate(settings);
    REQUIRE(edge.ok()); CHECK(edge->empty());
    settings.topBar.height = 48; settings.general.animationSpeed = 1.5;
    edge = Validate(settings);
    REQUIRE(edge.ok()); CHECK(edge->empty());
    for (const double value : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                               -std::numeric_limits<double>::infinity()}) {
        settings.general.animationSpeed = value;
        auto corrected = Validate(settings);
        REQUIRE(corrected.ok()); CHECK_EQ(corrected->size(), 1u);
        CHECK_EQ(settings.general.animationSpeed, 1.0);
    }
    const auto parsed = ParseText(R"({"dock":{"metricsVersion":2},"topBar":{"height":1000},"general":{"animationSpeed":-3}})");
    CHECK_EQ(parsed.value.topBar.height, 48);
    CHECK_EQ(parsed.value.general.animationSpeed, 0.5);
    CHECK_EQ(parsed.warnings.size(), 2u);
}

AETHER_TEST("settings schema: clamp fallbacks for non-finite values come from the defaults") {
    Settings settings;
    settings.dock.opacity = std::numeric_limits<double>::quiet_NaN();
    settings.dock.magnification = std::numeric_limits<double>::infinity();
    settings.topBar.opacity = -std::numeric_limits<double>::infinity();
    auto warnings = Validate(settings);
    REQUIRE(warnings.ok()); CHECK_EQ(warnings->size(), 3u);
    const Settings defaults;
    CHECK_EQ(settings.dock.opacity, defaults.dock.opacity);
    CHECK_EQ(settings.dock.magnification, defaults.dock.magnification);
    CHECK_EQ(settings.topBar.opacity, defaults.topBar.opacity);
}

AETHER_TEST("settings migration: an absent metrics marker moves the untouched 48/10 size to 40/4 once") {
    const auto parsed = ParseText(R"({"schemaVersion":1,"dock":{"iconSize":48,"bottomMargin":10,"sideMargin":16}})");
    CHECK_EQ(parsed.value.dock.iconSize, 40);
    CHECK_EQ(parsed.value.dock.bottomMargin, 4);
    CHECK_EQ(parsed.value.dock.metricsVersion, 2);
    REQUIRE_EQ(parsed.warnings.size(), 1u);
    CHECK(parsed.warnings[0].find("dock") != std::string::npos);
    // The migrated settings persist the marker, so loading them again changes nothing.
    auto document = ToJson(parsed.value);
    REQUIRE(document.ok());
    CHECK_EQ(document->Find("dock")->GetInt64("metricsVersion", 0), 2);
    const auto again = RoundTrip(parsed.value);
    CHECK(again.warnings.empty());
    CHECK_EQ(again.value.dock.iconSize, 40);
    CHECK_EQ(again.value.dock.bottomMargin, 4);
}

AETHER_TEST("settings migration: absent version-1 size members keep their 48/10 meaning") {
    // In version 1 an absent iconSize or bottomMargin meant 48 or 10, so each of these files held the
    // untouched size and moves to the compact default exactly once.
    for (const char* text : {
             R"({})",
             R"({"schemaVersion":1,"general":{}})",
             R"({"dock":[]})",
             R"({"dock":{"sideMargin":16}})",
             R"({"dock":{"iconSize":48}})",
             R"({"dock":{"bottomMargin":10}})"}) {
        const auto parsed = ParseText(text);
        CHECK_EQ(parsed.value.dock.iconSize, 40);
        CHECK_EQ(parsed.value.dock.bottomMargin, 4);
        CHECK_EQ(parsed.value.dock.metricsVersion, 2);
        const auto moved = std::count_if(parsed.warnings.begin(), parsed.warnings.end(),
            [](const std::string& warning) { return warning.find("compact default") != std::string::npos; });
        CHECK_EQ(moved, std::ptrdiff_t{1});
    }
}

AETHER_TEST("settings migration: customized sizes and current markers keep their values") {
    struct Case { const char* text; int iconSize; int bottomMargin; };
    for (const Case& item : {
             Case{R"({"dock":{"iconSize":48,"bottomMargin":12}})", 48, 12},
             Case{R"({"dock":{"iconSize":64,"bottomMargin":10}})", 64, 10},
             Case{R"({"dock":{"iconSize":40,"bottomMargin":10}})", 40, 10},
             Case{R"({"dock":{"iconSize":64}})", 64, 10},
             Case{R"({"dock":{"bottomMargin":12}})", 48, 12},
             Case{R"({"dock":{"metricsVersion":2,"iconSize":48}})", 48, 4},
             Case{R"({"dock":{"metricsVersion":2,"bottomMargin":10}})", 40, 10}}) {
        const auto parsed = ParseText(item.text);
        CHECK(parsed.warnings.empty());
        CHECK_EQ(parsed.value.dock.iconSize, item.iconSize);
        CHECK_EQ(parsed.value.dock.bottomMargin, item.bottomMargin);
        CHECK_EQ(parsed.value.dock.metricsVersion, 2);
    }
    // Once the marker is written, an explicit 48/10 choice is the user's and stays.
    const auto chosen = ParseText(R"({"dock":{"iconSize":48,"bottomMargin":10,"metricsVersion":2}})");
    CHECK(chosen.warnings.empty());
    CHECK_EQ(chosen.value.dock.iconSize, 48);
    CHECK_EQ(chosen.value.dock.bottomMargin, 10);
    Settings large;
    large.dock.iconSize = 48; large.dock.bottomMargin = 10;
    const auto kept = RoundTrip(large);
    CHECK(kept.warnings.empty());
    CHECK_EQ(kept.value.dock.iconSize, 48);
    CHECK_EQ(kept.value.dock.bottomMargin, 10);
}

AETHER_TEST("settings migration: an explicit version-1 marker is treated like an absent one") {
    const auto parsed = ParseText(R"({"dock":{"iconSize":48,"bottomMargin":10,"metricsVersion":1}})");
    CHECK_EQ(parsed.value.dock.iconSize, 40);
    CHECK_EQ(parsed.value.dock.bottomMargin, 4);
    CHECK_EQ(parsed.value.dock.metricsVersion, 2);
    const auto future = ParseText(R"({"dock":{"iconSize":48,"bottomMargin":10,"metricsVersion":5}})");
    CHECK_EQ(future.value.dock.iconSize, 48);
    CHECK_EQ(future.value.dock.bottomMargin, 10);
    CHECK_EQ(future.value.dock.metricsVersion, 2);
    CHECK_EQ(future.warnings.size(), 1u);
}

AETHER_TEST("settings validation: pinned identities deduplicate ignoring case") {
    Settings settings;
    PinnedApp first; first.id = L"exe:Synthetic-App"; first.displayName = L"First";
    PinnedApp variant; variant.id = L"EXE:synthetic-app"; variant.displayName = L"Variant";
    PinnedApp other; other.id = L"aumid:Synthetic.Other!App";
    settings.dock.pinned = {first, variant, other};
    auto warnings = Validate(settings);
    REQUIRE(warnings.ok()); CHECK_EQ(warnings->size(), 1u);
    REQUIRE_EQ(settings.dock.pinned.size(), 2u);
    CHECK_EQ(settings.dock.pinned[0].id, first.id);
    CHECK_EQ(settings.dock.pinned[0].displayName, std::wstring(L"First"));
    CHECK_EQ(settings.dock.pinned[1].id, other.id);
}

AETHER_TEST("settings validation: dismissed pins drop empty duplicate and excess ids and bound their length") {
    Settings settings;
    settings.dock.dismissedPins.push_back(L"");
    settings.dock.dismissedPins.push_back(L"exe:Synthetic-A");
    settings.dock.dismissedPins.push_back(L"EXE:SYNTHETIC-A");
    settings.dock.dismissedPins.push_back(L"exe:" + std::wstring(2000, L'x'));
    for (int i = 0; i < 140; ++i) settings.dock.dismissedPins.push_back(L"exe:synthetic-" + std::to_wstring(i));
    auto warnings = Validate(settings);
    REQUIRE(warnings.ok()); CHECK_FALSE(warnings->empty());
    REQUIRE_EQ(settings.dock.dismissedPins.size(), 128u);
    CHECK_EQ(settings.dock.dismissedPins[0], std::wstring(L"exe:Synthetic-A"));
    CHECK_EQ(settings.dock.dismissedPins[1].size(), 1024u);
    CHECK_EQ(settings.dock.dismissedPins[2], std::wstring(L"exe:synthetic-0"));
    CHECK_EQ(settings.dock.dismissedPins.back(), std::wstring(L"exe:synthetic-125"));
    const auto parsed = RoundTrip(settings);
    CHECK(parsed.warnings.empty());
    CHECK_EQ(parsed.value.dock.dismissedPins, settings.dock.dismissedPins);

    const auto mixed = ParseText(R"({"dock":{"metricsVersion":2,"dismissedPins":["exe:synthetic-kept",7,null,"EXE:Synthetic-Kept"]}})");
    REQUIRE_EQ(mixed.value.dock.dismissedPins.size(), 1u);
    CHECK_EQ(mixed.value.dock.dismissedPins[0], std::wstring(L"exe:synthetic-kept"));
    CHECK_EQ(mixed.warnings.size(), 3u);
    for (const auto& warning : mixed.warnings) CHECK(warning.find("synthetic") == std::string::npos);
}
