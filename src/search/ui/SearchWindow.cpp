// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "search/ui/SearchWindow.h"

#include "core/Strings.h"
#include "graphics/GlassPainter.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MonitorUtilities.h"

#include <commctrl.h>
#include <oleacc.h>
#include <algorithm>

namespace aether::search::ui {
namespace {
constexpr UINT_PTR QueryTimer = 1, CaretTimer = 2, CloseTimer = 3;
constexpr float RowTop = 90, RowHeight = 58;
}

SearchWindow::SearchWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler,
                           SearchService& search, apps::IApplicationLauncher& launcher, apps::IIconProvider& icons)
    : SurfaceWindow(device, scheduler), search_(search), launcher_(launcher), icons_(icons),
      tokens_(graphics::MakeDesignTokens(graphics::DetectThemeMode())), bitmaps_(device) {
    text_.Initialize(device.DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
}
SearchWindow::~SearchWindow() { *alive_ = false; indexReady_.Disconnect(); Destroy(); if (editFont_) ::DeleteObject(editFont_); }

Result<void> SearchWindow::Initialize() {
    aether::ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.Search"; options.title = L"Search applications"; options.noActivate = false;
    const auto created = Create(options, RECT{0, 0, 640, 400});
    if (!created) return created;
    // The native edit retains IME, selection and its standard accessibility provider.
    // A transparent layered child avoids a second GDI copy over the composition surface.
    edit_ = ::CreateWindowExW(WS_EX_LAYERED, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                              50, 16, 550, 36, Handle(), reinterpret_cast<HMENU>(100), ::GetModuleHandleW(nullptr), nullptr);
    if (!edit_) { Destroy(); return MakeError(ErrorCode::Platform, "search input could not be created"); }
    if (!::SetLayeredWindowAttributes(edit_, 0, 0, LWA_ALPHA)) {
        Destroy(); return MakeError(ErrorCode::Platform, "search input transparency could not be configured");
    }
    winrt::com_ptr<IAccPropServices> properties;
    HRESULT propertyStatus = ::CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER,
                                                IID_PPV_ARGS(properties.put()));
    if (SUCCEEDED(propertyStatus)) propertyStatus = properties->SetHwndPropStr(edit_, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF,
                                                                            PROPID_ACC_NAME, L"Search applications");
    if (FAILED(propertyStatus)) { Destroy(); return MakeError(ErrorCode::Platform, "search input accessibility could not be configured"); }
    ::SendMessageW(edit_, EM_SETLIMITTEXT, 128, 0);
    ::SendMessageW(edit_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search applications"));
    ::SetWindowSubclass(edit_, &EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
    if (auto connected = accessible_.Connect(Handle(), *this); !connected) return connected;
    const auto weak = std::weak_ptr<bool>(alive_);
    indexReady_ = search_.IndexReady().Connect([this, weak](const IndexReport&) { if (const auto live = weak.lock(); live && *live) RefreshResults(); });
    Position();
    return Ok();
}

void SearchWindow::Position() {
    POINT cursor{}; ::GetCursorPos(&cursor);
    const auto monitor = platform::QueryMonitor(::MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY)).value_or(platform::PrimaryMonitor());
    const auto scale = platform::ScaleForDpi(monitor.dpi);
    const RECT area = placementArea_.value_or(monitor.workArea);
    const int width = std::max(160, std::min(platform::Width(area) - 32, static_cast<int>(640 * scale)));
    const int desired = static_cast<int>((108 + RowHeight * static_cast<float>(std::max<std::size_t>(results_.size(), 1)) +
                                         (error_.empty() ? 0.0f : 36.0f)) * scale);
    const int left = area.left + (platform::Width(area) - width) / 2;
    const int top = area.top + std::max(16, static_cast<int>(platform::Height(area) * 0.18f));
    SetBoundsPx(RECT{left, top, left + width, top + std::max(static_cast<int>(156 * scale),
                std::min(static_cast<int>(area.bottom) - 16 - top, desired))});
    if (edit_) {
        ::SetWindowPos(edit_, nullptr, static_cast<int>(ToPx(58)), static_cast<int>(ToPx(20)),
                       std::max(1, width - static_cast<int>(ToPx(86))), static_cast<int>(ToPx(32)), SWP_NOZORDER | SWP_NOACTIVATE);
        if (editFont_) ::DeleteObject(editFont_);
        editFont_ = ::CreateFontW(-static_cast<int>(ToPx(20)), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        ::SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    }
    EnsureVisible();
}
void SearchWindow::SetPlacementArea(std::optional<RECT> area) { placementArea_ = area; if (Handle()) Position(); }
void SearchWindow::Open() {
    if (!Handle()) return;
    if (IsOpen()) { ::SetForegroundWindow(Handle()); ::SetFocus(edit_); return; }
    ::KillTimer(Handle(), CloseTimer); closing_ = false; ::EnableWindow(edit_, TRUE);
    previousFocus_ = ::GetForegroundWindow(); error_.clear();
    Position(); RefreshResults(); Show(true);
    (void)AnimateEntrance(std::chrono::milliseconds(motion_.reduced || tokens_.mode == graphics::ThemeMode::HighContrast ? 0 : 140));
    ::SetForegroundWindow(Handle()); ::SetFocus(edit_);
    const auto blink = ::GetCaretBlinkTime();
    if (blink && blink != INFINITE) ::SetTimer(Handle(), CaretTimer, blink, nullptr);
}
void SearchWindow::StopTimers() { if (Handle()) { ::KillTimer(Handle(), QueryTimer); ::KillTimer(Handle(), CaretTimer); ::KillTimer(Handle(), CloseTimer); } queryPending_ = false; }
void SearchWindow::BeginClose(bool restoreFocus) {
    if (closing_ || !IsShown()) return;
    StopTimers(); closing_ = true; ::EnableWindow(edit_, FALSE);
    const bool instant = motion_.reduced || tokens_.mode == graphics::ThemeMode::HighContrast;
    if (instant || !AnimateExit(std::chrono::milliseconds(160)) || !::SetTimer(Handle(), CloseTimer, 160, nullptr)) {
        Hide(); closing_ = false;
    }
    if (restoreFocus && previousFocus_ && ::IsWindow(previousFocus_)) ::SetForegroundWindow(previousFocus_);
    previousFocus_ = nullptr;
}
void SearchWindow::Close() { BeginClose(true); }
void SearchWindow::Toggle() { if (IsOpen()) Close(); else Open(); }
void SearchWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    tokens_ = tokens; motion_ = motion;
    if (motion_.reduced || tokens_.mode == graphics::ThemeMode::HighContrast) {
        if (closing_) { StopTimers(); Hide(); closing_ = false; }
        (void)AnimateEntrance(std::chrono::milliseconds(0));
    }
    Invalidate();
}

void SearchWindow::ReadQuery() {
    if (!edit_) return;
    const int length = ::GetWindowTextLengthW(edit_);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    ::GetWindowTextW(edit_, value.data(), length + 1); value.resize(static_cast<std::size_t>(length)); query_ = std::move(value);
}
void SearchWindow::FlushQuery() { if (queryPending_) RefreshResults(); }
void SearchWindow::RefreshResults() {
    if (!edit_) return;
    ::KillTimer(Handle(), QueryTimer); queryPending_ = false; ReadQuery(); ++queryCount_;
    auto queried = search_.Query(query_);
    results_ = queried ? std::move(queried.value()) : std::vector<SearchResult>{};
    selected_ = firstVisible_ = 0;
    if (!queried) error_ = L"Search could not be completed.";
    const auto weak = std::weak_ptr<bool>(alive_);
    for (const auto& result : results_) {
        if (!accessibleIds_.contains(result.application.id)) accessibleIds_.emplace(result.application.id, nextAccessibleId_++);
        if (images_.contains(result.application.id)) continue;
        images_[result.application.id] = nullptr;
        icons_.RequestIcon(apps::IconRequest{result.application.id, result.application, static_cast<std::uint32_t>(ToPx(40))},
            [this, weak](const std::wstring& key, std::shared_ptr<const apps::IconImage> image) {
                if (const auto live = weak.lock(); live && *live) { images_[key] = std::move(image); Invalidate(); }
            });
    }
    Position();
    (void)accessible_.NotifyStructureChanged(); Invalidate();
}

void SearchWindow::OnRender(const aether::ui::RenderContext& context) {
    const auto panel = D2D1::RectF(8, 8, context.widthDip - 8, context.heightDip - 8);
    SetBackdropPanel(panel, tokens_.popupCornerRadius, tokens_.mode != graphics::ThemeMode::HighContrast);
    graphics::GlassStyle style; style.cornerRadius = tokens_.popupCornerRadius; style.backdropBlur = BackdropBlurActive();
    graphics::PaintGlassPanel(context.d2d, panel, tokens_, style);
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    context.d2d->CreateSolidColorBrush(tokens_.textPrimary.ToD2D(), brush.put());
    if (!brush) return;
    context.d2d->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(33, 36), 8, 8), brush.get(), 1.7f);
    context.d2d->DrawLine(D2D1::Point2F(39, 42), D2D1::Point2F(45, 48), brush.get(), 1.7f);
    graphics::TextStyle textStyle; textStyle.size = 20;
    const auto inputBounds = D2D1::RectF(58, 20, panel.right - 22, 56);
    context.d2d->PushAxisAlignedClip(inputBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DWORD selectionStart = 0, selectionEnd = 0;
    ::SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    winrt::com_ptr<IDWriteTextLayout> input;
    Device().DWriteFactory()->CreateTextLayout(query_.data(), static_cast<UINT32>(query_.size()),
        text_.Format(textStyle), 10000, 36, input.put());
    if (input) {
        FLOAT caretX = 0, caretY = 0; DWRITE_HIT_TEST_METRICS caret{};
        input->HitTestTextPosition(selectionEnd, FALSE, &caretX, &caretY, &caret);
        const float offset = std::max(0.0f, caretX - (inputBounds.right - inputBounds.left) + 3);
        const auto origin = D2D1::Point2F(inputBounds.left - offset, inputBounds.top);
        if (selectionEnd > selectionStart && ::GetFocus() == edit_) {
            std::vector<DWRITE_HIT_TEST_METRICS> ranges(query_.size() + 1); UINT32 count = 0;
            if (SUCCEEDED(input->HitTestTextRange(selectionStart, selectionEnd - selectionStart, origin.x, origin.y,
                                                ranges.data(), static_cast<UINT32>(ranges.size()), &count))) {
                auto selectionColor = tokens_.accent; selectionColor.a = 0.28f;
                brush->SetColor(selectionColor.ToD2D());
                for (UINT32 i = 0; i < count; ++i) {
                    const auto& range = ranges[i];
                    context.d2d->FillRectangle(D2D1::RectF(range.left, range.top, range.left + range.width, range.top + range.height), brush.get());
                }
                brush->SetColor(tokens_.textPrimary.ToD2D());
            }
        }
        if (!query_.empty()) context.d2d->DrawTextLayout(origin, input.get(), brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (::GetFocus() == edit_ && caretVisible_ && selectionStart == selectionEnd)
            context.d2d->DrawLine(D2D1::Point2F(origin.x + caretX, 24), D2D1::Point2F(origin.x + caretX, 47), brush.get(), 1);
    }
    if (query_.empty()) {
        brush->SetColor(tokens_.textSecondary.ToD2D());
        text_.Draw(context.d2d, L"Search applications", inputBounds, textStyle, brush.get());
    }
    context.d2d->PopAxisAlignedClip();
    brush->SetColor(tokens_.textSecondary.ToD2D()); textStyle.size = 11;
    text_.Draw(context.d2d, L"APPLICATIONS", D2D1::RectF(24, 64, panel.right - 20, 84), textStyle, brush.get());
    for (std::size_t i = firstVisible_; i < std::min(results_.size(), firstVisible_ + VisibleRows()); ++i) {
        const float top = RowTop + static_cast<float>(i - firstVisible_) * RowHeight;
        const auto row = D2D1::RoundedRect(D2D1::RectF(18, top, panel.right - 10, top + 54), tokens_.itemCornerRadius, tokens_.itemCornerRadius);
        if (i == selected_) { brush->SetColor(tokens_.hoverOverlay.ToD2D()); context.d2d->FillRoundedRectangle(row, brush.get()); }
        const auto& app = results_[i].application;
        const auto rect = D2D1::RectF(28, top + 7, 68, top + 47);
        const auto image = images_.find(app.id);
        ID2D1Bitmap1* bitmap = bitmaps_.Find(app.id);
        if (!bitmap && image != images_.end() && image->second) bitmap = bitmaps_.Put(context.d2d, app.id, image->second->width, image->second->height, image->second->pixels);
        if (bitmap) context.d2d->DrawBitmap(bitmap, rect, 1.0f, D2D1_INTERPOLATION_MODE_LINEAR);
        else graphics::PaintFallbackIcon(context.d2d, Device().DWriteFactory(), rect, app.displayName.empty() ? L'?' : app.displayName.front(), tokens_);
        brush->SetColor(tokens_.textPrimary.ToD2D()); textStyle.size = 14;
        winrt::com_ptr<IDWriteTextLayout> layout;
        Device().DWriteFactory()->CreateTextLayout(app.displayName.data(), static_cast<UINT32>(app.displayName.size()),
            text_.Format(textStyle), std::max(1.0f, panel.right - 106), 25, layout.put());
        if (layout) {
            winrt::com_ptr<ID2D1SolidColorBrush> highlight;
            context.d2d->CreateSolidColorBrush(tokens_.accent.ToD2D(), highlight.put());
            if (highlight) for (const auto& range : results_[i].highlights)
                layout->SetDrawingEffect(highlight.get(), DWRITE_TEXT_RANGE{static_cast<UINT32>(range.start), static_cast<UINT32>(range.length)});
            context.d2d->DrawTextLayout(D2D1::Point2F(82, top + 5), layout.get(), brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        brush->SetColor(tokens_.textSecondary.ToD2D()); textStyle.size = 11;
        text_.Draw(context.d2d, app.kind == apps::ApplicationKind::Packaged ? L"Installed application" : L"Desktop application", D2D1::RectF(82, top + 27, panel.right - 24, top + 47), textStyle, brush.get());
    }
    if (results_.empty()) {
        textStyle.size = 14;
        const auto message = search_.Current().state == IndexState::Building ? L"Indexing applications…" :
                             query_.empty() ? L"Type an application name to begin" : L"No applications match";
        text_.Draw(context.d2d, message, D2D1::RectF(28, 96, panel.right - 28, 144), textStyle, brush.get());
    }
    if (!error_.empty()) {
        brush->SetColor(tokens_.textPrimary.ToD2D()); textStyle.size = 13;
        text_.Draw(context.d2d, error_, D2D1::RectF(28, panel.bottom - 36, panel.right - 28, panel.bottom - 6), textStyle, brush.get());
    }
}

bool SearchWindow::OnKeyDown(UINT key) {
    if (!IsOpen()) return false;
    if (key == VK_ESCAPE) { if (!query_.empty()) { ::SetWindowTextW(edit_, L""); FlushQuery(); } else Close(); return true; }
    if (key == VK_RETURN || key == VK_TAB || key == VK_UP || key == VK_DOWN || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT) FlushQuery();
    if (key == VK_RETURN && !results_.empty()) { Launch(selected_); return true; }
    if (key == VK_UP || key == VK_DOWN || key == VK_TAB || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT) {
        if (results_.empty()) return true;
        if (key == VK_UP || (key == VK_TAB && (::GetKeyState(VK_SHIFT) & 0x8000))) selected_ = selected_ ? selected_ - 1 : results_.size() - 1;
        else if (key == VK_DOWN || key == VK_TAB) selected_ = (selected_ + 1) % results_.size();
        else selected_ = key == VK_HOME || key == VK_PRIOR ? 0 : results_.size() - 1;
        UpdateSelection(selected_); return true;
    }
    return false;
}
void SearchWindow::OnPointerDown(aether::ui::PointerButton button, D2D1_POINT_2F point) {
    if (!IsOpen() || button != aether::ui::PointerButton::Left || point.x < 58 || point.y < 20 || point.y >= 56) return;
    ::SetFocus(edit_);
    // Alpha-zero child pixels are hit-transparent; forward input to the real edit.
    const auto x = static_cast<short>(ToPx(point.x - 58)), y = static_cast<short>(ToPx(point.y - 20));
    ::SendMessageW(edit_, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
}
std::size_t SearchWindow::VisibleRows() const {
    const float available = ToDip(static_cast<float>(platform::Height(BoundsPx()))) - RowTop - 18 - (error_.empty() ? 0.0f : 36.0f);
    return available > 0 ? static_cast<std::size_t>(available / RowHeight) : 0;
}
void SearchWindow::EnsureVisible() {
    const auto rows = VisibleRows();
    if (selected_ < firstVisible_) firstVisible_ = selected_;
    if (rows && selected_ >= firstVisible_ + rows) firstVisible_ = selected_ - rows + 1;
    if (results_.size() <= rows) firstVisible_ = 0;
    else firstVisible_ = std::min(firstVisible_, results_.size() - rows);
}
void SearchWindow::UpdateSelection(std::size_t index) {
    selected_ = index; EnsureVisible();
    (void)accessible_.NotifyFocusChanged(IdFor(selected_)); Invalidate();
}
std::optional<std::size_t> SearchWindow::Hit(D2D1_POINT_2F p) const {
    if (!IsOpen() || p.y < RowTop || p.x < 18 || p.x > ToDip(static_cast<float>(platform::Width(BoundsPx()))) - 18) return {};
    const auto row = static_cast<std::size_t>((p.y - RowTop) / RowHeight);
    if (row >= VisibleRows() || p.y - RowTop - static_cast<float>(row) * RowHeight >= 54) return {};
    const auto index = firstVisible_ + row;
    return index < results_.size() ? std::optional(index) : std::nullopt;
}
void SearchWindow::OnPointerMove(D2D1_POINT_2F p) { if (const auto hit = Hit(p); hit && *hit != selected_) UpdateSelection(*hit); }
void SearchWindow::OnWheel(float notches, D2D1_POINT_2F) {
    if (!IsOpen()) return;
    FlushQuery(); if (results_.empty() || notches == 0) return;
    UpdateSelection(notches > 0 ? (selected_ ? selected_ - 1 : 0) : std::min(selected_ + 1, results_.size() - 1));
}
void SearchWindow::OnPointerUp(aether::ui::PointerButton button, D2D1_POINT_2F p) { if (button == aether::ui::PointerButton::Left) if (const auto hit = Hit(p)) Launch(*hit); }
void SearchWindow::Launch(std::size_t index) {
    if (!IsOpen() || index >= results_.size()) return;
    const auto weak = std::weak_ptr<bool>(alive_);
    launcher_.LaunchAsync(results_[index].application, [this, weak](Result<void> result) {
        if (const auto live = weak.lock(); live && *live) {
            if (result) Close(); else { error_ = L"Windows could not open this application."; Position(); Invalidate(); }
        }
    });
}
LRESULT CALLBACK SearchWindow::EditProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<SearchWindow*>(ref);
    if (message == WM_KEYDOWN && self->OnKeyDown(static_cast<UINT>(wParam))) return 0;
    if (message == WM_NCDESTROY) ::RemoveWindowSubclass(hwnd, &EditProc, 1);
    const auto result = ::DefSubclassProc(hwnd, message, wParam, lParam);
    if (message == WM_KEYDOWN || message == WM_CHAR || message == WM_LBUTTONUP || message == WM_MOUSEMOVE || message == EM_SETSEL || message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        self->caretVisible_ = true; self->Invalidate();
    }
    return result;
}
std::optional<LRESULT> SearchWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    LRESULT result{};
    if (message == WM_GETOBJECT && accessible_.HandleGetObject(Handle(), wParam, lParam, result)) return result;
    if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE && reinterpret_cast<HWND>(lParam) == edit_) {
        error_.clear(); ReadQuery(); queryPending_ = true;
        if (IsOpen()) ::SetTimer(Handle(), QueryTimer, 70, nullptr);
        caretVisible_ = true; Invalidate(); return 0;
    }
    if (message == WM_TIMER && wParam == QueryTimer) { if (queryPending_ && IsOpen()) RefreshResults(); return 0; }
    if (message == WM_TIMER && wParam == CaretTimer) { caretVisible_ = !caretVisible_; Invalidate(); return 0; }
    if (message == WM_TIMER && wParam == CloseTimer) { ::KillTimer(Handle(), CloseTimer); Hide(); closing_ = false; return 0; }
    if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) BeginClose(false);
    return std::nullopt;
}
void SearchWindow::OnDestroying() { StopTimers(); accessible_.Disconnect(); edit_ = nullptr; }
void SearchWindow::OnDpiChanged(UINT dpi, const RECT& suggested) { SurfaceWindow::OnDpiChanged(dpi, suggested); Position(); }

