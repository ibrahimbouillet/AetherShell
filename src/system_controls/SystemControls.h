// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>

namespace aether::controlcenter {

enum class RadioKind { WiFi, Bluetooth };
enum class SystemControl {
    WiFi, Bluetooth, FlightMode, EnergySaver, MobileHotspot, NearbySharing,
    NightLight, Accessibility, LiveCaptions, Cast, Project, Settings
};
enum class TileKind { Toggle, Action, Unsupported };
enum class SystemActionKind { Settings, KeyboardShortcut };
// Documented Windows keyboard shortcuts (they start/stop the Windows feature; Windows reports no result).
enum class SystemShortcut { None, Accessibility, LiveCaptions, Cast, Project, Magnifier, Narrator, OnScreenKeyboard };

struct RadioStatus {
    // An empty enumeration is unavailable, not proof that hardware is absent.
    bool present = false;
    std::optional<bool> enabled; // unknown for mixed/unreadable states; never infer from connectivity
    bool hardwareDisabled = false;
    bool canRequestAccess = false;
    bool canToggle = false; // requires a granted session permission plus a controllable known state
    std::optional<Error> error;
};

struct SystemControlsSnapshot {
    RadioStatus wifi, bluetooth;
    std::optional<bool> energySaver; // SYSTEM_POWER_STATUS::SystemStatusFlag, read-only
    std::optional<Error> powerError;
    unsigned windowsBuild = 0; // gates Windows 11-only actions; zero stays unknown/unsupported
    // SPI_GETSTICKYKEYS: unknown when unreadable or when the feature is not available on this system.
    std::optional<bool> stickyKeys;
    // SPI_GETSCREENREADER: some screen reader is running (not necessarily Narrator).
    std::optional<bool> screenReader;
    // No global flight-mode/hotspot/sharing/night-light state is invented from radio state.
};

// Worker only, and only for an explicit user action. Sets the documented Sticky Keys flag
// (SPI_SETSTICKYKEYS, saved to the user profile and broadcast) and succeeds only when a reread
// confirms the requested state. Unavailable when Windows does not offer the feature.
[[nodiscard]] Result<void> SetStickyKeys(bool enabled);
// Pure: the reading of a STICKYKEYS flag set (exposed for tests).
[[nodiscard]] std::optional<bool> StickyKeysState(unsigned long flags) noexcept;

struct SystemAction {
    SystemActionKind kind = SystemActionKind::Settings;
    std::wstring settingsUri;
    SystemShortcut shortcut = SystemShortcut::None;
    unsigned minimumWindowsBuild = 0; // Live captions requires Windows 11 22H2 (22621)
};

// UI-thread STA only, and only in direct response to an explicit user radio-control click.
// Never blocks or asks for consent on a worker. Callback can run on a Windows completion thread;
// callers capture a lifetime guard and post it to their UI dispatcher. false means permission denied.
// Reuse the permission for this app session; do not request it on panel open/refresh or every toggle.
[[nodiscard]] Result<void> BeginRadioAccessRequest(std::function<void(Result<bool>)> completion);

// Worker MTA only. No radio names, addresses, SSIDs, profile names or device identifiers are read.
// Root reuses its existing NetworkSnapshot for connection kind/connectivity/signal bars.
[[nodiscard]] Result<SystemControlsSnapshot> QuerySystemControls(bool grantedAccess = false,
    CancellationToken cancellation = {}, std::chrono::milliseconds timeout = std::chrono::seconds(3));

// Explicit desired state, never an optimistic toggle. Permission must come from the UI request.
// On/Off apply to every discovered radio of this kind. Hardware-disabled/ambiguous collections fail
// before mutation; OS/policy errors remain errors. Success requires rereading the effective state.
// A later device/policy failure can leave a partial change; refresh after every result, including error.
[[nodiscard]] Result<void> SetRadioEnabled(RadioKind kind, bool enabled, bool grantedAccess,
    CancellationToken cancellation = {}, std::chrono::milliseconds timeout = std::chrono::seconds(3));

// Pure fixed routing/model helpers: these never launch Settings, inject keys or change system state.
// Action tiles must be presented as links/actions, never as fake checked/unchecked toggles.
[[nodiscard]] Result<SystemAction> ActionFor(SystemControl control);
// The accessibility features offered in the quick controls' Accessibility view (Sticky Keys is a
// real setting, see SetStickyKeys; these are Windows' own documented start/stop shortcuts).
enum class AccessibilityFeature { Magnifier, Narrator, OnScreenKeyboard, LiveCaptions, Settings };
[[nodiscard]] Result<SystemAction> ActionFor(AccessibilityFeature feature);
[[nodiscard]] Result<TileKind> TileKindFor(SystemControl control, const SystemControlsSnapshot& snapshot);

} // namespace aether::controlcenter
