// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// FormSurface: the shared Aether presentation for normal, activatable application windows that keep
// native child controls (Settings, setup, confirmations). It attaches to a top-level window created
// by its owner and gives the whole client area one material:
//
//   * Acrylic  - Windows 11 22H2+ with transparency effects on: DWMWA_SYSTEMBACKDROP_TYPE =
//                DWMSBT_TRANSIENTWINDOW with the frame extended over the client, so the real scene
//                behind the window shows through a genuine blur; the client is painted with the
//                translucent form tint and grain in premultiplied alpha.
//   * Solid    - no supported backdrop (Windows 10, Windows 11 21H2, transparency off): the same
//                layout on the opaque windowSolid fill. An honest no-blur fallback.
//   * Native   - high contrast, or Direct2D unavailable: no custom painting at all. Registered
//                controls keep stock system-colored rendering; owner-drawn labels and lists are drawn
//                with GDI in system colors. This is also the reliable path when the renderer fails.
//
// Painting model. The window keeps WS_CLIPCHILDREN. Every pixel of the client area belongs to one
// scene: the window background (tint and grain, anchored to the client origin), the owner's cards
// and dividers, and the visuals of the registered controls. Whichever window repaints a region -
// the form itself, a registered control, or a container such as a FormScrollView canvas - renders
// that region of the same scene through a buffered-paint DIB and a Direct2D DC render target with
// premultiplied alpha, so child controls blend seamlessly with the glass behind them.
//
// Native input stays native. Push buttons, check boxes, combo boxes, track bars, progress bars and
// list boxes keep their window classes, messages, keyboard behavior and their system UI Automation
// providers; only their painting is replaced. Text editors (EDIT, HOTKEY) and inset lists/documents
// become opaque layered child windows inside a drawn rounded field, so the caret, IME composition,
// selection and clipboard work exactly as in any native edit control.
//
// Threading: UI thread only. Every public function must be called on the thread that owns the window.
#pragma once

#include "core/Result.h"
#include "graphics/Animation.h"
#include "graphics/Glyphs.h"
#include "graphics/Theme.h"

#include <windows.h>

#include <d2d1_1.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::graphics {
class FrameScheduler;
}

