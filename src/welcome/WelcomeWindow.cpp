// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "welcome/WelcomeWindow.h"

#include "diagnostics/Logger.h"
#include "graphics/FrameScheduler.h"
#include "graphics/Transition.h"

#include <algorithm>
#include <cmath>

namespace aether::welcome {
namespace {
constexpr double kStillReadingSeconds = 3.0;  // without motion: long enough to read two words
constexpr double kReadingHoldSeconds = 1.4;   // the finished words stay a moment before they leave
constexpr double kFadeOutSeconds = 0.5;       // the whole screen eases back to the desktop

D2D1_COLOR_F SystemColor(int index) {
    const COLORREF color = ::GetSysColor(index);
    return D2D1::ColorF(GetRValue(color) / 255.0f, GetGValue(color) / 255.0f, GetBValue(color) / 255.0f);
}
winrt::com_ptr<ID2D1StrokeStyle1> RoundStroke(ID2D1DeviceContext* context) {
    winrt::com_ptr<ID2D1Factory> factory;
    context->GetFactory(factory.put());
    winrt::com_ptr<ID2D1Factory1> factory1;
    winrt::com_ptr<ID2D1StrokeStyle1> round;
    if (!factory || FAILED(factory->QueryInterface(IID_PPV_ARGS(factory1.put())))) return nullptr;
    const auto properties = D2D1::StrokeStyleProperties1(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
    if (FAILED(factory1->CreateStrokeStyle(properties, nullptr, 0, round.put()))) return nullptr;
    return round;
}
void FillPen(ID2D1DeviceContext* context, const WelcomeScene& scene, D2D1_POINT_2F origin, float time, ID2D1Brush* pen) {
    if (const auto position = PenAt(scene, time)) {
        // The tip: a little wider than the line, smaller while it travels above the page between strokes.
        const float radius = std::max(3.4f, scene.penWidth * 0.34f) * (position->lifted ? 0.6f : 1.0f);
        context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(origin.x + position->x, origin.y + position->y), radius, radius), pen);
    }
}
}  // namespace

void PaintHandwritingRange(ID2D1DeviceContext* context, const WelcomeScene& scene, D2D1_POINT_2F origin, float fromTime, float toTime, ID2D1Brush* ink) {
    if (!context || !ink || toTime <= fromTime) return;
    const auto round = RoundStroke(context);
    if (!round) return;
    for (const auto& stroke : scene.strokes) {
        const auto& points = stroke.points;
        if (points.empty() || points.front().time >= toTime) break;
        if (points.back().time <= fromTime) continue;
        for (std::size_t i = 1; i < points.size(); ++i) {
            const auto& from = points[i - 1];
            const auto& to = points[i];
            if (from.time >= toTime) break;
            if (to.time <= fromTime) continue;
            const float span = to.time - from.time;
            const float begin = span > 0.0f ? std::clamp((fromTime - from.time) / span, 0.0f, 1.0f) : 0.0f;
            const float end = span > 0.0f ? std::clamp((toTime - from.time) / span, 0.0f, 1.0f) : 1.0f;
            const auto at = [&](float t) { return D2D1::Point2F(origin.x + from.x + (to.x - from.x) * t, origin.y + from.y + (to.y - from.y) * t); };
            context->DrawLine(at(begin), at(end), ink, (from.width + to.width) * 0.5f, round.get());
        }
    }
}

void PaintHandwriting(ID2D1DeviceContext* context, const WelcomeScene& scene, D2D1_POINT_2F origin, float time, ID2D1Brush* ink, ID2D1Brush* pen) {
    if (!context || !ink) return;
    PaintHandwritingRange(context, scene, origin, -1.0f, time, ink);
    if (pen) FillPen(context, scene, origin, time, pen);
}

WelcomeWindow::WelcomeWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler) : SurfaceWindow(device, scheduler) {}
WelcomeWindow::~WelcomeWindow() { Destroy(); }

void WelcomeWindow::SetBackdrop(std::shared_ptr<const BackdropImage> image) {
    if (image == image_) return;
    image_ = std::move(image);
    blurred_ = nullptr;
    if (!Handle()) return;
    EnsureBackdrop();
    Invalidate();
}

void WelcomeWindow::EnsureBackdrop() {
    if (!blurred_ && image_) blurred_ = BlurBackdrop(Device().D2DDevice(), *image_);
}

void WelcomeWindow::ResetInk() noexcept {
    inkLayer_ = nullptr;
    inkBrush_ = nullptr;
    inkUntil_ = -1.0f;
}

float WelcomeWindow::InkMargin() const noexcept {
    return scene_.penWidth * 1.5f + 4.0f;  // the line has width on either side of its path
}

