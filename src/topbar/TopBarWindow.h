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
#include "ui/PopupMenu.h"
#include "ui/SurfaceWindow.h"
#include "weather/WeatherProvider.h"

#include <memory>
#include <optional>
#include <functional>

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
    // Optional externally owned presenter; isolated tests inject a fake that never opens a popup.
    ui::IMenuPresenter* menu = nullptr;
    // Focus handoff seam for deterministic voice-typing tests; default uses real window operations.
    ui::IMenuWindowOps* menuWindowOps = nullptr;
    // Optional deterministic foreground classifier. With no override, allowAppBar=false also
    // disables native fullscreen observation; isolated tests never inspect another application.
    std::function<bool()> fullscreenProbe;
    bool groupQuickControls = false; // production groups network, output and battery into one button
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
    [[nodiscard]] Result<void> SetTaskbarReplacementState(bool available, bool active, bool suspended);
    void SetFullscreenHidden(bool hidden);
    // A host action started from `item` failed: a short explanation below that item, never over a
    // hidden, suppressed or disabled bar.
    void ReportItemFailure(ItemKind item, std::wstring_view message);
    [[nodiscard]] const std::vector<PlacedItem>& Items() const;
    [[nodiscard]] std::uint64_t RenderCount() const noexcept;

    Signal<> SearchRequested, SettingsRequested, CheckForUpdatesRequested, UpdatesRequested, AboutRequested, QuitRequested;
    Signal<> KeyboardRequested, NetworkRequested, WeatherRequested, MicrophoneSettingsRequested;
    // ClockRequested: Windows' notifications with their calendar; CalendarRequested: the calendar alone.
    Signal<> ClockRequested, CalendarRequested, PowerSettingsRequested, SoundSettingsRequested;
    // The hidden-icons chevron (D45): the host opens Windows' own notification-area overflow.
    Signal<> HiddenIconsRequested;
    Signal<> VoiceTypingRequested;
    // Observed output state changed on the UI thread; never an instruction to change a device.
    // Emitted after the Top Bar model/render update, allowing listeners to destroy the surface.
    Signal<> AudioObservedChanged;
    Signal<RECT> QuickControlsRequested;
    Signal<> RevealWindowsTaskbarRequested, ResumeTaskbarReplacementRequested;
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
    void ShowItemMenu(int id);
    void ShowItemMenu(int id, bool keyboard);
    void ApplyObservedAudio(status::AudioSnapshot snapshot);
};

}  // namespace aether::topbar
