// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "system_controls/SystemControls.h"

#include <windows.h>

#include <array>
#include <chrono>

namespace {
using namespace aether;
using namespace aether::controlcenter;
using namespace std::chrono_literals;

template<class T>
void ExpectError(const Result<T>& result, ErrorCode code) {
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, code);
}
} // namespace

AETHER_TEST("system_controls fixed settings routes are actions without shortcuts") {
    struct Route { SystemControl control; const wchar_t* uri; };
    constexpr std::array routes{
        Route{SystemControl::WiFi, L"ms-settings:network-wifi"},
        Route{SystemControl::Bluetooth, L"ms-settings:bluetooth"},
        Route{SystemControl::FlightMode, L"ms-settings:network-airplanemode"},
        Route{SystemControl::EnergySaver, L"ms-settings:batterysaver"},
        Route{SystemControl::MobileHotspot, L"ms-settings:network-mobilehotspot"},
        Route{SystemControl::NearbySharing, L"ms-settings:crossdevice"},
        Route{SystemControl::NightLight, L"ms-settings:nightlight"},
        Route{SystemControl::Settings, L"ms-settings:"},
    };
    for (const auto& route : routes) {
        const auto action = ActionFor(route.control);
        REQUIRE(action.ok());
        CHECK_EQ(action->kind, SystemActionKind::Settings);
        CHECK_EQ(action->settingsUri, route.uri);
        CHECK_EQ(action->shortcut, SystemShortcut::None);
        CHECK_EQ(action->minimumWindowsBuild, 0u);
    }
}

AETHER_TEST("system_controls shortcut routes remain explicit bounded commands") {
    struct Route { SystemControl control; SystemShortcut shortcut; unsigned minimumBuild; };
    constexpr std::array routes{
        Route{SystemControl::Accessibility, SystemShortcut::Accessibility, 0},
        Route{SystemControl::LiveCaptions, SystemShortcut::LiveCaptions, 22621},
        Route{SystemControl::Cast, SystemShortcut::Cast, 0},
        Route{SystemControl::Project, SystemShortcut::Project, 0},
    };
    for (const auto& route : routes) {
        const auto action = ActionFor(route.control);
        REQUIRE(action.ok());
        CHECK_EQ(action->kind, SystemActionKind::KeyboardShortcut);
        CHECK(action->settingsUri.empty());
        CHECK_EQ(action->shortcut, route.shortcut);
        CHECK_EQ(action->minimumWindowsBuild, route.minimumBuild);
    }
    ExpectError(ActionFor(static_cast<SystemControl>(-1)), ErrorCode::InvalidArgument);
    ExpectError(TileKindFor(static_cast<SystemControl>(999), {}), ErrorCode::InvalidArgument);
}

AETHER_TEST("system_controls live captions requires a known supported Windows build") {
    SystemControlsSnapshot snapshot;
    for (const unsigned build : {0u, 19041u, 22000u, 22620u}) {
        snapshot.windowsBuild = build;
        const auto tile = TileKindFor(SystemControl::LiveCaptions, snapshot);
        REQUIRE(tile.ok());
        CHECK_EQ(*tile, TileKind::Unsupported);
    }
    for (const unsigned build : {22621u, 26100u, 30000u}) {
        snapshot.windowsBuild = build;
        const auto tile = TileKindFor(SystemControl::LiveCaptions, snapshot);
        REQUIRE(tile.ok());
        CHECK_EQ(*tile, TileKind::Action);
    }
}

AETHER_TEST("system_controls unavailable and unknown radios expose settings actions") {
    for (const auto control : {SystemControl::WiFi, SystemControl::Bluetooth}) {
        SystemControlsSnapshot snapshot;
        auto& radio = control == SystemControl::WiFi ? snapshot.wifi : snapshot.bluetooth;
        const auto checkAction = [&] {
            const auto tile = TileKindFor(control, snapshot);
            REQUIRE(tile.ok());
            CHECK_EQ(*tile, TileKind::Action);
            CHECK_FALSE(radio.enabled.has_value());
        };
        checkAction(); // Empty enumeration is unknown, not a fabricated off state.
        radio.present = true;
        checkAction(); // Mixed or unreadable states also stay unknown.
        radio.hardwareDisabled = true;
        radio.error = MakeError(ErrorCode::Unavailable, "Synthetic radio unavailable");
        checkAction();
    }
}

AETHER_TEST("system_controls known radio capability does not imply granted control") {
    for (const auto control : {SystemControl::WiFi, SystemControl::Bluetooth}) {
        SystemControlsSnapshot snapshot;
        auto& radio = control == SystemControl::WiFi ? snapshot.wifi : snapshot.bluetooth;
        radio.present = true;
        radio.enabled = false;
        radio.canRequestAccess = true;
        const auto tile = TileKindFor(control, snapshot);
        REQUIRE(tile.ok());
        CHECK_EQ(*tile, TileKind::Toggle);
        CHECK_FALSE(radio.canToggle); // The host must request permission after an explicit click.
        REQUIRE(radio.enabled.has_value());
        CHECK_FALSE(*radio.enabled);
        radio.enabled = true;
        radio.canToggle = true;
        CHECK_EQ(*TileKindFor(control, snapshot), TileKind::Toggle);
        CHECK(*radio.enabled);
    }
}