Result<void> WelcomeWindow::Prepare(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion, const RECT& boundsPx) {
    tokens_ = tokens;
    motion_ = motion;
    animated_ = motion.AllowsMovement() && tokens.mode != graphics::ThemeMode::HighContrast;
    if (!Handle()) {
        ui::SurfaceWindowOptions options;
        options.className = L"AetherShell.Welcome";
        options.title = L"Aether Shell welcome";  // the window text never carries the name
        if (auto created = Create(options, boundsPx); !created) return created;
    }
    SetBoundsPx(boundsPx);
    if (!textReady_) {
        text_.Initialize(Device().DWriteFactory(), tokens.fontFamily, tokens.displayFontFamily);
        textReady_ = true;
    }
    EnsureBackdrop();
    return Ok();
}

Result<void> WelcomeWindow::ShowCover(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion, const RECT& boundsPx) {
    if (auto prepared = Prepare(tokens, motion, boundsPx); !prepared) return prepared;
    scene_ = {};
    ResetInk();
    presenting_ = false;
    covering_ = true;
    skipRequested_ = false;
    Invalidate();
    Show(false);  // draws its first frame before it becomes visible
    return Ok();
}

Result<void> WelcomeWindow::Present(std::wstring_view greeting, std::wstring_view name, const graphics::DesignTokens& tokens,
                                    const graphics::MotionPolicy& motion, const RECT& boundsPx) {
    if (auto prepared = Prepare(tokens, motion, boundsPx); !prepared) return prepared;
    const float width = ToDip(static_cast<float>(boundsPx.right - boundsPx.left));
    const float height = ToDip(static_cast<float>(boundsPx.bottom - boundsPx.top));
    auto scene = ComposeScene(greeting, name, width * 0.88f, height * 0.82f);
    if (!scene) return scene.error();
    scene_ = std::move(*scene);
    ResetInk();
    startedAt_ = graphics::FrameScheduler::Now();
    const bool coverIsUp = covering_ && IsShown() && !IsHiding();
    covering_ = false;
    presenting_ = true;
    skipRequested_ = false;
    AdvanceInk(startedAt_);
    Invalidate();
    if (!coverIsUp) {
        graphics::TransitionShape shape;
        ShowAnimated(graphics::MakeEnterTransition(shape, tokens_, motion_), false);
    }
    // In front of the bars that start with it; they keep preparing and presenting underneath.
    ::SetWindowPos(Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (animated_) RequestAnimationFrame();
    return Ok();
}

void WelcomeWindow::Dismiss(bool animated) {
    presenting_ = false;
    covering_ = false;
    if (!Handle()) return;
    if (animated) {
        graphics::TransitionShape shape;
        auto exit = graphics::MakeExitTransition(shape, tokens_, motion_);
        if (!exit.IsInstant()) exit.duration = kFadeOutSeconds;
        HideAnimated(exit);
    } else {
        Hide();
    }
}

bool WelcomeWindow::WritingFinished(double now) const noexcept {
    const double length = animated_ ? static_cast<double>(scene_.duration) + kReadingHoldSeconds : kStillReadingSeconds;
    return now - startedAt_ >= length;
}

bool WelcomeWindow::EnsureInkLayer() {
    if (inkLayer_) return true;
    auto* device = Device().D2DDevice();
    if (!device || scene_.strokes.empty()) return false;
    if (!inkContext_ && FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, inkContext_.put()))) return false;
    const auto dpi = static_cast<float>(Dpi());
    inkContext_->SetDpi(dpi, dpi);
    const float margin = InkMargin();
    const auto size = D2D1::SizeU(static_cast<UINT32>(std::ceil(ToPx(scene_.width + margin * 2.0f))), static_cast<UINT32>(std::ceil(ToPx(scene_.height + margin * 2.0f))));
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    const auto color = tokens_.mode == graphics::ThemeMode::HighContrast ? SystemColor(COLOR_WINDOWTEXT) : D2D1::ColorF(0.98f, 0.98f, 0.99f);
    if (FAILED(inkContext_->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, dpi, dpi), inkLayer_.put())) ||
        FAILED(inkContext_->CreateSolidColorBrush(color, inkBrush_.put()))) {
        ResetInk();
        return false;
    }
    inkContext_->SetTarget(inkLayer_.get());
    inkContext_->BeginDraw();
    inkContext_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    const HRESULT cleared = inkContext_->EndDraw();
    inkContext_->SetTarget(nullptr);
    if (FAILED(cleared)) {
        ResetInk();
        return false;
    }
    return true;
}

// Separate from the surface's own drawing, which only ever puts this bitmap on screen.
void WelcomeWindow::AdvanceInk(double now) {
    if (!presenting_ || !EnsureInkLayer()) return;
    const float time = animated_ ? static_cast<float>(now - startedAt_) : scene_.duration + 1.0f;
    if (time <= inkUntil_) return;
    const float margin = InkMargin();
    inkContext_->SetTarget(inkLayer_.get());
    inkContext_->BeginDraw();
    PaintHandwritingRange(inkContext_.get(), scene_, D2D1::Point2F(margin, margin), inkUntil_, time, inkBrush_.get());
    const HRESULT drawn = inkContext_->EndDraw();
    inkContext_->SetTarget(nullptr);
    if (FAILED(drawn)) {
        ResetInk();
        return;
    }
    inkUntil_ = time;
}

