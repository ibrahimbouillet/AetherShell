// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "topbar/TopBarModel.h"

#include <algorithm>
#include <limits>

using namespace aether;
using namespace aether::topbar;

namespace {
const DisplayItem* Find(const std::vector<DisplayItem>& items, ItemKind kind) {
    const auto found = std::find_if(items.begin(), items.end(), [kind](const auto& item) { return item.kind == kind; });
    return found == items.end() ? nullptr : &*found;
}
}

AETHER_TEST("topbar model: initializing and unknown values never become invented zeroes") {
    TopBarSnapshots snapshots;
    settings::TopBarSettings config;
    auto built = BuildDisplayItems(snapshots, config, false, L"en-US");
    REQUIRE(built.ok());
    for (const auto kind : {ItemKind::Battery, ItemKind::Clock, ItemKind::Keyboard}) {
        const auto* item = Find(built.value(), kind);
        REQUIRE(item != nullptr);
        CHECK_EQ(item->text, std::wstring(L"—"));
        CHECK(item->unknown);
        CHECK_FALSE(item->level.has_value());
    }
    CHECK(Find(built.value(), ItemKind::Weather) == nullptr);
    snapshots.audio.state = status::ProviderState::Available;
    snapshots.audio.deviceAvailable = true; snapshots.audio.muted = false;
    snapshots.battery.state = status::ProviderState::Available; snapshots.battery.batteryPresent = true;
    built = BuildDisplayItems(snapshots, config, false);
    REQUIRE(built.ok());
    CHECK(Find(built.value(), ItemKind::Volume)->unknown);
    CHECK_FALSE(Find(built.value(), ItemKind::Battery)->level.has_value());
    CHECK(Find(built.value(), ItemKind::Battery)->tooltip.find(L"unavailable") != std::wstring::npos);
}

AETHER_TEST("topbar model: absent battery hides only when absence is known") {
    TopBarSnapshots snapshots;
    settings::TopBarSettings config;
    snapshots.battery.state = status::ProviderState::Unavailable;
    auto built = BuildDisplayItems(snapshots, config, false);
    REQUIRE(built.ok());
    CHECK(Find(built.value(), ItemKind::Battery) == nullptr);
    config.hideBatteryWhenAbsent = false;
    snapshots.battery.source = status::PowerSource::External;
    built = BuildDisplayItems(snapshots, config, false);
    REQUIRE(built.ok()); REQUIRE(Find(built.value(), ItemKind::Battery) != nullptr);
    CHECK_EQ(Find(built.value(), ItemKind::Battery)->powerSource, status::PowerSource::External);
    config.hideBatteryWhenAbsent = true;
    snapshots.battery.state = status::ProviderState::Error;
    built = BuildDisplayItems(snapshots, config, false);
    REQUIRE(built.ok()); CHECK(Find(built.value(), ItemKind::Battery) != nullptr);
}

AETHER_TEST("topbar model: cached weather is labelled and carries attribution and units") {
    TopBarSnapshots snapshots;
    settings::TopBarSettings config;
    auto& weather = snapshots.weather.snapshot;
    weather.state = status::WeatherState::Cached; weather.temperature = -2.6;
    weather.attribution = L"Weather data by Open-Meteo.com"; weather.locationName = L"Example City";
    weather.conditionText = L"Snow"; snapshots.weather.glyph = aether::weather::WeatherGlyph::Snow;
    auto built = BuildDisplayItems(snapshots, config, true);
    REQUIRE(built.ok());
    const auto* item = Find(built.value(), ItemKind::Weather);
    REQUIRE(item != nullptr); CHECK(item->cached); CHECK_EQ(item->text, std::wstring(L"-3°C"));
    CHECK(item->tooltip.find(L"Cached") != std::wstring::npos);
    CHECK(item->tooltip.find(weather.attribution) != std::wstring::npos);
    weather.state = status::WeatherState::Offline; weather.temperature.reset();
    built = BuildDisplayItems(snapshots, config, true);
    REQUIRE(built.ok()); item = Find(built.value(), ItemKind::Weather);
    REQUIRE(item != nullptr); CHECK(item->unknown); CHECK(item->tooltip.find(L"offline") != std::wstring::npos);
}

