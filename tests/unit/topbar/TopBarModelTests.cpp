// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "topbar/TopBarModel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

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

AETHER_TEST("topbar actions: the hidden-icons chevron follows its own setting, never the legacy arrow flag") {
    // D45: shown by default whatever the legacy D27 flag says (false in every Alpha 2 file) and
    // whatever the taskbar replacement state is; recovery is not this item's job.
    for (const bool legacy : {false, true}) {
        settings::TopBarSettings config; config.showNotificationAreaButton = legacy;
        for (const auto state : {TaskbarReplacementState{}, TaskbarReplacementState{true, true, false}, TaskbarReplacementState{true, false, true}}) {
            auto items = BuildDisplayItems({}, config, false, {}, state); REQUIRE(items.ok());
            const auto* chevron = Find(*items, ItemKind::NotificationArea);
            REQUIRE(chevron != nullptr);
            CHECK(chevron->actionable);
            CHECK(chevron->accessibleName == L"Show hidden icons");
            CHECK(chevron->text.empty());
            // It leads the status cluster, ahead of the input indicator, like Windows' own chevron.
            const auto position = [&](ItemKind kind) {
                return std::find_if(items->begin(), items->end(), [kind](const auto& item) { return item.kind == kind; }) - items->begin();
            };
            CHECK(position(ItemKind::NotificationArea) < position(ItemKind::Keyboard));
        }
        config.showHiddenIconsButton = false;
        auto items = BuildDisplayItems({}, config, false, {}, {}); REQUIRE(items.ok());
        CHECK(Find(*items, ItemKind::NotificationArea) == nullptr);
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

AETHER_TEST("topbar geometry: written on the desktop the bar is flush with the top and spans the screen (D71)") {
    for (const UINT dpi : {96u, 144u, 192u}) for (const int height : {24, 32, 48}) {
        const RECT area{-1920, -200, 0, 880};
        const auto placement = ComputeTopBarPlacement(area, dpi, height, 0, 0.0f, 0.0f);
        REQUIRE(placement.ok());
        const auto& p = placement.value();
        const int heightPx = height * static_cast<int>(dpi) / 96;
        CHECK_EQ(p.windowBoundsPx.left, area.left);
        CHECK_EQ(p.windowBoundsPx.right, area.right);
        CHECK_EQ(p.windowBoundsPx.top, area.top);
        CHECK_EQ(p.windowBoundsPx.bottom, area.top + heightPx);
        CHECK_EQ(p.reservationHeightPx, heightPx);   // exactly the strip: no margin, no shadow
        CHECK_EQ(p.panelBoundsPx.left, 0); CHECK_EQ(p.panelBoundsPx.top, 0);
        CHECK_EQ(p.panelBoundsPx.right, area.right - area.left); CHECK_EQ(p.panelBoundsPx.bottom, heightPx);
    }
    // The floating bar keeps its 12 DIPs at each end, and a senseless side is refused.
    const RECT area{0, 0, 1920, 1080};
    const auto floating = ComputeTopBarPlacement(area, 96, 32, 6, 4.0f);
    REQUIRE(floating.ok());
    CHECK_EQ(floating->windowBoundsPx.left + floating->panelBoundsPx.left, 12);
    CHECK_FALSE(ComputeTopBarPlacement(area, 96, 32, 0, 0.0f, -1.0f).ok());
    CHECK_FALSE(ComputeTopBarPlacement(area, 96, 32, 0, 0.0f, std::numeric_limits<float>::quiet_NaN()).ok());
}

namespace {
// A picture `width` x `height` whose left half is `left` and right half `right` (grey levels 0..255).
std::vector<std::uint8_t> Halves(int width, int height, std::uint8_t left, std::uint8_t right) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 255);
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        auto* pixel = pixels.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
        pixel[0] = pixel[1] = pixel[2] = x < width / 2 ? left : right;
    }
    return pixels;
}
float ContrastWith(float ink, float backdrop) { return (std::max(ink, backdrop) + 0.05f) / (std::min(ink, backdrop) + 0.05f); }
}

