// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "welcome/WelcomeWindow.h"

#include "diagnostics/Logger.h"
#include "graphics/FrameScheduler.h"
#include "graphics/Transition.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace aether::welcome {
namespace {
constexpr double kStillReadingSeconds = 2.0;  // without motion: the same two seconds
constexpr double kReadingHoldSeconds = 0.5;   // the finished words stay a moment before they leave
constexpr double kFadeOutSeconds = 0.3;       // the whole screen eases back to the desktop
constexpr float kPenHeadSeconds = 0.08f;      // D60: the newest ink is still finer than it will settle to
constexpr float kPenHeadFineness = 0.3f;      // its width at the tip, as a share of its final width

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
        // The tip: fine, like the ink it leaves, and finer while it travels above the page between strokes.
        const float radius = std::max(2.4f, scene.penWidth * 0.13f) * (position->lifted ? 0.6f : 1.0f);
        context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(origin.x + position->x, origin.y + position->y), radius, radius), pen);
    }
}

// Each piece of line the pen lays between two moments: its two points and the share of it inside them.
template <typename Piece>
void ForEachPiece(const WelcomeScene& scene, float fromTime, float toTime, Piece&& piece) {
    if (toTime <= fromTime) return;
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
            piece(from, to, begin, end);
        }
    }
}

// Without `headAt`, each piece of line at its final width; with it, as the head of the line `headAt` seconds in.
void PaintRange(ID2D1DeviceContext* context, const WelcomeScene& scene, D2D1_POINT_2F origin, float fromTime, float toTime, ID2D1Brush* ink, std::optional<float> headAt) {
    if (!context || !ink || toTime <= fromTime) return;
    const auto round = RoundStroke(context);
    if (!round) return;
    ForEachPiece(scene, fromTime, toTime, [&](const auto& from, const auto& to, float begin, float end) {
        const auto at = [&](float t) { return D2D1::Point2F(origin.x + from.x + (to.x - from.x) * t, origin.y + from.y + (to.y - from.y) * t); };
        float width = (from.width + to.width) * 0.5f;
        if (headAt) {
            const float settled = std::clamp((*headAt - (from.time + to.time) * 0.5f) / kPenHeadSeconds, 0.0f, 1.0f);
            width *= kPenHeadFineness + (1.0f - kPenHeadFineness) * settled * settled * (3.0f - 2.0f * settled);
        }
        context->DrawLine(at(begin), at(end), ink, width, round.get());
    });
}

constexpr float kShadowDeviation = 0.9f;  // of the pen's width: the soft dark edge under the ink
constexpr float kShadowDrop = 0.3f;       // of the pen's width: how far below the ink it falls
}  // namespace

std::optional<D2D1_RECT_F> HandwritingBounds(const WelcomeScene& scene, float fromTime, float toTime) {
    std::optional<D2D1_RECT_F> bounds;
    ForEachPiece(scene, fromTime, toTime, [&](const auto& from, const auto& to, float begin, float end) {
        // Round caps reach half the width beyond each end; one more DIP for antialiasing.
        const float half = std::max(from.width, to.width) * 0.5f + 1.0f;
        for (const float t : {begin, end}) {
            const float x = from.x + (to.x - from.x) * t, y = from.y + (to.y - from.y) * t;
            const D2D1_RECT_F piece{x - half, y - half, x + half, y + half};
            if (!bounds) bounds = piece;
            else bounds = D2D1::RectF(std::min(bounds->left, piece.left), std::min(bounds->top, piece.top),
                                      std::max(bounds->right, piece.right), std::max(bounds->bottom, piece.bottom));
        }
    });
    return bounds;
}

void InkShadow::Clear() noexcept {
    effect_ = nullptr;
    bitmap_ = nullptr;
    reach_ = 0.0f;
}

bool InkShadow::Reset(ID2D1DeviceContext* context, ID2D1Bitmap1* ink, float deviation) {
    Clear();
    if (!context || !ink || !(deviation > 0.0f)) return false;
    float dpiX = 96.0f, dpiY = 96.0f;
    context->GetDpi(&dpiX, &dpiY);
    scale_ = dpiX / 96.0f;
    // The blur reaches about three deviations. On whole pixels, so that the edge lies on the ink's grid.
    reach_ = std::ceil((deviation * 3.0f + 2.0f) * scale_) / scale_;
    const auto pixels = ink->GetPixelSize();
    const UINT32 margin = static_cast<UINT32>(std::lround(reach_ * scale_)) * 2u;
    const auto format = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    if (FAILED(context->CreateBitmap(D2D1::SizeU(pixels.width + margin, pixels.height + margin), nullptr, 0,
                                     D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, format, dpiX, dpiY), bitmap_.put())) ||
        FAILED(context->CreateEffect(CLSID_D2D1Shadow, effect_.put()))) {
        Clear();
        return false;
    }
    effect_->SetInput(0, ink);
    (void)effect_->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, deviation);
    (void)effect_->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(0.0f, 0.0f, 0.0f, 0.5f));
    // No approximation that depends on the area drawn: a piece blurred again must meet the edge around it.
    (void)effect_->SetValue(D2D1_SHADOW_PROP_OPTIMIZATION, D2D1_SHADOW_OPTIMIZATION_QUALITY);
    context->SetTarget(bitmap_.get());
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    const HRESULT cleared = context->EndDraw();
    context->SetTarget(nullptr);
    if (FAILED(cleared)) {
        Clear();
        return false;
    }
    return true;
}

