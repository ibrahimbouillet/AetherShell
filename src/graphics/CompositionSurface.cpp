// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/CompositionSurface.h"

#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>

namespace aether::graphics {
namespace {

// DWMWA_USE_HOSTBACKDROPBRUSH (Windows 11): lets a Win32 window use HostBackdropBrush.
constexpr DWORD kDwmUseHostBackdropBrush = 17;

using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;

}  // namespace

CompositionSurface::~CompositionSurface() { Detach(); }

Result<void> CompositionSurface::AnimateEntrance(std::chrono::milliseconds duration, float offsetPx) {
    if (!root_ || !device_) return MakeError(ErrorCode::InvalidState, "surface not attached");
    if (duration.count() < 0 || duration > std::chrono::seconds(10) || !std::isfinite(offsetPx))
        return MakeError(ErrorCode::InvalidArgument, "invalid entrance animation");
    try {
        root_.StopAnimation(L"Opacity"); root_.StopAnimation(L"Offset");
        root_.Opacity(1.0f); root_.Offset(float3{0, 0, 0});
        if (duration.count() == 0) return Ok();
        const auto& compositor = device_->Compositor();
        const auto easing = compositor.CreateCubicBezierEasingFunction(float2{0.2f, 0.8f}, float2{0.2f, 1.0f});
        auto fade = compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(0, 0); fade.InsertKeyFrame(1, 1, easing); fade.Duration(duration);
        auto rise = compositor.CreateVector3KeyFrameAnimation();
        rise.InsertKeyFrame(0, float3{0, offsetPx, 0}); rise.InsertKeyFrame(1, float3{0, 0, 0}, easing); rise.Duration(duration);
        root_.StartAnimation(L"Opacity", fade); root_.StartAnimation(L"Offset", rise);
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Platform, "entrance animation unavailable"); }
}

Result<void> CompositionSurface::AnimateExit(std::chrono::milliseconds duration, float offsetPx) {
    if (!root_ || !device_) return MakeError(ErrorCode::InvalidState, "surface not attached");
    if (duration.count() < 0 || duration > std::chrono::seconds(10) || !std::isfinite(offsetPx))
        return MakeError(ErrorCode::InvalidArgument, "invalid exit animation");
    try {
        root_.StopAnimation(L"Opacity"); root_.StopAnimation(L"Offset");
        root_.Opacity(0.0f); root_.Offset(float3{0, offsetPx, 0});
        if (duration.count() == 0) return Ok();
        const auto& compositor = device_->Compositor();
        const auto easing = compositor.CreateCubicBezierEasingFunction(float2{0.4f, 0.0f}, float2{1.0f, 1.0f});
        auto fade = compositor.CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(0, 1); fade.InsertKeyFrame(1, 0, easing); fade.Duration(duration);
        auto rise = compositor.CreateVector3KeyFrameAnimation();
        rise.InsertKeyFrame(0, float3{0, 0, 0}); rise.InsertKeyFrame(1, float3{0, offsetPx, 0}, easing); rise.Duration(duration);
        root_.StartAnimation(L"Opacity", fade); root_.StartAnimation(L"Offset", rise);
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Platform, "exit animation unavailable"); }
}

Result<void> CompositionSurface::Attach(GraphicsDevice& device, HWND hwnd) {
    Detach();
    try {
        device_ = &device;
        hwnd_ = hwnd;
        const auto& compositor = device.Compositor();
        const auto interop = compositor.as<abi_wuc::Desktop::ICompositorDesktopInterop>();
        winrt::check_hresult(interop->CreateDesktopWindowTarget(
            hwnd, FALSE, reinterpret_cast<abi_wuc::Desktop::IDesktopWindowTarget**>(winrt::put_abi(target_))));

        root_ = compositor.CreateContainerVisual();
        root_.RelativeSizeAdjustment(float2{1.0f, 1.0f});
        target_.Root(root_);

        backdrop_ = compositor.CreateSpriteVisual();
        backdrop_.IsVisible(false);
        clipGeometry_ = compositor.CreateRoundedRectangleGeometry();
        backdrop_.Clip(compositor.CreateGeometricClip(clipGeometry_));

        surface_ = device.CompositionDevice().CreateDrawingSurface(
            winrt::Windows::Foundation::Size{1.0f, 1.0f},
            winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            winrt::Windows::Graphics::DirectX::DirectXAlphaMode::Premultiplied);
        surfaceBrush_ = compositor.CreateSurfaceBrush(surface_);
        surfaceBrush_.Stretch(wuc::CompositionStretch::None);
        surfaceBrush_.HorizontalAlignmentRatio(0.0f);
        surfaceBrush_.VerticalAlignmentRatio(0.0f);
        content_ = compositor.CreateSpriteVisual();
        content_.Brush(surfaceBrush_);

        root_.Children().InsertAtBottom(backdrop_);
        root_.Children().InsertAtTop(content_);
        return Ok();
    } catch (const winrt::hresult_error& error) {
        Detach();
        return platform::ErrorFromHResult(error.code(), "composition target creation");
    }
}

