// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Owner-drawn popup menus in the design language, behind the shared IMenuPresenter interface.
//
// ThemedMenu builds a native HMENU from MenuEntry values with MFT_OWNERDRAW items. Each item's
// dwItemData points to data that starts with an MSAAMENUINFO (MSAA_MENU_SIG plus the label), so
// screen readers still read the item names; WM_MENUCHAR keeps '&' mnemonics working; the menu
// background, hover pill, check marks, accelerator text and submenu chevrons follow the design
// tokens. The frame and drop shadow stay system-drawn.
//
// ThemedMenuPresenter shows it under the same foreground rule as NativeMenuPresenter. While the menu
// is tracked it subclasses the owner window (SetWindowSubclass) to answer WM_MEASUREITEM,
// WM_DRAWITEM and WM_MENUCHAR, so owners need no forwarding. It falls back to the native presenter in
// high contrast (system-drawn menus with system colors) and whenever the owner cannot be subclassed
// or the themed menu cannot be built; command ids are identical either way.
#pragma once

#include "graphics/TextCache.h"
#include "graphics/Theme.h"
#include "ui/PopupMenu.h"

#include <oleacc.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aether::ui {

// A menu label split into the drawn text (without '&' markers; "&&" is a literal '&'), the
// right-aligned accelerator text after '\t', and the lower-case mnemonic with its index in `text`.
struct MenuLabel {
    std::wstring text;
    std::wstring accelerator;
    wchar_t mnemonic = 0;
    std::size_t mnemonicIndex = std::wstring::npos;
};
[[nodiscard]] MenuLabel ParseMenuLabel(std::wstring_view label);

// Item metrics in DIPs.
struct ThemedMenuMetrics {
    float itemHeight = 30.0f;
    float separatorHeight = 9.0f;
    float paddingX = 12.0f;      // left and right edge to content
    float checkColumn = 16.0f;   // check mark / radio bullet column
    float columnGap = 8.0f;      // between the check column and the label
    float acceleratorGap = 32.0f;
    float submenuColumn = 16.0f;
    float pillInsetX = 4.0f;     // hover pill inset inside the item
    float pillInsetY = 2.0f;
    float minimumWidth = 120.0f;
};

// Owner-draw data of one item. MSAAMENUINFO must stay the first member.
struct ThemedMenuItem {
    MSAAMENUINFO msaa{};
    std::uint32_t magic = 0;
    MenuLabel label;
    bool separator = false;
    bool checked = false;
    bool radio = false;
    bool enabled = true;
    bool submenu = false;
};

class ThemedMenu {
public:
    ThemedMenu(const ThemedMenu&) = delete;
    ThemedMenu& operator=(const ThemedMenu&) = delete;
    ~ThemedMenu();

    // Validates `entries` (ValidateMenu) and builds the menu for an owner at `dpi`. High contrast
    // tokens build a plain native menu (OwnerDrawn() false) with the same ids and states.
    [[nodiscard]] static Result<std::unique_ptr<ThemedMenu>> Build(std::span<const MenuEntry> entries,
                                                                  const graphics::DesignTokens& tokens, UINT dpi = 96);

    [[nodiscard]] HMENU Handle() const noexcept { return menu_.get(); }
    [[nodiscard]] bool OwnerDrawn() const noexcept { return ownerDrawn_; }
    [[nodiscard]] const ThemedMenuMetrics& Metrics() const noexcept { return metrics_; }

    // Answers WM_MEASUREITEM, WM_DRAWITEM and WM_MENUCHAR for this menu's items (any level); nullopt
    // for every other message or item, which the caller passes on.
    [[nodiscard]] std::optional<LRESULT> HandleMessage(HWND owner, UINT message, WPARAM wParam, LPARAM lParam);

    // The item data behind a menu item's dwItemData, or null when the item is not one of ours.
    [[nodiscard]] static const ThemedMenuItem* ItemFromData(ULONG_PTR data) noexcept;

private:
    ThemedMenu() = default;
    [[nodiscard]] Result<UniqueMenu> BuildLevel(std::span<const MenuEntry> entries);
    [[nodiscard]] bool OwnsMenu(HMENU menu) const noexcept;
    [[nodiscard]] bool OwnsItem(const ThemedMenuItem* item) const noexcept;
    [[nodiscard]] float ToPx(float dip) const noexcept { return dip * static_cast<float>(dpi_) / 96.0f; }
    [[nodiscard]] float MeasureText(std::wstring_view text);
    void Measure(MEASUREITEMSTRUCT& measure, const ThemedMenuItem& item);
    void Draw(const DRAWITEMSTRUCT& draw, const ThemedMenuItem& item);
    [[nodiscard]] bool EnsureRenderTarget();

    UniqueMenu menu_;
    std::vector<std::unique_ptr<ThemedMenuItem>> items_;
    std::vector<HMENU> levels_;
    graphics::DesignTokens tokens_;
    ThemedMenuMetrics metrics_;
    UINT dpi_ = 96;
    bool ownerDrawn_ = false;
    HBRUSH background_ = nullptr;
    winrt::com_ptr<ID2D1Factory> factory_;
    winrt::com_ptr<ID2D1DCRenderTarget> target_;
    winrt::com_ptr<IDWriteFactory> dwrite_;
    graphics::TextCache text_;
};

class ThemedMenuPresenter final : public IMenuPresenter {
public:
    explicit ThemedMenuPresenter(const graphics::DesignTokens& tokens, IMenuWindowOps& ops = SystemMenuWindowOps());
    void SetTheme(const graphics::DesignTokens& tokens);
    Result<MenuSelection> Show(std::span<const MenuEntry> entries, const MenuRequest& request) override;

    // How the last Show presented the menu (for tests and diagnostics).
    [[nodiscard]] bool LastShowWasThemed() const noexcept { return lastThemed_; }

private:
    graphics::DesignTokens tokens_;
    IMenuWindowOps* ops_;
    bool lastThemed_ = false;
};

[[nodiscard]] Result<std::unique_ptr<IMenuPresenter>> MakeThemedMenuPresenter(const graphics::DesignTokens& tokens);

}  // namespace aether::ui
