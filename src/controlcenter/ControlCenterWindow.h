// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "media_controls/MediaBrightness.h"
#include "system_controls/SystemControls.h"
#include "core/Dispatcher.h"
#include "core/Signal.h"
#include "graphics/Theme.h"
#include "status/StatusTypes.h"
#include "ui/SurfaceWindow.h"

#include <memory>

namespace aether::controlcenter {

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
};

enum class ControlIntentKind { SystemTile, SystemSettings, Volume, Brightness, ToggleMute,
    MediaPrevious, MediaPlayPause, MediaNext, SelectMedia, SoundSettings, DisplaySettings, BatterySettings, AllSettings };
struct ControlIntent {
    ControlIntentKind kind = ControlIntentKind::SystemTile;
    SystemControl control = SystemControl::Settings;
    std::optional<bool> enabled;
    std::optional<int> percent;
    std::uint64_t session = 0;
};

// Presentation and input only. The host supplies real snapshots and handles explicit intents on
// workers/providers; constructing or showing this window never changes a system setting.
class ControlCenterWindow final : public ui::SurfaceWindow {
public:
    ControlCenterWindow(graphics::GraphicsDevice& graphics, graphics::FrameScheduler& scheduler,
                        IUiDispatcher& dispatcher);
    ~ControlCenterWindow() override;
    [[nodiscard]] Result<void> Initialize();
    [[nodiscard]] Result<void> Toggle(const RECT& anchorPx);
    void Close();
    void SetState(ControlCenterState state);
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);
    Signal<ControlIntent> Requested;
    Signal<> RefreshRequested;
    Signal<> Closed;

protected:
    void OnRender(const ui::RenderContext& context) override;
    bool OnAnimate(double now) override;
    void OnPointerMove(D2D1_POINT_2F point) override;
    void OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerCaptureLost() override;
    bool OnKeyDown(UINT key) override;
    void OnFocusChanged(bool focused) override;
    void OnDpiChanged(UINT dpi, const RECT& suggested) override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnDestroying() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aether::controlcenter