aether::ui::accessibility::AccessibleItem SearchWindow::Item(std::size_t index) const {
    aether::ui::accessibility::AccessibleItem item;
    if (index >= results_.size()) return item;
    item.id = IdFor(index); item.name = results_[index].application.displayName;
    item.controlType = UIA_ListItemControlTypeId; item.supportsInvoke = true; item.isFocusable = true; item.isEnabled = IsOpen();
    item.hasKeyboardFocus = IsOpen() && ::GetFocus() == edit_ && index == selected_;
    item.isOffscreen = !IsOpen() || index < firstVisible_ || index >= firstVisible_ + VisibleRows();
    if (item.isOffscreen) return item;
    const auto b = BoundsPx(); const int top = b.top + static_cast<int>(ToPx(RowTop + static_cast<float>(index - firstVisible_) * RowHeight));
    item.boundsPx = RECT{b.left + static_cast<int>(ToPx(18)), top, b.right - static_cast<int>(ToPx(18)), top + static_cast<int>(ToPx(54))};
    return item;
}
int SearchWindow::IdFor(std::size_t index) const {
    if (index >= results_.size()) return 0;
    const auto found = accessibleIds_.find(results_[index].application.id);
    return found == accessibleIds_.end() ? 0 : found->second;
}
std::optional<std::size_t> SearchWindow::IndexFor(int id) const {
    for (std::size_t i = 0; i < results_.size(); ++i) if (IdFor(i) == id) return i;
    return {};
}
Result<void> SearchWindow::Invoke(int id) { if (!IsOpen()) return MakeError(ErrorCode::InvalidState, "search is closed"); const auto index = IndexFor(id); if (!index) return MakeError(ErrorCode::NotFound, "search result absent"); Launch(*index); return Ok(); }
Result<void> SearchWindow::Toggle(int) { return MakeError(ErrorCode::Unsupported, "search result cannot toggle"); }
Result<void> SearchWindow::SetFocus(int id) { if (!IsOpen()) return MakeError(ErrorCode::InvalidState, "search is closed"); const auto index = IndexFor(id); if (!index) return MakeError(ErrorCode::NotFound, "search result absent"); ::SetFocus(edit_); UpdateSelection(*index); return Ok(); }

}  // namespace aether::search::ui