bool InkShadow::Update(ID2D1DeviceContext* context, const D2D1_RECT_F& inkRect) {
    if (!context || !bitmap_ || !effect_) return false;
    const auto size = bitmap_->GetSize();
    // In the edge's own coordinates, where the ink bitmap lies at (reach, reach); new ink changes the edge
    // up to `reach` around it. Whole pixels, so that what is drawn again replaces exactly what it covers.
    const auto snapDown = [this](float v) { return std::floor(v * scale_) / scale_; };
    const auto snapUp = [this](float v) { return std::ceil(v * scale_) / scale_; };
    const D2D1_RECT_F area{std::max(0.0f, snapDown(inkRect.left)), std::max(0.0f, snapDown(inkRect.top)),
                           std::min(size.width, snapUp(inkRect.right + reach_ * 2.0f)), std::min(size.height, snapUp(inkRect.bottom + reach_ * 2.0f))};
    if (area.right <= area.left || area.bottom <= area.top) return true;
    context->SetTarget(bitmap_.get());
    context->BeginDraw();
    context->PushAxisAlignedClip(area, D2D1_ANTIALIAS_MODE_ALIASED);
    context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    const auto image = D2D1::RectF(area.left - reach_, area.top - reach_, area.right - reach_, area.bottom - reach_);
    context->DrawImage(effect_.get(), D2D1::Point2F(area.left, area.top), image, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    context->PopAxisAlignedClip();
    const HRESULT drawn = context->EndDraw();
    context->SetTarget(nullptr);
    return SUCCEEDED(drawn);
}

void PaintHandwritingRange(ID2D1DeviceContext* context, const WelcomeScene& scene, D2D1_POINT_2F origin, float fromTime, float toTime, ID2D1Brush* ink) {
    PaintRange(context, scene, origin, fromTime, toTime, ink, std::nullopt);
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
    shadow_.Clear();
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
    writingFrames_ = 0;
    longestGap_ = 0.0;
    writingLogged_ = !animated_;
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
    writingLogged_ = true;  // a skipped greeting says nothing about the pen's pace
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
    // High contrast draws no edge; without one the ink still shows, so a refusal is not a failure.
    if (tokens_.mode != graphics::ThemeMode::HighContrast) (void)shadow_.Reset(inkContext_.get(), inkLayer_.get(), scene_.penWidth * kShadowDeviation);
    return true;
}

// Separate from the surface's own drawing, which only ever puts these bitmaps on screen.
void WelcomeWindow::AdvanceInk(double now) {
    if (!presenting_ || !EnsureInkLayer()) return;
    // What is kept is settled ink; its newest part is drawn over it with each frame (OnRender).
    const float time = animated_ ? static_cast<float>(now - startedAt_) - kPenHeadSeconds : scene_.duration + 1.0f;
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
    if (shadow_.Bitmap()) {
        if (const auto added = HandwritingBounds(scene_, inkUntil_, time)) {
            const auto inLayer = D2D1::RectF(added->left + margin, added->top + margin, added->right + margin, added->bottom + margin);
            if (!shadow_.Update(inkContext_.get(), inLayer)) shadow_.Clear();  // the ink alone, rather than a wrong edge
        }
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
    if (!writingLogged_) {
        if (writingFrames_ > 0) longestGap_ = std::max(longestGap_, context.now - lastFrameAt_);
        lastFrameAt_ = context.now;
        ++writingFrames_;
    }
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
        if (auto* edge = contrast ? nullptr : shadow_.Bitmap()) {
            // A soft dark edge: pale ink stays clear even where a picture is pale behind it. Already blurred.
            const auto reach = shadow_.Reach();
            const auto edgeSize = edge->GetSize();
            const float top = place.y - reach + scene_.penWidth * kShadowDrop;
            d2d->DrawBitmap(edge, D2D1::RectF(place.x - reach, top, place.x - reach + edgeSize.width, top + edgeSize.height), 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
        }
        d2d->DrawBitmap(inkLayer_.get(), D2D1::RectF(place.x, place.y, place.x + size.width, place.y + size.height), 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }
    winrt::com_ptr<ID2D1SolidColorBrush> pen;
    if (FAILED(d2d->CreateSolidColorBrush(contrast ? SystemColor(COLOR_WINDOWTEXT) : D2D1::ColorF(0.98f, 0.98f, 0.99f), pen.put()))) return;
    if (animated_) {
        PaintRange(d2d, scene_, origin, time - kPenHeadSeconds, time, pen.get(), time);
        FillPen(d2d, scene_, origin, time, pen.get());
    }
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
    const bool writing = now - startedAt_ < static_cast<double>(scene_.duration + kPenHeadSeconds) + 0.05;  // no frames once it is written
    if (!writing && !writingLogged_) {
        // Whether the pen moved smoothly on this PC: a long gap is a visible jump. Never the words or the name.
        writingLogged_ = true;
        const double seconds = now - startedAt_;
        AETHER_LOG_INFO("welcome", "written in {:.2f} s: {} frames, {:.0f} per second, longest gap {:.0f} ms, background {}", seconds, writingFrames_,
                        seconds > 0.0 ? static_cast<double>(writingFrames_) / seconds : 0.0, longestGap_ * 1000.0, blurred_ ? "picture" : "plain colour");
    }
    return writing;
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
