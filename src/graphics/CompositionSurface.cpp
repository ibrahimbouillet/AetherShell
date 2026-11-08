// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/CompositionSurface.h"

#include "diagnostics/Logger.h"
#include "platform/windows/Win32Error.h"

#include <dwmapi.h>
#include <windows.graphics.effects.interop.h>
#include <winrt/Windows.Graphics.Effects.h>

#include <algorithm>
#include <cmath>

namespace aether::graphics {
namespace {

// DWMWA_USE_HOSTBACKDROPBRUSH (Windows 11): lets a Win32 window use HostBackdropBrush.
constexpr DWORD kDwmUseHostBackdropBrush = 17;

using winrt::Windows::Foundation::Numerics::float2;
using winrt::Windows::Foundation::Numerics::float3;
namespace wge = winrt::Windows::Graphics::Effects;
namespace abi_wge = ABI::Windows::Graphics::Effects;

// D66: Liquid's regular glass as one colour matrix ([r g b a 1] times M): the saturation around each pixel's own
// grey (Rec. 709 weights), then the brightness compressed into the readable band. Alpha passes unchanged.
D2D1_MATRIX_5X4_F GlassMatrix(const LiquidGlassTokens& glass) noexcept {
    constexpr float wr = 0.2126f, wg = 0.7152f, wb = 0.0722f;
    const float s = std::isfinite(glass.saturation) ? std::clamp(glass.saturation, 0.0f, 3.0f) : 1.0f;
    const float k = std::isfinite(glass.scale) ? std::clamp(glass.scale, 0.0f, 1.0f) : 1.0f;
    const float c = std::isfinite(glass.offset) ? std::clamp(glass.offset, 0.0f, 1.0f) : 0.0f;
    D2D1_MATRIX_5X4_F m{};
    m._11 = k * (wr + s * (1.0f - wr)); m._12 = k * wr * (1.0f - s);          m._13 = k * wr * (1.0f - s);
    m._21 = k * wg * (1.0f - s);          m._22 = k * (wg + s * (1.0f - wg)); m._23 = k * wg * (1.0f - s);
    m._31 = k * wb * (1.0f - s);          m._32 = k * wb * (1.0f - s);          m._33 = k * (wb + s * (1.0f - wb));
    m._44 = 1.0f;
    m._51 = c; m._52 = c; m._53 = c;
    return m;
}

// The Direct2D colour-matrix effect, described the way the compositor reads effects (this project has no Win2D).
struct GlassMatrixEffect : winrt::implements<GlassMatrixEffect, wge::IGraphicsEffect, wge::IGraphicsEffectSource, abi_wge::IGraphicsEffectD2D1Interop> {
    GlassMatrixEffect(const D2D1_MATRIX_5X4_F& matrix, wge::IGraphicsEffectSource source) : matrix_(matrix), source_(std::move(source)) {}
    winrt::hstring Name() const { return name_; }
    void Name(const winrt::hstring& name) { name_ = name; }
    HRESULT __stdcall GetEffectId(GUID* id) noexcept override {
        if (!id) return E_POINTER;
        *id = CLSID_D2D1ColorMatrix;
        return S_OK;
    }
    HRESULT __stdcall GetNamedPropertyMapping(LPCWSTR, UINT*, abi_wge::GRAPHICS_EFFECT_PROPERTY_MAPPING*) noexcept override { return E_INVALIDARG; }
    HRESULT __stdcall GetPropertyCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        *count = 3;
        return S_OK;
    }
    HRESULT __stdcall GetProperty(UINT index, ABI::Windows::Foundation::IPropertyValue** value) noexcept override try {
        if (!value) return E_POINTER;
        *value = nullptr;
        winrt::Windows::Foundation::IInspectable boxed{nullptr};
        switch (index) {
            case D2D1_COLORMATRIX_PROP_COLOR_MATRIX: {
                const auto* first = reinterpret_cast<const float*>(&matrix_);
                boxed = winrt::Windows::Foundation::PropertyValue::CreateSingleArray(winrt::array_view<const float>(first, first + 20));
                break;
            }
            case D2D1_COLORMATRIX_PROP_ALPHA_MODE:
                boxed = winrt::Windows::Foundation::PropertyValue::CreateUInt32(D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED);
                break;
            case D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT: boxed = winrt::Windows::Foundation::PropertyValue::CreateBoolean(true); break;
            default: return E_INVALIDARG;
        }
        *value = boxed.as<ABI::Windows::Foundation::IPropertyValue>().detach();
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
    HRESULT __stdcall GetSource(UINT index, abi_wge::IGraphicsEffectSource** source) noexcept override try {
        if (!source) return E_POINTER;
        *source = nullptr;
        if (index != 0 || !source_) return E_INVALIDARG;
        *source = source_.as<abi_wge::IGraphicsEffectSource>().detach();
        return S_OK;
    } catch (...) {
        return winrt::to_hresult();
    }
    HRESULT __stdcall GetSourceCount(UINT* count) noexcept override {
        if (!count) return E_POINTER;
        *count = 1;
        return S_OK;
    }

private:
    D2D1_MATRIX_5X4_F matrix_{};
    wge::IGraphicsEffectSource source_{nullptr};
    winrt::hstring name_;
};

}  // namespace

CompositionSurface::~CompositionSurface() {
    *alive_ = false;
    Detach();
}

Result<void> CompositionSurface::AnimateEntrance(std::chrono::milliseconds duration, float offsetPx) {
    if (!root_ || !device_) return MakeError(ErrorCode::InvalidState, "surface not attached");
    if (duration.count() < 0 || duration > std::chrono::seconds(10) || !std::isfinite(offsetPx))
        return MakeError(ErrorCode::InvalidArgument, "invalid entrance animation");
    Transition transition;
    transition.opacityFrom = duration.count() == 0 ? 1.0f : 0.0f;
    transition.offsetFromY = duration.count() == 0 ? 0.0f : offsetPx;
    transition.duration = static_cast<Seconds>(duration.count()) / 1000.0;
    transition.curve = CurveTokens{}.decelerate;
    return Animate(transition, 1.0f);
}

Result<void> CompositionSurface::AnimateExit(std::chrono::milliseconds duration, float offsetPx) {
    if (!root_ || !device_) return MakeError(ErrorCode::InvalidState, "surface not attached");
    if (duration.count() < 0 || duration > std::chrono::seconds(10) || !std::isfinite(offsetPx))
        return MakeError(ErrorCode::InvalidArgument, "invalid exit animation");
    Transition transition;
    transition.opacityTo = 0.0f;
    transition.offsetToY = offsetPx;
    transition.duration = static_cast<Seconds>(duration.count()) / 1000.0;
    transition.curve = CurveTokens{}.accelerate;
    return Animate(transition, 1.0f);
}

Result<void> CompositionSurface::Animate(const Transition& transition, float pxPerDip, std::function<void()> completed) {
    if (!root_ || !device_) return MakeError(ErrorCode::InvalidState, "surface not attached");
    const float values[] = {transition.opacityFrom, transition.opacityTo, transition.offsetFromX, transition.offsetFromY,
                            transition.offsetToX,   transition.offsetToY,   transition.scaleFrom,   transition.scaleTo,
                            transition.centerX,     transition.centerY,     pxPerDip};
    for (const float value : values) {
        if (!std::isfinite(value)) return MakeError(ErrorCode::InvalidArgument, "invalid transition");
    }
    if (pxPerDip <= 0.0f || transition.scaleFrom <= 0.0f || transition.scaleTo <= 0.0f)
        return MakeError(ErrorCode::InvalidArgument, "invalid transition");
    const auto clampOpacity = [](float value) { return std::clamp(value, 0.0f, 1.0f); };
    const float3 offsetFrom{transition.offsetFromX * pxPerDip, transition.offsetFromY * pxPerDip, 0.0f};
    const float3 offsetTo{transition.offsetToX * pxPerDip, transition.offsetToY * pxPerDip, 0.0f};
    const float3 scaleFrom{transition.scaleFrom, transition.scaleFrom, 1.0f};
    const float3 scaleTo{transition.scaleTo, transition.scaleTo, 1.0f};
    const std::uint64_t generation = ++transitionGeneration_;
    try {
        RevokeTransitionCompletion();  // a superseded transition never reports completion
        transitionPending_ = false;
        root_.StopAnimation(L"Opacity");
        root_.StopAnimation(L"Offset");
        root_.StopAnimation(L"Scale");
        root_.CenterPoint(float3{transition.centerX * pxPerDip, transition.centerY * pxPerDip, 0.0f});
        if (transition.IsInstant()) {
            root_.Opacity(clampOpacity(transition.opacityTo));
            root_.Offset(offsetTo);
            root_.Scale(scaleTo);
        } else {
            const auto& compositor = device_->Compositor();
            const auto& curve = transition.curve;
            const auto easing = compositor.CreateCubicBezierEasingFunction(
                float2{std::clamp(curve.x1, 0.0f, 1.0f), curve.y1}, float2{std::clamp(curve.x2, 0.0f, 1.0f), curve.y2});
            const auto duration = std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
                std::chrono::duration<double>(transition.duration));
            auto batch = compositor.CreateScopedBatch(wuc::CompositionBatchTypes::Animation);
            auto fade = compositor.CreateScalarKeyFrameAnimation();
            fade.InsertKeyFrame(0.0f, clampOpacity(transition.opacityFrom));
            fade.InsertKeyFrame(1.0f, clampOpacity(transition.opacityTo), easing);
            fade.Duration(duration);
            auto move = compositor.CreateVector3KeyFrameAnimation();
            move.InsertKeyFrame(0.0f, offsetFrom);
            move.InsertKeyFrame(1.0f, offsetTo, easing);
            move.Duration(duration);
            auto scale = compositor.CreateVector3KeyFrameAnimation();
            scale.InsertKeyFrame(0.0f, scaleFrom);
            scale.InsertKeyFrame(1.0f, scaleTo, easing);
            scale.Duration(duration);
            // Set the end state first, so it stays when the animations finish or are stopped.
            root_.Opacity(clampOpacity(transition.opacityTo));
            root_.Offset(offsetTo);
            root_.Scale(scaleTo);
            root_.StartAnimation(L"Opacity", fade);
            root_.StartAnimation(L"Offset", move);
            root_.StartAnimation(L"Scale", scale);
            transitionPending_ = true;
            transitionBatch_ = batch;
            batchCompleted_ = batch.Completed(
                [this, generation, weak = std::weak_ptr<bool>(alive_), done = std::move(completed)](const auto&, const auto&) mutable {
                    const auto lifetime = weak.lock();
                    if (!lifetime || !*lifetime) return;  // an already-queued callback may outlive revocation
                    if (generation != transitionGeneration_) return;
                    // Revoking may release this delegate and destroy its captures. Keep the callback
                    // on the stack before removing the subscription, and never touch captures after.
                    auto callback = std::move(done);
                    transitionPending_ = false;
                    RevokeTransitionCompletion();
                    try {
                        if (callback) callback();
                    } catch (...) {
                        AETHER_LOG_ERROR("graphics", "window transition completion threw");
                    }
                });
            batch.End();
            return Ok();
        }
    } catch (const winrt::hresult_error& error) {
        ResetTransition();
        return platform::ErrorFromHResult(error.code(), "window transition");
    } catch (...) {
        ResetTransition();
        return MakeError(ErrorCode::Platform, "window transition unavailable");
    }
    try {
        if (completed) completed();
    } catch (...) {
        return MakeError(ErrorCode::Internal, "window transition completion threw");
    }
    return Ok();
}

