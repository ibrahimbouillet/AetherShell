// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/SurfaceWindow.h"

#include "diagnostics/Logger.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/Handles.h"
#include "platform/windows/PointerInput.h"
#include "platform/windows/Win32Error.h"
#include "platform/windows/WindowUtilities.h"
#include "core/Strings.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <exception>

namespace aether::ui {
namespace {

constexpr DWORD kDwmBorderColor = 34;               // DWMWA_BORDER_COLOR (Windows 11)
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;      // DWMWA_COLOR_NONE

}  // namespace

SurfaceWindow::SurfaceWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler)
    : device_(device), scheduler_(scheduler) {}

SurfaceWindow::~SurfaceWindow() {
    // Subclasses must call Destroy() in their own destructor while their virtual overrides still
    // exist; this is a safety net only.
    Destroy();
}

Result<void> SurfaceWindow::AnimateEntrance(std::chrono::milliseconds duration, float offsetDip) {
    return surface_.AnimateEntrance(duration, ToPx(offsetDip));
}

Result<void> SurfaceWindow::AnimateExit(std::chrono::milliseconds duration, float offsetDip) {
    return surface_.AnimateExit(duration, ToPx(offsetDip));
}

Result<void> SurfaceWindow::Create(const SurfaceWindowOptions& options, const RECT& boundsPx) {
    if (hwnd_) return MakeError(ErrorCode::InvalidState, "window already created");
    options_ = options;
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &SurfaceWindow::WindowProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = options_.className.c_str();
    if (auto registered = platform::RegisterWindowClassOnce(wc); !registered) return registered.error();

    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP;
    if (options_.topmost) exStyle |= WS_EX_TOPMOST;
    if (options_.noActivate) exStyle |= WS_EX_NOACTIVATE;
    if (options_.toolWindow) exStyle |= WS_EX_TOOLWINDOW;

    bounds_ = boundsPx;
    hwnd_ = ::CreateWindowExW(exStyle, options_.className.c_str(), options_.title.c_str(), WS_POPUP,
                              boundsPx.left, boundsPx.top, boundsPx.right - boundsPx.left,
                              boundsPx.bottom - boundsPx.top, options_.owner, nullptr, instance, this);
    if (!hwnd_) return platform::LastWin32Error("CreateWindowEx(surface)");
    dpi_ = platform::DpiForWindow(hwnd_);

    // Windows 11 would otherwise round and outline the popup itself.
    platform::SetCornerPreference(hwnd_, platform::CornerPreference::DoNotRound);
    ::DwmSetWindowAttribute(hwnd_, kDwmBorderColor, &kDwmColorNone, sizeof(kDwmColorNone));
    platform::ExcludeFromPeek(hwnd_);

    if (auto attached = surface_.Attach(device_, hwnd_); !attached) {
        const Error error = attached.error();
        ::DestroyWindow(hwnd_);
        hwnd_ = nullptr;
        return error;
    }
    RECT client{};
    ::GetClientRect(hwnd_, &client);
    if (auto resized = surface_.Resize(client.right, client.bottom); !resized) {
        AETHER_LOG_WARN("ui", "initial surface resize failed: {}", resized.error().Describe());
    }
    deviceReplaced_ = device_.DeviceReplaced.Connect([this] {
        OnDeviceReplaced();
        backdropApplied_ = false;   // the compositor's objects are new: the backdrop is given again
        if (backdropBlurRequested_) SetBackdropPanel(backdropPanelDip_, backdropRadiusDip_, true, backdropOpacity_, backdropGlass_ ? &*backdropGlass_ : nullptr);
        Invalidate();
    });
    OnCreated();
    Invalidate();
    return Ok();
}

void SurfaceWindow::Destroy() noexcept {
    scheduler_.Cancel(this);
    deviceReplaced_.Disconnect();
    if (hwnd_) {
        ::DestroyWindow(hwnd_);  // WM_DESTROY detaches the surface
        hwnd_ = nullptr;
    }
    surface_.Detach();
    shown_ = false;
    hiding_ = false;
}

