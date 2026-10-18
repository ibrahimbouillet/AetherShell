// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Small glass tooltip shared by the Dock and the Top Bar. It never takes focus,
// is transparent to hit-testing, fades in/out (instantly under reduced motion)
// and keeps itself inside the monitor work area.
#pragma once

#include "graphics/Animation.h"
#include "graphics/TextCache.h"
#include "graphics/Theme.h"
#include "ui/SurfaceWindow.h"

#include <string>

namespace aether::ui {

enum class TooltipPlacement { Above, Below };

class TooltipWindow final : public SurfaceWindow {
public:
    TooltipWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler);
    ~TooltipWindow() override;

    [[nodiscard]] Result<void> Initialize();
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);

    // Shows `text` next to `anchorPx` (screen pixels). Repeated calls with the same text just move it.
    void ShowFor(const std::wstring& text, const RECT& anchorPx, TooltipPlacement placement);
    void HideTooltip();

protected:
    void OnRender(const RenderContext& context) override;
    bool OnAnimate(double now) override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    graphics::DesignTokens tokens_;
    graphics::MotionPolicy motion_;
    graphics::TextCache text_;
    graphics::Tween opacity_{0.0f};
    std::wstring textValue_;
    bool hiding_ = false;
};

}  // namespace aether::ui
