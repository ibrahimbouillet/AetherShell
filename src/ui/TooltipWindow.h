// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Small glass tooltip shared by the Dock and the Top Bar. It never takes focus, is transparent to
// hit-testing and keeps itself inside the monitor work area. It fades in while sliding 4 DIPs
// towards its anchor and fades out (instant under reduced motion and in high contrast).
//
// Warm-up: once a tooltip was shown, moving to the next item should not wait for the delay again.
// IsWarm() is true while a tooltip is shown or for DesignTokens::tooltipWarmWindow after it was
// hidden; callers then call ShowFor at once instead of arming their delay timer, and the tooltip
// switches without the slide.
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

    // True while shown, or when hidden less than tokens.tooltipWarmWindow before `now`
    // (FrameScheduler::Now()).
    [[nodiscard]] bool IsWarm(double now) const noexcept;
    [[nodiscard]] bool IsAnimating(double now) const noexcept { return opacity_.IsAnimating(now) || slide_.IsAnimating(now); }
    [[nodiscard]] const std::wstring& Text() const noexcept { return textValue_; }

protected:
    void OnRender(const RenderContext& context) override;
    bool OnAnimate(double now) override;
    std::optional<LRESULT> OnMessage(UINT message, WPARAM wParam, LPARAM lParam) override;

private:
    [[nodiscard]] float MarginDip() const noexcept;

    graphics::DesignTokens tokens_;
    graphics::MotionPolicy motion_;
    graphics::TextCache text_;
    graphics::Tween opacity_{0.0f};
    graphics::Tween slide_{0.0f};  // DIPs away from the anchor
    std::wstring textValue_;
    TooltipPlacement placement_ = TooltipPlacement::Above;
    double hiddenAt_ = -1.0;
    bool hiding_ = false;
};

}  // namespace aether::ui
