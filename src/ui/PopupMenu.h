// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Shared popup-menu model and presentation. The Dock and the Top Bar describe a context menu as
// MenuEntry values and show it through an IMenuPresenter: NativeMenuPresenter by default, or an
// owner-drawn presenter behind the same interface (which falls back to the native one in high
// contrast).
//
// Foreground rule (TrackPopupMenu documentation): a popup menu only closes when the user clicks
// elsewhere if its owner window is the foreground window. Shell surfaces never take the foreground
// on their own (WS_EX_NOACTIVATE), so for the duration of the modal menu the presenter remembers
// the foreground window, clears WS_EX_NOACTIVATE, makes the owner foreground (allowed because the
// owner received the input that opened the menu), tracks the menu, posts WM_NULL to the owner so the
// menu loop ends cleanly, restores WS_EX_NOACTIVATE and, per ForegroundRestore, gives the foreground
// back to the window the user was using.
#pragma once

#include "core/Result.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace aether::ui {

inline constexpr std::uint32_t kMaxMenuCommand = 0xFFFF;
inline constexpr std::size_t kMaxMenuEntries = 256;  // all levels together
inline constexpr std::size_t kMaxMenuDepth = 4;      // top level plus three nested submenus

struct MenuEntry {
    std::uint32_t id = 0;   // command returned when chosen, 1..kMaxMenuCommand and unique in the menu;
                            // ignored for separators and submenu headers
    std::wstring text;      // label; '&' marks the mnemonic, '\t' starts right-aligned accelerator text
    bool enabled = true;
    bool checked = false;
    bool radio = false;     // a checked entry shows a bullet instead of a check mark
    bool separator = false;
    std::vector<MenuEntry> submenu;  // non-empty: this entry opens a submenu

    [[nodiscard]] static MenuEntry Command(std::uint32_t id, std::wstring text, bool enabled = true);
    [[nodiscard]] static MenuEntry Separator();
    [[nodiscard]] static MenuEntry Submenu(std::wstring text, std::vector<MenuEntry> entries, bool enabled = true);
};

// Rejects an empty menu or submenu, command ids outside 1..kMaxMenuCommand or repeated anywhere in
// the menu, more than kMaxMenuEntries entries and nesting deeper than kMaxMenuDepth.
[[nodiscard]] Result<void> ValidateMenu(std::span<const MenuEntry> entries);
// The command entry with `id` at any level, or null.
[[nodiscard]] const MenuEntry* FindMenuCommand(std::span<const MenuEntry> entries, std::uint32_t id) noexcept;

enum class MenuPlacement {
    AtPoint,      // top-left corner at `point`
    AboveAnchor,  // centred above `anchor` without covering it; Windows flips it below when there is no room
    BelowAnchor,  // centred below `anchor` without covering it; Windows flips it above when there is no room
};

enum class ForegroundRestore {
    WhenDismissed,  // give the foreground back only when no command was chosen (the caller decides otherwise)
    Always,         // give it back before returning, also after a command
    Never,
};

struct MenuRequest {
    HWND owner = nullptr;  // window of the calling UI thread that owns the menu
    POINT point{};         // screen pixels; used by AtPoint and when `anchor` is empty
    RECT anchor{};         // screen pixels: the item the menu belongs to (TPMPARAMS::rcExclude)
    MenuPlacement placement = MenuPlacement::AboveAnchor;
    bool rightButton = true;  // the right button can also choose an entry (TPM_RIGHTBUTTON)
    ForegroundRestore restore = ForegroundRestore::WhenDismissed;
};

struct MenuSelection {
    std::uint32_t command = 0;          // 0 when the menu was dismissed
    HWND previousForeground = nullptr;  // foreground window before the menu opened, for RestorePreviousForeground
};

class IMenuPresenter {
public:
    virtual ~IMenuPresenter() = default;
    // Shows a modal popup menu on the calling UI thread and returns the chosen command.
    virtual Result<MenuSelection> Show(std::span<const MenuEntry> entries, const MenuRequest& request) = 0;
};

// The window operations of the foreground rule, injectable so tests never touch real windows or
// the real foreground.
class IMenuWindowOps {
public:
    virtual ~IMenuWindowOps() = default;
    virtual HWND Foreground() noexcept = 0;
    virtual bool SetForeground(HWND window) noexcept = 0;
    virtual bool IsValidWindow(HWND window) noexcept = 0;
    virtual LONG_PTR ExtendedStyle(HWND window) noexcept = 0;
    virtual void SetExtendedStyle(HWND window, LONG_PTR style) noexcept = 0;
    // TrackPopupMenuEx; `exclude` is null when there is no anchor. Returns the command (TPM_RETURNCMD) or 0.
    virtual UINT Track(HMENU menu, UINT flags, POINT point, HWND owner, const TPMPARAMS* exclude) noexcept = 0;
    virtual void PostNull(HWND window) noexcept = 0;
};
[[nodiscard]] IMenuWindowOps& SystemMenuWindowOps() noexcept;

struct MenuDeleter {
    void operator()(HMENU menu) const noexcept;
};
using UniqueMenu = std::unique_ptr<std::remove_pointer_t<HMENU>, MenuDeleter>;

// Validates `entries` and builds the equivalent native popup menu (MFT_RADIOCHECK for radio
// entries, MFS_DISABLED / MFS_CHECKED states, nested popups).
[[nodiscard]] Result<UniqueMenu> BuildNativeMenu(std::span<const MenuEntry> entries);

// TPM_RETURNCMD | TPM_NONOTIFY plus the button and placement flags; anchored placements add
// TPM_CENTERALIGN | TPM_VERTICAL and TPM_BOTTOMALIGN (above) or TPM_TOPALIGN (below).
[[nodiscard]] UINT MenuTrackFlags(const MenuRequest& request) noexcept;
// Anchored placements: the anchor's horizontal centre at its top (above) or bottom (below) edge.
// AtPoint, or an empty anchor: `point`.
[[nodiscard]] POINT MenuTrackPoint(const MenuRequest& request) noexcept;

// Tracks an already built menu (native or owner-drawn) under the foreground rule above.
[[nodiscard]] Result<MenuSelection> TrackMenuWithForegroundRule(IMenuWindowOps& ops, HMENU menu, const MenuRequest& request);
// Gives the foreground back to `previous` when `owner` still has it and `previous` is another live
// window. For callers using ForegroundRestore::WhenDismissed after a command that activated nothing.
void RestorePreviousForeground(IMenuWindowOps& ops, HWND owner, HWND previous) noexcept;

class NativeMenuPresenter final : public IMenuPresenter {
public:
    explicit NativeMenuPresenter(IMenuWindowOps& ops = SystemMenuWindowOps()) noexcept : ops_(&ops) {}
    Result<MenuSelection> Show(std::span<const MenuEntry> entries, const MenuRequest& request) override;

private:
    IMenuWindowOps* ops_;
};

[[nodiscard]] Result<std::unique_ptr<IMenuPresenter>> MakeNativeMenuPresenter();

}  // namespace aether::ui