namespace aether::ui::form {

enum class FormMaterial { Acrylic, Solid, Native };
[[nodiscard]] std::string_view ToString(FormMaterial material) noexcept;

// Pure material rule (exposed for tests): Native for high contrast or when custom painting is
// unavailable; Acrylic when the system backdrop is supported and transparency is enabled; else Solid.
[[nodiscard]] FormMaterial ResolveFormMaterial(graphics::ThemeMode mode, bool transparencyEnabled,
                                               bool systemBackdropSupported, bool renderer) noexcept;
// True on Windows 11 build 22621 or later (DWMWA_SYSTEMBACKDROP_TYPE).
[[nodiscard]] bool SystemBackdropSupported() noexcept;
// True when two token sets present a form identically (mode, transparency, text scale, families,
// accent and material colors). SetTheme with the same appearance and motion does nothing.
[[nodiscard]] bool SameAppearance(const graphics::DesignTokens& a, const graphics::DesignTokens& b) noexcept;

enum class ControlRole {
    Label,       // STATIC with SS_OWNERDRAW. Text from the window text, style from TextRole.
    Button,      // BUTTON BS_PUSHBUTTON / BS_DEFPUSHBUTTON, painted per ButtonStyle.
    Switch,      // BUTTON BS_AUTOCHECKBOX spanning a whole settings row: label (+ description) on the
                 // leading side, a switch on the trailing side. The row itself is the hit target.
    Check,       // BUTTON BS_AUTOCHECKBOX painted as a rounded check box followed by its label.
    ComboBox,    // COMBOBOX CBS_DROPDOWNLIST painted as a closed dropdown field with a chevron.
    Slider,      // msctls_trackbar32 (TBS_HORZ | TBS_NOTICKS) painted as a slim track and round thumb.
    Field,       // EDIT (single line) or HOTKEY: opaque layered child inset in a drawn rounded field.
    Document,    // EDIT ES_MULTILINE (usually read-only): opaque layered child inset in a drawn well.
    List,        // LISTBOX LBS_OWNERDRAWFIXED | LBS_HASSTRINGS: opaque layered child in a drawn well;
                 // rows are painted by the surface.
    Navigation,  // LISTBOX LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT on the glass:
                 // glyph + label rows, hover and selection pills and a sliding accent indicator.
    Progress,    // msctls_progress32; PBS_MARQUEE (or SetProgress(nullopt)) means indeterminate.
    Container,   // a plain child window that only paints the scene behind its own children
                 // (FormScrollView's viewport and canvas register themselves with this role).
};

enum class TextRole { Title, Section, Body, Strong, Helper, Status, Caption };
enum class ButtonStyle { Primary, Secondary, Subtle, Danger };
enum class Severity { None, Info, Success, Warning, Error, Busy };

struct ControlSpec {
    ControlRole role = ControlRole::Label;
    TextRole text = TextRole::Body;           // Label
    ButtonStyle button = ButtonStyle::Secondary;
    std::optional<graphics::Glyph> glyph;     // leading glyph: buttons and labels
    std::wstring description;                 // Switch/Check helper text under the label; it is also
                                              // published as the control's accessible help text
    std::wstring accessibleName;              // explicit label for native controls whose neighbours
                                              // cannot reliably supply their accessible name
    Severity severity = Severity::None;       // Label (Status role): a leading status badge
    bool wrap = true;                         // Label: wrap within the frame instead of an ellipsis
    bool trailing = false;                    // Label: right-aligned text (unit suffixes, values)
};

// A decorative rounded card or hairline divider of the owner's layout, in the coordinates of the
// form's client area (pixels). Cards inside a scrolling canvas are given in canvas coordinates
// with `canvas` set; the surface maps them while painting.
struct FormCard {
    RECT px{};
    HWND canvas = nullptr;
    bool emphasis = false;  // a slightly stronger fill (status card of the Updates page, setup welcome)
};
struct FormDivider {
    RECT px{};  // a one-pixel-high or one-pixel-wide rectangle
    HWND canvas = nullptr;
};

// Extra art drawn by the owner after cards and dividers (the brand mark on About/setup welcome),
// in DIPs of the form's client area. `clip` is the region being painted.
using FormDecorator = std::function<void(ID2D1DeviceContext* target, const D2D1_RECT_F& clip,
                                         const graphics::DesignTokens& tokens)>;

class FormSurface {
public:
    FormSurface();
    ~FormSurface();
    FormSurface(const FormSurface&) = delete;
    FormSurface& operator=(const FormSurface&) = delete;

    // Attaches to `window` (a top-level window of this thread with WS_CLIPCHILDREN) and applies the
    // material. `scheduler` is optional: with it, animation frames follow the display refresh;
    // without it a short timer drives them. No frames are requested while nothing animates.
    [[nodiscard]] Result<void> Attach(HWND window, const graphics::DesignTokens& tokens,
                                      const graphics::MotionPolicy& motion,
                                      graphics::FrameScheduler* scheduler = nullptr);
    // Restores every registered control's window procedure and the window's DWM attributes.
    void Detach() noexcept;
    [[nodiscard]] bool Attached() const noexcept;
    [[nodiscard]] HWND Window() const noexcept;

    // Restyles the window and every control (material, colors, fonts) without recreating anything:
    // text, selection, focus, scroll positions and drafts are untouched. Switching to or from high
    // contrast changes the material and the native painting of the registered controls in place.
    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);
    [[nodiscard]] const graphics::DesignTokens& Tokens() const noexcept;
    [[nodiscard]] const graphics::MotionPolicy& Motion() const noexcept;
    [[nodiscard]] FormMaterial Material() const noexcept;
    // D45: the window is genuinely maximized, so DWM no longer rounds or borders it. The form then
    // paints its whole client as one rounded opaque body with the thin windowRim along its edge (DWM
    // draws its system material over the whole window rectangle, even outside a window region). The
    // owner gives the window the matching rounded window region, so the corners show the scene behind
    // it; DWM draws no caption buttons under a region, so the owner draws those too. Native material
    // (high contrast) keeps the system frame.
    void SetMaximizedFrame(bool maximized);
    [[nodiscard]] bool MaximizedFrame() const noexcept;
    [[nodiscard]] UINT Dpi() const noexcept;
    [[nodiscard]] float ToPx(float dip) const noexcept;
    [[nodiscard]] float ToDip(float px) const noexcept;

