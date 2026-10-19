// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "ui/ThemedMenu.h"

#include "diagnostics/Logger.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "platform/windows/WindowUtilities.h"

#include <commctrl.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace aether::ui {
namespace {

constexpr std::uint32_t kItemMagic = 0x41455448u;  // "AETH"
constexpr UINT_PTR kSubclassId = 0x4D454E55u;     // "MENU"

LRESULT CALLBACK OwnerSubclass(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR reference) {
    auto* menu = reinterpret_cast<ThemedMenu*>(reference);
    if (menu && (message == WM_MEASUREITEM || message == WM_DRAWITEM || message == WM_MENUCHAR)) {
        try {
            if (auto handled = menu->HandleMessage(hwnd, message, wParam, lParam)) return *handled;
        } catch (...) {
            AETHER_LOG_ERROR("ui", "themed menu message {} threw", message);
        }
    }
    if (message == WM_NCDESTROY) ::RemoveWindowSubclass(hwnd, &OwnerSubclass, id);
    return ::DefSubclassProc(hwnd, message, wParam, lParam);
}

bool OwnedByThisThread(HWND window) noexcept {
    return window && ::IsWindow(window) && ::GetWindowThreadProcessId(window, nullptr) == ::GetCurrentThreadId();
}

}  // namespace

MenuLabel ParseMenuLabel(std::wstring_view label) {
    MenuLabel parsed;
    const auto tab = label.find(L'\t');
    const std::wstring_view text = label.substr(0, tab);
    if (tab != std::wstring_view::npos) parsed.accelerator.assign(label.substr(tab + 1));
    parsed.text.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        const wchar_t ch = text[index];
        if (ch == L'&' && index + 1 < text.size()) {
            const wchar_t next = text[index + 1];
            ++index;
            if (next != L'&' && parsed.mnemonic == 0) {
                parsed.mnemonic = static_cast<wchar_t>(std::towlower(next));
                parsed.mnemonicIndex = parsed.text.size();
            }
            parsed.text.push_back(next);
            continue;
        }
        if (ch == L'&') continue;  // a trailing '&' marks nothing
        parsed.text.push_back(ch);
    }
    return parsed;
}

ThemedMenu::~ThemedMenu() {
    menu_.reset();
    if (background_) ::DeleteObject(background_);
}

const ThemedMenuItem* ThemedMenu::ItemFromData(ULONG_PTR data) noexcept {
    const auto* item = reinterpret_cast<const ThemedMenuItem*>(data);
    if (!item || item->msaa.dwMSAASignature != MSAA_MENU_SIG || item->magic != kItemMagic) return nullptr;
    return item;
}

Result<std::unique_ptr<ThemedMenu>> ThemedMenu::Build(std::span<const MenuEntry> entries,
                                                      const graphics::DesignTokens& tokens, UINT dpi) {
    try {
        if (auto valid = ValidateMenu(entries); !valid) return valid.error();
        std::unique_ptr<ThemedMenu> menu(new ThemedMenu());
        menu->tokens_ = tokens;
        menu->dpi_ = dpi >= 48 && dpi <= 960 ? dpi : 96;
        if (tokens.mode == graphics::ThemeMode::HighContrast) {
            auto native = BuildNativeMenu(entries);
            if (!native) return native.error();
            menu->menu_ = std::move(*native);
            return menu;
        }
        auto built = menu->BuildLevel(entries);
        if (!built) return built.error();
        menu->menu_ = std::move(*built);
        menu->ownerDrawn_ = true;
        menu->background_ = ::CreateSolidBrush(tokens.surface.popupBackground.ToColorRef());
        if (menu->background_) {
            MENUINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
            info.hbrBack = menu->background_;
            ::SetMenuInfo(menu->menu_.get(), &info);
        }
        if (SUCCEEDED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                            reinterpret_cast<IUnknown**>(menu->dwrite_.put())))) {
            menu->text_.Initialize(menu->dwrite_.get(), tokens.fontFamily, tokens.displayFontFamily);
        }
        return menu;
    } catch (...) {
        return MakeError(ErrorCode::Internal, "themed menu could not be built");
    }
}

