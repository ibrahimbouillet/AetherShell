// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/TooltipWindow.h"

#include "graphics/GlassPainter.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MonitorUtilities.h"

#include <algorithm>
#include <cmath>

namespace aether::ui {
namespace {
constexpr float kSlide = 4.0f;          // the bubble slides this far towards its anchor while appearing
constexpr float kGap = 8.0f;            // distance from the anchor
constexpr float kMaxTextWidth = 320.0f;
}  // namespace

TooltipWindow::TooltipWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler)
    : SurfaceWindow(device, scheduler), tokens_(graphics::MakeDesignTokens(graphics::ThemeMode::Dark)) {}

TooltipWindow::~TooltipWindow() { Destroy(); }

Result<void> TooltipWindow::Initialize() {
    SurfaceWindowOptions options;
    options.className = L"AetherShell.Tooltip";
    options.title = L"Aether Shell tooltip";
    text_.Initialize(Device().DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
    return Create(options, RECT{0, 0, 1, 1});
}

void TooltipWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    tokens_ = tokens;
    motion_ = motion;
    text_.Initialize(Device().DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
    if (!motion_.AllowsMovement() || tokens_.mode == graphics::ThemeMode::HighContrast) {
        const bool finishHide = hiding_;
        hiding_ = false;
        slide_.SnapTo(0.0f);
        opacity_.SnapTo(IsShown() && !finishHide ? 1.0f : 0.0f);
        if (finishHide) Hide();
    }
    Invalidate();
}

float TooltipWindow::MarginDip() const noexcept {
    // Room for the shadow around the bubble plus the slide distance.
    return std::ceil(graphics::ShadowExtent(tokens_.elevation.tooltip)) + kSlide;
}

bool TooltipWindow::IsWarm(double now) const noexcept {
    if (IsShown() && !hiding_) return true;
    return hiddenAt_ >= 0.0 && now >= hiddenAt_ && now - hiddenAt_ < tokens_.tooltipWarmWindow;
}

void TooltipWindow::ShowFor(const std::wstring& text, const RECT& anchorPx, TooltipPlacement placement) {
    if (!Handle() || text.empty()) return;
    const double now = graphics::FrameScheduler::Now();
    const bool showing = IsShown() && !hiding_;
    const bool warm = IsWarm(now);
    textValue_ = text;
    placement_ = placement;
    hiding_ = false;

    const POINT anchorCenter{(anchorPx.left + anchorPx.right) / 2, (anchorPx.top + anchorPx.bottom) / 2};
    const auto monitor = platform::MonitorForWindow(Handle());
    const UINT dpi = platform::DpiForPoint(anchorCenter);
    const float scale = static_cast<float>(dpi) / 96.0f;
    const float margin = MarginDip();

    graphics::TextStyle style;
    style.size = tokens_.type.caption;
    const auto measured = text_.Measure(text, style, kMaxTextWidth);
    const float widthDip = std::ceil(std::min(measured.width, kMaxTextWidth)) + 2 * tokens_.tooltipPaddingX + 2 * margin;
    const float heightDip = std::ceil(measured.height) + 2 * tokens_.tooltipPaddingY + 2 * margin;
    const int width = static_cast<int>(std::lround(widthDip * scale));
    const int height = static_cast<int>(std::lround(heightDip * scale));
    const int gap = static_cast<int>(std::lround((kGap - margin) * scale));

    RECT bounds{};
    bounds.left = anchorCenter.x - width / 2;
    bounds.top = placement == TooltipPlacement::Above ? anchorPx.top - height - gap : anchorPx.bottom + gap;
    // Keep inside the monitor that contains the anchor.
    const auto target = platform::QueryMonitor(::MonitorFromPoint(anchorCenter, MONITOR_DEFAULTTONEAREST));
    const RECT area = target ? target->workArea : monitor.workArea;
    bounds.left = std::clamp<LONG>(bounds.left, area.left, std::max<LONG>(area.left, area.right - width));
    bounds.top = std::clamp<LONG>(bounds.top, area.top, std::max<LONG>(area.top, area.bottom - height));
    bounds.right = bounds.left + width;
    bounds.bottom = bounds.top + height;
    SetBoundsPx(bounds);

    const bool instant = !motion_.AllowsMovement() || tokens_.mode == graphics::ThemeMode::HighContrast;
    if (instant) {
        opacity_.SnapTo(1.0f);
        slide_.SnapTo(0.0f);
    } else if (showing || warm) {
        // Switching between items: no slide, a short fade only when it was hidden.
        slide_.SnapTo(0.0f);
        opacity_.AnimateTo(1.0f, now, motion_.Scale(tokens_.durationMicro), tokens_.curves.decelerate);
    } else {
        if (opacity_.Value(now) <= 0.0f) slide_.SnapTo(kSlide);
        slide_.AnimateTo(0.0f, now, motion_.Scale(tokens_.durationFast), tokens_.curves.decelerate);
        opacity_.AnimateTo(1.0f, now, motion_.Scale(tokens_.durationFast), tokens_.curves.decelerate);
    }
    Invalidate();
    RequestAnimationFrame();
    if (!IsShown()) Show(false);
}

void TooltipWindow::HideTooltip() {
    if (!IsShown() || hiding_) return;
    const double now = graphics::FrameScheduler::Now();
    hiddenAt_ = now;
    if (!motion_.AllowsMovement() || tokens_.mode == graphics::ThemeMode::HighContrast) {
        opacity_.SnapTo(0.0f);
        slide_.SnapTo(0.0f);
        Hide();
        return;
    }
    hiding_ = true;
    opacity_.AnimateTo(0.0f, now, motion_.Scale(tokens_.durationFast), tokens_.curves.accelerate);
    RequestAnimationFrame();
}

bool TooltipWindow::OnAnimate(double now) {
    const bool animating = IsAnimating(now);
    if (!animating && hiding_ && opacity_.Value(now) <= 0.0f) {
        hiding_ = false;
        Hide();
    }
    return animating;
}

void TooltipWindow::OnRender(const RenderContext& context) {
    const float alpha = opacity_.Value(context.now);
    if (alpha <= 0.001f || textValue_.empty()) return;
    const float margin = MarginDip();
    const float slide = slide_.Value(context.now);
    const float dy = placement_ == TooltipPlacement::Above ? -slide : slide;
    const D2D1_RECT_F bubble{margin, margin + dy, context.widthDip - margin, context.heightDip - margin + dy};
    context.d2d->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                  D2D1::IdentityMatrix(), alpha, nullptr,
                                                  D2D1_LAYER_OPTIONS1_NONE),
                           nullptr);
    graphics::GlassStyle glass;
    glass.cornerRadius = tokens_.radii.tooltip;
    glass.opacity = 1.0f;
    glass.backdropBlur = false;
    glass.elevation = graphics::Elevation::Tooltip;
    glass.neutral = true;
    graphics::PaintGlassPanel(context.d2d, bubble, tokens_, glass);

    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    context.d2d->CreateSolidColorBrush(tokens_.textPrimary.ToD2D(), brush.put());
    graphics::TextStyle style;
    style.size = tokens_.type.caption;
    style.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
    text_.Draw(context.d2d, textValue_,
               D2D1::RectF(bubble.left + tokens_.tooltipPaddingX, bubble.top + tokens_.tooltipPaddingY,
                           bubble.right - tokens_.tooltipPaddingX, bubble.bottom - tokens_.tooltipPaddingY),
               style, brush.get());
    context.d2d->PopLayer();
}

std::optional<LRESULT> TooltipWindow::OnMessage(UINT message, WPARAM, LPARAM) {
    // Never intercept the pointer: hit-testing falls through to the window underneath.
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    return std::nullopt;
}

}  // namespace aether::ui
