// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Owner-drawn popup menus: label parsing, the owner-draw item data that keeps MSAA names, measuring,
// drawing into an offscreen DC, mnemonics, and the high-contrast and non-window fallbacks to the
// native presenter. No menu is shown and no real window or foreground is touched.
#include "AetherTest.h"

#include "graphics/Theme.h"
#include "ui/ThemedMenu.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::ui;

namespace {

HWND FakeWindow(std::uintptr_t value) { return reinterpret_cast<HWND>(value); }

class FakeOps final : public IMenuWindowOps {
public:
    HWND foreground = FakeWindow(0x200);
    UINT command = 0;
    int tracked = 0;
    bool ownerDrawnTracked = false;

    HWND Foreground() noexcept override { return foreground; }
    bool SetForeground(HWND window) noexcept override {
        foreground = window;
        return true;
    }
    bool IsValidWindow(HWND window) noexcept override { return window == FakeWindow(0x100) || window == FakeWindow(0x200); }
    LONG_PTR ExtendedStyle(HWND) noexcept override { return WS_EX_NOACTIVATE; }
    void SetExtendedStyle(HWND, LONG_PTR) noexcept override {}
    UINT Track(HMENU menu, UINT, POINT, HWND, const TPMPARAMS*) noexcept override {
        ++tracked;
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE;
        if (::GetMenuItemInfoW(menu, 0, TRUE, &info)) ownerDrawnTracked = (info.fType & MFT_OWNERDRAW) != 0;
        return command;
    }
    void PostNull(HWND) noexcept override {}
};

std::vector<MenuEntry> SampleMenu() {
    std::vector<MenuEntry> size{MenuEntry::Command(10, L"&Small"), MenuEntry::Command(11, L"&Medium"),
                                MenuEntry::Command(12, L"&Large")};
    size[1].checked = true;
    for (auto& entry : size) entry.radio = true;
    auto pinned = MenuEntry::Command(4, L"&Keep in Dock");
    pinned.checked = true;
    return {MenuEntry::Command(1, L"&Open"), MenuEntry::Command(2, L"&New window\tCtrl+N", false), MenuEntry::Separator(),
            MenuEntry::Submenu(L"Dock &size", std::move(size)), pinned, MenuEntry::Command(3, L"Settings && help")};
}

graphics::DesignTokens Tokens(graphics::ThemeMode mode = graphics::ThemeMode::Dark) { return graphics::MakeDesignTokens(mode); }

MENUITEMINFOW ItemInfo(HMENU menu, UINT position) {
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STATE | MIIM_ID | MIIM_SUBMENU;
    (void)::GetMenuItemInfoW(menu, position, TRUE, &info);
    return info;
}

// An offscreen 32-bit DIB to receive WM_DRAWITEM output.
struct Offscreen {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    std::uint32_t* bits = nullptr;
    int width = 0;
    int height = 0;

