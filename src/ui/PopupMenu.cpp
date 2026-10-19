// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/PopupMenu.h"

#include <set>

namespace aether::ui {
namespace {

class SystemWindowOps final : public IMenuWindowOps {
public:
    HWND Foreground() noexcept override { return ::GetForegroundWindow(); }
    bool SetForeground(HWND window) noexcept override { return ::SetForegroundWindow(window) != FALSE; }
    bool IsValidWindow(HWND window) noexcept override { return window && ::IsWindow(window) != FALSE; }
    LONG_PTR ExtendedStyle(HWND window) noexcept override { return ::GetWindowLongPtrW(window, GWL_EXSTYLE); }
    void SetExtendedStyle(HWND window, LONG_PTR style) noexcept override {
        ::SetWindowLongPtrW(window, GWL_EXSTYLE, style);
    }
    UINT Track(HMENU menu, UINT flags, POINT point, HWND owner, const TPMPARAMS* exclude) noexcept override {
        TPMPARAMS params{};
        if (exclude) params = *exclude;
        const BOOL command = ::TrackPopupMenuEx(menu, flags, point.x, point.y, owner, exclude ? &params : nullptr);
        return command > 0 ? static_cast<UINT>(command) : 0u;
    }
    void PostNull(HWND window) noexcept override { ::PostMessageW(window, WM_NULL, 0, 0); }
};

bool HasArea(const RECT& rect) noexcept { return rect.right > rect.left && rect.bottom > rect.top; }

Result<void> ValidateLevel(std::span<const MenuEntry> entries, std::size_t depth, std::size_t& count,
                           std::set<std::uint32_t>& ids) {
    if (entries.empty()) return MakeError(ErrorCode::InvalidArgument, "a menu needs at least one entry");
    if (depth > kMaxMenuDepth) return MakeError(ErrorCode::InvalidArgument, "menu nesting is too deep");
    for (const auto& entry : entries) {
        if (++count > kMaxMenuEntries) return MakeError(ErrorCode::InvalidArgument, "menu has too many entries");
        if (entry.separator) continue;
        if (!entry.submenu.empty()) {
            if (auto nested = ValidateLevel(entry.submenu, depth + 1, count, ids); !nested) return nested;
            continue;
        }
        if (entry.id == 0 || entry.id > kMaxMenuCommand) return MakeError(ErrorCode::InvalidArgument, "menu command id is out of range");
        if (!ids.insert(entry.id).second) return MakeError(ErrorCode::InvalidArgument, "menu command id is repeated");
    }
    return Ok();
}

Result<UniqueMenu> BuildLevel(std::span<const MenuEntry> entries) {
    UniqueMenu menu(::CreatePopupMenu());
    if (!menu) return MakeError(ErrorCode::Platform, "popup menu could not be created", static_cast<std::int64_t>(::GetLastError()));
    for (const auto& entry : entries) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        std::wstring text = entry.text;
        UniqueMenu child;
        if (entry.separator) {
            info.fMask = MIIM_FTYPE;
            info.fType = MFT_SEPARATOR;
        } else {
            info.fMask = MIIM_FTYPE | MIIM_STRING | MIIM_STATE;
            info.fType = MFT_STRING | (entry.radio ? MFT_RADIOCHECK : 0u);
            info.fState = (entry.enabled ? MFS_ENABLED : MFS_DISABLED) | (entry.checked ? MFS_CHECKED : MFS_UNCHECKED);
            info.dwTypeData = text.data();
            if (!entry.submenu.empty()) {
                auto nested = BuildLevel(entry.submenu);
                if (!nested) return nested.error();
                child = std::move(*nested);
                info.fMask |= MIIM_SUBMENU;
                info.hSubMenu = child.get();
            } else {
                info.fMask |= MIIM_ID;
                info.wID = entry.id;
            }
        }
        const int position = ::GetMenuItemCount(menu.get());
        if (position < 0 || !::InsertMenuItemW(menu.get(), static_cast<UINT>(position), TRUE, &info)) {
            return MakeError(ErrorCode::Platform, "popup menu entry could not be added", static_cast<std::int64_t>(::GetLastError()));
        }
        (void)child.release();  // owned by the parent menu now
    }
    return menu;
}

const MenuEntry* FindIn(std::span<const MenuEntry> entries, std::uint32_t id) noexcept {
    for (const auto& entry : entries) {
        if (entry.separator) continue;
        if (!entry.submenu.empty()) {
            if (const auto* found = FindIn(entry.submenu, id)) return found;
            continue;
        }
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

}  // namespace

MenuEntry MenuEntry::Command(std::uint32_t id, std::wstring text, bool enabled) {
    MenuEntry entry;
    entry.id = id;
    entry.text = std::move(text);
    entry.enabled = enabled;
    return entry;
}

MenuEntry MenuEntry::Separator() {
    MenuEntry entry;
    entry.separator = true;
    return entry;
}

MenuEntry MenuEntry::Submenu(std::wstring text, std::vector<MenuEntry> entries, bool enabled) {
    MenuEntry entry;
    entry.text = std::move(text);
    entry.submenu = std::move(entries);
    entry.enabled = enabled;
    return entry;
}

Result<void> ValidateMenu(std::span<const MenuEntry> entries) {
    try {
        std::size_t count = 0;
        std::set<std::uint32_t> ids;
        return ValidateLevel(entries, 1, count, ids);
    } catch (...) { return MakeError(ErrorCode::Internal, "menu could not be validated"); }
}

const MenuEntry* FindMenuCommand(std::span<const MenuEntry> entries, std::uint32_t id) noexcept {
    return id == 0 ? nullptr : FindIn(entries, id);
}

IMenuWindowOps& SystemMenuWindowOps() noexcept {
    static SystemWindowOps ops;
    return ops;
}

void MenuDeleter::operator()(HMENU menu) const noexcept {
    if (menu) ::DestroyMenu(menu);
}

Result<UniqueMenu> BuildNativeMenu(std::span<const MenuEntry> entries) {
    try {
        if (auto valid = ValidateMenu(entries); !valid) return valid.error();
        return BuildLevel(entries);
    } catch (...) { return MakeError(ErrorCode::Internal, "popup menu could not be built"); }
}

UINT MenuTrackFlags(const MenuRequest& request) noexcept {
    UINT flags = TPM_RETURNCMD | TPM_NONOTIFY | (request.rightButton ? TPM_RIGHTBUTTON : TPM_LEFTBUTTON);
    switch (request.placement) {
        case MenuPlacement::AboveAnchor: flags |= TPM_CENTERALIGN | TPM_BOTTOMALIGN | TPM_VERTICAL; break;
        case MenuPlacement::BelowAnchor: flags |= TPM_CENTERALIGN | TPM_TOPALIGN | TPM_VERTICAL; break;
        case MenuPlacement::AtPoint: flags |= TPM_LEFTALIGN | TPM_TOPALIGN; break;
    }
    return flags;
}

POINT MenuTrackPoint(const MenuRequest& request) noexcept {
    if (request.placement == MenuPlacement::AtPoint || !HasArea(request.anchor)) return request.point;
    const LONG x = request.anchor.left + (request.anchor.right - request.anchor.left) / 2;
    return POINT{x, request.placement == MenuPlacement::AboveAnchor ? request.anchor.top : request.anchor.bottom};
}

Result<MenuSelection> TrackMenuWithForegroundRule(IMenuWindowOps& ops, HMENU menu, const MenuRequest& request) {
    if (!menu) return MakeError(ErrorCode::InvalidArgument, "no popup menu to show");
    const HWND owner = request.owner;
    if (!ops.IsValidWindow(owner)) return MakeError(ErrorCode::InvalidArgument, "popup menu owner is not a window");
    MenuSelection selection;
    selection.previousForeground = ops.Foreground();
    const LONG_PTR style = ops.ExtendedStyle(owner);
    const bool noActivate = (style & WS_EX_NOACTIVATE) != 0;
    if (noActivate) ops.SetExtendedStyle(owner, style & ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE));
    // Windows may refuse; the menu is shown anyway, as before this rule existed.
    (void)ops.SetForeground(owner);
    TPMPARAMS params{};
    params.cbSize = sizeof(params);
    params.rcExclude = request.anchor;
    const bool exclude = request.placement != MenuPlacement::AtPoint && HasArea(request.anchor);
    selection.command = ops.Track(menu, MenuTrackFlags(request), MenuTrackPoint(request), owner, exclude ? &params : nullptr);
    // The owner may have been destroyed by a message handled inside the menu loop.
    if (ops.IsValidWindow(owner)) {
        ops.PostNull(owner);  // lets the menu's modal loop finish before the next input
        if (noActivate) ops.SetExtendedStyle(owner, ops.ExtendedStyle(owner) | WS_EX_NOACTIVATE);
    }
    if (request.restore == ForegroundRestore::Always ||
        (request.restore == ForegroundRestore::WhenDismissed && selection.command == 0)) {
        RestorePreviousForeground(ops, owner, selection.previousForeground);
    }
    return selection;
}

void RestorePreviousForeground(IMenuWindowOps& ops, HWND owner, HWND previous) noexcept {
    if (!previous || previous == owner || !ops.IsValidWindow(previous)) return;
    if (ops.Foreground() != owner) return;  // the user already moved on (for example by clicking elsewhere)
    (void)ops.SetForeground(previous);
}

Result<MenuSelection> NativeMenuPresenter::Show(std::span<const MenuEntry> entries, const MenuRequest& request) {
    try {
        auto menu = BuildNativeMenu(entries);
        if (!menu) return menu.error();
        return TrackMenuWithForegroundRule(*ops_, menu->get(), request);
    } catch (...) { return MakeError(ErrorCode::Internal, "popup menu could not be shown"); }
}

Result<std::unique_ptr<IMenuPresenter>> MakeNativeMenuPresenter() try {
    return std::unique_ptr<IMenuPresenter>(std::make_unique<NativeMenuPresenter>());
} catch (...) { return MakeError(ErrorCode::Internal, "popup menu presenter unavailable"); }

}  // namespace aether::ui
