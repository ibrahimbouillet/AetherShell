// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "media_controls/MediaBrightness.h"
#include "system_controls/SystemControls.h"
#include "system_controls/WiFiNetworks.h"
#include "core/Dispatcher.h"
#include "core/Signal.h"
#include "graphics/Theme.h"
#include "status/StatusTypes.h"
#include "ui/SurfaceWindow.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace aether::controlcenter {

// What the Wi-Fi view lists (D56): real networks only, supplied by the host.
struct WiFiPanelState {
    WiFiAccess access = WiFiAccess::Allowed;
    std::vector<WiFiNetwork> networks;
    bool scanning = false;
    std::wstring connecting;  // the name of the network being connected to
    std::wstring message;     // the outcome to show, in place of the usual status
};

struct ControlCenterState {
    status::NetworkSnapshot network;
    status::AudioSnapshot audio;
    status::BatterySnapshot battery;
    SystemControlsSnapshot system;
    MediaSnapshot media;
    BrightnessSnapshot brightness;
    bool mediaAvailable = false;
    bool loading = false;
    std::wstring message;
    WiFiPanelState wifi;
};

enum class ControlIntentKind { SystemTile, SystemSettings, Volume, Brightness, ToggleMute,
    MediaPrevious, MediaPlayPause, MediaNext, SelectMedia, SoundSettings, DisplaySettings, BatterySettings, AllSettings,
    NetworkList,          // Windows' own list of available networks (ms-availablenetworks:)
    Accessibility,        // `feature`: a Windows accessibility start/stop shortcut or its settings page
    StickyKeys,           // `enabled`: the documented Sticky Keys setting
    VolumeMixer,          // Windows' volume mixer page
    WiFiConnect,          // `ssid`: connect to this network, asking for a password if it needs one
    WiFiDisconnect };     // leave the connected network
struct ControlIntent {
    ControlIntentKind kind = ControlIntentKind::SystemTile;
    SystemControl control = SystemControl::Settings;
    AccessibilityFeature feature = AccessibilityFeature::Settings;
    std::optional<bool> enabled;
    std::optional<int> percent;
    std::uint64_t session = 0;
    std::wstring ssid;
};

// The Aether-owned quick-controls panel (D44): a compact Windows-style panel of split radio tiles,
// action tiles, two pages, brightness and volume sliders and a battery/settings footer, with Wi-Fi,
// Bluetooth, Accessibility and sound-output detail views and a compact media card only while a real
// session exists. Presentation and input only: the host supplies real snapshots and handles explicit
// intents on workers/providers; constructing or showing this window never changes a system setting.
// Capabilities Windows offers no documented control for (airplane mode, energy saver, night light,
// hotspot, nearby sharing) are marked as opening Windows Settings, never as working on/off toggles.
class ControlCenterWindow final : public ui::SurfaceWindow {
public:
    ControlCenterWindow(graphics::GraphicsDevice& graphics, graphics::FrameScheduler& scheduler,
                        IUiDispatcher& dispatcher);
    ~ControlCenterWindow() override;
    [[nodiscard]] Result<void> Initialize();
    // Opens right-aligned directly below `anchorPx` (the Top Bar group, screen pixels) on its monitor,
    // or closes when already open. `keyboard` shows focus cues from the first frame.
    [[nodiscard]] Result<void> Toggle(const RECT& anchorPx, bool keyboard = false);
    void Close();
    // Test and diagnostics seam: the panel's visible rectangle in screen pixels (empty when hidden).
    [[nodiscard]] RECT PanelBoundsPx() const noexcept;
    // Diagnostics seam: frames rendered so far (settled and hidden panels must stop rendering).
    [[nodiscard]] std::uint64_t RenderCount() const noexcept;
    // Diagnostics seam: the views composed at `now`, bottom to top - 0 the main view, 1..4 the Wi-Fi,
    // Bluetooth, Accessibility and sound-output details - with opacity and horizontal offset (DIPs).
    struct ComposedView {
        int view = 0;
        float opacity = 1.0f;
        float offsetDip = 0.0f;
    };
    [[nodiscard]] std::vector<ComposedView> ComposedViews(double now) const;
    // Diagnostics seam: the pages' position at `now` (0 the first page, 1 the second, between while they move).
    [[nodiscard]] float PagePosition(double now) const noexcept;
    // Diagnostics seam: the value a slider shows at `now`, whether held, requested or read from Windows.
    [[nodiscard]] std::optional<int> ShownSliderValue(bool volume, double now) const;
    // Diagnostics seam: how far the Wi-Fi list is scrolled (DIPs).
    [[nodiscard]] float WiFiScrollOffset() const noexcept;
    // Opens the panel at its Wi-Fi view, or moves an open panel there.
    [[nodiscard]] Result<void> ShowWiFi(const RECT& anchorPx);
    [[nodiscard]] bool WiFiViewShown() const noexcept;
    void SetState(ControlCenterState state);
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);
    Signal<ControlIntent> Requested;
    Signal<> RefreshRequested;
    Signal<> WiFiListRequested;  // the Wi-Fi view opened: the host reads the networks around
    Signal<> Closed;

protected:
    void OnRender(const ui::RenderContext& context) override;
    bool OnAnimate(double now) override;
    void OnPointerMove(D2D1_POINT_2F point) override;
    void OnPointerLeave() override;
    void OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerCaptureLost() override;
    bool OnTouchDown(const ui::TouchContact& contact) override;
    void OnTouchMove(const ui::TouchContact& contact) override;
    void OnTouchUp(const ui::TouchContact& contact) override;
    void OnTouchCancel() override;
    bool OnKeyDown(UINT key) override;
    void OnFocusChanged(bool focused) override;
    void OnDpiChanged(UINT dpi, const RECT& suggested) override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnDestroying() override;

private:
    // Sizes and places the panel below `anchorPx` on its monitor's work area (scaled down when needed);
    // false when no readable scale fits.
    bool PlaceAt(const RECT& anchorPx);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aether::controlcenter