    Offscreen(int w, int h) : width(w), height(h) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        dc = ::CreateCompatibleDC(nullptr);
        void* raw = nullptr;
        bitmap = ::CreateDIBSection(dc, &info, DIB_RGB_COLORS, &raw, nullptr, 0);
        bits = static_cast<std::uint32_t*>(raw);
        if (dc && bitmap) previous = ::SelectObject(dc, bitmap);
    }
    ~Offscreen() {
        if (previous) ::SelectObject(dc, previous);
        if (bitmap) ::DeleteObject(bitmap);
        if (dc) ::DeleteDC(dc);
    }
    [[nodiscard]] COLORREF At(int x, int y) const {
        const std::uint32_t value = bits[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
        return RGB((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
    }
};

bool Close(COLORREF a, COLORREF b, int tolerance) {
    return std::abs(GetRValue(a) - GetRValue(b)) <= tolerance && std::abs(GetGValue(a) - GetGValue(b)) <= tolerance &&
           std::abs(GetBValue(a) - GetBValue(b)) <= tolerance;
}

}  // namespace

AETHER_TEST("themed menu: labels split into text, accelerator and mnemonic") {
    auto label = ParseMenuLabel(L"&Open");
    CHECK(label.text == L"Open");
    CHECK_EQ(label.mnemonic, L'o');
    CHECK_EQ(label.mnemonicIndex, std::size_t{0});
    CHECK(label.accelerator.empty());

    label = ParseMenuLabel(L"New &Window\tCtrl+N");
    CHECK(label.text == L"New Window");
    CHECK(label.accelerator == L"Ctrl+N");
    CHECK_EQ(label.mnemonic, L'w');
    CHECK_EQ(label.mnemonicIndex, std::size_t{4});

    label = ParseMenuLabel(L"Settings && help");
    CHECK(label.text == L"Settings & help");
    CHECK_EQ(label.mnemonic, L'\0');
    CHECK_EQ(label.mnemonicIndex, std::wstring::npos);

    label = ParseMenuLabel(L"Trailing&");
    CHECK(label.text == L"Trailing");
    label = ParseMenuLabel(L"&First &second");
    CHECK_EQ(label.mnemonic, L'f');  // only the first marker counts
    CHECK(label.text == L"First second");
}

AETHER_TEST("themed menu: items are owner drawn and keep their ids, states and MSAA names") {
    const auto entries = SampleMenu();
    auto built = ThemedMenu::Build(entries, Tokens(), 96);
    REQUIRE(built.ok());
    auto& menu = **built;
    REQUIRE(menu.OwnerDrawn());
    const HMENU handle = menu.Handle();
    REQUIRE(handle != nullptr);
    REQUIRE_EQ(::GetMenuItemCount(handle), 6);

    const auto open = ItemInfo(handle, 0);
    CHECK((open.fType & MFT_OWNERDRAW) != 0);
    CHECK_EQ(open.wID, 1u);
    CHECK((open.fState & MFS_DISABLED) == 0);
    const auto* data = ThemedMenu::ItemFromData(open.dwItemData);
    REQUIRE(data != nullptr);
    // MSAAMENUINFO first: screen readers read the label without '&'.
    const auto* msaa = reinterpret_cast<const MSAAMENUINFO*>(open.dwItemData);
    CHECK_EQ(msaa->dwMSAASignature, static_cast<DWORD>(MSAA_MENU_SIG));
    CHECK(std::wstring(msaa->pszWText, msaa->cchWText) == L"Open");

    const auto newWindow = ItemInfo(handle, 1);
    CHECK((newWindow.fState & MFS_DISABLED) != 0);
    CHECK(ThemedMenu::ItemFromData(newWindow.dwItemData)->label.accelerator == L"Ctrl+N");

    const auto separator = ItemInfo(handle, 2);
    CHECK((separator.fType & MFT_SEPARATOR) != 0);
    CHECK((separator.fType & MFT_OWNERDRAW) != 0);

    const auto submenu = ItemInfo(handle, 3);
    REQUIRE(submenu.hSubMenu != nullptr);
    REQUIRE_EQ(::GetMenuItemCount(submenu.hSubMenu), 3);
    const auto medium = ItemInfo(submenu.hSubMenu, 1);
    CHECK_EQ(medium.wID, 11u);
    CHECK((medium.fType & (MFT_OWNERDRAW | MFT_RADIOCHECK)) == (MFT_OWNERDRAW | MFT_RADIOCHECK));
    CHECK((medium.fState & MFS_CHECKED) != 0);
    CHECK(ThemedMenu::ItemFromData(medium.dwItemData)->radio);

    const auto pinned = ItemInfo(handle, 4);
    CHECK((pinned.fState & MFS_CHECKED) != 0);
    CHECK((pinned.fType & MFT_RADIOCHECK) == 0);

    // Foreign item data is never mistaken for ours.
    CHECK(ThemedMenu::ItemFromData(0) == nullptr);
    MSAAMENUINFO foreign{};
    CHECK(ThemedMenu::ItemFromData(reinterpret_cast<ULONG_PTR>(&foreign)) == nullptr);
}

AETHER_TEST("themed menu: high contrast builds a plain native menu with the same ids") {
    const auto entries = SampleMenu();
    auto built = ThemedMenu::Build(entries, Tokens(graphics::ThemeMode::HighContrast), 96);
    REQUIRE(built.ok());
    auto& menu = **built;
    CHECK_FALSE(menu.OwnerDrawn());
    REQUIRE_EQ(::GetMenuItemCount(menu.Handle()), 6);
    for (UINT position = 0; position < 6; ++position) CHECK((ItemInfo(menu.Handle(), position).fType & MFT_OWNERDRAW) == 0);
    CHECK_EQ(ItemInfo(menu.Handle(), 0).wID, 1u);
    // A native menu answers no owner-draw messages.
    MEASUREITEMSTRUCT measure{};
    measure.CtlType = ODT_MENU;
    CHECK_FALSE(menu.HandleMessage(nullptr, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)).has_value());
}

AETHER_TEST("themed menu: invalid menus are rejected before anything is built") {
    std::vector<MenuEntry> duplicate{MenuEntry::Command(1, L"A"), MenuEntry::Command(1, L"B")};
    CHECK_FALSE(ThemedMenu::Build(duplicate, Tokens()).ok());
    CHECK_FALSE(ThemedMenu::Build(std::vector<MenuEntry>{}, Tokens()).ok());
}

AETHER_TEST("themed menu: items are measured in DIPs scaled to the owner's DPI") {
    const auto entries = SampleMenu();
    auto at96 = ThemedMenu::Build(entries, Tokens(), 96);
    auto at144 = ThemedMenu::Build(entries, Tokens(), 144);
    REQUIRE(at96.ok());
    REQUIRE(at144.ok());
    const auto measure = [](ThemedMenu& menu, UINT position) {
        MEASUREITEMSTRUCT result{};
        result.CtlType = ODT_MENU;
        result.itemData = ItemInfo(menu.Handle(), position).dwItemData;
        const auto handled = menu.HandleMessage(nullptr, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&result));
        CHECK(handled.has_value() && *handled == TRUE);
        return result;
    };
    const auto& metrics = (*at96)->Metrics();
    CHECK_EQ(measure(**at96, 0).itemHeight, static_cast<UINT>(std::lround(metrics.itemHeight)));
    CHECK_EQ(measure(**at144, 0).itemHeight, static_cast<UINT>(std::lround(metrics.itemHeight * 1.5f)));
    CHECK_EQ(measure(**at96, 2).itemHeight, static_cast<UINT>(std::lround(metrics.separatorHeight)));
    // Longer labels and accelerator text make wider items.
    CHECK(measure(**at96, 1).itemWidth > measure(**at96, 0).itemWidth);
    CHECK(measure(**at144, 1).itemWidth > measure(**at96, 1).itemWidth);