Result<UniqueMenu> ThemedMenu::BuildLevel(std::span<const MenuEntry> entries) {
    UniqueMenu menu(::CreatePopupMenu());
    if (!menu) return MakeError(ErrorCode::Platform, "popup menu could not be created", static_cast<std::int64_t>(::GetLastError()));
    for (const auto& entry : entries) {
        auto item = std::make_unique<ThemedMenuItem>();
        item->magic = kItemMagic;
        item->separator = entry.separator;
        item->checked = entry.checked;
        item->radio = entry.radio;
        item->enabled = entry.enabled;
        item->submenu = !entry.separator && !entry.submenu.empty();
        if (!entry.separator) item->label = ParseMenuLabel(entry.text);
        item->msaa.dwMSAASignature = MSAA_MENU_SIG;
        item->msaa.cchWText = static_cast<DWORD>(item->label.text.size());
        item->msaa.pszWText = item->label.text.data();

        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_DATA | MIIM_STATE;
        info.fType = MFT_OWNERDRAW | (entry.separator ? MFT_SEPARATOR : 0u) | (entry.radio ? MFT_RADIOCHECK : 0u);
        info.fState = (entry.enabled ? MFS_ENABLED : MFS_DISABLED) | (entry.checked ? MFS_CHECKED : MFS_UNCHECKED);
        info.dwItemData = reinterpret_cast<ULONG_PTR>(item.get());
        UniqueMenu child;
        if (item->submenu) {
            auto nested = BuildLevel(entry.submenu);
            if (!nested) return nested.error();
            child = std::move(*nested);
            info.fMask |= MIIM_SUBMENU;
            info.hSubMenu = child.get();
        } else if (!entry.separator) {
            info.fMask |= MIIM_ID;
            info.wID = entry.id;
        }
        const int position = ::GetMenuItemCount(menu.get());
        if (position < 0 || !::InsertMenuItemW(menu.get(), static_cast<UINT>(position), TRUE, &info)) {
            return MakeError(ErrorCode::Platform, "popup menu entry could not be added", static_cast<std::int64_t>(::GetLastError()));
        }
        if (child) levels_.push_back(child.get());
        (void)child.release();  // owned by the parent menu now
        items_.push_back(std::move(item));
    }
    levels_.push_back(menu.get());
    return menu;
}

bool ThemedMenu::OwnsMenu(HMENU menu) const noexcept {
    return menu && std::find(levels_.begin(), levels_.end(), menu) != levels_.end();
}

bool ThemedMenu::OwnsItem(const ThemedMenuItem* item) const noexcept {
    if (!item) return false;
    return std::any_of(items_.begin(), items_.end(), [item](const auto& owned) { return owned.get() == item; });
}

float ThemedMenu::MeasureText(std::wstring_view text) {
    if (text.empty()) return 0.0f;
    graphics::TextStyle style;
    style.size = tokens_.type.body;
    style.ellipsis = false;
    return text_.Measure(text, style).width;
}

void ThemedMenu::Measure(MEASUREITEMSTRUCT& measure, const ThemedMenuItem& item) {
    if (item.separator) {
        measure.itemWidth = static_cast<UINT>(std::lround(ToPx(metrics_.minimumWidth)));
        measure.itemHeight = static_cast<UINT>(std::lround(ToPx(metrics_.separatorHeight)));
        return;
    }
    float width = metrics_.paddingX + metrics_.checkColumn + metrics_.columnGap + MeasureText(item.label.text) +
                  metrics_.paddingX;
    if (!item.label.accelerator.empty()) width += metrics_.acceleratorGap + MeasureText(item.label.accelerator);
    if (item.submenu) width += metrics_.submenuColumn;
    width = std::max(width, metrics_.minimumWidth);
    // Windows adds the width of a check-mark bitmap to owner-drawn items; leave it out of ours.
    const int checkWidth = std::max(0, ::GetSystemMetricsForDpi(SM_CXMENUCHECK, dpi_) - 1);
    measure.itemWidth = static_cast<UINT>(std::max(1L, std::lround(ToPx(width)) - checkWidth));
    measure.itemHeight = static_cast<UINT>(std::lround(ToPx(metrics_.itemHeight)));
}

bool ThemedMenu::EnsureRenderTarget() {
    if (target_) return true;
    if (!factory_ && FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), nullptr,
                                                factory_.put_void()))) {
        return false;
    }
    const auto dpi = static_cast<float>(dpi_);
    const auto properties = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), dpi, dpi,
        D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE);
    return SUCCEEDED(factory_->CreateDCRenderTarget(&properties, target_.put()));
}

