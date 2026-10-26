// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/form/FormPaint.h"

#include "graphics/GlassPainter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace aether::ui::form::paint {
namespace {

using graphics::Color;
using graphics::DesignTokens;

float Width(const D2D1_RECT_F& rect) noexcept { return rect.right - rect.left; }
float Height(const D2D1_RECT_F& rect) noexcept { return rect.bottom - rect.top; }

bool Usable(const D2D1_RECT_F& rect) noexcept {
    return std::isfinite(rect.left) && std::isfinite(rect.top) && std::isfinite(rect.right) &&
           std::isfinite(rect.bottom) && rect.right - rect.left > 0.0f && rect.bottom - rect.top > 0.0f;
}

D2D1_RECT_F Deflate(const D2D1_RECT_F& rect, float dx, float dy) noexcept {
    return D2D1::RectF(rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy);
}

float Clamp01(float value) noexcept { return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f; }

float TextScale(const DesignTokens& tokens) noexcept {
    return std::isfinite(tokens.textScale) ? std::clamp(tokens.textScale, 1.0f, 2.25f) : 1.0f;
}

// One reusable solid brush per painter call; D2D captures the color at each draw call.
class Brush {
public:
    explicit Brush(ID2D1RenderTarget* target) noexcept : target_(target) {}
    ID2D1SolidColorBrush* operator()(const Color& color) {
        if (!brush_) {
            if (FAILED(target_->CreateSolidColorBrush(color.ToD2D(), brush_.put()))) return nullptr;
        } else {
            brush_->SetColor(color.ToD2D());
        }
        return brush_.get();
    }

private:
    ID2D1RenderTarget* target_;
    winrt::com_ptr<ID2D1SolidColorBrush> brush_;
};

void FillRound(ID2D1RenderTarget* target, Brush& brush, const D2D1_RECT_F& rect, float radius, const Color& color) {
    if (!Usable(rect) || !(color.a > 0.0f)) return;
    const float r = std::clamp(radius, 0.0f, std::min(Width(rect), Height(rect)) / 2.0f);
    if (auto* b = brush(color)) target->FillRoundedRectangle(D2D1::RoundedRect(rect, r, r), b);
}

// A stroke kept inside `rect`.
void StrokeRound(ID2D1RenderTarget* target, Brush& brush, const D2D1_RECT_F& rect, float radius, const Color& color,
                 float width) {
    if (!Usable(rect) || !(color.a > 0.0f) || !(width > 0.0f)) return;
    const D2D1_RECT_F inner = Deflate(rect, width / 2.0f, width / 2.0f);
    if (!Usable(inner)) return;
    const float r = std::clamp(radius - width / 2.0f, 0.0f, std::min(Width(inner), Height(inner)) / 2.0f);
    if (auto* b = brush(color)) target->DrawRoundedRectangle(D2D1::RoundedRect(inner, r, r), b, width);
}

graphics::TextStyle WithVertical(graphics::TextStyle style, DWRITE_PARAGRAPH_ALIGNMENT alignment) {
    style.verticalAlignment = alignment;
    return style;
}

Color DisabledOnAccent(const DesignTokens& tokens) noexcept {
    if (tokens.mode == graphics::ThemeMode::HighContrast) return tokens.textDisabled;
    return tokens.mode == graphics::ThemeMode::Dark ? Color::FromHex(0xFFFFFF, 0.53f) : Color::FromHex(0xFFFFFF);
}

struct TextBlock {
    float labelHeight = 0.0f;
    float descriptionHeight = 0.0f;
    float top = 0.0f;
};

// Label (one line) and optional wrapped description, vertically centred in `row` between `left`
// and `right`.
TextBlock LayoutBlock(graphics::TextCache& text, const DesignTokens& tokens, const D2D1_RECT_F& row, float left, float right,
                      std::wstring_view label, std::wstring_view description) {
    TextBlock block;
    const float width = std::max(1.0f, right - left);
    const auto labelStyle = BodyStyle(tokens);
    block.labelHeight = text.Measure(label.empty() ? std::wstring_view(L" ") : label, labelStyle, width).height;
    float total = block.labelHeight;
    if (!description.empty()) {
        block.descriptionHeight = text.Measure(description, StyleFor(tokens, TextRole::Helper, true), width).height;
        total += tokens.form.labelGap + block.descriptionHeight;
    }
    block.top = std::max(row.top, row.top + (Height(row) - total) / 2.0f);
    return block;
}

void DrawBlock(ID2D1DeviceContext* context, graphics::TextCache& text, Brush& brush, const DesignTokens& tokens,
               const TextBlock& block, float left, float right, const D2D1_RECT_F& row, std::wstring_view label,
               std::wstring_view description, const Visual& visual) {
    const Mnemonic parsed = ParseMnemonic(label);
    const auto labelStyle = WithVertical(BodyStyle(tokens), DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    DrawText(context, text, parsed.text, D2D1::RectF(left, block.top, right, block.top + block.labelHeight + 1.0f), labelStyle,
             brush(visual.enabled ? tokens.textPrimary : tokens.textDisabled), visual.showMnemonic ? parsed.index : -1);
    if (description.empty()) return;
    const float top = block.top + block.labelHeight + tokens.form.labelGap;
    DrawText(context, text, description, D2D1::RectF(left, top, right, std::max(top + block.descriptionHeight + 1.0f, row.bottom)),
             StyleFor(tokens, TextRole::Helper, true), brush(visual.enabled ? tokens.textSecondary : tokens.textDisabled));
}

}  // namespace

Mnemonic ParseMnemonic(std::wstring_view label, bool prefixes) {
    Mnemonic parsed;
    parsed.text.reserve(label.size());
    for (std::size_t index = 0; index < label.size(); ++index) {
        const wchar_t ch = label[index];
        if (prefixes && ch == L'&') {
            if (index + 1 >= label.size()) break;  // a lone trailing '&' is not drawn
            const wchar_t next = label[++index];
            if (next != L'&' && parsed.index < 0) parsed.index = static_cast<int>(parsed.text.size());
            parsed.text.push_back(next);
            continue;
        }
        parsed.text.push_back(ch);
    }
    return parsed;
}

Color Mix(const Color& a, const Color& b, float t) noexcept {
    const float k = Clamp01(t);
    return Color{a.r + (b.r - a.r) * k, a.g + (b.g - a.g) * k, a.b + (b.b - a.b) * k, a.a + (b.a - a.a) * k};
}

Color Over(const Color& top, const Color& bottom) noexcept {
    const float ta = Clamp01(top.a);
    const float ba = Clamp01(bottom.a);
    const float a = ta + ba * (1.0f - ta);
    if (a <= 0.0f) return Color{0.0f, 0.0f, 0.0f, 0.0f};
    const auto channel = [&](float t, float b) { return (t * ta + b * ba * (1.0f - ta)) / a; };
    return Color{channel(top.r, bottom.r), channel(top.g, bottom.g), channel(top.b, bottom.b), a};
}

graphics::TextStyle StyleFor(const DesignTokens& tokens, TextRole role, bool wrap, bool trailing) noexcept {
    graphics::TextStyle style;
    const auto& f = tokens.form;
    switch (role) {
        case TextRole::Title: style.size = f.titleSize; style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; style.display = true; break;
        case TextRole::Section: style.size = f.sectionSize; style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case TextRole::Strong: style.size = f.bodySize; style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD; break;
        case TextRole::Helper:
        case TextRole::Caption: style.size = f.helperSize; break;
        case TextRole::Status: style.size = f.statusSize; break;
        case TextRole::Body: default: style.size = f.bodySize; break;
    }
    style.size *= TextScale(tokens);
    style.ellipsis = !wrap;
    style.alignment = trailing ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING;
    style.verticalAlignment = wrap ? DWRITE_PARAGRAPH_ALIGNMENT_NEAR : DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    style.opticalSmall = !style.display && style.size <= 12.5f;
    return style;
}

Color TextColorFor(const DesignTokens& tokens, TextRole role, bool enabled) noexcept {
    if (!enabled) return tokens.textDisabled;
    return role == TextRole::Helper || role == TextRole::Caption ? tokens.textSecondary : tokens.textPrimary;
}

graphics::TextStyle BodyStyle(const DesignTokens& tokens, bool semibold) noexcept {
    auto style = StyleFor(tokens, semibold ? TextRole::Strong : TextRole::Body, false);
    return style;
}

void DrawText(ID2D1DeviceContext* context, graphics::TextCache& text, std::wstring_view value, const D2D1_RECT_F& rect,
              const graphics::TextStyle& style, ID2D1Brush* brush, int underlineIndex) {
    if (!context || !brush || value.empty() || !Usable(rect)) return;
    if (underlineIndex < 0 || underlineIndex >= static_cast<int>(value.size()) || !text.Factory()) {
        text.Draw(context, value, rect, style, brush);
        return;
    }
    auto* format = text.Format(style);
    if (!format) return;
    winrt::com_ptr<IDWriteTextLayout> layout;
    if (FAILED(text.Factory()->CreateTextLayout(value.data(), static_cast<UINT32>(value.size()), format, Width(rect),
                                                Height(rect), layout.put()))) {
        text.Draw(context, value, rect, style, brush);
        return;
    }
    layout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{static_cast<UINT32>(underlineIndex), 1});
    context->DrawTextLayout(D2D1::Point2F(rect.left, rect.top), layout.get(), brush,
                            D2D1_DRAW_TEXT_OPTIONS_CLIP | D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

float PixelOf(ID2D1RenderTarget* target) noexcept {
    float dpiX = 96.0f;
    float dpiY = 96.0f;
    if (target) target->GetDpi(&dpiX, &dpiY);
    return dpiX > 0.0f ? 96.0f / dpiX : 1.0f;
}

float FocusRingExtent(const DesignTokens& tokens) noexcept {
    return std::max(0.0f, tokens.focus.gap) + std::max(0.0f, tokens.focus.innerWidth) + std::max(0.0f, tokens.focus.outerWidth);
}

void PaintInnerFocusRing(ID2D1DeviceContext* context, const D2D1_RECT_F& frame, float cornerRadius, const DesignTokens& tokens) {
    const float extent = FocusRingExtent(tokens);
    const auto target = Deflate(frame, extent, extent);
    if (!context || !Usable(target)) return;
    graphics::PaintFocusRing(context, target, std::max(0.0f, cornerRadius - extent), tokens);
}

void PaintFocusRingWithin(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius, float margin,
                          const DesignTokens& tokens) {
    const float inset = std::max(0.0f, FocusRingExtent(tokens) - std::max(0.0f, margin));
    const auto target = Deflate(rect, inset, inset);
    if (!context || !Usable(target)) return;
    graphics::PaintFocusRing(context, target, std::max(0.0f, cornerRadius - inset), tokens);
}

void PaintBackground(ID2D1DeviceContext* context, const D2D1_RECT_F& area, const DesignTokens& tokens, FormMaterial material) {
    if (!context || !Usable(area) || material == FormMaterial::Native) return;
    // The glass painter supplies the shared grain; the tint is the form's own window tint.
    DesignTokens materialTokens = tokens;
    graphics::GlassStyle style;
    style.cornerRadius = 0.0f;
    style.opacity = 0.8f;  // maps exactly to tintAlphaWithBlur
    style.backdropBlur = true;
    style.shadow = false;
    style.elevation = graphics::Elevation::None;
    style.neutral = true;
    style.highlight = false;
    style.border = false;
    if (material == FormMaterial::Acrylic) {
        materialTokens.glass.tint = tokens.form.windowTint;
        materialTokens.glass.tintAlphaWithBlur = Clamp01(tokens.form.windowTintAlpha);
        style.noise = true;
    } else {
        materialTokens.glass.tint = tokens.form.windowSolid;
        materialTokens.glass.tintAlphaWithBlur = 1.0f;
        style.noise = tokens.transparencyEnabled;
    }
    graphics::PaintGlassPanel(context, area, materialTokens, style);
}

void PaintCard(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, const DesignTokens& tokens, FormMaterial material,
               bool emphasis, float pixel) {
    if (!context || !Usable(rect)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    Color fill = material == FormMaterial::Acrylic ? f.cardFill : f.cardFillSolid;
    if (emphasis) fill = Over(f.cardFill, fill);
    FillRound(context, brush, rect, f.cardRadius, fill);
    StrokeRound(context, brush, rect, f.cardRadius, f.cardStroke, pixel > 0.0f ? pixel : PixelOf(context));
}

void PaintDivider(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, const DesignTokens& tokens) {
    if (!context || !Usable(rect)) return;
    Brush brush(context);
    if (auto* b = brush(tokens.form.divider)) context->FillRectangle(rect, b);
}

ButtonColors ButtonPalette(const DesignTokens& tokens, ButtonStyle style, const Visual& visual) noexcept {
    const auto& f = tokens.form;
    const auto& accent = tokens.accentRoles;
    ButtonColors colors;
    switch (style) {
        case ButtonStyle::Primary:
            if (!visual.enabled) {
                colors.fill = accent.fillDisabled;
                colors.stroke = accent.fillDisabled.WithAlpha(0.0f);
                colors.text = DisabledOnAccent(tokens);
                return colors;
            }
            colors.fill = Mix(Mix(accent.fill, accent.fillHover, visual.hover), accent.fillPressed, visual.pressed);
            colors.stroke = colors.fill.WithAlpha(0.0f);
            colors.text = Mix(accent.textOnAccent, accent.textOnAccent.WithAlpha(0.8f), visual.pressed);
            return colors;
        case ButtonStyle::Subtle: {
            const Color clear = f.rowHover.WithAlpha(0.0f);
            colors.fill = visual.enabled ? Mix(Mix(clear, f.rowHover, visual.hover), f.rowPressed, visual.pressed) : clear;
            colors.stroke = clear;
            colors.text = visual.enabled ? tokens.textPrimary : tokens.textDisabled;
            return colors;
        }
        case ButtonStyle::Secondary:
        case ButtonStyle::Danger:
        default:
            if (!visual.enabled) {
                colors.fill = f.buttonFill.WithAlpha(f.buttonFill.a * 0.6f);
                colors.stroke = f.buttonStroke.WithAlpha(f.buttonStroke.a * 0.6f);
                colors.text = tokens.textDisabled;
                return colors;
            }
            colors.fill = Mix(Mix(f.buttonFill, f.buttonFillHover, visual.hover), f.buttonFillPressed, visual.pressed);
            colors.stroke = f.buttonStroke;
            colors.text = style == ButtonStyle::Danger ? tokens.critical
                                                       : Mix(tokens.textPrimary, tokens.textSecondary, visual.pressed * 0.7f);
            return colors;
    }
}

void PaintButton(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& rect, std::wstring_view label,
                 std::optional<graphics::Glyph> glyph, ButtonStyle style, const Visual& visual, const DesignTokens& tokens,
                 bool multiline) {
    if (!context || !Usable(rect)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    const auto colors = ButtonPalette(tokens, style, visual);
    const float radius = std::min(f.controlRadius, Height(rect) / 2.0f);
    FillRound(context, brush, rect, radius, colors.fill);
    StrokeRound(context, brush, rect, radius, colors.stroke, PixelOf(context));

    const Mnemonic parsed = ParseMnemonic(label);
    auto textStyle = BodyStyle(tokens);
    textStyle.ellipsis = !multiline;
    const float gap = 8.0f;
    const float glyphSize = glyph ? f.glyphSize : 0.0f;
    const float pad = std::min(f.buttonPaddingX, Width(rect) / 4.0f);
    const float available = std::max(1.0f, Width(rect) - 2.0f * pad);
    const float labelSpace = std::max(1.0f, available - (glyph ? glyphSize + gap : 0.0f));
    const float textWidth = parsed.text.empty() ? 0.0f : text.Measure(parsed.text, textStyle, labelSpace).width;
    const float content = std::min(available, glyphSize + (glyph && textWidth > 0.0f ? gap : 0.0f) + textWidth);
    const float left = rect.left + (Width(rect) - content) / 2.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    if (glyph) {
        graphics::PaintGlyph(context, *glyph, D2D1::RectF(left, cy - glyphSize / 2.0f, left + glyphSize, cy + glyphSize / 2.0f),
                             brush(colors.text), tokens.glyph.stroke);
    }
    if (!parsed.text.empty()) {
        const float textLeft = left + (glyph ? glyphSize + gap : 0.0f);
        // Without a glyph the text is centred in the padded frame; with one it follows the glyph.
        auto drawStyle = textStyle;
        D2D1_RECT_F textRect = D2D1::RectF(textLeft, rect.top, rect.right - pad, rect.bottom);
        if (!glyph) {
            drawStyle.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
            textRect = D2D1::RectF(rect.left + pad, rect.top, rect.right - pad, rect.bottom);
        }
        DrawText(context, text, parsed.text, textRect, drawStyle, brush(colors.text), visual.showMnemonic ? parsed.index : -1);
    }
    if (visual.focusRing) PaintFocusRingWithin(context, rect, radius, f.focusMargin, tokens);
}

SwitchColors SwitchPalette(const DesignTokens& tokens, float on, const Visual& visual) noexcept {
    const auto& f = tokens.form;
    const auto& accent = tokens.accentRoles;
    const float k = Clamp01(on);
    const Color offStroke = visual.enabled ? f.switchOffStroke : tokens.textDisabled;
    const Color offFill = visual.enabled ? Mix(f.buttonFillHover.WithAlpha(0.0f), f.buttonFillHover, visual.hover)
                                         : f.buttonFillHover.WithAlpha(0.0f);
    const Color onFill = visual.enabled ? Mix(Mix(accent.fill, accent.fillHover, visual.hover), accent.fillPressed, visual.pressed)
                                        : accent.fillDisabled;
    const Color knobOn = visual.enabled ? accent.textOnAccent : DisabledOnAccent(tokens);
    SwitchColors colors;
    colors.trackFill = Mix(offFill, onFill, k);
    colors.trackStroke = offStroke.WithAlpha(offStroke.a * (1.0f - k));
    colors.knob = Mix(offStroke, knobOn, k);
    return colors;
}

D2D1_RECT_F SwitchTrackRect(const D2D1_RECT_F& row, const DesignTokens& tokens) noexcept {
    const auto& f = tokens.form;
    const float right = std::max(row.left + f.switchWidth, row.right - f.cardPaddingX);
    const float cy = (row.top + row.bottom) / 2.0f;
    return D2D1::RectF(right - f.switchWidth, cy - f.switchHeight / 2.0f, right, cy + f.switchHeight / 2.0f);
}

D2D1_RECT_F SwitchKnobRect(const D2D1_RECT_F& track, const DesignTokens& tokens, float on, const Visual& visual) noexcept {
    const float height = Height(track);
    const float hover = visual.enabled ? Clamp01(visual.hover) : 0.0f;
    const float pressed = visual.enabled ? Clamp01(visual.pressed) : 0.0f;
    const float diameter = std::min(height - 2.0f, tokens.form.switchKnob * (1.0f + 0.17f * hover));
    const float width = diameter + 4.0f * pressed;
    const float start = track.left + height / 2.0f;
    const float end = track.right - height / 2.0f;
    float cx = start + (end - start) * Clamp01(on);
    // A stretched knob stays inside the track.
    cx = std::clamp(cx, track.left + height / 2.0f + (width - diameter) / 2.0f, track.right - height / 2.0f - (width - diameter) / 2.0f);
    const float cy = (track.top + track.bottom) / 2.0f;
    return D2D1::RectF(cx - width / 2.0f, cy - diameter / 2.0f, cx + width / 2.0f, cy + diameter / 2.0f);
}

void PaintSwitch(ID2D1DeviceContext* context, const D2D1_RECT_F& track, float on, const Visual& visual, const DesignTokens& tokens) {
    if (!context || !Usable(track)) return;
    Brush brush(context);
    const auto colors = SwitchPalette(tokens, on, visual);
    const float radius = Height(track) / 2.0f;
    FillRound(context, brush, track, radius, colors.trackFill);
    StrokeRound(context, brush, track, radius, colors.trackStroke, 1.0f);
    const auto knob = SwitchKnobRect(track, tokens, on, visual);
    FillRound(context, brush, knob, Height(knob) / 2.0f, colors.knob);
}

void PaintSwitchRow(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& row, std::wstring_view label,
                    std::wstring_view description, float on, const Visual& visual, const DesignTokens& tokens) {
    if (!context || !Usable(row)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    if (visual.enabled) {
        const Color clear = f.rowHover.WithAlpha(0.0f);
        FillRound(context, brush, row, f.controlRadius, Mix(Mix(clear, f.rowHover, visual.hover), f.rowPressed, visual.pressed));
    }
    const auto track = SwitchTrackRect(row, tokens);
    PaintSwitch(context, track, on, visual, tokens);
    const float left = row.left + f.cardPaddingX;
    const float right = std::max(left + 1.0f, track.left - f.controlGap);
    const auto block = LayoutBlock(text, tokens, row, left, right, ParseMnemonic(label).text, description);
    DrawBlock(context, text, brush, tokens, block, left, right, row, label, description, visual);
    if (visual.focusRing) PaintInnerFocusRing(context, row, f.controlRadius, tokens);
}

D2D1_RECT_F CheckBoxRect(const D2D1_RECT_F& row, const DesignTokens& tokens, float labelCenterY) noexcept {
    const float size = std::min(tokens.form.checkSize, Height(row));
    const float left = row.left + std::max(0.0f, FocusRingExtent(tokens) - 1.0f);
    return D2D1::RectF(left, labelCenterY - size / 2.0f, left + size, labelCenterY + size / 2.0f);
}

void PaintCheckRow(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& row, std::wstring_view label,
                   std::wstring_view description, int state, const Visual& visual, const DesignTokens& tokens) {
    if (!context || !Usable(row)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    const auto& accent = tokens.accentRoles;
    const float boxLeft = row.left + std::max(0.0f, FocusRingExtent(tokens) - 1.0f);
    const float left = boxLeft + f.checkSize + f.relatedGap;
    const float right = std::max(left + 1.0f, row.right - 2.0f);
    const auto block = LayoutBlock(text, tokens, row, left, right, ParseMnemonic(label).text, description);
    const auto box = CheckBoxRect(row, tokens, block.top + block.labelHeight / 2.0f);
    const float radius = std::min(4.0f, Height(box) / 4.0f);
    if (state == 0) {
        const Color fill = visual.enabled ? Mix(Mix(f.buttonFill, f.buttonFillHover, visual.hover), f.buttonFillPressed, visual.pressed)
                                          : f.buttonFill.WithAlpha(f.buttonFill.a * 0.6f);
        FillRound(context, brush, box, radius, fill);
        StrokeRound(context, brush, box, radius, visual.enabled ? f.switchOffStroke : tokens.textDisabled, 1.0f);
    } else {
        const Color fill = visual.enabled ? Mix(Mix(accent.fill, accent.fillHover, visual.hover), accent.fillPressed, visual.pressed)
                                          : accent.fillDisabled;
        FillRound(context, brush, box, radius, fill);
        const float inset = Width(box) * 0.125f;
        graphics::PaintGlyph(context, state == 2 ? graphics::Glyph::Minus : graphics::Glyph::Check, Deflate(box, inset, inset),
                             brush(visual.enabled ? accent.textOnAccent : DisabledOnAccent(tokens)), 2.0f);
    }
    DrawBlock(context, text, brush, tokens, block, left, right, row, label, description, visual);
    if (visual.focusRing) PaintInnerFocusRing(context, row, f.controlRadius, tokens);
}

void PaintComboField(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& rect, std::wstring_view value,
                     const Visual& visual, const DesignTokens& tokens) {
    if (!context || !Usable(rect)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    const float radius = std::min(f.controlRadius, Height(rect) / 2.0f);
    const Color fill = visual.enabled ? Mix(Mix(f.buttonFill, f.buttonFillHover, visual.hover), f.buttonFillPressed, visual.pressed)
                                      : f.buttonFill.WithAlpha(f.buttonFill.a * 0.6f);
    FillRound(context, brush, rect, radius, fill);
    if (visual.error) {
        StrokeRound(context, brush, rect, radius, tokens.critical, 1.0f);
    } else {
        StrokeRound(context, brush, rect, radius, visual.enabled ? f.buttonStroke : f.buttonStroke.WithAlpha(f.buttonStroke.a * 0.6f),
                    PixelOf(context));
    }
    const float chevron = 12.0f;
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const auto chevronBox = D2D1::RectF(rect.right - f.fieldPaddingX - chevron, cy - chevron / 2.0f, rect.right - f.fieldPaddingX,
                                        cy + chevron / 2.0f);
    graphics::PaintGlyph(context, graphics::Glyph::ChevronDown, chevronBox,
                         brush(visual.enabled ? tokens.textSecondary : tokens.textDisabled), tokens.glyph.stroke);
    if (!value.empty()) {
        DrawText(context, text, value, D2D1::RectF(rect.left + f.fieldPaddingX + 1.0f, rect.top, chevronBox.left - 8.0f, rect.bottom),
                 BodyStyle(tokens), brush(visual.enabled ? tokens.textPrimary : tokens.textDisabled));
    }
    if (visual.focusRing) PaintFocusRingWithin(context, rect, radius, f.focusMargin, tokens);
}

void PaintFieldWell(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, bool enabled, bool focused, bool error, float pixel,
                    const DesignTokens& tokens, float hover) {
    if (!context || !Usable(rect)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    const float radius = std::min(f.fieldRadius, Height(rect) / 2.0f);
    // The fill must stay identical to the editor's own opaque background: hover strengthens the rim.
    FillRound(context, brush, rect, radius, enabled ? f.fieldFill : f.fieldFillDisabled);
    if (error) {
        StrokeRound(context, brush, rect, radius, tokens.critical, 1.0f);
    } else {
        const Color rim = enabled ? Mix(f.fieldStroke, tokens.textSecondary.WithAlpha(0.55f), Clamp01(hover)) : f.fieldStroke;
        StrokeRound(context, brush, rect, radius, rim, pixel > 0.0f ? pixel : PixelOf(context));
    }
    if (focused && enabled) {
        // The accent line follows the rounded bottom corners: the rounded shape clipped to its bottom.
        context->PushAxisAlignedClip(D2D1::RectF(rect.left, rect.bottom - 2.0f, rect.right, rect.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
        FillRound(context, brush, rect, radius, error ? tokens.critical : tokens.accentRoles.fill);
        context->PopAxisAlignedClip();
    }
}

void PaintSlider(ID2D1DeviceContext* context, const D2D1_RECT_F& channel, const D2D1_RECT_F& thumb, const Visual& visual,
                 const DesignTokens& tokens) {
    if (!context || !Usable(channel)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    const float cx = std::clamp((thumb.left + thumb.right) / 2.0f, channel.left, channel.right);
    const float cy = Usable(thumb) ? (thumb.top + thumb.bottom) / 2.0f : (channel.top + channel.bottom) / 2.0f;
    const float h = f.sliderTrackHeight;
    const auto track = D2D1::RectF(channel.left, cy - h / 2.0f, channel.right, cy + h / 2.0f);
    FillRound(context, brush, track, h / 2.0f, visual.enabled ? f.sliderTrack : tokens.textDisabled.WithAlpha(0.4f));
    const Color accent = visual.enabled ? Mix(tokens.accentRoles.fill, tokens.accentRoles.fillHover, visual.hover) : tokens.textDisabled;
    if (cx > channel.left) FillRound(context, brush, D2D1::RectF(channel.left, track.top, cx, track.bottom), h / 2.0f, accent);
    const float d = f.sliderThumbSize;
    const auto ring = D2D1::RectF(cx - d / 2.0f, cy - d / 2.0f, cx + d / 2.0f, cy + d / 2.0f);
    if (auto* b = brush(f.sliderThumb)) context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), d / 2.0f, d / 2.0f), b);
    const float pixel = PixelOf(context);
    if (auto* b = brush(f.fieldStroke))
        context->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), d / 2.0f - pixel / 2.0f, d / 2.0f - pixel / 2.0f), b, pixel);
    const float hover = visual.enabled ? Clamp01(visual.hover) : 0.0f;
    const float pressed = visual.enabled ? Clamp01(visual.pressed) : 0.0f;
    const float dot = std::max(2.0f, d * 0.28f + 1.0f * hover - 1.5f * pressed);
    if (auto* b = brush(accent)) context->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), dot, dot), b);
    if (visual.focusRing) PaintFocusRingWithin(context, ring, d / 2.0f, f.focusMargin, tokens);
}

ProgressColors ProgressPalette(const DesignTokens& tokens, bool enabled) noexcept {
    const Color base = tokens.form.windowSolid.WithAlpha(1.0f);
    ProgressColors colors{
        Over(enabled ? tokens.form.sliderTrack : tokens.textDisabled.WithAlpha(0.3f), base),
        (enabled ? tokens.accentRoles.fill : tokens.textDisabled).WithAlpha(1.0f)};
    if (!enabled || tokens.mode == graphics::ThemeMode::HighContrast || graphics::ContrastRatio(colors.fill, colors.track) >= 3.0f)
        return colors;
    const Color black = Color::FromHex(0x000000);
    const Color white = Color::FromHex(0xFFFFFF);
    const Color endpoint = graphics::ContrastRatio(colors.fill, black) >= graphics::ContrastRatio(colors.fill, white) ? black : white;
    // Leave a small margin for the final eight-bit raster/compositor rounding. The active
    // segment keeps the real accent; neither progress geometry nor its timing changes.
    const Color original = colors.track;
    for (int step = 1; step <= 64; ++step) {
        colors.track = Mix(original, endpoint, static_cast<float>(step) / 64.0f);
        if (graphics::ContrastRatio(colors.fill, colors.track) >= 3.05f) break;
    }
    return colors;
}

void PaintProgress(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, std::optional<double> fraction, double phase, bool enabled,
                   const DesignTokens& tokens) {
    if (!context || !Usable(rect)) return;
    Brush brush(context);
    const float h = std::min(Height(rect), std::max(3.0f, tokens.form.sliderTrackHeight));
    const float cy = (rect.top + rect.bottom) / 2.0f;
    const auto track = D2D1::RectF(rect.left, cy - h / 2.0f, rect.right, cy + h / 2.0f);
    const auto colors = ProgressPalette(tokens, enabled);
    FillRound(context, brush, track, h / 2.0f, colors.track);
    const Color accent = colors.fill;
    const float width = Width(track);
    if (fraction) {
        const float filled = width * Clamp01(static_cast<float>(*fraction));
        if (filled > 0.0f) FillRound(context, brush, D2D1::RectF(track.left, track.top, track.left + filled, track.bottom), h / 2.0f, accent);
        return;
    }
    const float segment = width * 0.4f;
    float start = track.left + (width - segment) / 2.0f;
    if (phase >= 0.0) start = track.left - segment + (width + segment) * static_cast<float>(std::fmod(phase, 1.0));
    context->PushAxisAlignedClip(track, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    FillRound(context, brush, D2D1::RectF(start, track.top, start + segment, track.bottom), h / 2.0f, accent);
    context->PopAxisAlignedClip();
}

D2D1_RECT_F NavigationPillRect(const D2D1_RECT_F& row) noexcept { return Deflate(row, 4.0f, 2.0f); }

D2D1_RECT_F NavigationIndicatorRect(const D2D1_RECT_F& pill, float centerY, const DesignTokens& tokens) noexcept {
    const float w = tokens.form.navIndicatorWidth;
    const float h = std::min(tokens.form.navIndicatorHeight, Height(pill));
    return D2D1::RectF(pill.left, centerY - h / 2.0f, pill.left + w, centerY + h / 2.0f);
}

void PaintNavigationRow(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& row, std::wstring_view label,
                        std::optional<graphics::Glyph> glyph, bool glyphColumn, bool selected, float hover, bool compact,
                        bool enabled, const DesignTokens& tokens) {
    if (!context || !Usable(row)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    const auto pill = NavigationPillRect(row);
    if (selected) FillRound(context, brush, pill, f.controlRadius, f.navSelected);
    if (enabled && hover > 0.0f) FillRound(context, brush, pill, f.controlRadius, f.navHover.WithAlpha(f.navHover.a * Clamp01(hover)));
    const Color color = enabled ? tokens.textPrimary : tokens.textDisabled;
    const float gs = f.glyphSize;
    const float cy = (pill.top + pill.bottom) / 2.0f;
    if (compact) {
        const float cx = (pill.left + pill.right) / 2.0f;
        if (glyph) {
            graphics::PaintGlyph(context, *glyph, D2D1::RectF(cx - gs / 2.0f, cy - gs / 2.0f, cx + gs / 2.0f, cy + gs / 2.0f), brush(color),
                                 tokens.glyph.stroke);
        } else if (!label.empty()) {
            auto style = BodyStyle(tokens, true);
            style.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
            DrawText(context, text, label.substr(0, 1), pill, style, brush(color));
        }
        return;
    }
    float x = pill.left + 12.0f;
    if (glyph) {
        graphics::PaintGlyph(context, *glyph, D2D1::RectF(x, cy - gs / 2.0f, x + gs, cy + gs / 2.0f), brush(color), tokens.glyph.stroke);
    }
    if (glyph || glyphColumn) x += gs + 12.0f;
    DrawText(context, text, label, D2D1::RectF(x, pill.top, pill.right - 8.0f, pill.bottom), BodyStyle(tokens), brush(color));
}

void PaintListRow(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& row, std::wstring_view value,
                  bool selected, float hover, bool focusRing, bool enabled, const DesignTokens& tokens) {
    if (!context || !Usable(row)) return;
    Brush brush(context);
    const auto& f = tokens.form;
    const Color base = enabled ? f.fieldFill.WithAlpha(1.0f) : f.fieldFillDisabled.WithAlpha(1.0f);
    if (auto* b = brush(base)) context->FillRectangle(row, b);
    const auto pill = Deflate(row, 3.0f, 1.0f);
    Color tint = base;
    if (selected) tint = Over(tokens.accentRoles.fill.WithAlpha(tokens.mode == graphics::ThemeMode::Dark ? 0.26f : 0.16f), tint);
    const float amount = enabled ? Clamp01(hover) : 0.0f;
    if (amount > 0.0f) tint = Over(f.rowHover.WithAlpha(f.rowHover.a * amount), tint);
    if (selected || amount > 0.0f) FillRound(context, brush, pill, 4.0f, tint);
    const Color color = enabled ? f.fieldText : tokens.textDisabled;
    DrawText(context, text, value, D2D1::RectF(pill.left + 8.0f, row.top, pill.right - 6.0f, row.bottom), BodyStyle(tokens), brush(color));
    if (focusRing) PaintInnerFocusRing(context, row, 4.0f + FocusRingExtent(tokens), tokens);
}

void PaintBusyRing(ID2D1DeviceContext* context, const D2D1_RECT_F& box, float angleDegrees, const Color& color) {
    if (!context || !Usable(box)) return;
    winrt::com_ptr<ID2D1Factory> factory;
    context->GetFactory(factory.put());
    if (!factory) return;
    const float size = std::min(Width(box), Height(box));
    const float stroke = std::max(1.5f, size / 8.0f);
    const float radius = size / 2.0f - stroke / 2.0f;
    const D2D1_POINT_2F center = D2D1::Point2F((box.left + box.right) / 2.0f, (box.top + box.bottom) / 2.0f);
    Brush brush(context);
    if (auto* b = brush(color.WithAlpha(color.a * 0.22f))) context->DrawEllipse(D2D1::Ellipse(center, radius, radius), b, stroke);
    const float start = angleDegrees * std::numbers::pi_v<float> / 180.0f;
    const float sweep = 1.5f * std::numbers::pi_v<float>;  // a 270-degree arc
    winrt::com_ptr<ID2D1PathGeometry> path;
    winrt::com_ptr<ID2D1GeometrySink> sink;
    if (FAILED(factory->CreatePathGeometry(path.put())) || FAILED(path->Open(sink.put()))) return;
    sink->BeginFigure(D2D1::Point2F(center.x + radius * std::cos(start), center.y + radius * std::sin(start)), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(center.x + radius * std::cos(start + sweep), center.y + radius * std::sin(start + sweep)),
                                  D2D1::SizeF(radius, radius), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    if (FAILED(sink->Close())) return;
    if (auto* b = brush(color)) context->DrawGeometry(path.get(), b, stroke, graphics::RoundStrokeStyle(factory.get()));
}

void PaintLabel(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& rect, std::wstring_view value,
                const ControlSpec& spec, bool enabled, bool showMnemonic, float busyAngle, const DesignTokens& tokens) {
    if (!context || !Usable(rect)) return;
    Brush brush(context);
    const auto style = StyleFor(tokens, spec.text, spec.wrap, spec.trailing);
    const Color color = TextColorFor(tokens, spec.text, enabled);
    const Mnemonic parsed = ParseMnemonic(value);
    const bool badge = spec.text == TextRole::Status && spec.severity != Severity::None;
    const bool lead = badge || spec.glyph.has_value();
    const float box = badge ? std::round(style.size * 1.2f) : tokens.form.glyphSize;
    const float gap = 8.0f;
    float boxLeft = rect.left;
    D2D1_RECT_F textRect = rect;
    if (lead) {
        textRect.left = rect.left + box + gap;
        if (spec.trailing && !parsed.text.empty()) {
            const float w = text.Measure(parsed.text, style, std::max(1.0f, Width(rect) - box - gap)).width;
            boxLeft = std::max(rect.left, rect.right - w - gap - box);
        }
    }
    if (lead) {
        const float lineHeight = text.Measure(L"Ag", style).height;
        const float cy = spec.wrap ? rect.top + std::min(Height(rect), lineHeight) / 2.0f : (rect.top + rect.bottom) / 2.0f;
        const auto leadBox = D2D1::RectF(boxLeft, cy - box / 2.0f, boxLeft + box, cy + box / 2.0f);
        if (badge) {
            switch (spec.severity) {
                case Severity::Info: graphics::PaintStatusBadge(context, graphics::StatusBadge::Info, leadBox, tokens); break;
                case Severity::Success: graphics::PaintStatusBadge(context, graphics::StatusBadge::Success, leadBox, tokens); break;
                case Severity::Warning: graphics::PaintStatusBadge(context, graphics::StatusBadge::Warning, leadBox, tokens); break;
                case Severity::Error: graphics::PaintStatusBadge(context, graphics::StatusBadge::Error, leadBox, tokens); break;
                case Severity::Busy: PaintBusyRing(context, Deflate(leadBox, 1.0f, 1.0f), busyAngle, enabled ? tokens.accentRoles.fill : color); break;
                case Severity::None: break;
            }
        } else if (spec.glyph) {
            graphics::PaintGlyph(context, *spec.glyph, leadBox, brush(color), tokens.glyph.stroke);
        }
    }
    DrawText(context, text, parsed.text, textRect, style, brush(color), showMnemonic ? parsed.index : -1);
}

void PaintScrollThumb(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float hover, const DesignTokens& tokens) {
    if (!context || !Usable(rect)) return;
    Brush brush(context);
    FillRound(context, brush, rect, Width(rect) / 2.0f, Mix(tokens.form.scrollThumb, tokens.form.scrollThumbHover, hover));
}

}  // namespace aether::ui::form::paint
