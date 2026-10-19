// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Popup-menu model, native menu construction and the foreground rule. Window operations are faked:
// these tests never show a menu, change the foreground or touch a real window.
#include "AetherTest.h"

#include "ui/PopupMenu.h"

#include <string>
#include <vector>

using namespace aether;
using namespace aether::ui;

namespace {

HWND FakeWindow(std::uintptr_t value) { return reinterpret_cast<HWND>(value); }

class FakeOps final : public IMenuWindowOps {
public:
    HWND foreground = FakeWindow(0x200);
    HWND foregroundAfterTrack = nullptr;  // when set, the foreground after the menu closed
    std::vector<HWND> windows{FakeWindow(0x100), FakeWindow(0x200)};
    LONG_PTR style = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    bool allowForeground = true;
    UINT command = 0;
    std::vector<std::string> log;
    UINT trackedFlags = 0;
    POINT trackedPoint{};
    bool trackedExclude = false;
    RECT trackedRect{};
    int trackedItems = -1;

    HWND Foreground() noexcept override { log.push_back("foreground"); return foreground; }
    bool SetForeground(HWND window) noexcept override {
        log.push_back(window == FakeWindow(0x100) ? "set-foreground owner" : "set-foreground previous");
        if (allowForeground) foreground = window;
        return allowForeground;
    }
    bool IsValidWindow(HWND window) noexcept override {
        for (const HWND candidate : windows) if (candidate == window) return true;
        return false;
    }
    LONG_PTR ExtendedStyle(HWND) noexcept override { return style; }
    void SetExtendedStyle(HWND, LONG_PTR value) noexcept override {
        log.push_back((value & WS_EX_NOACTIVATE) ? "style noactivate" : "style activatable");
        style = value;
    }
    UINT Track(HMENU menu, UINT flags, POINT point, HWND, const TPMPARAMS* exclude) noexcept override {
        log.push_back(foreground == FakeWindow(0x100) ? "track (owner foreground)" : "track");
        trackedFlags = flags; trackedPoint = point; trackedExclude = exclude != nullptr;
        if (exclude) trackedRect = exclude->rcExclude;
        trackedItems = ::GetMenuItemCount(menu);
        if (foregroundAfterTrack) foreground = foregroundAfterTrack;
        return command;
    }
    void PostNull(HWND) noexcept override { log.push_back("post-null"); }
};

std::vector<MenuEntry> SampleMenu() {
    std::vector<MenuEntry> size{MenuEntry::Command(10, L"Small"), MenuEntry::Command(11, L"Medium"),
                                MenuEntry::Command(12, L"Large")};
    size[1].checked = true;
    for (auto& entry : size) entry.radio = true;
    return {MenuEntry::Command(1, L"Open"), MenuEntry::Command(2, L"New instance", false), MenuEntry::Separator(),
            MenuEntry::Submenu(L"Dock size", std::move(size)), MenuEntry::Command(3, L"Dock settings…")};
}

MenuRequest Request() {
    MenuRequest request;
    request.owner = FakeWindow(0x100);
    request.anchor = RECT{100, 900, 140, 940};
    request.point = POINT{5, 6};
    return request;
}

bool Contains(UINT flags, UINT expected) { return (flags & expected) == expected; }

}  // namespace

