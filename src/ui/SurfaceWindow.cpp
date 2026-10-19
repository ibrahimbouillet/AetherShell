// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/SurfaceWindow.h"

#include "diagnostics/Logger.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/Handles.h"
#include "platform/windows/Win32Error.h"
#include "platform/windows/WindowUtilities.h"
#include "core/Strings.h"

#include <dwmapi.h>
#include <windowsx.h>

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
        if (backdropBlurRequested_) SetBackdropPanel(backdropPanelDip_, backdropRadiusDip_, true, backdropOpacity_);
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

void SurfaceWindow::ApplyInputRegion() {
    if (!hwnd_) return;
    if (inputRegion_.empty()) {
        ::SetWindowRgn(hwnd_, nullptr, TRUE);
        return;
    }
    platform::UniqueRegion combined(::CreateRectRgn(0, 0, 0, 0));
    for (const RECT& r : inputRegion_) {
        platform::UniqueRegion part(::CreateRectRgn(r.left, r.top, r.right, r.bottom));
        ::CombineRgn(combined.Get(), combined.Get(), part.Get(), RGN_OR);
    }
    // On success the system owns the region.
    if (::SetWindowRgn(hwnd_, combined.Get(), TRUE)) (void)combined.Release();
}

void SurfaceWindow::SetBackdropPanel(const D2D1_RECT_F& panelDip, float cornerRadiusDip, bool blur, float backdropOpacity) {
    backdropPanelDip_ = panelDip;
    backdropRadiusDip_ = cornerRadiusDip;
    backdropOpacity_ = backdropOpacity;
    backdropBlurRequested_ = blur;
    const D2D1_RECT_F panelPx{ToPx(panelDip.left), ToPx(panelDip.top), ToPx(panelDip.right), ToPx(panelDip.bottom)};
    surface_.SetBackdrop(blur ? graphics::BackdropKind::HostBlur : graphics::BackdropKind::None, panelPx,
                         ToPx(cornerRadiusDip), backdropOpacity);
}

bool SurfaceWindow::BackdropBlurActive() const noexcept {
    return surface_.EffectiveBackdrop() == graphics::BackdropKind::HostBlur;
}

void SurfaceWindow::Show(bool activate) {
    if (!hwnd_) return;
    if (hiding_) {
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
    renderPending_ = true;
    scheduler_.RequestFrame(this);
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
        try {
            keepAnimating = OnAnimate(now);
        } catch (const std::exception&) {
            AETHER_LOG_ERROR("ui", "animation step threw an exception");
        }
        renderPending_ = true;
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
    auto result = surface_.Draw(dpi_, [&](ID2D1DeviceContext* d2d) {
        context.d2d = d2d;
        OnRender(context);
    });
    inRender_ = false;
    if (!result) {
        if (result.error().code == ErrorCode::Unavailable) {
            if (auto recovered = device_.HandleDeviceLost(); !recovered) {
                AETHER_LOG_ERROR("ui", "device recovery failed: {}", recovered.error().Describe());
            }
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

LRESULT SurfaceWindow::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (auto handled = OnMessage(message, wParam, lParam)) return *handled;

    switch (message) {
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
            if (backdropBlurRequested_) SetBackdropPanel(backdropPanelDip_, backdropRadiusDip_, true, backdropOpacity_);
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
