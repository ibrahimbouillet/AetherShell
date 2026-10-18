// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "graphics/GlassPainter.h"

#include "graphics/WinRT.h"

#include <algorithm>
#include <cwctype>

namespace aether::graphics {
namespace {

winrt::com_ptr<ID2D1SolidColorBrush> Solid(ID2D1DeviceContext* context, const Color& color) {
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    context->CreateSolidColorBrush(color.ToD2D(), brush.put());
    return brush;
}

winrt::com_ptr<ID2D1LinearGradientBrush> Vertical(ID2D1DeviceContext* context, float top, float bottom,
                                                  const Color& from, const Color& to) {
    const D2D1_GRADIENT_STOP stops[] = {{0.0f, from.ToD2D()}, {1.0f, to.ToD2D()}};
    winrt::com_ptr<ID2D1GradientStopCollection> collection;
    if (FAILED(context->CreateGradientStopCollection(stops, 2, collection.put()))) return nullptr;
    winrt::com_ptr<ID2D1LinearGradientBrush> brush;
    context->CreateLinearGradientBrush(
        D2D1::LinearGradientBrushProperties(D2D1::Point2F(0.0f, top), D2D1::Point2F(0.0f, bottom)),
        collection.get(), brush.put());
    return brush;
}

void PaintShadow(ID2D1DeviceContext* context, const D2D1_ROUNDED_RECT& shape, const GlassTokens& glass) {
    if (glass.shadow.a <= 0.0f) return;
    winrt::com_ptr<ID2D1CommandList> list;
    if (FAILED(context->CreateCommandList(list.put()))) return;

    // Record the silhouette in local coordinates, then draw the blurred result with the caller's
    // transform (which already includes the surface offset).
    winrt::com_ptr<ID2D1Image> previousTarget;
    context->GetTarget(previousTarget.put());
    D2D1_MATRIX_3X2_F previousTransform{};
    context->GetTransform(&previousTransform);
    context->SetTarget(list.get());
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    if (auto black = Solid(context, Color{0, 0, 0, 1})) context->FillRoundedRectangle(shape, black.get());
    context->SetTarget(previousTarget.get());
    context->SetTransform(previousTransform);
    if (FAILED(list->Close())) return;

    winrt::com_ptr<ID2D1Effect> shadow;
    if (FAILED(context->CreateEffect(CLSID_D2D1Shadow, shadow.put()))) return;
    shadow->SetInput(0, list.get());
    shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, glass.shadowBlur / 2.0f);
    shadow->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(glass.shadow.r, glass.shadow.g, glass.shadow.b, glass.shadow.a));
    context->DrawImage(shadow.get(), D2D1::Point2F(0.0f, glass.shadowOffsetY));
}

}  // namespace

float GlassTintAlpha(const DesignTokens& tokens, const GlassStyle& style) noexcept {
    const float opacity = std::clamp(style.opacity, 0.3f, 1.0f);
    if (tokens.mode == ThemeMode::HighContrast) return 1.0f;
    if (style.backdropBlur) {
        // 0.3 -> very clear glass, 1.0 -> nearly solid. 0.8 (default) maps to the token value.
        return std::clamp(tokens.glass.tintAlphaWithBlur * opacity / 0.8f, 0.12f, 1.0f);
    }
    // Without blur, keep a floor that preserves text contrast on busy wallpapers.
    return std::clamp(tokens.glass.tintAlphaWithoutBlur * (0.75f + 0.25f * opacity / 0.8f), 0.72f, 1.0f);
}

void PaintGlassPanel(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, const DesignTokens& tokens,
                     const GlassStyle& style) {
    const D2D1_ROUNDED_RECT shape{rect, style.cornerRadius, style.cornerRadius};
    if (style.shadow && tokens.mode != ThemeMode::HighContrast) PaintShadow(context, shape, tokens.glass);

    if (auto tint = Solid(context, tokens.glass.tint.WithAlpha(GlassTintAlpha(tokens, style)))) {
        context->FillRoundedRectangle(shape, tint.get());
    }

    if (tokens.mode != ThemeMode::HighContrast) {
        const float height = rect.bottom - rect.top;
        if (auto sheen = Vertical(context, rect.top, rect.top + height * tokens.glass.highlightFraction,
                                  tokens.glass.highlight, tokens.glass.highlight.WithAlpha(0.0f))) {
            context->FillRoundedRectangle(shape, sheen.get());
        }
    }

    // Hairline border inset by half a DIP so it stays inside the panel.
    const D2D1_ROUNDED_RECT borderShape{
        D2D1::RectF(rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f),
        std::max(0.0f, style.cornerRadius - 0.5f), std::max(0.0f, style.cornerRadius - 0.5f)};
    const float borderWidth = tokens.mode == ThemeMode::HighContrast ? 2.0f : 1.0f;
    if (auto border = Vertical(context, rect.top, rect.bottom, tokens.glass.borderTop, tokens.glass.borderBottom)) {
        context->DrawRoundedRectangle(borderShape, border.get(), borderWidth);
    }
}

void PaintFocusRing(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius,
                    const DesignTokens& tokens) {
    const float inset = -(tokens.focusRingWidth + 1.0f);
    const D2D1_ROUNDED_RECT ring{
        D2D1::RectF(rect.left + inset, rect.top + inset, rect.right - inset, rect.bottom - inset),
        cornerRadius - inset, cornerRadius - inset};
    if (auto brush = Solid(context, tokens.focusRing)) context->DrawRoundedRectangle(ring, brush.get(), tokens.focusRingWidth);
}

void PaintFallbackIcon(ID2D1DeviceContext* context, IDWriteFactory* dwrite, const D2D1_RECT_F& rect,
                       wchar_t initial, const DesignTokens& tokens) {
    const float size = std::min(rect.right - rect.left, rect.bottom - rect.top);
    const float radius = size * 0.24f;
    const D2D1_ROUNDED_RECT tile{rect, radius, radius};
    if (auto gradient = Vertical(context, rect.top, rect.bottom, tokens.fallbackIconBackground,
                                 tokens.fallbackIconBackground.WithAlpha(0.8f))) {
        context->FillRoundedRectangle(tile, gradient.get());
    }
    if (!dwrite || initial == 0) return;
    winrt::com_ptr<IDWriteTextFormat> format;
    if (FAILED(dwrite->CreateTextFormat(tokens.displayFontFamily.c_str(), nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size * 0.46f, L"",
                                        format.put()))) {
        return;
    }
    format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    const wchar_t text[2] = {static_cast<wchar_t>(std::towupper(initial)), 0};
    if (auto brush = Solid(context, tokens.textPrimary)) context->DrawTextW(text, 1, format.get(), rect, brush.get());
}

}  // namespace aether::graphics
