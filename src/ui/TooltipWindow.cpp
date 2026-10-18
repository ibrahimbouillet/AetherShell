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
constexpr float kPaddingX = 10.0f;
constexpr float kPaddingY = 5.0f;
constexpr float kShadowMargin = 10.0f;  // room for the soft shadow around the bubble
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
    Invalidate();
}

void TooltipWindow::ShowFor(const std::wstring& text, const RECT& anchorPx, TooltipPlacement placement) {
    if (!Handle() || text.empty()) return;
    textValue_ = text;
    hiding_ = false;

    const POINT anchorCenter{(anchorPx.left + anchorPx.right) / 2, (anchorPx.top + anchorPx.bottom) / 2};
    const auto monitor = platform::MonitorForWindow(Handle());
    const UINT dpi = platform::DpiForPoint(anchorCenter);
    const float scale = static_cast<float>(dpi) / 96.0f;

    graphics::TextStyle style;
    style.size = tokens_.fontSizeSmall;
    const auto measured = text_.Measure(text, style, kMaxTextWidth);
    const float widthDip = std::ceil(std::min(measured.width, kMaxTextWidth)) + 2 * kPaddingX + 2 * kShadowMargin;
    const float heightDip = std::ceil(measured.height) + 2 * kPaddingY + 2 * kShadowMargin;
    const int width = static_cast<int>(std::lround(widthDip * scale));
    const int height = static_cast<int>(std::lround(heightDip * scale));
    const int gap = static_cast<int>(std::lround((kGap - kShadowMargin) * scale));

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

    const double now = graphics::FrameScheduler::Now();
    opacity_.AnimateTo(1.0f, now, motion_.Scale(tokens_.durationFast));
    Invalidate();
    RequestAnimationFrame();
    if (!IsShown()) Show(false);
}

void TooltipWindow::HideTooltip() {
    if (!IsShown() || hiding_) return;
    hiding_ = true;
    const double now = graphics::FrameScheduler::Now();
    opacity_.AnimateTo(0.0f, now, motion_.Scale(tokens_.durationFast));
    RequestAnimationFrame();
}

bool TooltipWindow::OnAnimate(double now) {
    const bool animating = opacity_.IsAnimating(now);
    if (!animating && hiding_ && opacity_.Value(now) <= 0.0f) {
        hiding_ = false;
        Hide();
    }
    return animating;
}

void TooltipWindow::OnRender(const RenderContext& context) {
    const float alpha = opacity_.Value(context.now);
    if (alpha <= 0.001f || textValue_.empty()) return;
    winrt::com_ptr<ID2D1Layer> layer;
    const D2D1_RECT_F bubble{kShadowMargin, kShadowMargin, context.widthDip - kShadowMargin,
                             context.heightDip - kShadowMargin};
    context.d2d->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                                  D2D1::IdentityMatrix(), alpha, nullptr,
                                                  D2D1_LAYER_OPTIONS1_NONE),
                           nullptr);
    graphics::GlassStyle glass;
    glass.cornerRadius = tokens_.tooltipCornerRadius;
    glass.opacity = 1.0f;
    glass.backdropBlur = false;
    graphics::PaintGlassPanel(context.d2d, bubble, tokens_, glass);

    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    context.d2d->CreateSolidColorBrush(tokens_.textPrimary.ToD2D(), brush.put());
    graphics::TextStyle style;
    style.size = tokens_.fontSizeSmall;
    style.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
    text_.Draw(context.d2d, textValue_,
               D2D1::RectF(bubble.left + kPaddingX, bubble.top + kPaddingY, bubble.right - kPaddingX,
                           bubble.bottom - kPaddingY),
               style, brush.get());
    context.d2d->PopLayer();
}

std::optional<LRESULT> TooltipWindow::OnMessage(UINT message, WPARAM, LPARAM) {
    // Never intercept the pointer: hit-testing falls through to the window underneath.
    if (message == WM_NCHITTEST) return HTTRANSPARENT;
    return std::nullopt;
}

}  // namespace aether::ui