void CompositionSurface::ResetTransition() noexcept {
    ++transitionGeneration_;
    transitionPending_ = false;
    try {
        RevokeTransitionCompletion();
        if (!root_) return;
        root_.StopAnimation(L"Opacity");
        root_.StopAnimation(L"Offset");
        root_.StopAnimation(L"Scale");
        root_.Opacity(1.0f);
        root_.Offset(float3{0, 0, 0});
        root_.Scale(float3{1, 1, 1});
    } catch (...) {
    }
}

void CompositionSurface::RevokeTransitionCompletion() noexcept {
    try {
        if (transitionBatch_ && batchCompleted_.value != 0) transitionBatch_.Completed(batchCompleted_);
    } catch (...) {
    }
    batchCompleted_ = {};
    transitionBatch_ = nullptr;
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
    ++transitionGeneration_;
    transitionPending_ = false;
    try {
        RevokeTransitionCompletion();
    } catch (...) {
    }
    try {
        if (target_) target_.Root(nullptr);
    } catch (...) {
    }
    hostBackdropBrush_ = nullptr;
    glassBrush_ = nullptr;
    glassRefused_ = false;
    glassShown_ = false;
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
    // AS4-24: the cached size described the released drawing surface. A later Attach creates a new
    // 1x1 surface, which the next Resize must size even when the requested size is unchanged.
    widthPx_ = 0;
    heightPx_ = 0;
}