void CompositionSurface::Detach() noexcept {
    try {
        if (target_) target_.Root(nullptr);
    } catch (...) {
    }
    hostBackdropBrush_ = nullptr;
    clipGeometry_ = nullptr;
    surfaceBrush_ = nullptr;
    surface_ = nullptr;
    content_ = nullptr;
    backdrop_ = nullptr;
    root_ = nullptr;
    target_ = nullptr;
    device_ = nullptr;
    hwnd_ = nullptr;
    backdropKind_ = BackdropKind::None;
}

Result<void> CompositionSurface::Resize(int widthPx, int heightPx) {
    if (!surface_) return MakeError(ErrorCode::InvalidState, "surface not attached");
    widthPx = std::max(1, widthPx);
    heightPx = std::max(1, heightPx);
    if (widthPx == widthPx_ && heightPx == heightPx_) return Ok();
    try {
        const auto interop = surface_.as<abi_wuc::ICompositionDrawingSurfaceInterop>();
        winrt::check_hresult(interop->Resize(SIZE{widthPx, heightPx}));
        content_.Size(float2{static_cast<float>(widthPx), static_cast<float>(heightPx)});
        widthPx_ = widthPx;
        heightPx_ = heightPx;
        return Ok();
    } catch (const winrt::hresult_error& error) {
        return platform::ErrorFromHResult(error.code(), "drawing surface resize");
    }
}

BackdropKind CompositionSurface::SetBackdrop(BackdropKind kind, const D2D1_RECT_F& panelPx, float cornerRadiusPx) {
    if (!device_ || !backdrop_) return BackdropKind::None;
    if (kind == BackdropKind::HostBlur && !device_->Capabilities().hostBackdrop) kind = BackdropKind::None;
    try {
        if (kind == BackdropKind::HostBlur) {
            if (!hostBackdropBrush_) {
                const BOOL enable = TRUE;
                if (FAILED(::DwmSetWindowAttribute(hwnd_, kDwmUseHostBackdropBrush, &enable, sizeof(enable)))) {
                    AETHER_LOG_WARN("graphics", "DWMWA_USE_HOSTBACKDROPBRUSH rejected; backdrop blur disabled");
                    kind = BackdropKind::None;
                } else {
                    hostBackdropBrush_ = device_->Compositor().CreateHostBackdropBrush();
                    backdrop_.Brush(hostBackdropBrush_);
                }
            }
        }
        if (kind == BackdropKind::HostBlur) {
            const float width = std::max(0.0f, panelPx.right - panelPx.left);
            const float height = std::max(0.0f, panelPx.bottom - panelPx.top);
            backdrop_.Offset(float3{panelPx.left, panelPx.top, 0.0f});
            backdrop_.Size(float2{width, height});
            clipGeometry_.Size(float2{width, height});
            clipGeometry_.CornerRadius(float2{cornerRadiusPx, cornerRadiusPx});
            backdrop_.IsVisible(width > 0.0f && height > 0.0f);
        } else {
            backdrop_.IsVisible(false);
        }
    } catch (const winrt::hresult_error& error) {
        AETHER_LOG_WARN("graphics", "backdrop configuration failed (0x{:08X})",
                        static_cast<unsigned>(error.code().value));
        kind = BackdropKind::None;
        try {
            backdrop_.IsVisible(false);
        } catch (...) {
        }
    }
    backdropKind_ = kind;
    return kind;
}

Result<void> CompositionSurface::Draw(UINT dpi, const std::function<void(ID2D1DeviceContext*)>& draw) {
    if (!surface_) return MakeError(ErrorCode::InvalidState, "surface not attached");
    winrt::com_ptr<abi_wuc::ICompositionDrawingSurfaceInterop> interop;
    try {
        interop = surface_.as<abi_wuc::ICompositionDrawingSurfaceInterop>();
    } catch (const winrt::hresult_error& error) {
        return platform::ErrorFromHResult(error.code(), "drawing surface interop");
    }

    winrt::com_ptr<ID2D1DeviceContext> context;
    POINT offset{};
    HRESULT hr = interop->BeginDraw(nullptr, __uuidof(ID2D1DeviceContext), context.put_void(), &offset);
    if (FAILED(hr)) {
        if (GraphicsDevice::IsDeviceLostError(hr)) {
            return MakeError(ErrorCode::Unavailable, "rendering device lost", static_cast<std::uint32_t>(hr));
        }
        return platform::ErrorFromHResult(hr, "BeginDraw");
    }

    const float dpiF = static_cast<float>(dpi ? dpi : 96);
    const float scale = dpiF / 96.0f;
    context->SetDpi(dpiF, dpiF);
    context->SetTransform(D2D1::Matrix3x2F::Translation(static_cast<float>(offset.x) / scale,
                                                        static_cast<float>(offset.y) / scale));
    context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    context->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    context->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
    try {
        draw(context.get());
    } catch (...) {
        interop->EndDraw();
        return MakeError(ErrorCode::Internal, "render callback threw");
    }
    hr = interop->EndDraw();
    if (FAILED(hr)) {
        if (GraphicsDevice::IsDeviceLostError(hr)) {
            return MakeError(ErrorCode::Unavailable, "rendering device lost", static_cast<std::uint32_t>(hr));
        }
        return platform::ErrorFromHResult(hr, "EndDraw");
    }
    return Ok();
}

}  // namespace aether::graphics