    // Registers a child control created by the caller (a descendant of the form window). The surface
    // subclasses it, assigns ControlFont() and applies the role's styles (layered opaque fields,
    // owner-draw flags already chosen by the caller). The registration ends automatically when the
    // control is destroyed.
    [[nodiscard]] Result<void> Register(HWND control, const ControlSpec& spec);
    void Unregister(HWND control) noexcept;
    void SetSpec(HWND control, const ControlSpec& spec);
    [[nodiscard]] const ControlSpec* Spec(HWND control) const noexcept;

    // Positions a registered control by its visual frame, in pixels of its parent's client area.
    // Field/Document/List children are placed inset within the frame (the frame is the drawn field);
    // Button/ComboBox/Slider windows are enlarged by focusMargin so their own paint holds the focus
    // ring; Switch/Check/Navigation rows draw their ring inside the frame. Other roles use it as is.
    void SetFrame(HWND control, const RECT& framePx);
    [[nodiscard]] RECT Frame(HWND control) const noexcept;
    // Marks a Field/ComboBox as invalid (critical stroke) until cleared.
    void SetError(HWND control, bool invalid);
    // Per-row glyphs of a Navigation list (index order). Rows without a glyph show only text.
    void SetNavigationGlyphs(HWND list, std::vector<graphics::Glyph> glyphs);
    // Icon-only navigation rows (narrow windows); names stay available to UI Automation.
    void SetNavigationCompact(HWND list, bool compact);
    // Progress: a fraction 0..1, or nullopt for indeterminate (the sweep only animates while the
    // window is visible and not minimized). Also follows PBM_SETPOS/PBM_SETMARQUEE.
    void SetProgress(HWND progress, std::optional<double> fraction);

    void SetCards(std::vector<FormCard> cards);
    void SetDividers(std::vector<FormDivider> dividers);
    void SetDecorator(FormDecorator decorator);

    // Fades the content inside `container` (a registered Container and its descendants) from
    // transparent to opaque; cards inside it fade too, the window background does not. Input works
    // immediately. Restarting retargets from the current opacity; reduced motion skips it.
    void FadeIn(HWND container);
    [[nodiscard]] bool Animating() const noexcept;

    // The window procedure of the form forwards every message here first and returns the value when
    // one is produced (WM_PAINT, WM_ERASEBKGND, WM_CTLCOLOR*, WM_DRAWITEM, WM_MEASUREITEM, ...).
    // WM_DPICHANGED, WM_SETTINGCHANGE and WM_THEMECHANGED are observed but returned as nullopt so the
    // owner can relayout; call SetTheme afterwards when the owner recomputes tokens.
    [[nodiscard]] std::optional<LRESULT> HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    // Layout helpers using the same DirectWrite formats as painting (DIPs, Windows text scale applied).
    [[nodiscard]] D2D1_SIZE_F MeasureText(std::wstring_view text, TextRole role, float maxWidthDip,
                                          bool wrap = true) const;
    // The preferred width of a Button/ComboBox/Check for its text (DIPs), within the token minimums.
    [[nodiscard]] float PreferredWidth(HWND control) const;
    // HFONT for native children (body size, Windows text scale, current DPI). Owned by the surface.
    [[nodiscard]] HFONT ControlFont() const noexcept;

    // Paints the scene for `clipPx` (client pixels of `target`'s window, which is the form window or
    // one of its descendants) into `hdc`. Used by FormScrollView and by tests rendering offscreen.
    void PaintRegion(HWND windowBeingPainted, HDC hdc, const RECT& clipPx);
    // The same, then `overlay` draws on top of the scene in DIPs of the painted window's own client
    // area (FormScrollView's overlay indicator). Not called in the Native material.
    using RegionOverlay = std::function<void(ID2D1DeviceContext* target, const graphics::DesignTokens& tokens)>;
    void PaintRegion(HWND windowBeingPainted, HDC hdc, const RECT& clipPx, const RegionOverlay& overlay);