    // Items of other menus (or other controls) pass through untouched.
    MEASUREITEMSTRUCT other{};
    other.CtlType = ODT_MENU;
    auto foreign = ThemedMenu::Build(entries, Tokens(), 96);
    REQUIRE(foreign.ok());
    other.itemData = ItemInfo((*foreign)->Handle(), 0).dwItemData;
    CHECK_FALSE((*at96)->HandleMessage(nullptr, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&other)).has_value());
    other.CtlType = ODT_BUTTON;
    CHECK_FALSE((*foreign)->HandleMessage(nullptr, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&other)).has_value());
}

AETHER_TEST("themed menu: mnemonics choose enabled items at the matching level") {
    const auto entries = SampleMenu();
    auto built = ThemedMenu::Build(entries, Tokens(), 96);
    REQUIRE(built.ok());
    auto& menu = **built;
    const auto menuChar = [&](wchar_t typed, HMENU level) {
        return menu.HandleMessage(nullptr, WM_MENUCHAR, MAKEWPARAM(typed, MF_POPUP), reinterpret_cast<LPARAM>(level));
    };
    auto result = menuChar(L'O', menu.Handle());
    REQUIRE(result.has_value());
    CHECK_EQ(HIWORD(*result), static_cast<WORD>(MNC_EXECUTE));
    CHECK_EQ(LOWORD(*result), static_cast<WORD>(0));
    result = menuChar(L'k', menu.Handle());
    REQUIRE(result.has_value());
    CHECK_EQ(LOWORD(*result), static_cast<WORD>(4));
    // 'n' belongs to a disabled item; 'z' to nothing.
    CHECK_EQ(HIWORD(*menuChar(L'n', menu.Handle())), static_cast<WORD>(MNC_IGNORE));
    CHECK_EQ(HIWORD(*menuChar(L'z', menu.Handle())), static_cast<WORD>(MNC_IGNORE));
    // Submenu level.
    const HMENU sizes = ItemInfo(menu.Handle(), 3).hSubMenu;
    result = menuChar(L'l', sizes);
    REQUIRE(result.has_value());
    CHECK_EQ(HIWORD(*result), static_cast<WORD>(MNC_EXECUTE));
    CHECK_EQ(LOWORD(*result), static_cast<WORD>(2));
    // A menu that is not ours is left to the caller.
    UniqueMenu foreign(::CreatePopupMenu());
    CHECK_FALSE(menuChar(L'o', foreign.get()).has_value());
}