AETHER_TEST("system_controls global settings never invent toggle state from radios or power") {
    SystemControlsSnapshot snapshot;
    snapshot.wifi.present = snapshot.bluetooth.present = true;
    snapshot.wifi.enabled = snapshot.bluetooth.enabled = false;
    snapshot.energySaver = true;
    for (const auto control : {SystemControl::FlightMode, SystemControl::EnergySaver,
             SystemControl::MobileHotspot, SystemControl::NearbySharing, SystemControl::NightLight,
             SystemControl::Accessibility, SystemControl::Cast, SystemControl::Project,
             SystemControl::Settings}) {
        const auto tile = TileKindFor(control, snapshot);
        REQUIRE(tile.ok());
        CHECK_EQ(*tile, TileKind::Action);
    }
}

AETHER_TEST("system_controls empty permission completion fails before requesting consent") {
    ExpectError(BeginRadioAccessRequest({}), ErrorCode::InvalidArgument);
}

AETHER_TEST("system_controls invalid timeouts fail before discovery or radio mutation") {
    // Only admission failures are invoked. No valid query or mutation is submitted in this suite.
    for (const auto timeout : {0ms, -1ms, 30001ms}) {
        ExpectError(QuerySystemControls(false, {}, timeout), ErrorCode::InvalidArgument);
        for (const auto kind : {RadioKind::WiFi, RadioKind::Bluetooth})
            ExpectError(SetRadioEnabled(kind, true, true, {}, timeout), ErrorCode::InvalidArgument);
    }
}

AETHER_TEST("system_controls pre-cancelled operations fail before discovery or radio mutation") {
    CancellationSource source;
    source.Cancel();
    ExpectError(QuerySystemControls(false, source.Token()), ErrorCode::Cancelled);
    for (const auto kind : {RadioKind::WiFi, RadioKind::Bluetooth})
        for (const bool enabled : {false, true})
            ExpectError(SetRadioEnabled(kind, enabled, true, source.Token()), ErrorCode::Cancelled);
}

AETHER_TEST("system_controls radio mutation requires a known kind and explicit permission") {
    ExpectError(SetRadioEnabled(static_cast<RadioKind>(-1), true, true), ErrorCode::InvalidArgument);
    for (const auto kind : {RadioKind::WiFi, RadioKind::Bluetooth})
        for (const bool enabled : {false, true})
            ExpectError(SetRadioEnabled(kind, enabled, false), ErrorCode::AccessDenied);
}

AETHER_TEST("system_controls accessibility features use documented shortcuts and settings") {
    struct Expected { AccessibilityFeature feature; SystemShortcut shortcut; unsigned build; };
    constexpr std::array shortcuts{
        Expected{AccessibilityFeature::Magnifier, SystemShortcut::Magnifier, 0},
        Expected{AccessibilityFeature::Narrator, SystemShortcut::Narrator, 0},
        Expected{AccessibilityFeature::OnScreenKeyboard, SystemShortcut::OnScreenKeyboard, 0},
        Expected{AccessibilityFeature::LiveCaptions, SystemShortcut::LiveCaptions, 22621}};
    for (const auto& expected : shortcuts) {
        const auto action = ActionFor(expected.feature);
        REQUIRE(action.ok());
        CHECK(action->kind == SystemActionKind::KeyboardShortcut);
        CHECK(action->shortcut == expected.shortcut);
        CHECK_EQ(action->minimumWindowsBuild, expected.build);
        CHECK(action->settingsUri.empty());
    }
    const auto settings = ActionFor(AccessibilityFeature::Settings);
    REQUIRE(settings.ok());
    CHECK(settings->kind == SystemActionKind::Settings);
    CHECK(settings->settingsUri == L"ms-settings:easeofaccess");
    ExpectError(ActionFor(static_cast<AccessibilityFeature>(99)), ErrorCode::InvalidArgument);
}

AETHER_TEST("system_controls sticky keys reading needs the feature to be available") {
    CHECK_FALSE(StickyKeysState(0).has_value());                                 // not offered: unknown
    CHECK_FALSE(StickyKeysState(SKF_STICKYKEYSON).has_value());                  // on without availability: unknown
    CHECK(StickyKeysState(SKF_AVAILABLE) == std::optional<bool>(false));
    CHECK(StickyKeysState(SKF_AVAILABLE | SKF_STICKYKEYSON) == std::optional<bool>(true));
    CHECK(StickyKeysState(SKF_AVAILABLE | SKF_HOTKEYACTIVE) == std::optional<bool>(false));
}