AETHER_TEST("topbar model: real readings retain mute, connectivity and battery values") {
    TopBarSnapshots snapshots;
    settings::TopBarSettings config;
    snapshots.audio = {status::ProviderState::Available, true, 0.37f, true, L"Synthetic speaker", {}};
    snapshots.network.state = status::ProviderState::Available;
    snapshots.network.kind = status::ConnectionKind::WiFi;
    snapshots.network.connectivity = status::InternetConnectivity::Constrained;
    snapshots.network.signalBars = std::uint8_t{2};
    snapshots.network.profileName = L"Synthetic identifier that must never be shown";
    snapshots.battery.state = status::ProviderState::Available; snapshots.battery.batteryPresent = true;
    snapshots.battery.percent = 64;
    auto built = BuildDisplayItems(snapshots, config, false);
    REQUIRE(built.ok());
    CHECK(Find(built.value(), ItemKind::Volume)->muted);
    CHECK(Find(built.value(), ItemKind::Volume)->level == std::optional<int>(37));
    CHECK(Find(built.value(), ItemKind::Network)->level == std::optional<int>(2));
    CHECK(Find(built.value(), ItemKind::Network)->tooltip.find(snapshots.network.profileName) == std::wstring::npos);
    CHECK_EQ(Find(built.value(), ItemKind::Battery)->text, std::wstring(L"64%"));
    config.showVolume = false; config.showNetwork = false;
    built = BuildDisplayItems(snapshots, config, false);
    REQUIRE(built.ok()); CHECK(Find(built.value(), ItemKind::Volume) == nullptr);
    CHECK(Find(built.value(), ItemKind::Network) == nullptr);
}

AETHER_TEST("topbar model: localized clock formatting preserves configured seconds and date visibility") {
    TopBarSnapshots snapshots;
    snapshots.clock.state = status::ProviderState::Available;
    snapshots.clock.year = 2026; snapshots.clock.month = 10; snapshots.clock.day = 18;
    snapshots.clock.hour = 17; snapshots.clock.minute = 6; snapshots.clock.second = 9;
    settings::TopBarSettings config;
    config.clockFormat = settings::ClockFormat::Hours24; config.showSeconds = true; config.showDate = false;
    auto built = BuildDisplayItems(snapshots, config, false, L"en-US");
    REQUIRE(built.ok()); CHECK_EQ(Find(built.value(), ItemKind::Clock)->text, std::wstring(L"17:06:09"));
    CHECK_FALSE(Find(built.value(), ItemKind::Clock)->unknown);
}

AETHER_TEST("topbar layout: weather then language collapse and hit targets never overlap") {
    settings::TopBarSettings config;
    auto built = BuildDisplayItems({}, config, true);
    REQUIRE(built.ok());
    const auto& items = built.value();
    std::vector<float> widths(items.size(), 50);
    const auto placed = LayoutItems(items, widths, 400);
    CHECK(std::none_of(placed.begin(), placed.end(), [](const auto& item) { return item.item.kind == ItemKind::Weather; }));
    CHECK(std::any_of(placed.begin(), placed.end(), [](const auto& item) { return item.item.kind == ItemKind::Keyboard; }));
    for (const float width : {1.0f, 30.0f, 75.0f, 200.0f, 800.0f}) {
        const auto layout = LayoutItems(items, widths, width);
        float previous = 0;
        for (const auto& item : layout) {
            CHECK(item.rect.left >= previous); CHECK(item.rect.right <= width);
            CHECK(item.rect.right >= item.rect.left);
            previous = item.rect.right;
            const auto hit = HitTest(layout, {item.rect.left + (item.rect.right - item.rect.left) / 2, 16});
            CHECK(hit == item.item.id);
        }
    }
    CHECK(LayoutItems(items, widths, std::numeric_limits<float>::quiet_NaN()).empty());
    CHECK_FALSE(HitTest(placed, {-1, -1}).has_value());
}

AETHER_TEST("topbar controls: a burst of wheel notches accumulates before provider echoes arrive") {
    std::optional<float> pending;
    for (int index = 0; index < 3; ++index) {
        const auto target = WheelVolumeTarget(0.5f, pending, 1.0f);
        REQUIRE(target.ok()); pending = target.value();
    }
    REQUIRE(pending.has_value()); CHECK_NEAR(*pending, 0.56f, 0.0001f);
    auto lower = WheelVolumeTarget(0.5f, pending, -2.0f);
    REQUIRE(lower.ok()); CHECK_NEAR(lower.value(), 0.52f, 0.0001f);
    auto bounded = WheelVolumeTarget(0.99f, {}, 10.0f);
    REQUIRE(bounded.ok()); CHECK_NEAR(bounded.value(), 1.0f, 0.0001f);
    CHECK_FALSE(WheelVolumeTarget({}, {}, 1).ok());
    CHECK_FALSE(WheelVolumeTarget(std::numeric_limits<float>::quiet_NaN(), {}, 1).ok());
}

