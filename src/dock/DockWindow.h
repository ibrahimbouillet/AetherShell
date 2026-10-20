// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "applications/ApplicationTypes.h"
#include "core/Dispatcher.h"
#include "core/Signal.h"
#include "dock/DockLayout.h"
#include "graphics/Animation.h"
#include "graphics/Theme.h"
#include "settings/SettingsService.h"
#include "ui/SurfaceWindow.h"
#include "ui/PopupMenu.h"

#include <memory>

namespace aether::dock {

struct DockWindowContext {
    graphics::GraphicsDevice& graphics;
    graphics::FrameScheduler& scheduler;
    IUiDispatcher& dispatcher;
    apps::IApplicationLauncher& launcher;
    apps::IIconProvider& icons;
    settings::SettingsService& settings;
    ui::IMenuPresenter* menuPresenter = nullptr; // optional injected presenter; caller owns its lifetime
};

struct DockWindowOptions {
    // An explicit work area and disabled reservation keep own-window integration tests isolated.
    std::optional<RECT> workAreaPx;
    bool allowWorkAreaReservation = true;
    HWND owner = nullptr;
    // The application owns reversible native-taskbar visibility; the Dock owns its reserved area.
    bool replacementTaskbarMode = false;
};

class DockWindow final : public ui::SurfaceWindow {
public:
    explicit DockWindow(DockWindowContext context);
    ~DockWindow() override;
    Result<void> Create(DockWindowOptions options = {});
    void Show(bool activate = false);
    Result<void> FocusDock();
    Result<void> SetRunningApplications(std::vector<apps::RunningApplication> applications);
    Result<void> SetApplicationCatalog(std::vector<apps::ApplicationInfo> applications);
    Result<void> SetSuppressed(bool suppressed);
    Result<void> Relayout();
    Result<void> OnExplorerRestarted();
    Result<void> SetTaskbarReplacement(bool active);
    Result<void> SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
                          const settings::DockSettings& settings);
    [[nodiscard]] Result<DockLayout> Geometry() const;
    [[nodiscard]] std::uint64_t RenderedFrames() const noexcept;
    Signal<> SearchRequested;
    Signal<> SettingsRequested;
    Signal<> ImportWindowsPinsRequested;
    Signal<> StartRequested;
    Signal<> TaskViewRequested;

protected:
    void OnRender(const ui::RenderContext& context) override;
    bool OnAnimate(double now) override;
    void OnDpiChanged(UINT dpi, const RECT& suggestedPx) override;
    void OnPointerMove(D2D1_POINT_2F point) override;
    void OnPointerLeave() override;
    void OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) override;
    void OnPointerCaptureLost() override;
    bool OnKeyDown(UINT virtualKey) override;
    void OnFocusChanged(bool focused) override;
    void OnDeviceReplaced() override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
    void OnCreated() override;
    void OnDestroying() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::dock
