// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure Direct2D painters of the form material and its control visuals (module-private). They draw
// one visual into a rectangle given in DIPs of the caller's coordinate space and never touch a
// window, so FormSurface can render any region of its scene with them and tests can render them
// headless. Colors come from DesignTokens::form and the accent roles; geometry from the 4-DIP
// rhythm metrics. No directional lighting: fills are flat, rims are uniform hairlines.
#pragma once

#include "graphics/Glyphs.h"
#include "graphics/TextCache.h"
#include "graphics/Theme.h"
#include "ui/form/FormSurface.h"

#include <d2d1_1.h>

#include <optional>
#include <string>
#include <string_view>

namespace aether::ui::form::paint {

// Animated and discrete state of one control visual. Amounts are 0..1.
struct Visual {
    float hover = 0.0f;
    float pressed = 0.0f;
    bool enabled = true;
    bool focusRing = false;     // draw the two-tone keyboard focus ring
    bool error = false;         // critical stroke (fields, dropdowns)
    bool showMnemonic = false;  // underline the access key (keyboard cues shown)
};

// A label with its access-key marker removed: "&Reset" -> "Reset" with index 0; "&&" -> "&".
struct Mnemonic {
    std::wstring text;
    int index = -1;  // UTF-16 index of the underlined character, -1 when there is none
};
[[nodiscard]] Mnemonic ParseMnemonic(std::wstring_view label, bool prefixes = true);

[[nodiscard]] graphics::Color Mix(const graphics::Color& a, const graphics::Color& b, float t) noexcept;
// `top` over `bottom`, both possibly translucent (premultiplied-correct source-over).
[[nodiscard]] graphics::Color Over(const graphics::Color& top, const graphics::Color& bottom) noexcept;

// Text style and color of a label role (sizes include tokens.textScale).
[[nodiscard]] graphics::TextStyle StyleFor(const graphics::DesignTokens& tokens, TextRole role, bool wrap,
                                           bool trailing = false) noexcept;
[[nodiscard]] graphics::Color TextColorFor(const graphics::DesignTokens& tokens, TextRole role, bool enabled) noexcept;
// Body text style used by buttons, rows, fields and lists.
[[nodiscard]] graphics::TextStyle BodyStyle(const graphics::DesignTokens& tokens, bool semibold = false) noexcept;

// Draws `text` (already stripped of '&') into `rect`, optionally underlining one character.
void DrawText(ID2D1DeviceContext* context, graphics::TextCache& text, std::wstring_view value, const D2D1_RECT_F& rect,
              const graphics::TextStyle& style, ID2D1Brush* brush, int underlineIndex = -1);

// --- Material ---------------------------------------------------------------------------------
// Window background over `area` (DIPs of the form client): Acrylic = the translucent window tint
// and grain over the DWM backdrop; Solid = opaque windowSolid (grain only with transparency on).
// The grain brush is anchored to the coordinate origin, i.e. the form client origin.
// `frame` (D45, a maximized window): the whole client as a rounded body - outside it stays transparent,
// so the scene behind the window shows in the corners - with the thin windowRim along its edge.
void PaintBackground(ID2D1DeviceContext* context, const D2D1_RECT_F& area, const graphics::DesignTokens& tokens,
                     FormMaterial material, const D2D1_RECT_F* frame = nullptr);
// Corner radius of a form window's own frame (DIPs), matching Windows 11 windows.
inline constexpr float kWindowFrameRadius = 8.0f;
// A rounded card with a uniform hairline (`pixel` = one device pixel in DIPs).
void PaintCard(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, const graphics::DesignTokens& tokens,
               FormMaterial material, bool emphasis, float pixel);
void PaintDivider(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, const graphics::DesignTokens& tokens);

// --- Controls ---------------------------------------------------------------------------------
struct ButtonColors {
    graphics::Color fill;
    graphics::Color stroke;
    graphics::Color text;
};
[[nodiscard]] ButtonColors ButtonPalette(const graphics::DesignTokens& tokens, ButtonStyle style, const Visual& visual) noexcept;
// `rect` is the visual frame (without the focus margin).
void PaintButton(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& rect, std::wstring_view label,
                 std::optional<graphics::Glyph> glyph, ButtonStyle style, const Visual& visual,
                 const graphics::DesignTokens& tokens, bool multiline = false);

struct SwitchColors {
    graphics::Color trackFill;
    graphics::Color trackStroke;
    graphics::Color knob;
};
// `on` is the animated amount 0 (off) .. 1 (on).
[[nodiscard]] SwitchColors SwitchPalette(const graphics::DesignTokens& tokens, float on, const Visual& visual) noexcept;
// The switch track inside a row frame: trailing, vertically centred, inset by cardPaddingX.
[[nodiscard]] D2D1_RECT_F SwitchTrackRect(const D2D1_RECT_F& row, const graphics::DesignTokens& tokens) noexcept;
// Knob rectangle for an animated position (fully rounded; it grows on hover, stretches while pressed).
[[nodiscard]] D2D1_RECT_F SwitchKnobRect(const D2D1_RECT_F& track, const graphics::DesignTokens& tokens, float on,
                                         const Visual& visual) noexcept;
void PaintSwitch(ID2D1DeviceContext* context, const D2D1_RECT_F& track, float on, const Visual& visual,
                 const graphics::DesignTokens& tokens);
void PaintSwitchRow(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& row, std::wstring_view label,
                    std::wstring_view description, float on, const Visual& visual, const graphics::DesignTokens& tokens);

// Check box (3-state aware: `state` 0 off, 1 on, 2 indeterminate) with label and description. The
// box sits at the row's leading edge, centred on the label line at `labelCenterY`.
[[nodiscard]] D2D1_RECT_F CheckBoxRect(const D2D1_RECT_F& row, const graphics::DesignTokens& tokens, float labelCenterY) noexcept;
void PaintCheckRow(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& row, std::wstring_view label,
                   std::wstring_view description, int state, const Visual& visual, const graphics::DesignTokens& tokens);

void PaintComboField(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& rect, std::wstring_view value,
                     const Visual& visual, const graphics::DesignTokens& tokens);

// Opaque field well (Field, Document, List): fieldFill (fieldFillDisabled when disabled), hairline
// stroke, critical stroke on error and a 2-DIP accent bottom line while the editor has focus.
// `hover` 0..1 strengthens the rim only: the fill must match the editor's own background.
void PaintFieldWell(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, bool enabled, bool focused, bool error,
                    float pixel, const graphics::DesignTokens& tokens, float hover = 0.0f);

// Slider at the native geometry: `channel` and `thumb` are the trackbar's own rectangles (DIPs).
void PaintSlider(ID2D1DeviceContext* context, const D2D1_RECT_F& channel, const D2D1_RECT_F& thumb, const Visual& visual,
                 const graphics::DesignTokens& tokens);

// Progress: `fraction` 0..1, or nullopt for indeterminate with `phase` 0..1 of the sweep (a
// negative phase draws the static reduced-motion segment).
struct ProgressColors {
    graphics::Color track;
    graphics::Color fill;
};
// Progress has its own opaque track: keep the actual accent, but adapt the track when needed
// so an enabled segment remains distinguishable (3:1) even with a neutral Windows accent.
[[nodiscard]] ProgressColors ProgressPalette(const graphics::DesignTokens& tokens, bool enabled) noexcept;
void PaintProgress(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, std::optional<double> fraction, double phase,
                   bool enabled, const graphics::DesignTokens& tokens);

// Navigation row: hover and selection pills; the indicator is painted separately (it slides).
[[nodiscard]] D2D1_RECT_F NavigationPillRect(const D2D1_RECT_F& row) noexcept;
void PaintNavigationRow(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& row, std::wstring_view label,
                        std::optional<graphics::Glyph> glyph, bool glyphColumn, bool selected, float hover, bool compact,
                        bool enabled, const graphics::DesignTokens& tokens);
[[nodiscard]] D2D1_RECT_F NavigationIndicatorRect(const D2D1_RECT_F& pill, float centerY,
                                                  const graphics::DesignTokens& tokens) noexcept;

// Owner-drawn list row on the opaque field fill (the whole row is painted opaque); `hover` 0..1.
void PaintListRow(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& row, std::wstring_view value,
                  bool selected, float hover, bool focusRing, bool enabled, const graphics::DesignTokens& tokens);

// Label text with an optional leading glyph or status badge; `busyAngle` (degrees) rotates the
// Busy ring.
void PaintLabel(ID2D1DeviceContext* context, graphics::TextCache& text, const D2D1_RECT_F& rect, std::wstring_view value,
                const ControlSpec& spec, bool enabled, bool showMnemonic, float busyAngle,
                const graphics::DesignTokens& tokens);
void PaintBusyRing(ID2D1DeviceContext* context, const D2D1_RECT_F& box, float angleDegrees, const graphics::Color& color);

// Overlay scroll thumb (fully rounded); `hover` 0..1 blends to scrollThumbHover.
void PaintScrollThumb(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float hover, const graphics::DesignTokens& tokens);

// The two-tone ring drawn so that it stays inside `frame` (Switch/Check/Navigation rows).
void PaintInnerFocusRing(ID2D1DeviceContext* context, const D2D1_RECT_F& frame, float cornerRadius,
                         const graphics::DesignTokens& tokens);
// The ring around `rect` limited to `margin` DIPs outside it (Button/ComboBox/Slider windows are
// enlarged by form.focusMargin only): the ring moves inwards when it would not fit.
void PaintFocusRingWithin(ID2D1DeviceContext* context, const D2D1_RECT_F& rect, float cornerRadius, float margin,
                          const graphics::DesignTokens& tokens);
// Space a focus ring occupies outside its target (gap + inner + outer widths), DIPs.
[[nodiscard]] float FocusRingExtent(const graphics::DesignTokens& tokens) noexcept;
// One device pixel in DIPs for the target's DPI (hairlines).
[[nodiscard]] float PixelOf(ID2D1RenderTarget* target) noexcept;

}  // namespace aether::ui::form::paint