void SurfaceWindow::SetBoundsPx(const RECT& boundsPx) {
    if (!hwnd_) return;
    const bool sizeChanged = (boundsPx.right - boundsPx.left) != (bounds_.right - bounds_.left) ||
                             (boundsPx.bottom - boundsPx.top) != (bounds_.bottom - bounds_.top);
    const bool moved = boundsPx.left != bounds_.left || boundsPx.top != bounds_.top;
    if (!sizeChanged && !moved) return;
    bounds_ = boundsPx;
    ::SetWindowPos(hwnd_, nullptr, boundsPx.left, boundsPx.top, boundsPx.right - boundsPx.left,
                   boundsPx.bottom - boundsPx.top, SWP_NOZORDER | SWP_NOACTIVATE);
    if (sizeChanged) {
        if (auto resized = surface_.Resize(boundsPx.right - boundsPx.left, boundsPx.bottom - boundsPx.top); !resized) {
            AETHER_LOG_WARN("ui", "surface resize failed: {}", resized.error().Describe());
        }
        ApplyInputRegion();
        Invalidate();
    }
}

void SurfaceWindow::SetInputRegionPx(std::span<const RECT> rects) {
    inputRegion_.assign(rects.begin(), rects.end());
    ApplyInputRegion();
}

void SurfaceWindow::SetVisualRegionPx(std::span<const RECT> rects) {
    visualRegion_.assign(rects.begin(), rects.end());
    ApplyInputRegion();
}

void SurfaceWindow::ApplyInputRegion() {
    if (!hwnd_) return;
    const auto& region = visualRegion_.empty() ? inputRegion_ : visualRegion_;
    // AS4-08: an unchanged region is not written again. Every SetWindowRgn sends position messages
    // (and, for a registered AppBar, a shell notification) although nothing changed.
    const auto same = [](const std::vector<RECT>& a, const std::vector<RECT>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
            [](const RECT& x, const RECT& y) { return ::EqualRect(&x, &y) != FALSE; });
    };
    if (appliedRegionWindow_ == hwnd_ && same(appliedRegion_, region)) return;
    if (region.empty()) {
        if (::SetWindowRgn(hwnd_, nullptr, TRUE)) { appliedRegion_.clear(); appliedRegionWindow_ = hwnd_; }
        return;
    }
    platform::UniqueRegion combined(::CreateRectRgn(0, 0, 0, 0));
    for (const RECT& r : region) {
        platform::UniqueRegion part(::CreateRectRgn(r.left, r.top, r.right, r.bottom));
        ::CombineRgn(combined.Get(), combined.Get(), part.Get(), RGN_OR);
    }
    // On success the system owns the region.
    if (::SetWindowRgn(hwnd_, combined.Get(), TRUE)) {
        (void)combined.Release();
        appliedRegion_ = region; appliedRegionWindow_ = hwnd_;
    }
}

