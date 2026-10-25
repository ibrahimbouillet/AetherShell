// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// GlassMenuPresenter: popup menus on the Aether glass material behind the shared IMenuPresenter
// contract (the Top Bar logo menu, Dock context menus and Settings dropdowns use the same component).
//
// Each menu level is a SurfaceWindow (composition tree with the blurred host backdrop where
// supported) holding a compact neutral glass panel: consistent icon/check column, labels with
// mnemonic underlines, right-aligned accelerator text, chevrons for submenus, subtle separators and a
// restrained animated hover/selection pill. No directional sheen and no square hover plates.
//
// Semantics match NativeMenuPresenter/TrackPopupMenu: Show is modal on the calling UI thread and
// returns the chosen command id (0 when dismissed); ids, enabled/checked/radio states, separators
// and nesting come from the MenuEntry model unchanged (ValidateMenu is applied first). The owner
// becomes foreground under the same foreground rule (WS_EX_NOACTIVATE cleared for the duration,
// ForegroundRestore honored afterwards). The menu windows themselves never activate: like the
// system menu loop, the presenter's own message loop takes the keyboard input of the thread while
// the menu is open, and captures the mouse so a click outside every level dismisses the menu
// without reaching the window underneath (clicks in other applications dismiss it as well, through
// the loss of activation or capture).
//
// Keyboard: Up/Down (wrapping), Home/End, PageUp/PageDown, Enter/Space choose, Right/Left open and
// close submenus, Escape closes one level, Alt/F10 close all, letters choose by mnemonic (a unique
// mnemonic chooses at once, repeated ones cycle), disabled entries are skipped. Mouse: hover selects,
// a short hover opens a submenu (travel towards the open submenu keeps it), click chooses enabled
// commands, a click on a disabled entry or separator does nothing.
//
// Placement: anchored above/below the anchor rect or at a point, flipped and clamped to the work
// area of the anchor's monitor with an edge margin; tall menus get a maximum height and scroll
// (wheel, keyboard selection and edge arrows). Submenus open beside their row and flip at edges.
//
// Motion: opening settles a small eased translation towards the anchor plus scale and fade on the
// compositor; closing fades quickly after Show has already returned, so the chosen command runs
// without waiting for the animation. Reduced motion and high contrast show and hide instantly.
//
// Accessibility: each level exposes a UI Automation Menu with MenuItem children (Invoke; names,
// enabled state, accelerator and "submenu" help text) and raises focus changes for the selected row.
//
// Fallbacks: high contrast uses ThemedMenuPresenter's native system-colored menu; when the
// composition window cannot be created the themed (owner-drawn HMENU) presenter is used, and failing
// that the native menu. Command ids are identical on every path.
//
// Lifetime: one presenter can be kept for the lifetime of its owner (Dock, Top Bar, Settings). Show
// is not reentrant; a nested Show while a menu is open returns InvalidState. Cancel() dismisses an
// open menu (teardown, a display change) and Show then returns command 0.
#pragma once

#include "graphics/Animation.h"
#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "graphics/Theme.h"
#include "ui/PopupMenu.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace aether::ui {

// Pure placement rule (exposed for tests). `panel` is the menu's size in pixels; returns its top-left
// corner in screen pixels for `request` within `workArea`, honoring the anchor gap and edge margin
// (pixels), flipping above/below (or left/right for submenus) when the preferred side has no room.
[[nodiscard]] POINT PlaceMenuPanel(SIZE panel, const MenuRequest& request, const RECT& workArea, int anchorGapPx,
                                   int edgeMarginPx) noexcept;
// Where a submenu of `panel` size goes next to `row` (screen pixels) within `workArea`.
[[nodiscard]] POINT PlaceSubmenuPanel(SIZE panel, const RECT& row, const RECT& workArea, int overlapPx,
                                      int edgeMarginPx) noexcept;
// Panel width rule (exposed for tests), DIPs: the widest of menu.minWidth, `minimumWidthPx` (the
// request's minimum, converted at `dpi`) and `contentDip`, clamped to menu.maxWidth and to
// `availableDip` (the work-area width minus edge margins; ignored when not positive).
[[nodiscard]] float MenuPanelWidthDip(float contentDip, int minimumWidthPx, UINT dpi, const graphics::MenuTokens& menu,
                                      float availableDip) noexcept;

// Keyboard model of one level (exposed for tests): the next enabled, non-separator row from `from`
// in `direction` (+1/-1), wrapping; npos when there is none.
[[nodiscard]] std::size_t NextMenuRow(std::span<const MenuEntry> entries, std::size_t from, int direction) noexcept;
// Rows whose mnemonic is `letter` (case-insensitive), enabled only, in order.
[[nodiscard]] std::vector<std::size_t> MnemonicRows(std::span<const MenuEntry> entries, wchar_t letter);

class GlassMenuPresenter final : public IMenuPresenter {
public:
    GlassMenuPresenter(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler,
                       const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
                       IMenuWindowOps& ops = SystemMenuWindowOps());
    ~GlassMenuPresenter() override;
    GlassMenuPresenter(const GlassMenuPresenter&) = delete;
    GlassMenuPresenter& operator=(const GlassMenuPresenter&) = delete;

    void SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion);
    Result<MenuSelection> Show(std::span<const MenuEntry> entries, const MenuRequest& request) override;

    // Dismisses an open menu; Show then returns command 0. Safe to call when nothing is open.
    void Cancel() noexcept;
    [[nodiscard]] bool IsOpen() const noexcept;
    // How the last Show presented the menu (diagnostics and tests).
    enum class Presentation { None, Glass, Themed, Native };
    [[nodiscard]] Presentation LastPresentation() const noexcept;

    // Test seam: `opened` runs inside the modal loop once a level is visible (depth 0 = root), with
    // that level's window; tests post keyboard/mouse input to the owner or the level from it.
    struct TestHooks {
        std::function<void(HWND level, std::size_t depth)> opened;
        // Runs whenever a level renders a frame (idle-frame and settle checks).
        std::function<void(std::size_t depth)> rendered;
    };
    void SetTestHooks(TestHooks hooks);
    // Screen-pixel bounds of entry `row` of the open level `depth` as laid out and scrolled now (tests
    // and diagnostics); nullopt when no such level is open or the row is not entirely visible.
    [[nodiscard]] std::optional<RECT> RowBoundsPx(std::size_t depth, std::size_t row) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::ui
