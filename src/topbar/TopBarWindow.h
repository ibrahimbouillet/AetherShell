// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Dispatcher.h"
#include "core/Signal.h"
#include "graphics/Animation.h"
#include "graphics/Theme.h"
#include "settings/SettingsService.h"
#include "status/audio/AudioProvider.h"
#include "status/audio/MicrophoneProvider.h"
#include "topbar/TopBarModel.h"
#include "ui/SurfaceWindow.h"
#include "weather/WeatherProvider.h"

#include <memory>
#include <optional>

namespace aether::topbar {

struct TopBarContext {
    graphics::GraphicsDevice& device;
    graphics::FrameScheduler& scheduler;
    IUiDispatcher& dispatcher;
    settings::SettingsService* settings = nullptr;
    status::IStatusProvider<status::ClockSnapshot>* clock = nullptr;
    status::IStatusProvider<status::BatterySnapshot>* battery = nullptr;
    status::IStatusProvider<status::KeyboardSnapshot>* keyboard = nullptr;
    status::IStatusProvider<status::NetworkSnapshot>* network = nullptr;
    status::audio::AudioProvider* audio = nullptr;
    status::audio::MicrophoneProvider* microphone = nullptr;
    weather::WeatherProvider* weather = nullptr;
    // Isolated rendering tests pass false, so no SHAppBarMessage can run at all.
    bool allowAppBar = true;
};

// UI-thread owned. Provider pointers are optional, unstarted and must outlive the window. Create
// starts their listeners; destruction stops them and invalidates queued dispatcher work safely.
class TopBarWindow final : public ui::SurfaceWindow {
public:
    explicit TopBarWindow(TopBarContext context);
    ~TopBarWindow() override;
    [[nodiscard]] Result<void> Create(std::optional<RECT> testBoundsPx = {});
    void Relayout();
    void OnExplorerRestarted();
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion = {});
    void SetSnapshots(TopBarSnapshots snapshots);
    void SetSettings(const settings::TopBarSettings& settings, bool weatherEnabled);
    [[nodiscard]] Result<void> SetUpdateAvailable(bool available);
    void SetFullscreenHidden(bool hidden);
    [[nodiscard]] const std::vector<PlacedItem>& Items() const;
    [[nodiscard]] std::uint64_t RenderCount() const noexcept;

    Signal<> SearchRequested, SettingsRequested, CheckForUpdatesRequested, UpdatesRequested, AboutRequested, QuitRequested;
    Signal<bool> FullscreenChanged;

protected:
    void OnRender(const ui::RenderContext& context) override;
    bool OnAnimate(double now) override;
    void OnPointerMove(D2D1_POINT_2F point) override;
    void OnPointerLeave() override;
    void OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnWheel(float notches, D2D1_POINT_2F point) override;
    bool OnKeyDown(UINT key) override;
    void OnFocusChanged(bool focused) override;
    void OnDpiChanged(UINT dpi, const RECT& suggested) override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnDestroying() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void Rebuild();
    void Activate(int id);
};

}  // namespace aether::topbar