void ThemedMenu::Draw(const DRAWITEMSTRUCT& draw, const ThemedMenuItem& item) {
    if (!draw.hDC || !EnsureRenderTarget()) return;
    RECT bounds = draw.rcItem;
    if (FAILED(target_->BindDC(draw.hDC, &bounds))) return;
    const float width = static_cast<float>(bounds.right - bounds.left) * 96.0f / static_cast<float>(dpi_);
    const float height = static_cast<float>(bounds.bottom - bounds.top) * 96.0f / static_cast<float>(dpi_);
    const bool selected = (draw.itemState & ODS_SELECTED) != 0;
    const bool disabled = (draw.itemState & (ODS_DISABLED | ODS_GRAYED)) != 0 || !item.enabled;
    const bool checked = (draw.itemState & ODS_CHECKED) != 0 || item.checked;
    const bool showAccelerators = (draw.itemState & ODS_NOACCEL) == 0;

    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    target_->Clear(tokens_.surface.popupBackground.ToD2D());
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    target_->CreateSolidColorBrush(tokens_.textPrimary.ToD2D(), brush.put());
    if (brush && item.separator) {
        brush->SetColor(tokens_.surface.divider.ToD2D());
        const float y = std::floor(height / 2.0f) + 0.5f;
        target_->DrawLine(D2D1::Point2F(metrics_.pillInsetX, y), D2D1::Point2F(width - metrics_.pillInsetX, y), brush.get(), 1.0f);
    } else if (brush) {
        if (selected) {
            graphics::PaintSelectionPill(target_.get(),
                                         D2D1::RectF(metrics_.pillInsetX, metrics_.pillInsetY, width - metrics_.pillInsetX,
                                                     height - metrics_.pillInsetY),
                                         tokens_.radii.control, tokens_, true, false);
        }
        const graphics::Color textColor = disabled ? tokens_.textDisabled : tokens_.textPrimary;
        brush->SetColor(textColor.ToD2D());
        const float checkLeft = metrics_.paddingX;
        const float centerY = height / 2.0f;
        if (checked) {
            if (item.radio) {
                target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(checkLeft + metrics_.checkColumn / 2.0f, centerY), 3.0f, 3.0f),
                                     brush.get());
            } else {
                const float box = 12.0f;
                const float left = checkLeft + (metrics_.checkColumn - box) / 2.0f;
                graphics::PaintGlyph(target_.get(), graphics::Glyph::Check,
                                     D2D1::RectF(left, centerY - box / 2.0f, left + box, centerY + box / 2.0f), brush.get(),
                                     tokens_.glyph.stroke * 1.2f);
            }
        }
        graphics::TextStyle style;
        style.size = tokens_.type.body;
        style.ellipsis = false;
        const float textLeft = checkLeft + metrics_.checkColumn + metrics_.columnGap;
        const float rightEdge = width - metrics_.paddingX - (item.submenu ? metrics_.submenuColumn : 0.0f);
        if (auto* format = text_.Format(style); format && dwrite_) {
            winrt::com_ptr<IDWriteTextLayout> layout;
            if (SUCCEEDED(dwrite_->CreateTextLayout(item.label.text.data(), static_cast<UINT32>(item.label.text.size()), format,
                                                    std::max(1.0f, rightEdge - textLeft), height, layout.put()))) {
                layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                if (showAccelerators && item.label.mnemonicIndex != std::wstring::npos)
                    layout->SetUnderline(TRUE, DWRITE_TEXT_RANGE{static_cast<UINT32>(item.label.mnemonicIndex), 1});
                target_->DrawTextLayout(D2D1::Point2F(textLeft, 0.0f), layout.get(), brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
            }
            if (!item.label.accelerator.empty()) {
                winrt::com_ptr<IDWriteTextLayout> accelerator;
                if (SUCCEEDED(dwrite_->CreateTextLayout(item.label.accelerator.data(),
                                                        static_cast<UINT32>(item.label.accelerator.size()), format,
                                                        std::max(1.0f, rightEdge - textLeft), height, accelerator.put()))) {
                    accelerator->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    brush->SetColor((disabled ? tokens_.textDisabled : tokens_.textSecondary).ToD2D());
                    target_->DrawTextLayout(D2D1::Point2F(textLeft, 0.0f), accelerator.get(), brush.get(),
                                            D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    brush->SetColor(textColor.ToD2D());
                }
            }
        }
        if (item.submenu) {
            const float box = 12.0f;
            const float left = width - metrics_.paddingX - box;
            graphics::PaintGlyph(target_.get(), graphics::Glyph::ChevronRight,
                                 D2D1::RectF(left, centerY - box / 2.0f, left + box, centerY + box / 2.0f), brush.get(),
                                 tokens_.glyph.stroke);
        }
    }
    if (target_->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) target_ = nullptr;
    // Keep Windows from drawing its own (light-theme) submenu arrow over ours.
    if (item.submenu) ::ExcludeClipRect(draw.hDC, draw.rcItem.left, draw.rcItem.top, draw.rcItem.right, draw.rcItem.bottom);
}