AETHER_TEST("popup menu: valid menus pass and malformed menus are rejected") {
    CHECK(ValidateMenu(SampleMenu()).ok());
    CHECK_EQ(ValidateMenu({}).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(ValidateMenu(std::vector{MenuEntry::Command(0, L"Zero")}).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(ValidateMenu(std::vector{MenuEntry::Command(0x10000, L"Wide")}).error().code, ErrorCode::InvalidArgument);
    CHECK(ValidateMenu(std::vector{MenuEntry::Command(0xFFFF, L"Last")}).ok());
    // A command id may appear only once, also across submenus.
    auto repeated = SampleMenu();
    repeated.push_back(MenuEntry::Command(11, L"Duplicate"));
    CHECK_EQ(ValidateMenu(repeated).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(ValidateMenu(std::vector{MenuEntry::Submenu(L"Empty", {})}).error().code, ErrorCode::InvalidArgument);
    // Separators need no id.
    CHECK(ValidateMenu(std::vector{MenuEntry::Command(1, L"One"), MenuEntry::Separator(), MenuEntry::Command(2, L"Two")}).ok());
    // Depth: four levels are accepted, five are not.
    auto nested = std::vector{MenuEntry::Command(1, L"Leaf")};
    for (int level = 0; level < 3; ++level) nested = std::vector{MenuEntry::Submenu(L"Level", std::move(nested))};
    CHECK(ValidateMenu(nested).ok());
    nested = std::vector{MenuEntry::Submenu(L"Level", std::move(nested))};
    CHECK_EQ(ValidateMenu(nested).error().code, ErrorCode::InvalidArgument);
    std::vector<MenuEntry> many;
    for (std::uint32_t id = 1; id <= kMaxMenuEntries; ++id) many.push_back(MenuEntry::Command(id, L"Entry"));
    CHECK(ValidateMenu(many).ok());
    many.push_back(MenuEntry::Separator());
    CHECK_EQ(ValidateMenu(many).error().code, ErrorCode::InvalidArgument);
}

AETHER_TEST("popup menu: commands are found at any level") {
    const auto menu = SampleMenu();
    const auto* medium = FindMenuCommand(menu, 11);
    REQUIRE(medium != nullptr);
    CHECK_EQ(medium->text, std::wstring(L"Medium"));
    CHECK(medium->checked);
    REQUIRE(FindMenuCommand(menu, 3) != nullptr);
    CHECK(FindMenuCommand(menu, 0) == nullptr);
    CHECK(FindMenuCommand(menu, 99) == nullptr);
}

AETHER_TEST("popup menu: anchored placements open centred above or below the item and exclude it") {
    auto request = Request();
    const UINT above = MenuTrackFlags(request);
    CHECK(Contains(above, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_CENTERALIGN | TPM_BOTTOMALIGN | TPM_VERTICAL));
    const POINT abovePoint = MenuTrackPoint(request);
    CHECK_EQ(abovePoint.x, 120L); CHECK_EQ(abovePoint.y, 900L);
    request.placement = MenuPlacement::BelowAnchor;
    const UINT below = MenuTrackFlags(request);
    CHECK(Contains(below, TPM_CENTERALIGN | TPM_VERTICAL));
    CHECK_EQ(below & TPM_BOTTOMALIGN, 0u);
    const POINT belowPoint = MenuTrackPoint(request);
    CHECK_EQ(belowPoint.x, 120L); CHECK_EQ(belowPoint.y, 940L);
    request.placement = MenuPlacement::AtPoint;
    request.rightButton = false;
    const UINT atPoint = MenuTrackFlags(request);
    CHECK(Contains(atPoint, TPM_RETURNCMD | TPM_NONOTIFY));
    CHECK_EQ(atPoint & (TPM_RIGHTBUTTON | TPM_CENTERALIGN | TPM_BOTTOMALIGN | TPM_VERTICAL), 0u);
    CHECK_EQ(MenuTrackPoint(request).x, 5L); CHECK_EQ(MenuTrackPoint(request).y, 6L);
    // Without an anchor rectangle, anchored placements fall back to the point.
    request.placement = MenuPlacement::AboveAnchor;
    request.anchor = RECT{};
    CHECK_EQ(MenuTrackPoint(request).x, 5L); CHECK_EQ(MenuTrackPoint(request).y, 6L);
}

AETHER_TEST("popup menu: native menus mirror entries states radio checks and submenus") {
    auto built = BuildNativeMenu(SampleMenu());
    REQUIRE(built.ok());
    const HMENU menu = built->get();
    REQUIRE_EQ(::GetMenuItemCount(menu), 5);
    const auto item = [](HMENU owner, UINT position) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU;
        CHECK(::GetMenuItemInfoW(owner, position, TRUE, &info) != FALSE);
        return info;
    };
    const auto text = [](HMENU owner, UINT position) {
        wchar_t buffer[64]{};
        ::GetMenuStringW(owner, position, buffer, 64, MF_BYPOSITION);
        return std::wstring(buffer);
    };
    const auto open = item(menu, 0);
    CHECK_EQ(open.wID, 1u); CHECK_EQ(open.fState & MFS_DISABLED, 0u); CHECK_EQ(text(menu, 0), std::wstring(L"Open"));
    const auto disabled = item(menu, 1);
    CHECK_EQ(disabled.wID, 2u); CHECK_NE(disabled.fState & MFS_DISABLED, 0u);
    CHECK_NE(item(menu, 2).fType & MFT_SEPARATOR, 0u);
    const auto size = item(menu, 3);
    REQUIRE(size.hSubMenu != nullptr);
    CHECK_EQ(text(menu, 3), std::wstring(L"Dock size"));
    REQUIRE_EQ(::GetMenuItemCount(size.hSubMenu), 3);
    for (UINT position = 0; position < 3; ++position) {
        const auto preset = item(size.hSubMenu, position);
        CHECK_EQ(preset.wID, 10u + position);
        CHECK_NE(preset.fType & MFT_RADIOCHECK, 0u);
        CHECK_EQ((preset.fState & MFS_CHECKED) != 0, position == 1);
    }
    CHECK_EQ(item(menu, 4).wID, 3u);
    CHECK_FALSE(BuildNativeMenu(std::vector{MenuEntry::Command(0, L"Invalid")}).ok());
}

AETHER_TEST("popup menu: the foreground rule wraps a dismissed menu and gives the foreground back") {
    FakeOps ops;
    NativeMenuPresenter presenter(ops);
    auto selection = presenter.Show(SampleMenu(), Request());
    REQUIRE(selection.ok());
    CHECK_EQ(selection->command, 0u);
    CHECK(selection->previousForeground == FakeWindow(0x200));
    const std::vector<std::string> expected{
        "foreground", "style activatable", "set-foreground owner", "track (owner foreground)", "post-null",
        "style noactivate", "foreground", "set-foreground previous"};
    CHECK(ops.log == expected);
    CHECK(ops.foreground == FakeWindow(0x200));
    CHECK_NE(ops.style & WS_EX_NOACTIVATE, 0);
    CHECK_NE(ops.style & WS_EX_TOOLWINDOW, 0);
    CHECK_EQ(ops.trackedItems, 5);
    CHECK(ops.trackedExclude);
    CHECK_EQ(ops.trackedRect.left, 100L); CHECK_EQ(ops.trackedRect.top, 900L);
    CHECK_EQ(ops.trackedRect.right, 140L); CHECK_EQ(ops.trackedRect.bottom, 940L);
    CHECK_EQ(ops.trackedPoint.x, 120L); CHECK_EQ(ops.trackedPoint.y, 900L);
    CHECK(Contains(ops.trackedFlags, TPM_RETURNCMD | TPM_BOTTOMALIGN | TPM_CENTERALIGN | TPM_VERTICAL));
}

AETHER_TEST("popup menu: a chosen command leaves the foreground decision to the caller unless asked") {
    FakeOps ops; ops.command = 11;
    NativeMenuPresenter presenter(ops);
    auto selection = presenter.Show(SampleMenu(), Request());
    REQUIRE(selection.ok());
    CHECK_EQ(selection->command, 11u);
    CHECK(ops.foreground == FakeWindow(0x100));
    CHECK(ops.log.back() == "style noactivate");
    // The caller restores after running a command that activated nothing.
    RestorePreviousForeground(ops, FakeWindow(0x100), selection->previousForeground);
    CHECK(ops.foreground == FakeWindow(0x200));

    FakeOps always; always.command = 3;
    auto request = Request(); request.restore = ForegroundRestore::Always;
    auto restored = NativeMenuPresenter(always).Show(SampleMenu(), request);
    REQUIRE(restored.ok());
    CHECK_EQ(restored->command, 3u);
    CHECK(always.foreground == FakeWindow(0x200));

    FakeOps never;
    request.restore = ForegroundRestore::Never;
    REQUIRE(NativeMenuPresenter(never).Show(SampleMenu(), request).ok());
    CHECK(never.foreground == FakeWindow(0x100));
}

AETHER_TEST("popup menu: the foreground is never taken back from a window the user chose") {
    FakeOps ops; ops.foregroundAfterTrack = FakeWindow(0x300);
    ops.windows.push_back(FakeWindow(0x300));
    REQUIRE(NativeMenuPresenter(ops).Show(SampleMenu(), Request()).ok());
    CHECK(ops.foreground == FakeWindow(0x300));
    CHECK(ops.log.back() == "foreground");
    // A previous window that disappeared, or none at all, is left alone too.
    FakeOps gone; gone.foreground = FakeWindow(0x999);
    REQUIRE(NativeMenuPresenter(gone).Show(SampleMenu(), Request()).ok());
    CHECK(gone.foreground == FakeWindow(0x100));
    FakeOps none; none.foreground = nullptr;
    auto selection = NativeMenuPresenter(none).Show(SampleMenu(), Request());
    REQUIRE(selection.ok());
    CHECK(selection->previousForeground == nullptr);
    CHECK(none.foreground == FakeWindow(0x100));
}

AETHER_TEST("popup menu: activatable owners keep their style and a refused foreground still shows the menu") {
    FakeOps ops; ops.style = WS_EX_TOOLWINDOW; ops.allowForeground = false; ops.command = 1;
    auto request = Request(); request.placement = MenuPlacement::AtPoint;
    auto selection = NativeMenuPresenter(ops).Show(SampleMenu(), request);
    REQUIRE(selection.ok());
    CHECK_EQ(selection->command, 1u);
    const std::vector<std::string> expected{"foreground", "set-foreground owner", "track", "post-null"};
    CHECK(ops.log == expected);
    CHECK_EQ(ops.style, static_cast<LONG_PTR>(WS_EX_TOOLWINDOW));
    CHECK_FALSE(ops.trackedExclude);
    CHECK_EQ(ops.trackedPoint.x, 5L);
}

AETHER_TEST("popup menu: invalid owners and entries fail before any window is touched") {
    FakeOps ops;
    auto request = Request(); request.owner = FakeWindow(0x555);
    CHECK_EQ(NativeMenuPresenter(ops).Show(SampleMenu(), request).error().code, ErrorCode::InvalidArgument);
    CHECK_EQ(NativeMenuPresenter(ops).Show(std::vector{MenuEntry::Command(0, L"Bad")}, Request()).error().code,
             ErrorCode::InvalidArgument);
    CHECK(ops.log.empty());
    CHECK_EQ(TrackMenuWithForegroundRule(ops, nullptr, Request()).error().code, ErrorCode::InvalidArgument);
    CHECK(ops.log.empty());
}

AETHER_TEST("popup menu: the native presenter factory returns a presenter without showing anything") {
    auto presenter = MakeNativeMenuPresenter();
    REQUIRE(presenter.ok());
    CHECK(*presenter != nullptr);
}