AETHER_TEST("topbar network: Wi-Fi waves follow actual wireless bars and preserve unknown strength") {
    DisplayItem item; item.connection = status::ConnectionKind::WiFi;
    CHECK_FALSE(WiFiWaveCount(item).has_value());
    const int expected[] = {0, 1, 2, 2, 3, 3};
    for (int level = 0; level <= 5; ++level) {
        item.level = level;
        CHECK(WiFiWaveCount(item) == std::optional<int>(expected[level]));
    }
    item.level = 6; CHECK_FALSE(WiFiWaveCount(item).has_value());
    item.level = 5;
    for (const auto kind : {status::ConnectionKind::Cellular, status::ConnectionKind::Ethernet,
                           status::ConnectionKind::Unknown, status::ConnectionKind::None}) {
        item.connection = kind; CHECK_FALSE(WiFiWaveCount(item).has_value());
    }
}

AETHER_TEST("topbar actions: keyboard network and weather remain actionable with unknown readings") {
    settings::TopBarSettings config;
    const auto built = BuildDisplayItems({}, config, true);
    REQUIRE(built.ok());
    for (const auto kind : {ItemKind::Keyboard, ItemKind::Network, ItemKind::Weather}) {
        const auto* item = Find(built.value(), kind); REQUIRE(item != nullptr);
        CHECK(item->actionable); CHECK(item->unknown);
        CHECK(item->tooltip.find(L"Click to") != std::wstring::npos);
        CHECK_EQ(item->accessibleName, item->tooltip);
    }
    CHECK(Find(built.value(), ItemKind::Keyboard)->tooltip.find(L"Win+Space") != std::wstring::npos);
    CHECK(Find(built.value(), ItemKind::Microphone)->tooltip.find(L"right-click") != std::wstring::npos);
    CHECK(Find(built.value(), ItemKind::Microphone)->tooltip.find(L"Recording detection unsupported") != std::wstring::npos);
}

AETHER_TEST("topbar preferences: compact keyboard and hidden battery percentage retain honest tooltips") {
    TopBarSnapshots snapshots;
    snapshots.keyboard.state = status::ProviderState::Available;
    snapshots.keyboard.shortLabel = L"FR"; snapshots.keyboard.layoutName = L"Fixture French layout";
    snapshots.battery.state = status::ProviderState::Available;
    snapshots.battery.batteryPresent = true; snapshots.battery.percent = 64;
    settings::TopBarSettings config;
    config.keyboardIndicator = settings::KeyboardIndicatorStyle::TwoLine;
    auto built = BuildDisplayItems(snapshots, config, false);
    REQUIRE(built.ok());
    CHECK_EQ(Find(built.value(), ItemKind::Keyboard)->secondaryText, snapshots.keyboard.layoutName);
    config.keyboardIndicator = settings::KeyboardIndicatorStyle::Compact;
    config.showBatteryPercent = false;
    built = BuildDisplayItems(snapshots, config, false);
    REQUIRE(built.ok());
    CHECK(Find(built.value(), ItemKind::Keyboard)->secondaryText.empty());
    CHECK(Find(built.value(), ItemKind::Battery)->text.empty());
    CHECK(Find(built.value(), ItemKind::Battery)->tooltip.find(L"64%") != std::wstring::npos);
    CHECK(Find(built.value(), ItemKind::Battery)->actionable);
    CHECK(Find(built.value(), ItemKind::Clock)->actionable);
    config.keyboardIndicator = settings::KeyboardIndicatorStyle::TwoLine; config.height = 24;
    REQUIRE(BuildDisplayItems(snapshots, config, false).ok());
    CHECK(Find(BuildDisplayItems(snapshots, config, false).value(), ItemKind::Keyboard)->secondaryText.empty());
}

AETHER_TEST("topbar preferences: weather moves left while status targets remain ordered and separate") {
    const auto items = BuildDisplayItems({}, {}, true);
    REQUIRE(items.ok());
    const std::vector<float> widths(items.value().size(), 50);
    const auto left = LayoutItems(items.value(), widths, 1000, 48, settings::WeatherPlacement::Left);
    const auto right = LayoutItems(items.value(), widths, 1000, 48, settings::WeatherPlacement::Right);
    const auto weatherX = [](const auto& placed) {
        const auto found = std::find_if(placed.begin(), placed.end(), [](const auto& p) { return p.item.kind == ItemKind::Weather; });
        return found == placed.end() ? -1.0f : found->rect.left;
    };
    CHECK(weatherX(left) < weatherX(right));
    float previous = 0;
    for (const auto& item : left) {
        CHECK(item.rect.left >= previous); CHECK_EQ(item.rect.bottom, 48.0f);
        previous = item.rect.right;
    }
}