SIZE CompositionSurface::DrawingSizePx() const noexcept {
    try {
        if (!surface_) return SIZE{};
        const auto size = surface_.SizeInt32();
        return SIZE{size.Width, size.Height};
    } catch (...) { return SIZE{}; }
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

BackdropKind CompositionSurface::SetBackdrop(BackdropKind kind, const D2D1_RECT_F& panelPx, float cornerRadiusPx,
                                             float opacity, const LiquidGlassTokens& glass) {
    if (!device_ || !backdrop_) return BackdropKind::None;
    if (kind != BackdropKind::None && !device_->Capabilities().hostBackdrop) kind = BackdropKind::None;
    try {
        if (kind != BackdropKind::None) {
            if (!hostBackdropBrush_) {
                const BOOL enable = TRUE;
                if (FAILED(::DwmSetWindowAttribute(hwnd_, kDwmUseHostBackdropBrush, &enable, sizeof(enable)))) {
                    AETHER_LOG_WARN("graphics", "DWMWA_USE_HOSTBACKDROPBRUSH rejected; backdrop blur disabled");
                    kind = BackdropKind::None;
                } else {
                    hostBackdropBrush_ = device_->Compositor().CreateHostBackdropBrush();
                    backdrop_.Brush(hostBackdropBrush_);
                    glassShown_ = false;
                }
            }
        }
        if (kind == BackdropKind::HostGlass) {
            // D66: built once per band (a theme change), then only shown or not.
            if ((!glassBrush_ || !(glassTokens_ == glass)) && !glassRefused_) {
                try {
                    auto effect = winrt::make<GlassMatrixEffect>(GlassMatrix(glass), wuc::CompositionEffectSourceParameter(L"backdrop"));
                    auto brush = device_->Compositor().CreateEffectFactory(effect).CreateBrush();
                    brush.SetSourceParameter(L"backdrop", hostBackdropBrush_);
                    glassBrush_ = brush;
                    glassTokens_ = glass;
                    glassShown_ = false;
                } catch (const winrt::hresult_error& error) {
                    AETHER_LOG_WARN("graphics", "Liquid glass effect refused (0x{:08X}); the blurred backdrop instead",
                                    static_cast<unsigned>(error.code().value));
                    glassRefused_ = true;
                    glassBrush_ = nullptr;
                }
            }
            if (!glassBrush_) kind = BackdropKind::HostBlur;
        }
        if (kind != BackdropKind::None) {
            const bool showGlass = kind == BackdropKind::HostGlass;
            if (showGlass != glassShown_) {
                if (showGlass) backdrop_.Brush(glassBrush_);
                else backdrop_.Brush(hostBackdropBrush_);
                glassShown_ = showGlass;
            }
            const float width = std::max(0.0f, panelPx.right - panelPx.left);
            const float height = std::max(0.0f, panelPx.bottom - panelPx.top);
            backdrop_.Offset(float3{panelPx.left, panelPx.top, 0.0f});
            backdrop_.Size(float2{width, height});
            // The design tokens default to a fully opaque frosted layer (glass.backdropOpacity = 1): a
            // partly transparent one let the sharp desktop show through and ghost under the blur.
            backdrop_.Opacity(std::isfinite(opacity) ? std::clamp(opacity, 0.0f, 1.0f) : 1.0f);
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