bool SurfaceWindow::PaintLiquidPanel(const RenderContext& context, const D2D1_RECT_F& panelDip, float cornerRadiusDip, float shadow) {
    auto* renderer = device_.Material();
    if (!renderer || !context.d2d) return false;
    const float scale = static_cast<float>(dpi_) / 96.0f;
    // D65, D66: the glass's light and shade only (the compositor shows the adapted backdrop under it). Only the panel
    // and its shadow are drawn, from a bitmap kept until the panel changes: nothing in it depends on what lies
    // behind, so a frame that moves nothing costs one bitmap draw.
    constexpr float kShadowRoomDip = 30.0f;
    const auto left = static_cast<LONG>(std::floor(std::max(0.0f, panelDip.left - kShadowRoomDip) * scale));
    const auto top = static_cast<LONG>(std::floor(std::max(0.0f, panelDip.top - kShadowRoomDip) * scale));
    const auto right = static_cast<LONG>(std::ceil(std::min(context.widthDip, panelDip.right + kShadowRoomDip) * scale));
    const auto bottom = static_cast<LONG>(std::ceil(std::min(context.heightDip, panelDip.bottom + kShadowRoomDip) * scale));
    if (right <= left || bottom <= top) return false;
    const auto size = D2D1::SizeU(static_cast<UINT32>(right - left), static_cast<UINT32>(bottom - top));
    const std::array<float, 9> key{static_cast<float>(left), static_cast<float>(top), static_cast<float>(size.width), static_cast<float>(size.height),
                                   panelDip.left * scale, panelDip.top * scale, panelDip.right * scale, panelDip.bottom * scale, cornerRadiusDip * scale + shadow * 1000.0f};
    const auto area = D2D1::RectF(static_cast<float>(left) / scale, static_cast<float>(top) / scale, static_cast<float>(right) / scale, static_cast<float>(bottom) / scale);
    if (liquidCache_ && key == liquidKey_ && liquidGeneration_ == device_.Generation()) {
        context.d2d->DrawBitmap(liquidCache_.get(), area, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
        return true;
    }
    graphics::MaterialScene scene;
    scene.style = graphics::MaterialStyle::Liquid;
    scene.transparent = true;
    scene.edgeBend = 0.0f;
    scene.pxPerDip = scale;
    auto& body = scene.bodies[0];
    body.present = true;
    body.cx = (panelDip.left + panelDip.right) / 2.0f * scale - static_cast<float>(left);
    body.cy = (panelDip.top + panelDip.bottom) / 2.0f * scale - static_cast<float>(top);
    body.hx = (panelDip.right - panelDip.left) / 2.0f * scale;
    body.hy = (panelDip.bottom - panelDip.top) / 2.0f * scale;
    body.radius = cornerRadiusDip * scale;
    scene.lens = 1.0f;
    scene.shadow = shadow;
    scene.picture = D2D1::RectF(0.0f, 0.0f, static_cast<float>(size.width), static_cast<float>(size.height));   // unused: transparent
    context.d2d->Flush();   // Direct2D's work so far reaches the device before the material's own
    auto frame = renderer->Render(context.d2d, scene, size);
    if (!frame) return false;   // a lost device shows at the end of this frame, where the surface already recovers
    // Kept in a bitmap of this window's own: the renderer's texture serves the next surface that draws.
    D2D1_SIZE_U kept{};
    if (liquidCache_) kept = liquidCache_->GetPixelSize();
    if (!liquidCache_ || kept.width != size.width || kept.height != size.height || liquidGeneration_ != device_.Generation()) {
        liquidCache_ = nullptr;
        float dpiX = 96.0f, dpiY = 96.0f;
        context.d2d->GetDpi(&dpiX, &dpiY);
        const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpiX, dpiY);
        if (FAILED(context.d2d->CreateBitmap(size, nullptr, 0, properties, liquidCache_.put()))) liquidCache_ = nullptr;
    }
    if (liquidCache_ && SUCCEEDED(liquidCache_->CopyFromBitmap(nullptr, frame->get(), nullptr))) {
        liquidKey_ = key;
        liquidGeneration_ = device_.Generation();
        context.d2d->DrawBitmap(liquidCache_.get(), area, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    } else {
        liquidCache_ = nullptr;
        context.d2d->DrawBitmap(frame->get(), area, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
    }
    return true;
}

void SurfaceWindow::SetBackdropPanel(const D2D1_RECT_F& panelDip, float cornerRadiusDip, bool blur, float backdropOpacity,
                                     const graphics::LiquidGlassTokens* glass) {
    // Every frame repeats the same request: only a change reaches the compositor.
    const bool sameGlass = glass ? backdropGlass_.has_value() && *backdropGlass_ == *glass : !backdropGlass_.has_value();
    if (backdropApplied_ && sameGlass && backdropBlurRequested_ == blur && backdropOpacity_ == backdropOpacity &&
        backdropRadiusDip_ == cornerRadiusDip && backdropPanelDip_.left == panelDip.left && backdropPanelDip_.top == panelDip.top &&
        backdropPanelDip_.right == panelDip.right && backdropPanelDip_.bottom == panelDip.bottom)
        return;
    backdropApplied_ = true;
    backdropPanelDip_ = panelDip;
    backdropRadiusDip_ = cornerRadiusDip;
    backdropOpacity_ = backdropOpacity;
    backdropBlurRequested_ = blur;
    if (glass) backdropGlass_ = *glass;
    else backdropGlass_.reset();
    const D2D1_RECT_F panelPx{ToPx(panelDip.left), ToPx(panelDip.top), ToPx(panelDip.right), ToPx(panelDip.bottom)};
    const auto kind = !blur ? graphics::BackdropKind::None : glass ? graphics::BackdropKind::HostGlass : graphics::BackdropKind::HostBlur;
    surface_.SetBackdrop(kind, panelPx, ToPx(cornerRadiusDip), backdropOpacity, glass ? *glass : graphics::LiquidGlassTokens{});
}

bool SurfaceWindow::BackdropBlurActive() const noexcept {
    return surface_.EffectiveBackdrop() == graphics::BackdropKind::HostBlur;
}

bool SurfaceWindow::BackdropGlassActive() const noexcept {
    return surface_.EffectiveBackdrop() == graphics::BackdropKind::HostGlass;
}

void SurfaceWindow::Show(bool activate) {
    if (!hwnd_) return;
    if (hiding_ || surface_.IsTransitioning()) {
        hiding_ = false;
        surface_.ResetTransition();
    }
    if (renderPending_) Render(graphics::FrameScheduler::Now());  // avoid showing an empty frame
    ::ShowWindow(hwnd_, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    if (options_.topmost) {
        ::SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    shown_ = true;
}

void SurfaceWindow::Hide() {
    CancelTouch();
    if (!hwnd_) return;
    hiding_ = false;
    ::ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
    // The next Show starts from the resting state, whatever transition ran last.
    surface_.ResetTransition();
}

void SurfaceWindow::ShowAnimated(const graphics::Transition& transition, bool activate) {
    if (!hwnd_) return;
    hiding_ = false;
    // Apply the start state before the window becomes visible, so the first composed frame is not
    // the resting state.
    graphics::Transition start = transition;
    start.opacityTo = transition.opacityFrom;
    start.offsetToX = transition.offsetFromX;
    start.offsetToY = transition.offsetFromY;
    start.scaleTo = transition.scaleFrom;
    start.duration = 0.0;
    if (!transition.IsInstant()) (void)surface_.Animate(start, ToPx(1.0f));
    Show(activate);
    if (auto played = surface_.Animate(transition, ToPx(1.0f)); !played) {
        AETHER_LOG_WARN("ui", "show transition failed: {}", played.error().Describe());
        surface_.ResetTransition();
    }
}

void SurfaceWindow::HideAnimated(const graphics::Transition& transition) {
    CancelTouch();
    if (!hwnd_) return;
    if (!shown_ || transition.IsInstant()) {
        Hide();
        return;
    }
    hiding_ = true;
    auto played = surface_.Animate(transition, ToPx(1.0f), [this] {
        // CompositionSurface drops the completion of a superseded transition; hiding_ also covers a
        // Show() that reset the visual meanwhile.
        if (hiding_) Hide();
    });
    if (!played) {
        AETHER_LOG_WARN("ui", "hide transition failed: {}", played.error().Describe());
        Hide();
    }
}

void SurfaceWindow::Invalidate() {
    if (!hwnd_) return;
    fullDirty_ = true;
    dirtyArea_.reset();
    renderPending_ = true;
    scheduler_.RequestFrame(this);
}

void SurfaceWindow::InvalidateArea(const D2D1_RECT_F& areaDip) {
    if (!hwnd_) return;
    if (!fullDirty_ && areaDip.right > areaDip.left && areaDip.bottom > areaDip.top) {
        if (!dirtyArea_) dirtyArea_ = areaDip;
        else dirtyArea_ = D2D1::RectF(std::min(dirtyArea_->left, areaDip.left), std::min(dirtyArea_->top, areaDip.top),
                                      std::max(dirtyArea_->right, areaDip.right), std::max(dirtyArea_->bottom, areaDip.bottom));
    }
    renderPending_ = true;
    scheduler_.RequestFrame(this);
}

void SurfaceWindow::LimitAnimationFrameTo(const D2D1_RECT_F& areaDip) {
    if (!inAnimate_ || !(areaDip.right > areaDip.left) || !(areaDip.bottom > areaDip.top)) return;
    animationArea_ = animationArea_ ? D2D1::RectF(std::min(animationArea_->left, areaDip.left), std::min(animationArea_->top, areaDip.top),
                                                  std::max(animationArea_->right, areaDip.right),
                                                  std::max(animationArea_->bottom, areaDip.bottom))
                                    : areaDip;
}

void SurfaceWindow::RequestAnimationFrame() {
    if (!hwnd_) return;
    animationPending_ = true;
    scheduler_.RequestFrame(this);
}

void SurfaceWindow::OnFrame(double now) {
    if (!hwnd_) return;
    bool keepAnimating = false;
    if (animationPending_) {
        animationPending_ = false;
        animationArea_.reset();
        inAnimate_ = true;
        try {
            keepAnimating = OnAnimate(now);
        } catch (const std::exception&) {
            AETHER_LOG_ERROR("ui", "animation step threw an exception");
            animationArea_.reset();
        }
        inAnimate_ = false;
        renderPending_ = true;
        if (animationArea_ && !fullDirty_) {
            // D78: this step moved one item only (LimitAnimationFrameTo): the rest of the surface stays as drawn.
            const auto& area = *animationArea_;
            dirtyArea_ = dirtyArea_ ? D2D1::RectF(std::min(dirtyArea_->left, area.left), std::min(dirtyArea_->top, area.top),
                                                  std::max(dirtyArea_->right, area.right), std::max(dirtyArea_->bottom, area.bottom))
                                    : area;
        } else {
            fullDirty_ = true;   // an animation may move anything
        }
        animationArea_.reset();
    }
    if (renderPending_) Render(now);
    if (keepAnimating) RequestAnimationFrame();
}

void SurfaceWindow::Render(double now) {
    if (!hwnd_ || inRender_) return;
    renderPending_ = false;
    inRender_ = true;
    RenderContext context;
    context.dpi = dpi_;
    context.widthDip = ToDip(static_cast<float>(bounds_.right - bounds_.left));
    context.heightDip = ToDip(static_cast<float>(bounds_.bottom - bounds_.top));
    context.now = now;
    // A partial render keeps the rest of the last whole frame: only while that frame is still what the surface holds.
    const SIZE size = surface_.PixelSize();
    const bool whole = fullDirty_ || !dirtyArea_ || !presented_ || size.cx != drawnSize_.cx || size.cy != drawnSize_.cy ||
                       dpi_ != drawnDpi_ || device_.Generation() != drawnGeneration_;
    const auto area = dirtyArea_;
    fullDirty_ = false;
    dirtyArea_.reset();
    const auto paint = [&](ID2D1DeviceContext* d2d) {
        context.d2d = d2d;
        OnRender(context);
    };
    Result<void> result = Ok();
    if (whole) {
        result = surface_.Draw(dpi_, paint);
        if (result) { drawnSize_ = size; drawnDpi_ = dpi_; drawnGeneration_ = device_.Generation(); }
    } else {
        // A pixel of margin: antialiased edges reach just outside what changed.
        const RECT updatePx{static_cast<LONG>(std::floor(ToPx(area->left))) - 1, static_cast<LONG>(std::floor(ToPx(area->top))) - 1,
                            static_cast<LONG>(std::ceil(ToPx(area->right))) + 1, static_cast<LONG>(std::ceil(ToPx(area->bottom))) + 1};
        result = surface_.Draw(dpi_, updatePx, paint);
    }
    inRender_ = false;
    if (result) presented_ = true;
    if (!result) {
        fullDirty_ = true;   // whatever the surface holds now, the next frame is drawn whole
        if (result.error().code == ErrorCode::Unavailable) {
            // AS4-25: one shared, bounded recovery for every surface; it retries on its own timer,
            // so an idle surface is redrawn without waiting for unrelated input.
            device_.RequestRecovery();
            // DeviceReplaced invalidates every window, including this one.
        } else {
            AETHER_LOG_ERROR("ui", "render failed: {}", result.error().Describe());
        }
    }
}

void SurfaceWindow::OnDpiChanged(UINT /*newDpi*/, const RECT& suggestedPx) { SetBoundsPx(suggestedPx); }

D2D1_POINT_2F SurfaceWindow::PointFromLParam(LPARAM lParam) const noexcept {
    return D2D1::Point2F(ToDip(static_cast<float>(GET_X_LPARAM(lParam))), ToDip(static_cast<float>(GET_Y_LPARAM(lParam))));
}

bool SurfaceWindow::RouteTouch(UINT message, WPARAM wParam) {
    if (!hwnd_ || !platform::IsTouchPointer(wParam)) return false;
    const auto contact = platform::ReadPointer(wParam);
    if (!contact) return false;
    POINT client = contact->screenPx;
    ::ScreenToClient(hwnd_, &client);
    const TouchContact touch{contact->id, D2D1::Point2F(ToDip(static_cast<float>(client.x)), ToDip(static_cast<float>(client.y))), contact->time};
    if (message == WM_POINTERDOWN) {
        lastTouchDown_ = ::GetTickCount64();
        if (touchPointer_ && *touchPointer_ != contact->id) {
            (void)OnTouchDown(touch);  // a further finger of the owned gesture
            return true;
        }
        if (!OnTouchDown(touch)) return false;
        touchPointer_ = contact->id;
        return true;
    }
    if (!touchPointer_) return false;
    if (*touchPointer_ != contact->id) return true;
    if (contact->canceled) { CancelTouch(); return true; }
    if (message == WM_POINTERUPDATE) OnTouchMove(touch);
    else { touchPointer_.reset(); OnTouchUp(touch); }
    return true;
}

void SurfaceWindow::CancelTouch() noexcept {
    if (!touchPointer_) return;
    touchPointer_.reset();
    try { OnTouchCancel(); } catch (...) { AETHER_LOG_ERROR("ui", "touch cancellation threw an exception"); }
}

LRESULT SurfaceWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (auto handled = OnMessage(message, wParam, lParam)) return *handled;

    switch (message) {
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
            if (RouteTouch(message, wParam)) return 0;
            break;
        case WM_POINTERCAPTURECHANGED:
            if (touchPointer_ && GET_POINTERID_WPARAM(wParam) == *touchPointer_) CancelTouch();
            break;
        case WM_POINTERACTIVATE:
            if (options_.noActivate) return PA_NOACTIVATE;
            break;
        case WM_CANCELMODE:
            CancelTouch();
            break;
        case WM_NCHITTEST: {
            if (visualRegion_.empty() || inputRegion_.empty()) break;
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (!::ScreenToClient(hwnd_, &point)) break;
            for (const auto& region : inputRegion_) {
                if (::PtInRect(&region, point)) return HTCLIENT;
            }
            return HTTRANSPARENT;
        }
        case WM_MOUSEACTIVATE:
            if (options_.noActivate) return MA_NOACTIVATE;
            break;
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            ::BeginPaint(hwnd_, &ps);
            ::EndPaint(hwnd_, &ps);
            Invalidate();
            return 0;
        }
        case WM_SIZE: {
            const int width = LOWORD(lParam);
            const int height = HIWORD(lParam);
            if (width > 0 && height > 0 && surface_.IsAttached()) {
                if (auto resized = surface_.Resize(width, height); !resized) {
                    AETHER_LOG_WARN("ui", "surface resize failed: {}", resized.error().Describe());
                }
                Invalidate();
            }
            return 0;
        }
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wParam);
            OnDpiChanged(dpi_, *reinterpret_cast<const RECT*>(lParam));
            backdropApplied_ = false;   // the same DIPs are other pixels now
            if (backdropBlurRequested_) SetBackdropPanel(backdropPanelDip_, backdropRadiusDip_, true, backdropOpacity_, backdropGlass_ ? &*backdropGlass_ : nullptr);
            Invalidate();
            return 0;
        }
        case WM_MOUSEMOVE:
            if (!trackingLeave_) {
                TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd_, 0};
                trackingLeave_ = ::TrackMouseEvent(&track) != FALSE;
            }
            OnPointerMove(PointFromLParam(lParam));
            return 0;
        case WM_MOUSELEAVE:
            trackingLeave_ = false;
            OnPointerLeave();
            return 0;
        case WM_LBUTTONDOWN:
            ::SetCapture(hwnd_);
            OnPointerDown(PointerButton::Left, PointFromLParam(lParam));
            return 0;
        case WM_LBUTTONUP:
            if (::GetCapture() == hwnd_) {
                releasingCapture_ = true;
                ::ReleaseCapture();  // sends WM_CAPTURECHANGED synchronously
                releasingCapture_ = false;
            }
            OnPointerUp(PointerButton::Left, PointFromLParam(lParam));
            return 0;
        case WM_CAPTURECHANGED:
            if (!releasingCapture_) OnPointerCaptureLost();
            return 0;
        case WM_RBUTTONDOWN: OnPointerDown(PointerButton::Right, PointFromLParam(lParam)); return 0;
        case WM_RBUTTONUP: OnPointerUp(PointerButton::Right, PointFromLParam(lParam)); return 0;
        case WM_MBUTTONDOWN: OnPointerDown(PointerButton::Middle, PointFromLParam(lParam)); return 0;
        case WM_MBUTTONUP: OnPointerUp(PointerButton::Middle, PointFromLParam(lParam)); return 0;
        case WM_MOUSEWHEEL: {
            POINT screen{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ::ScreenToClient(hwnd_, &screen);
            OnWheel(static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / WHEEL_DELTA,
                    D2D1::Point2F(ToDip(static_cast<float>(screen.x)), ToDip(static_cast<float>(screen.y))));
            return 0;
        }
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (OnKeyDown(static_cast<UINT>(wParam))) return 0;
            break;
        case WM_SETFOCUS: OnFocusChanged(true); return 0;
        case WM_KILLFOCUS: OnFocusChanged(false); return 0;
        case WM_DESTROY:
            CancelTouch();
            OnDestroying();
            scheduler_.Cancel(this);
            surface_.Detach();
            return 0;
        default: break;
    }
    return ::DefWindowProcW(hwnd_, message, wParam, lParam);
}

LRESULT CALLBACK SurfaceWindow::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* self = static_cast<SurfaceWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd;
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<SurfaceWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!self || self->hwnd_ != hwnd) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    try {
        const LRESULT result = self->HandleMessage(message, wParam, lParam);
        if (message == WM_NCDESTROY) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            self->hwnd_ = nullptr;
        }
        return result;
    } catch (const std::exception&) {
        AETHER_LOG_ERROR("ui", "window message {} threw an exception", message);
    } catch (...) {
        AETHER_LOG_ERROR("ui", "window message {} threw a non-standard exception", message);
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace aether::ui