AETHER_TEST("topbar actions: minimal bar omits native-taskbar arrow even for legacy settings") {
    settings::TopBarSettings config;config.showNotificationAreaButton=true;
    for(const auto state:{TaskbarReplacementState{},TaskbarReplacementState{true,true,false},TaskbarReplacementState{true,false,true}}){
        auto items=BuildDisplayItems({},config,false,{},state);REQUIRE(items.ok());
        CHECK(Find(*items,ItemKind::NotificationArea)==nullptr);
    }
}

AETHER_TEST("topbar grouped controls preserve true readings and one action") {
    TopBarSnapshots state;
    settings::TopBarSettings settings;
    state.network.state=status::ProviderState::Available;
    state.network.kind=status::ConnectionKind::WiFi;state.network.signalBars=std::uint8_t{2};
    state.audio={status::ProviderState::Available,true,0.37f,true,L"Synthetic output",{}};
    state.battery.state=status::ProviderState::Available;state.battery.batteryPresent=true;state.battery.percent=64;
    auto plain=BuildDisplayItems(state,settings,false);REQUIRE(plain.ok());
    auto grouped=GroupQuickControlItems(*plain);REQUIRE(grouped.ok());
    CHECK(Find(*grouped,ItemKind::Network)==nullptr);CHECK(Find(*grouped,ItemKind::Volume)==nullptr);CHECK(Find(*grouped,ItemKind::Battery)==nullptr);
    const auto* group=Find(*grouped,ItemKind::QuickControls);REQUIRE(group!=nullptr);
    CHECK(group->actionable);CHECK_EQ(group->components.size(),std::size_t{3});
    CHECK(Find(group->components,ItemKind::Network)->level==std::optional<int>{2});
    CHECK(Find(group->components,ItemKind::Volume)->muted);
    CHECK_EQ(group->text,std::wstring(L"64%"));
    const auto clock=std::find_if(grouped->begin(),grouped->end(),[](const auto& item){return item.kind==ItemKind::Clock;});
    const auto quick=std::find_if(grouped->begin(),grouped->end(),[](const auto& item){return item.kind==ItemKind::QuickControls;});
    CHECK(quick<clock);
}
AETHER_TEST("topbar grouped controls retain unknowns and configured omissions") {
    TopBarSnapshots state;settings::TopBarSettings settings;
    settings.showNetwork=false;settings.showVolume=false;settings.hideBatteryWhenAbsent=true;
    state.battery.state=status::ProviderState::Unavailable;
    auto plain=BuildDisplayItems(state,settings,false);REQUIRE(plain.ok());
    auto grouped=GroupQuickControlItems(*plain);REQUIRE(grouped.ok());CHECK(Find(*grouped,ItemKind::QuickControls)==nullptr);
    settings.showNetwork=true;
    plain=BuildDisplayItems(state,settings,false);REQUIRE(plain.ok());grouped=GroupQuickControlItems(*plain);REQUIRE(grouped.ok());
    const auto* group=Find(*grouped,ItemKind::QuickControls);REQUIRE(group!=nullptr);CHECK_EQ(group->components.size(),std::size_t{1});
    CHECK(group->components.front().unknown);CHECK_FALSE(group->components.front().level.has_value());
}

AETHER_TEST("topbar battery: real Windows low flag propagates without inventing unknown state") {
    TopBarSnapshots state;settings::TopBarSettings config;
    state.battery.state=status::ProviderState::Available;state.battery.batteryPresent=true;state.battery.percent=18;
    state.battery.lowOrCritical=true;
    auto items=BuildDisplayItems(state,config,false);REQUIRE(items.ok());REQUIRE(Find(*items,ItemKind::Battery));
    CHECK(Find(*items,ItemKind::Battery)->lowOrCritical==true);
    state.battery.lowOrCritical.reset();items=BuildDisplayItems(state,config,false);REQUIRE(items.ok());
    CHECK_FALSE(Find(*items,ItemKind::Battery)->lowOrCritical.has_value());
}