std::optional<LRESULT> ThemedMenu::HandleMessage(HWND /*owner*/, UINT message, WPARAM wParam, LPARAM lParam) {
    if (!ownerDrawn_) return std::nullopt;
    switch (message) {
        case WM_MEASUREITEM: {
            auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lParam);
            if (!measure || measure->CtlType != ODT_MENU) return std::nullopt;
            const auto* item = ItemFromData(measure->itemData);
            if (!OwnsItem(item)) return std::nullopt;
            Measure(*measure, *item);
            return TRUE;
        }
        case WM_DRAWITEM: {
            const auto* draw = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            if (!draw || draw->CtlType != ODT_MENU) return std::nullopt;
            const auto* item = ItemFromData(draw->itemData);
            if (!OwnsItem(item)) return std::nullopt;
            Draw(*draw, *item);
            return TRUE;
        }
        case WM_MENUCHAR: {
            const auto menu = reinterpret_cast<HMENU>(lParam);
            if (!OwnsMenu(menu)) return std::nullopt;
            const auto typed = static_cast<wchar_t>(std::towlower(static_cast<wchar_t>(LOWORD(wParam))));
            const int count = ::GetMenuItemCount(menu);
            for (int index = 0; index < count; ++index) {
                MENUITEMINFOW info{};
                info.cbSize = sizeof(info);
                info.fMask = MIIM_DATA;
                if (!::GetMenuItemInfoW(menu, static_cast<UINT>(index), TRUE, &info)) continue;
                const auto* item = ItemFromData(info.dwItemData);
                if (!OwnsItem(item) || item->separator || !item->enabled || item->label.mnemonic != typed) continue;
                return MAKELRESULT(index, MNC_EXECUTE);
            }
            return MAKELRESULT(0, MNC_IGNORE);
        }
        default: return std::nullopt;
    }
}

ThemedMenuPresenter::ThemedMenuPresenter(const graphics::DesignTokens& tokens, IMenuWindowOps& ops)
    : tokens_(tokens), ops_(&ops) {}

void ThemedMenuPresenter::SetTheme(const graphics::DesignTokens& tokens) { tokens_ = tokens; }

Result<MenuSelection> ThemedMenuPresenter::Show(std::span<const MenuEntry> entries, const MenuRequest& request) {
    lastThemed_ = false;
    try {
        if (auto valid = ValidateMenu(entries); !valid) return valid.error();
        const bool highContrast = tokens_.mode == graphics::ThemeMode::HighContrast || platform::HighContrastEnabled();
        if (!highContrast && OwnedByThisThread(request.owner)) {
            auto themed = ThemedMenu::Build(entries, tokens_, ::GetDpiForWindow(request.owner));
            if (themed && (*themed)->OwnerDrawn() &&
                ::SetWindowSubclass(request.owner, &OwnerSubclass, kSubclassId, reinterpret_cast<DWORD_PTR>(themed->get()))) {
                lastThemed_ = true;
                auto selection = TrackMenuWithForegroundRule(*ops_, (*themed)->Handle(), request);
                ::RemoveWindowSubclass(request.owner, &OwnerSubclass, kSubclassId);
                return selection;
            }
            if (!themed) AETHER_LOG_WARN("ui", "themed menu unavailable ({}); using the native menu", themed.error().Describe());
        }
        NativeMenuPresenter native(*ops_);
        return native.Show(entries, request);
    } catch (...) {
        return MakeError(ErrorCode::Internal, "popup menu could not be shown");
    }
}

Result<std::unique_ptr<IMenuPresenter>> MakeThemedMenuPresenter(const graphics::DesignTokens& tokens) try {
    return std::unique_ptr<IMenuPresenter>(std::make_unique<ThemedMenuPresenter>(tokens));
} catch (...) {
    return MakeError(ErrorCode::Internal, "popup menu presenter unavailable");
}

}  // namespace aether::ui
