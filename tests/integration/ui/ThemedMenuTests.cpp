// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The themed presenter's owner subclass while a menu is tracked. The owner is a private window that
// is never shown; the window operations are faked, so no menu appears, the foreground is never
// changed and only messages sent to this test's own window are used.
#include "AetherTest.h"

#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "ui/ThemedMenu.h"

#include <cmath>
#include <vector>

using namespace aether;
using namespace aether::ui;

namespace {

struct OwnerWindow {
    HWND hwnd = nullptr;
    OwnerWindow() {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &::DefWindowProcW;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AetherShell.Tests.MenuOwner";
        if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"Menu owner", WS_POPUP, 0, 0, 10, 10,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    }
    ~OwnerWindow() {
        if (hwnd) ::DestroyWindow(hwnd);
    }
};

// Plays the part of the menu loop: while "tracking" it sends the owner-draw messages Windows would send.
class TrackingOps final : public IMenuWindowOps {
public:
    HWND owner = nullptr;
    HWND foreground = reinterpret_cast<HWND>(0x200);
    UINT command = 0;
    bool ownerDrawn = false;
    UINT measuredHeight = 0;
    LRESULT measureResult = 0;
    LRESULT drawResult = 0;
    LRESULT menuCharResult = 0;

    HWND Foreground() noexcept override { return foreground; }
    bool SetForeground(HWND window) noexcept override {
        foreground = window;
        return true;
    }
    bool IsValidWindow(HWND window) noexcept override { return window == owner || window == reinterpret_cast<HWND>(0x200); }
    LONG_PTR ExtendedStyle(HWND) noexcept override { return WS_EX_NOACTIVATE; }
    void SetExtendedStyle(HWND, LONG_PTR) noexcept override {}
    UINT Track(HMENU menu, UINT, POINT, HWND window, const TPMPARAMS*) noexcept override {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_ID;
        if (!::GetMenuItemInfoW(menu, 0, TRUE, &info)) return 0;
        ownerDrawn = (info.fType & MFT_OWNERDRAW) != 0;

        MEASUREITEMSTRUCT measure{};
        measure.CtlType = ODT_MENU;
        measure.itemID = info.wID;
        measure.itemData = info.dwItemData;
        measureResult = ::SendMessageW(window, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure));
        measuredHeight = measure.itemHeight;

        HDC dc = ::CreateCompatibleDC(nullptr);
        HBITMAP bitmap = ::CreateCompatibleBitmap(dc, 200, 40);
        const HGDIOBJ previous = ::SelectObject(dc, bitmap);
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_MENU;
        draw.itemID = info.wID;
        draw.itemAction = ODA_DRAWENTIRE;
        draw.hDC = dc;
        draw.rcItem = RECT{0, 0, 200, 30};
        draw.itemData = info.dwItemData;
        drawResult = ::SendMessageW(window, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw));
        ::SelectObject(dc, previous);
        ::DeleteObject(bitmap);
        ::DeleteDC(dc);

        menuCharResult = ::SendMessageW(window, WM_MENUCHAR, MAKEWPARAM(L'p', MF_POPUP), reinterpret_cast<LPARAM>(menu));
        return command;
    }
    void PostNull(HWND) noexcept override {}
};

std::vector<MenuEntry> Entries() {
    return {MenuEntry::Command(1, L"&Open"), MenuEntry::Separator(), MenuEntry::Command(2, L"&Pin to Dock")};
}

}  // namespace

AETHER_TEST("themed menu presenter: the owner answers owner-draw messages only while the menu is tracked") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    OwnerWindow owner;
    REQUIRE(owner.hwnd != nullptr);
    CHECK(::IsWindowVisible(owner.hwnd) == FALSE);
    TrackingOps ops;
    ops.owner = owner.hwnd;
    ops.command = 2;
    ThemedMenuPresenter presenter(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), ops);
    MenuRequest request;
    request.owner = owner.hwnd;
    request.anchor = RECT{100, 900, 140, 940};
    const auto entries = Entries();
    auto selection = presenter.Show(entries, request);
    REQUIRE(selection.ok());
    CHECK_EQ(selection->command, 2u);
    // High contrast on this machine makes the presenter fall back to the native menu by design.
    if (graphics::DetectThemeMode() == graphics::ThemeMode::HighContrast) {
        CHECK_FALSE(presenter.LastShowWasThemed());
        return;
    }
    CHECK(presenter.LastShowWasThemed());
    CHECK(ops.ownerDrawn);
    CHECK_EQ(ops.measureResult, static_cast<LRESULT>(TRUE));
    const UINT dpi = ::GetDpiForWindow(owner.hwnd);
    CHECK_EQ(ops.measuredHeight, static_cast<UINT>(std::lround(ThemedMenuMetrics{}.itemHeight * static_cast<float>(dpi) / 96.0f)));
    CHECK_EQ(ops.drawResult, static_cast<LRESULT>(TRUE));
    CHECK_EQ(HIWORD(ops.menuCharResult), static_cast<WORD>(MNC_EXECUTE));
    CHECK_EQ(LOWORD(ops.menuCharResult), static_cast<WORD>(2));

    // After the menu closed the subclass is gone: the owner's own procedure answers again.
    MEASUREITEMSTRUCT measure{};
    measure.CtlType = ODT_MENU;
    measure.itemHeight = 7;
    CHECK_EQ(::SendMessageW(owner.hwnd, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)), static_cast<LRESULT>(0));
    CHECK_EQ(measure.itemHeight, 7u);
}