void WelcomeWindow::OnDeviceReplaced() {
    blurred_ = nullptr;
    inkContext_ = nullptr;
    ResetInk();
    EnsureBackdrop();
    AdvanceInk(graphics::FrameScheduler::Now());
}

void WelcomeWindow::OnRender(const ui::RenderContext& context) {
    ++frames_;
    auto* d2d = context.d2d;
    const bool contrast = tokens_.mode == graphics::ThemeMode::HighContrast;
    if (contrast) {
        d2d->Clear(SystemColor(COLOR_WINDOW));
    } else {
        const auto desktop = SystemColor(COLOR_DESKTOP);
        const float brightness = image_ ? image_->brightness : 0.2126f * desktop.r + 0.7152f * desktop.g + 0.0722f * desktop.b;
        PaintBackdrop(d2d, blurred_.get(), desktop, brightness, context.widthDip, context.heightDip);
    }
    if (scene_.strokes.empty()) return;  // the cover waits for its words
    const float time = animated_ ? static_cast<float>(context.now - startedAt_) : scene_.duration + 1.0f;
    // On whole pixels, so that the kept ink is drawn exactly as it was written.
    const auto origin = D2D1::Point2F(ToDip(std::round(ToPx((context.widthDip - scene_.width) * 0.5f))), ToDip(std::round(ToPx((context.heightDip - scene_.height) * 0.5f))));
    if (inkLayer_) {
        const float margin = InkMargin();
        const auto size = inkLayer_->GetSize();
        const auto place = D2D1::Point2F(origin.x - margin, origin.y - margin);
        winrt::com_ptr<ID2D1Effect> shadow;
        if (!contrast && SUCCEEDED(d2d->CreateEffect(CLSID_D2D1Shadow, shadow.put()))) {
            // A soft dark edge: pale ink stays clear even where a picture is pale behind it.
            shadow->SetInput(0, inkLayer_.get());
            (void)shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, scene_.penWidth * 0.9f);
            (void)shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(0.0f, 0.0f, 0.0f, 0.5f));
            d2d->DrawImage(shadow.get(), D2D1::Point2F(place.x, place.y + scene_.penWidth * 0.3f));
        }
        d2d->DrawBitmap(inkLayer_.get(), D2D1::RectF(place.x, place.y, place.x + size.width, place.y + size.height), 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }
    winrt::com_ptr<ID2D1SolidColorBrush> pen;
    if (FAILED(d2d->CreateSolidColorBrush(contrast ? SystemColor(COLOR_WINDOWTEXT) : D2D1::ColorF(0.98f, 0.98f, 0.99f), pen.put()))) return;
    if (animated_) FillPen(d2d, scene_, origin, time, pen.get());
    if (!scene_.fallbackName.empty()) {
        // An unsupported script stays readable: real type, revealed behind the pen.
        const auto box = D2D1::RectF(origin.x, origin.y + scene_.fallbackTop, origin.x + scene_.fallbackWidth, origin.y + scene_.fallbackTop + scene_.fallbackHeight);
        const float reveal = animated_ ? std::clamp((time - scene_.inkDuration) / std::max(0.01f, scene_.duration - scene_.inkDuration), 0.0f, 1.0f) : 1.0f;
        if (reveal > 0.0f) {
            d2d->PushAxisAlignedClip(D2D1::RectF(box.left, box.top, box.left + (box.right - box.left) * reveal, box.bottom), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            graphics::TextStyle style;
            style.size = scene_.fallbackSize;
            style.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
            text_.Draw(d2d, scene_.fallbackName, box, style, pen.get());
            d2d->PopAxisAlignedClip();
            if (reveal < 1.0f) {
                const auto tip = D2D1::Point2F(box.left + (box.right - box.left) * reveal, (box.top + box.bottom) * 0.5f);
                d2d->FillEllipse(D2D1::Ellipse(tip, 3.4f, 3.4f), pen.get());
            }
        }
    }
}

bool WelcomeWindow::OnAnimate(double now) {
    if (!presenting_ || !animated_) return false;
    AdvanceInk(now);
    return now - startedAt_ < static_cast<double>(scene_.duration) + 0.05;  // no frames once it is written
}

void WelcomeWindow::RequestSkip() {
    // Also on the cover: nothing should be able to leave it in the way of a person who can reach it.
    if ((!presenting_ && !covering_) || skipRequested_) return;
    skipRequested_ = true;
    try { SkipRequested.Emit(); } catch (...) { AETHER_LOG_ERROR("welcome", "skip listener threw an exception"); }
}

void WelcomeWindow::OnPointerDown(ui::PointerButton, D2D1_POINT_2F) { RequestSkip(); }
bool WelcomeWindow::OnTouchDown(const ui::TouchContact&) { RequestSkip(); return true; }
bool WelcomeWindow::OnKeyDown(UINT virtualKey) {
    if (virtualKey != VK_ESCAPE) return false;
    RequestSkip();
    return true;
}

}  // namespace aether::welcome