AETHER_TEST("topbar backdrop: the strip under the bar is measured as luminance, column by column (D71)") {
    const auto pixels = Halves(64, 16, 0, 255);
    const auto profile = MeasureBackdrop(pixels, 64, 16, RECT{0, 0, 64, 8}, 8);
    REQUIRE_EQ(profile.columns.size(), std::size_t{8});
    for (std::size_t column = 0; column < 8; ++column) {
        const float expected = column < 4 ? 0.0f : 1.0f;
        CHECK(std::abs(profile.columns[column].mean - expected) < 0.001f);
        CHECK(std::abs(profile.columns[column].low - expected) < 0.001f);
        CHECK(std::abs(profile.columns[column].high - expected) < 0.001f);
    }
    // A column that holds both keeps its darkest and its lightest parts, not only their average.
    const auto wide = MeasureBackdrop(pixels, 64, 16, RECT{0, 0, 64, 8}, 1);
    REQUIRE_EQ(wide.columns.size(), std::size_t{1});
    CHECK(wide.columns[0].low < 0.01f); CHECK(wide.columns[0].high > 0.99f);
    CHECK(std::abs(wide.columns[0].mean - 0.5f) < 0.01f);
    // Mid grey is measured in linear light (sRGB 128 is about 0.216), and a strip beyond the picture is clipped to it.
    const auto grey = Halves(32, 8, 128, 128);
    const auto clipped = MeasureBackdrop(grey, 32, 8, RECT{-50, -50, 500, 500}, 4);
    REQUIRE_EQ(clipped.columns.size(), std::size_t{4});
    CHECK(std::abs(clipped.columns[0].mean - 0.2158f) < 0.002f);
    // Nothing to measure is no profile at all, never an invented one.
    CHECK(MeasureBackdrop(grey, 32, 8, RECT{40, 0, 60, 8}, 4).columns.empty());
    CHECK(MeasureBackdrop(grey, 32, 8, RECT{0, 0, 32, 8}, 0).columns.empty());
    CHECK(MeasureBackdrop(std::span<const std::uint8_t>(grey.data(), 16), 32, 8, RECT{0, 0, 32, 8}, 4).columns.empty());
    CHECK(MeasureBackdrop({}, 0, 0, RECT{0, 0, 1, 1}, 1).columns.empty());
}

AETHER_TEST("topbar ink: each side of the written bar reads at 4.5:1 over its own part of the picture (D71)") {
    // Over any plain backdrop, from black to white, the chosen ink over what the halo leaves of the backdrop keeps
    // 4.5:1 (WCAG): white ink over a backdrop darkened by a dark halo, dark ink over one lightened by a light halo.
    for (int level = 0; level <= 100; ++level) {
        const float luminance = static_cast<float>(level) / 100.0f;
        const BackdropProfile profile{{{luminance, luminance, luminance}}};
        const auto ink = ChooseTextInk(&profile, 0.0f, 1.0f, false);
        CHECK(ink.halo >= 0.20f); CHECK(ink.halo <= 0.85f);
        const float under = ink.dark ? luminance + (1.0f - luminance) * ink.halo : luminance * (1.0f - ink.halo);
        CHECK(ContrastWith(ink.dark ? 0.0f : 1.0f, under) >= 4.5f);
    }
    // Light ink over black and mid tones, as on a photograph; dark ink once the backdrop is clearly light.
    const BackdropProfile black{{{0.0f, 0.0f, 0.0f}}}, dim{{{0.15f, 0.15f, 0.15f}}}, pale{{{0.6f, 0.6f, 0.6f}}}, white{{{1.0f, 1.0f, 1.0f}}};
    CHECK_FALSE(ChooseTextInk(&black, 0.0f, 1.0f, true).dark);
    CHECK_FALSE(ChooseTextInk(&dim, 0.0f, 1.0f, true).dark);
    CHECK(ChooseTextInk(&pale, 0.0f, 1.0f, false).dark);
    CHECK(ChooseTextInk(&white, 0.0f, 1.0f, false).dark);
    // A busy part, dark and light at once: light ink, under a strong halo.
    const BackdropProfile busy{{{0.02f, 0.3f, 0.7f}}};
    const auto over = ChooseTextInk(&busy, 0.0f, 1.0f, false);
    CHECK_FALSE(over.dark); CHECK(over.halo >= 0.7f);
    // Each side reads its own part: a picture dark on the left and light on the right gives two inks.
    const auto pixels = Halves(64, 16, 10, 245);
    const auto split = MeasureBackdrop(pixels, 64, 16, RECT{0, 0, 64, 8}, 16);
    CHECK_FALSE(ChooseTextInk(&split, 0.0f, 0.2f, false).dark);
    CHECK(ChooseTextInk(&split, 0.7f, 1.0f, false).dark);
    CHECK(ChooseTextInk(&split, 1.0f, 0.7f, false).dark);   // either order of the ends
    CHECK_FALSE(ChooseTextInk(&split, -3.0f, 0.1f, false).dark);
    // Nothing known of the backdrop: the theme decides the ink, under a strong halo.
    const BackdropProfile none;
    for (const BackdropProfile* unknown : {static_cast<const BackdropProfile*>(nullptr), &none}) {
        CHECK_FALSE(ChooseTextInk(unknown, 0.0f, 1.0f, false).dark);
        CHECK(ChooseTextInk(unknown, 0.0f, 1.0f, true).dark);
        CHECK(ChooseTextInk(unknown, 0.0f, 1.0f, true).halo >= 0.6f);
    }
    CHECK(ChooseTextInk(&white, std::numeric_limits<float>::quiet_NaN(), 1.0f, false).halo >= 0.6f);
}