AETHER_TEST("themed menu: foreign item data is rejected without dereferencing it") {
    const auto entries = SampleMenu();
    auto built = ThemedMenu::Build(entries, Tokens());
    auto other = ThemedMenu::Build(entries, Tokens());
    REQUIRE(built.ok());
    REQUIRE(other.ok());
    MSAAMENUINFO unrelated{};
    unrelated.dwMSAASignature = MSAA_MENU_SIG;  // even a readable signature is not an owned item
    const ULONG_PTR foreign[] = {1u, reinterpret_cast<ULONG_PTR>(&unrelated),
                                 ItemInfo((*other)->Handle(), 0).dwItemData};
    for (const ULONG_PTR value : foreign) {
        MEASUREITEMSTRUCT measure{};
        measure.CtlType = ODT_MENU;
        measure.itemData = value;
        measure.itemHeight = 7;
        CHECK_FALSE((*built)->HandleMessage(nullptr, WM_MEASUREITEM, 0, reinterpret_cast<LPARAM>(&measure)).has_value());
        CHECK_EQ(measure.itemHeight, 7u);
        DRAWITEMSTRUCT draw{};
        draw.CtlType = ODT_MENU;
        draw.itemData = value;
        CHECK_FALSE((*built)->HandleMessage(nullptr, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&draw)).has_value());
    }
    CHECK(ThemedMenu::ItemFromData(1u) == nullptr);
    const auto released = ItemInfo((*other)->Handle(), 0).dwItemData;
    CHECK(ThemedMenu::ItemFromData(released) != nullptr);
    other->reset();
    CHECK(ThemedMenu::ItemFromData(released) == nullptr);
}

AETHER_TEST("themed menu: items draw the popup background, a selection pill and their label") {
    const auto tokens = Tokens(graphics::ThemeMode::Dark);
    const auto entries = SampleMenu();
    auto built = ThemedMenu::Build(entries, tokens, 96);
    REQUIRE(built.ok());
    auto& menu = **built;
    Offscreen surface(200, 64);
    REQUIRE(surface.bits != nullptr);
    const auto draw = [&](UINT position, UINT state, RECT bounds) {
        DRAWITEMSTRUCT item{};
        item.CtlType = ODT_MENU;
        item.itemID = ItemInfo(menu.Handle(), position).wID;
        item.itemAction = ODA_DRAWENTIRE;
        item.itemState = state;
        item.hDC = surface.dc;
        item.rcItem = bounds;
        item.itemData = ItemInfo(menu.Handle(), position).dwItemData;
        const auto handled = menu.HandleMessage(nullptr, WM_DRAWITEM, 0, reinterpret_cast<LPARAM>(&item));
        CHECK(handled.has_value());
        ::GdiFlush();
    };
    draw(0, 0, RECT{0, 0, 200, 30});
    draw(0, ODS_SELECTED, RECT{0, 32, 200, 62});
    const COLORREF background = tokens.surface.popupBackground.ToColorRef();
    CHECK(Close(surface.At(2, 15), background, 2));    // left of the pill
    CHECK(Close(surface.At(8, 15), background, 2));    // unselected: no pill
    CHECK(Close(surface.At(2, 47), background, 2));
    CHECK_FALSE(Close(surface.At(8, 47), background, 2));  // selected: the hover pill
    const COLORREF pill = graphics::CompositeOver(tokens.hoverOverlay, tokens.surface.popupBackground).ToColorRef();
    CHECK(Close(surface.At(8, 47), pill, 3));
    // Label pixels: some text-colored pixels in the label column.
    int ink = 0;
    for (int y = 4; y < 26; ++y) {
        for (int x = 36; x < 90; ++x) {
            if (!Close(surface.At(x, y), background, 40)) ++ink;
        }
    }
    CHECK(ink > 20);
}

AETHER_TEST("themed menu presenter: windows of other threads, fake owners and high contrast use the native menu") {
    const auto entries = SampleMenu();
    FakeOps ops;
    ops.command = 11;
    ThemedMenuPresenter presenter(Tokens(), ops);
    MenuRequest request;
    request.owner = FakeWindow(0x100);  // not a window of this thread: cannot be subclassed
    request.anchor = RECT{100, 900, 140, 940};
    auto selection = presenter.Show(entries, request);
    REQUIRE(selection.ok());
    CHECK_EQ(selection->command, 11u);
    CHECK_FALSE(presenter.LastShowWasThemed());
    CHECK_EQ(ops.tracked, 1);
    CHECK_FALSE(ops.ownerDrawnTracked);

    presenter.SetTheme(Tokens(graphics::ThemeMode::HighContrast));
    selection = presenter.Show(entries, request);
    REQUIRE(selection.ok());
    CHECK_FALSE(presenter.LastShowWasThemed());
    CHECK_FALSE(ops.ownerDrawnTracked);

    std::vector<MenuEntry> invalid{MenuEntry::Command(0, L"Zero")};
    CHECK_FALSE(presenter.Show(invalid, request).ok());
    CHECK_EQ(ops.tracked, 2);

    auto made = MakeThemedMenuPresenter(Tokens());
    REQUIRE(made.ok());
    CHECK(*made != nullptr);
}