    // Presents the complete scene of `container` (a registered Container): window material, cards,
    // dividers, decorator and the visual of every visible registered descendant, rendered in one
    // buffered pass and copied to the screen with a single blit; everything drawn is then validated.
    // Scrolling and container fades use it, so a moving page is never composed from some controls at
    // their old position and others at the new one. Opaque layered editors keep their own surfaces.
    // Returns false when nothing was presented (Native material, no renderer, hidden window): the
    // caller then invalidates the container the ordinary way.
    bool Present(HWND container);
    // The same for a move: `move` repositions `moving` (a descendant of `container`) by `dyPx`. The
    // frame is rendered at the destination first, then `move` runs and the frame is copied right
    // behind it, so opaque layered editors (which DWM moves on its own) and the scene appear in the
    // same composition. Returns false without calling `move` when nothing can be presented.
    bool PresentMove(HWND container, HWND moving, int dyPx, const std::function<void()>& move);
    // Motion masking of opaque layered editors (FR-037 SF-01). DWM composes those editors apart from
    // the form's own surface, and during continuous motion the two can reach the screen one frame
    // apart. Every editor's private image is kept current underneath the live editor (never visible
    // there). While `container` moves, MaskEditors leaves only that image in the presented scene and
    // keeps the live editor in place - focusable, accessible and hit-testable - at the smallest
    // nonzero opacity. Keyboard, pointer or IME input to an editor reveals it at once; a masked
    // editor that changes is re-imaged. MaskEditors returns false when an image first had to be
    // refreshed: present, let one frame pass and call it again. Callers keep one frame between
    // masking and the first movement and between the last movement and UnmaskEditors, so every
    // transition overlaps identical pixels whichever channel reaches the screen first.
    [[nodiscard]] bool HasVisibleEditors(HWND container) const noexcept;
    bool MaskEditors(HWND container);
    void UnmaskEditors(HWND container) noexcept;
    [[nodiscard]] bool EditorsMasked(HWND container) const noexcept;
    // Origin of `hwnd`'s client area in form-client pixels, including a move PresentMove is
    // rendering. Decorators that draw content attached to a scrolling canvas must use it.
    [[nodiscard]] POINT ClientOriginPx(HWND hwnd) const noexcept;
    // An overlay drawn over `container`'s presented scene (FormScrollView's scroll indicator) and the
    // child window that otherwise paints it; Present validates that window. An empty overlay removes it.
    void SetContainerOverlay(HWND container, RegionOverlay overlay, HWND overlayWindow);

    // UI-thread cost of Present (CPU time spent rendering and blitting; not compositor timing).
    struct PresentStats {
        std::uint64_t frames = 0;
        double totalSeconds = 0.0;
        double maxSeconds = 0.0;
        std::array<std::uint32_t, 34> histogram{};  // 0.5 ms buckets; the last one collects slower frames
        // Cumulative seconds per stage: rendering the scene, moving the windows, copying the frame to
        // the screen and the bookkeeping after it (validation).
        std::array<double, 4> stageSeconds{};
        // Retained composition: material, invalidation, shift, content, copy, blend, overlay (diagnostic).
        std::array<double, 7> composeSeconds{};
        std::uint64_t backgroundRenders = 0;  // retained material re-rendered (diagnostic)
        std::uint64_t contentRebuilds = 0;    // retained content re-rendered completely (diagnostic)
        std::array<std::uint64_t, 4> materialReasons{};  // created, origin, dpi, invalidated (diagnostic)
        std::uint64_t retainedFallbacks = 0;  // frames rendered in full because a retained layer failed
        POINT lastOriginFrom{}, lastOriginTo{};  // diagnostic
        std::uint64_t visualsReused = 0;    // control visuals Present drew from its cache...
        std::uint64_t visualsRendered = 0;  // ...and those it had to render again
    };
    [[nodiscard]] PresentStats Stats() const noexcept;
    void ResetStats() noexcept;

    // The frame scheduler given to Attach (null: timer-driven animation). FormScrollView uses it
    // for its own short scroll and indicator animations.
    [[nodiscard]] graphics::FrameScheduler* Scheduler() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::ui::form
