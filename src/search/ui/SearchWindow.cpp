// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "search/ui/SearchWindow.h"

#include "core/Strings.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "graphics/Transition.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MonitorUtilities.h"

#include <commctrl.h>
#include <oleacc.h>
#include <algorithm>
#include <cmath>

namespace aether::search::ui {
namespace {
constexpr UINT_PTR QueryTimer = 1, CaretTimer = 2;
// Panel layout in DIPs, relative to the panel's top-left corner.
constexpr float kPanelWidth = 680.0f;
constexpr float kMinPanel = 156.0f;
constexpr float kPadding = 12.0f;
constexpr float kFieldHeight = 48.0f;
constexpr float kFieldGlyph = 16.0f;
constexpr float kInputLeft = 42.0f;   // from the field's left edge
constexpr float kCaptionGap = 8.0f;
constexpr float kCaptionHeight = 20.0f;
constexpr float kRowGap = 4.0f;
constexpr float kRowTop = kPadding + kFieldHeight + kCaptionGap + kCaptionHeight + kRowGap;
constexpr float kRowHeight = 52.0f;   // row stride
constexpr float kRowPill = 48.0f;
constexpr float kRowInset = 8.0f;     // row pill inset from the panel edge
constexpr float kIconSize = 32.0f;
constexpr float kErrorHeight = 32.0f;
constexpr float kFooterHeight = 36.0f;
constexpr float kFieldFont = 16.0f;
constexpr float kIndicatorWidth = 3.0f;
constexpr float kIndicatorHeight = 16.0f;
// Open: from 12 DIPs lower and 97% scale; close: to 98% scale.
constexpr float kOpenOffset = 12.0f;
constexpr float kOpenScale = 0.97f;
constexpr float kCloseScale = 0.98f;
constexpr wchar_t kFooterHint[] = L"↑↓ Navigate   ·   Enter Open   ·   Esc Close";

bool Contains(const D2D1_RECT_F& rect, D2D1_POINT_2F point) noexcept {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

}  // namespace

SearchWindow::SearchWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler,
                           SearchService& search, apps::IApplicationLauncher& launcher, apps::IIconProvider& icons)
    : SurfaceWindow(device, scheduler), search_(search), launcher_(launcher), icons_(icons),
      tokens_(graphics::MakeDesignTokens(graphics::DetectShellThemeMode())), bitmaps_(device) {
    text_.Initialize(device.DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
}
SearchWindow::~SearchWindow() {
    *alive_ = false;
    indexReady_.Disconnect();
    Destroy();
    if (editFont_) ::DeleteObject(editFont_);
}

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
    SetPanelTarget(false);
    return Ok();
}

// --- Geometry ---

bool SearchWindow::Instant() const noexcept {
    return !motion_.AllowsMovement() || tokens_.mode == graphics::ThemeMode::HighContrast;
}
float SearchWindow::Margin() const noexcept {
    // Keep the exterior shadow and the downward entrance inside the HWND throughout the transition.
    return std::max(8.0f, std::ceil(graphics::ShadowExtent(tokens_.elevation.popup))) + kOpenOffset;
}
float SearchWindow::TextScale() const noexcept {
    return std::isfinite(tokens_.textScale) ? std::clamp(tokens_.textScale, 1.0f, 1.4f) : 1.0f;
}
float SearchWindow::PanelWidthDip() const noexcept {
    return std::max(1.0f, ToDip(static_cast<float>(platform::Width(BoundsPx()))) - 2.0f * Margin());
}
float SearchWindow::MaxPanelDip() const noexcept {
    return std::max(1.0f, ToDip(static_cast<float>(platform::Height(BoundsPx()))) - 2.0f * Margin());
}
float SearchWindow::ChromeDip() const noexcept {
    return kRowTop + kRowGap + (error_.empty() ? 0.0f : kErrorHeight) + kFooterHeight;
}
float SearchWindow::TargetPanelDip() const noexcept {
    const float rows = static_cast<float>(std::max<std::size_t>(results_.size(), 1));
    const float maximum = MaxPanelDip();
    return std::clamp(ChromeDip() + rows * kRowHeight, std::min(kMinPanel, maximum), maximum);
}
D2D1_RECT_F SearchWindow::PanelDip() const noexcept {
    const float margin = Margin();
    const float height = std::clamp(panelHeight_.Value(), 1.0f, MaxPanelDip());
    return D2D1::RectF(margin, margin, margin + PanelWidthDip(), margin + height);
}
D2D1_RECT_F SearchWindow::InputDip() const noexcept {
    const float margin = Margin();
    const float fieldLeft = margin + kPadding;
    const float fieldTop = margin + kPadding;
    return D2D1::RectF(fieldLeft + kInputLeft, fieldTop + 6.0f, margin + PanelWidthDip() - kPadding - 14.0f,
                       fieldTop + kFieldHeight - 6.0f);
}

void SearchWindow::Position() {
    POINT cursor{}; ::GetCursorPos(&cursor);
    const auto monitor = platform::QueryMonitor(::MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY)).value_or(platform::PrimaryMonitor());
    const auto scale = platform::ScaleForDpi(monitor.dpi);
    const RECT area = placementArea_.value_or(monitor.workArea);
    const int panelWidth = std::max(160, std::min(platform::Width(area) - 32, static_cast<int>(kPanelWidth * scale)));
    const int panelLeft = area.left + (platform::Width(area) - panelWidth) / 2;
    const int panelTop = area.top + std::max(16, static_cast<int>(platform::Height(area) * 0.18f));
    const int maxPanel = std::max(static_cast<int>(kMinPanel * scale), static_cast<int>(area.bottom) - 16 - panelTop);
    const int margin = static_cast<int>(std::lround(Margin() * scale));
    SetBoundsPx(RECT{panelLeft - margin, panelTop - margin, panelLeft + panelWidth + margin, panelTop + maxPanel + margin});
    UpdateEditPlacement();
    EnsureVisible();
}

void SearchWindow::UpdateEditPlacement() {
    if (!edit_) return;
    const auto input = InputDip();
    ::SetWindowPos(edit_, nullptr, static_cast<int>(ToPx(input.left)), static_cast<int>(ToPx(input.top)),
                   std::max(1, static_cast<int>(ToPx(input.right - input.left))), std::max(1, static_cast<int>(ToPx(input.bottom - input.top))),
                   SWP_NOZORDER | SWP_NOACTIVATE);
    // The invisible edit only places the IME window and scrolls; keep its font close to the drawn one.
    const int pixels = static_cast<int>(std::lround(ToPx(kFieldFont * TextScale())));
    if (pixels == editFontPx_ && editFont_) return;
    HFONT font = ::CreateFontW(-pixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                               CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, tokens_.fontFamily.c_str());
    if (font) {
        // GDI substitutes silently; fall back to Segoe UI when the family is unknown to it.
        wchar_t face[LF_FACESIZE]{};
        if (HDC dc = ::GetDC(edit_)) {
            const HGDIOBJ previous = ::SelectObject(dc, font);
            ::GetTextFaceW(dc, LF_FACESIZE, face);
            ::SelectObject(dc, previous);
            ::ReleaseDC(edit_, dc);
        }
        if (tokens_.fontFamily != face) {
            ::DeleteObject(font);
            font = ::CreateFontW(-pixels, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                 CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        }
    }
    if (!font) return;
    ::SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (editFont_) ::DeleteObject(editFont_);
    editFont_ = font;
    editFontPx_ = pixels;
}

void SearchWindow::UpdateInputRegion() {
    const auto panel = PanelDip();
    const RECT region{static_cast<LONG>(std::floor(ToPx(panel.left))), static_cast<LONG>(std::floor(ToPx(panel.top))),
                      static_cast<LONG>(std::ceil(ToPx(panel.right))), static_cast<LONG>(std::ceil(ToPx(panel.bottom)))};
    if (::EqualRect(&region, &appliedRegion_)) return;
    appliedRegion_ = region;
    SetInputRegionPx(std::span<const RECT>(&region, 1));
    // SetWindowRgn clips rendering as well as input. Preserve shadow and entrance space without
    // turning the fixed-height window's unused tail into a giant input rectangle.
    const LONG padding = static_cast<LONG>(std::ceil(ToPx(Margin())));
    const RECT visual{region.left - padding, region.top - padding, region.right + padding, region.bottom + padding};
    SetVisualRegionPx(std::span<const RECT>(&visual, 1));
}

void SearchWindow::SetPanelTarget(bool animate) {
    const float target = TargetPanelDip();
    panelHeight_.SetParameters(motion_.ScaleSpring(graphics::Motion().slide));
    if (!animate || Instant() || !IsShown() || panelHeight_.Value() <= 1.0f) {
        panelHeight_.SnapTo(target);
        UpdateInputRegion();
        Invalidate();
        return;
    }
    if (panelHeight_.Target() == target) return;
    panelHeight_.SetTarget(target);
    RequestAnimationFrame();
}

void SearchWindow::MoveIndicator(bool animate) {
    const float target = kRowTop + static_cast<float>(selected_ >= firstVisible_ ? selected_ - firstVisible_ : 0) * kRowHeight + kRowPill / 2.0f;
    if (!animate || Instant()) {
        indicatorY_.SnapTo(target);
        return;
    }
    indicatorY_.AnimateTo(target, graphics::FrameScheduler::Now(), motion_.Scale(tokens_.durationFast), tokens_.curves.standard);
    RequestAnimationFrame();
}

void SearchWindow::SetPlacementArea(std::optional<RECT> area) {
    placementArea_ = area;
    if (Handle()) { Position(); SetPanelTarget(false); }
}

// --- Open and close ---

void SearchWindow::Open() {
    if (!Handle()) return;
    if (IsOpen()) { ::SetForegroundWindow(Handle()); ::SetFocus(edit_); return; }
    closing_ = false; ::EnableWindow(edit_, TRUE);
    previousFocus_ = ::GetForegroundWindow(); error_.clear();
    Position(); RefreshResults();
    SetPanelTarget(false);
    MoveIndicator(false);
    const float margin = Margin();
    graphics::TransitionShape from;
    from.offsetY = kOpenOffset;
    from.scale = kOpenScale;
    from.centerX = margin + PanelWidthDip() / 2.0f;
    from.centerY = margin;
    ShowAnimated(graphics::MakeEnterTransition(from, tokens_, motion_), true);
    ::SetForegroundWindow(Handle()); ::SetFocus(edit_);
    const auto blink = ::GetCaretBlinkTime();
    if (blink && blink != INFINITE) ::SetTimer(Handle(), CaretTimer, blink, nullptr);
}
void SearchWindow::StopTimers() {
    if (Handle()) { ::KillTimer(Handle(), QueryTimer); ::KillTimer(Handle(), CaretTimer); }
    queryPending_ = false;
}
void SearchWindow::BeginClose(bool restoreFocus) {
    if (closing_ || !IsShown()) return;
    StopTimers(); closing_ = true; selectionAnchor_.reset(); ::EnableWindow(edit_, FALSE);
    const float margin = Margin();
    graphics::TransitionShape to;
    to.scale = kCloseScale;
    to.centerX = margin + PanelWidthDip() / 2.0f;
    to.centerY = margin;
    // Hides when the compositor reports completion; at once when motion is reduced.
    HideAnimated(graphics::MakeExitTransition(to, tokens_, motion_));
    if (restoreFocus && previousFocus_ && ::IsWindow(previousFocus_)) ::SetForegroundWindow(previousFocus_);
    previousFocus_ = nullptr;
}
void SearchWindow::Close() { BeginClose(true); }
void SearchWindow::Toggle() { if (IsOpen()) Close(); else Open(); }
void SearchWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    tokens_ = tokens; motion_ = motion;
    text_.Initialize(Device().DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
    editFontPx_ = 0;
    if (Instant() && IsShown()) {
        if (closing_) { StopTimers(); Hide(); }
        else Show(false);  // cancels a running entrance and restores its resting visual immediately
    }
    if (Handle()) { Position(); SetPanelTarget(false); MoveIndicator(false); }
    Invalidate();
}

// --- Query and results ---

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
    const auto iconPx = static_cast<std::uint32_t>(std::lround(ToPx(kIconSize)));
    for (const auto& result : results_) {
        if (!accessibleIds_.contains(result.application.id)) accessibleIds_.emplace(result.application.id, nextAccessibleId_++);
        if (images_.contains(result.application.id)) continue;
        images_[result.application.id] = nullptr;
        icons_.RequestIcon(apps::IconRequest{result.application.id, result.application, iconPx},
            [this, weak](const std::wstring& key, std::shared_ptr<const apps::IconImage> image) {
                if (const auto live = weak.lock(); live && *live) { images_[key] = std::move(image); Invalidate(); }
            });
    }
    EnsureVisible();
    SetPanelTarget(true);
    MoveIndicator(false);
    (void)accessible_.NotifyStructureChanged(); Invalidate();
}

// --- Rendering ---

void SearchWindow::OnRender(const aether::ui::RenderContext& context) {
    ++renderedFrames_;
    auto* dc = context.d2d;
    const bool highContrast = tokens_.mode == graphics::ThemeMode::HighContrast;
    const auto panel = PanelDip();
    SetBackdropPanel(panel, tokens_.radii.popup, tokens_.transparencyEnabled && !highContrast, tokens_.glass.backdropOpacity);
    graphics::GlassStyle style;
    style.cornerRadius = tokens_.radii.popup;
    style.backdropBlur = BackdropBlurActive();
    style.elevation = graphics::Elevation::Popup;
    style.neutral = true;      // FR-037: one neutral material; no directional sheen on any side
    style.highlight = false;
    graphics::PaintGlassPanel(dc, panel, tokens_, style);
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    dc->CreateSolidColorBrush(tokens_.textPrimary.ToD2D(), brush.put());
    if (!brush) return;
    dc->PushAxisAlignedClip(panel, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const float textScale = TextScale();

    // Search field: a rounded fill with an underline that turns into a 2-DIP accent bar when focused.
    const bool focused = ::GetFocus() == edit_ && edit_;
    const auto field = D2D1::RectF(panel.left + kPadding, panel.top + kPadding, panel.right - kPadding, panel.top + kPadding + kFieldHeight);
    const float fieldRadius = tokens_.radii.overlay;
    const graphics::Color fieldFill = highContrast ? tokens_.surface.controlFill
        : tokens_.mode == graphics::ThemeMode::Dark ? graphics::Color::FromHex(0xFFFFFF, 0.06f) : graphics::Color::FromHex(0xFFFFFF, 0.65f);
    brush->SetColor(fieldFill.ToD2D());
    dc->FillRoundedRectangle(D2D1::RoundedRect(field, fieldRadius, fieldRadius), brush.get());
    if (highContrast) {
        brush->SetColor(tokens_.textPrimary.ToD2D());
        dc->DrawRoundedRectangle(D2D1::RoundedRect(field, fieldRadius, fieldRadius), brush.get(), 1.0f);
    }
    {
        const float thickness = focused ? 2.0f : 1.0f;
        const graphics::Color line = focused ? tokens_.accentRoles.fill : tokens_.textSecondary.WithAlpha(highContrast ? 1.0f : 0.35f);
        brush->SetColor(line.ToD2D());
        dc->PushAxisAlignedClip(D2D1::RectF(field.left, field.bottom - thickness, field.right, field.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
        dc->FillRoundedRectangle(D2D1::RoundedRect(field, fieldRadius, fieldRadius), brush.get());
        dc->PopAxisAlignedClip();
    }
    brush->SetColor(tokens_.textSecondary.ToD2D());
    const float glyphTop = field.top + (kFieldHeight - kFieldGlyph) / 2.0f;
    graphics::PaintGlyph(dc, graphics::Glyph::Search, D2D1::RectF(field.left + 14.0f, glyphTop, field.left + 14.0f + kFieldGlyph, glyphTop + kFieldGlyph),
                         brush.get(), tokens_.glyph.stroke);

    graphics::TextStyle textStyle; textStyle.size = kFieldFont * textScale;
    const auto inputBounds = InputDip();
    const float inputHeight = inputBounds.bottom - inputBounds.top;
    dc->PushAxisAlignedClip(inputBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    DWORD selectionStart = 0, selectionEnd = 0;
    ::SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    winrt::com_ptr<IDWriteTextLayout> input;
    Device().DWriteFactory()->CreateTextLayout(query_.data(), static_cast<UINT32>(query_.size()),
        text_.Format(textStyle), 10000, inputHeight, input.put());
    if (input) {
        FLOAT caretX = 0, caretY = 0; DWRITE_HIT_TEST_METRICS caret{};
        input->HitTestTextPosition(selectionEnd, FALSE, &caretX, &caretY, &caret);
        inputScroll_ = std::max(0.0f, caretX - (inputBounds.right - inputBounds.left) + 3);
        const auto origin = D2D1::Point2F(inputBounds.left - inputScroll_, inputBounds.top);
        if (selectionEnd > selectionStart && focused) {
            std::vector<DWRITE_HIT_TEST_METRICS> ranges(query_.size() + 1); UINT32 count = 0;
            if (SUCCEEDED(input->HitTestTextRange(selectionStart, selectionEnd - selectionStart, origin.x, origin.y,
                                                ranges.data(), static_cast<UINT32>(ranges.size()), &count))) {
                auto selectionColor = highContrast ? tokens_.surface.selectedFill : tokens_.accentRoles.fill.WithAlpha(0.32f);
                brush->SetColor(selectionColor.ToD2D());
                for (UINT32 i = 0; i < count; ++i) {
                    const auto& range = ranges[i];
                    dc->FillRectangle(D2D1::RectF(range.left, range.top, range.left + range.width, range.top + range.height), brush.get());
                }
            }
        }
        brush->SetColor(tokens_.textPrimary.ToD2D());
        if (!query_.empty()) dc->DrawTextLayout(origin, input.get(), brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        if (focused && caretVisible_ && selectionStart == selectionEnd) {
            const float caretTop = inputBounds.top + (inputHeight - textStyle.size * 1.3f) / 2.0f;
            dc->DrawLine(D2D1::Point2F(origin.x + caretX + 0.5f, caretTop), D2D1::Point2F(origin.x + caretX + 0.5f, caretTop + textStyle.size * 1.3f),
                         brush.get(), 1.0f);
        }
    }
    if (query_.empty()) {
        brush->SetColor(tokens_.textSecondary.ToD2D());
        text_.Draw(dc, L"Search applications", inputBounds, textStyle, brush.get());
    }
    dc->PopAxisAlignedClip();

    // Caption.
    graphics::TextStyle caption; caption.size = tokens_.type.caption * textScale; caption.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    brush->SetColor(tokens_.textSecondary.ToD2D());
    const float captionTop = panel.top + kPadding + kFieldHeight + kCaptionGap;
    text_.Draw(dc, L"Applications", D2D1::RectF(panel.left + kPadding + 4.0f, captionTop, panel.right - kPadding, captionTop + kCaptionHeight),
               caption, brush.get());

    // Rows.
    const float footerTop = panel.bottom - kFooterHeight;
    const float rowsBottom = footerTop - (error_.empty() ? 0.0f : kErrorHeight) - kRowGap;
    dc->PushAxisAlignedClip(D2D1::RectF(panel.left, panel.top + kRowTop - kRowGap, panel.right, std::max(panel.top + kRowTop, rowsBottom)),
                            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    graphics::TextStyle nameStyle; nameStyle.size = tokens_.type.body * textScale;
    graphics::TextStyle kindStyle; kindStyle.size = tokens_.type.caption * textScale;
    const std::size_t lastVisible = std::min(results_.size(), firstVisible_ + VisibleRows());
    for (std::size_t i = firstVisible_; i < lastVisible; ++i) {
        const float top = panel.top + kRowTop + static_cast<float>(i - firstVisible_) * kRowHeight;
        const auto row = D2D1::RectF(panel.left + kRowInset, top, panel.right - kRowInset, top + kRowPill);
        const bool selected = i == selected_;
        if (selected) graphics::PaintSelectionPill(dc, row, tokens_.radii.overlay, tokens_, true, false);
        const auto& app = results_[i].application;
        const float iconTop = top + (kRowPill - kIconSize) / 2.0f;
        const auto icon = D2D1::RectF(row.left + 12.0f, iconTop, row.left + 12.0f + kIconSize, iconTop + kIconSize);
        const auto image = images_.find(app.id);
        ID2D1Bitmap1* bitmap = bitmaps_.Find(app.id);
        if (!bitmap && image != images_.end() && image->second) bitmap = bitmaps_.Put(dc, app.id, image->second->width, image->second->height, image->second->pixels);
        if (bitmap) graphics::DrawIconBitmap(dc, bitmap, icon);
        else graphics::PaintFallbackIcon(dc, Device().DWriteFactory(), icon, app.displayName.empty() ? L'?' : app.displayName.front(), tokens_);
        const bool inverse = highContrast && selected;
        const float textLeft = icon.right + 12.0f;
        brush->SetColor((inverse ? tokens_.surface.selectedText : tokens_.textPrimary).ToD2D());
        winrt::com_ptr<IDWriteTextLayout> layout;
        Device().DWriteFactory()->CreateTextLayout(app.displayName.data(), static_cast<UINT32>(app.displayName.size()),
            text_.Format(nameStyle), std::max(1.0f, row.right - 12.0f - textLeft), kRowPill / 2.0f, layout.put());
        if (layout) {
            winrt::com_ptr<ID2D1SolidColorBrush> highlight;
            dc->CreateSolidColorBrush((inverse ? tokens_.surface.selectedText : tokens_.accentRoles.text).ToD2D(), highlight.put());
            for (const auto& range : results_[i].highlights) {
                const DWRITE_TEXT_RANGE span{static_cast<UINT32>(range.start), static_cast<UINT32>(range.length)};
                if (highlight) layout->SetDrawingEffect(highlight.get(), span);
                layout->SetFontWeight(DWRITE_FONT_WEIGHT_SEMI_BOLD, span);
            }
            dc->DrawTextLayout(D2D1::Point2F(textLeft, top + 4.0f), layout.get(), brush.get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
        brush->SetColor((inverse ? tokens_.surface.selectedText : tokens_.textSecondary).ToD2D());
        text_.Draw(dc, app.kind == apps::ApplicationKind::Packaged ? L"Installed application" : L"Desktop application",
                   D2D1::RectF(textLeft, top + kRowPill / 2.0f, row.right - 12.0f, top + kRowPill - 4.0f), kindStyle, brush.get());
    }
    if (!results_.empty() && selected_ >= firstVisible_ && selected_ < lastVisible) {
        const float centre = panel.top + indicatorY_.Value(context.now);
        graphics::PaintAccentIndicator(dc, D2D1::RectF(panel.left + kRowInset + 2.0f, centre - kIndicatorHeight / 2.0f,
                                                       panel.left + kRowInset + 2.0f + kIndicatorWidth, centre + kIndicatorHeight / 2.0f),
                                       tokens_);
    }
    if (results_.empty()) {
        brush->SetColor(tokens_.textSecondary.ToD2D());
        const auto message = search_.Current().state == IndexState::Building ? L"Indexing applications…" :
                             query_.empty() ? L"Type an application name to begin" : L"No applications match";
        nameStyle.size = tokens_.type.body * textScale;
        text_.Draw(dc, message, D2D1::RectF(panel.left + kPadding + 8.0f, panel.top + kRowTop, panel.right - kPadding, panel.top + kRowTop + kRowPill),
                   nameStyle, brush.get());
    }
    dc->PopAxisAlignedClip();

    // Inline launch error.
    if (!error_.empty()) {
        const float errorTop = footerTop - kErrorHeight;
        graphics::PaintStatusBadge(dc, graphics::StatusBadge::Error,
                                   D2D1::RectF(panel.left + kPadding + 4.0f, errorTop + 8.0f, panel.left + kPadding + 20.0f, errorTop + 24.0f), tokens_);
        brush->SetColor(tokens_.textPrimary.ToD2D());
        graphics::TextStyle errorStyle; errorStyle.size = tokens_.type.body * textScale;
        text_.Draw(dc, error_, D2D1::RectF(panel.left + kPadding + 28.0f, errorTop, panel.right - kPadding, errorTop + kErrorHeight), errorStyle, brush.get());
    }

    // Footer with the keyboard hint.
    brush->SetColor(tokens_.surface.divider.WithAlpha(highContrast ? 1.0f : 0.8f).ToD2D());
    if (!highContrast && tokens_.mode == graphics::ThemeMode::Dark) brush->SetColor(graphics::Color::FromHex(0xFFFFFF, 0.08f).ToD2D());
    dc->DrawLine(D2D1::Point2F(panel.left + 1.0f, footerTop + 0.5f), D2D1::Point2F(panel.right - 1.0f, footerTop + 0.5f), brush.get(), 1.0f);
    brush->SetColor(tokens_.textSecondary.ToD2D());
    graphics::TextStyle hint; hint.size = tokens_.type.caption * textScale;
    text_.Draw(dc, kFooterHint, D2D1::RectF(panel.left + kPadding + 4.0f, footerTop, panel.right - kPadding, panel.bottom), hint, brush.get());
    dc->PopAxisAlignedClip();
}

bool SearchWindow::OnAnimate(double now) {
    const double dt = lastAnimate_ > 0.0 ? now - lastAnimate_ : 1.0 / 60.0;
    lastAnimate_ = now;
    bool moving = false;
    if (!panelHeight_.IsSettled()) {
        panelHeight_.Step(dt);
        UpdateInputRegion();
        moving = !panelHeight_.IsSettled();
    }
    if (indicatorY_.IsAnimating(now)) moving = true;
    if (!moving) lastAnimate_ = 0.0;
    return moving;
}

// --- Input ---

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

std::optional<UINT32> SearchWindow::CaretIndexAt(D2D1_POINT_2F point) {
    const auto inputBounds = InputDip();
    graphics::TextStyle textStyle; textStyle.size = kFieldFont * TextScale();
    winrt::com_ptr<IDWriteTextLayout> layout;
    if (FAILED(Device().DWriteFactory()->CreateTextLayout(query_.data(), static_cast<UINT32>(query_.size()), text_.Format(textStyle), 10000,
                                                         inputBounds.bottom - inputBounds.top, layout.put())) || !layout) {
        return std::nullopt;
    }
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    if (FAILED(layout->HitTestPoint(point.x - (inputBounds.left - inputScroll_), point.y - inputBounds.top, &trailing, &inside, &metrics)))
        return std::nullopt;
    const UINT32 index = metrics.textPosition + (trailing ? metrics.length : 0u);
    return std::min<UINT32>(index, static_cast<UINT32>(query_.size()));
}

void SearchWindow::OnPointerDown(aether::ui::PointerButton button, D2D1_POINT_2F point) {
    if (!IsOpen() || IsTransitioning() || button != aether::ui::PointerButton::Left) return;
    const auto margin = Margin();
    const auto field = D2D1::RectF(margin + kPadding, margin + kPadding, margin + PanelWidthDip() - kPadding, margin + kPadding + kFieldHeight);
    if (!Contains(field, point)) return;
    ::SetFocus(edit_);
    // Caret placement uses the drawn DirectWrite layout, so it matches the visible glyphs.
    const auto index = CaretIndexAt(point);
    if (!index) return;
    DWORD start = 0, end = 0;
    ::SendMessageW(edit_, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
    const bool extend = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    selectionAnchor_ = extend ? static_cast<UINT32>(start) : *index;
    ::SendMessageW(edit_, EM_SETSEL, *selectionAnchor_, *index);
    caretVisible_ = true; Invalidate();
}
std::size_t SearchWindow::VisibleRows() const {
    const auto panel = PanelDip();
    const float available = panel.bottom - panel.top - ChromeDip();
    return available > 0 ? static_cast<std::size_t>(available / kRowHeight) : 0;
}
std::size_t SearchWindow::RowCapacity() const {
    const float available = TargetPanelDip() - ChromeDip();
    return available > 0 ? static_cast<std::size_t>(available / kRowHeight) : 0;
}
void SearchWindow::EnsureVisible() {
    const auto rows = RowCapacity();
    if (selected_ < firstVisible_) firstVisible_ = selected_;
    if (rows && selected_ >= firstVisible_ + rows) firstVisible_ = selected_ - rows + 1;
    if (results_.size() <= rows) firstVisible_ = 0;
    else firstVisible_ = std::min(firstVisible_, results_.size() - rows);
}
void SearchWindow::UpdateSelection(std::size_t index) {
    const std::size_t previousFirst = firstVisible_;
    selected_ = index; EnsureVisible();
    MoveIndicator(previousFirst == firstVisible_);
    (void)accessible_.NotifyFocusChanged(IdFor(selected_)); Invalidate();
}
std::optional<std::size_t> SearchWindow::Hit(D2D1_POINT_2F p) const {
    if (!IsOpen() || IsTransitioning()) return {};
    const float margin = Margin();
    const float x = p.x - margin, y = p.y - margin;
    if (y < kRowTop || x < kRowInset || x > PanelWidthDip() - kRowInset) return {};
    const auto row = static_cast<std::size_t>((y - kRowTop) / kRowHeight);
    if (row >= VisibleRows() || y - kRowTop - static_cast<float>(row) * kRowHeight >= kRowPill) return {};
    const auto index = firstVisible_ + row;
    return index < results_.size() ? std::optional(index) : std::nullopt;
}
void SearchWindow::OnPointerMove(D2D1_POINT_2F p) {
    if (IsTransitioning()) return;  // hit geometry is at rest while the compositor moves the panel
    if (selectionAnchor_ && IsOpen() && (::GetKeyState(VK_LBUTTON) & 0x8000)) {
        if (const auto index = CaretIndexAt(p)) { ::SendMessageW(edit_, EM_SETSEL, *selectionAnchor_, *index); Invalidate(); }
        return;
    }
    if (const auto hit = Hit(p); hit && *hit != selected_) UpdateSelection(*hit);
}
void SearchWindow::OnWheel(float notches, D2D1_POINT_2F) {
    if (!IsOpen()) return;
    FlushQuery(); if (results_.empty() || notches == 0) return;
    UpdateSelection(notches > 0 ? (selected_ ? selected_ - 1 : 0) : std::min(selected_ + 1, results_.size() - 1));
}
void SearchWindow::OnPointerUp(aether::ui::PointerButton button, D2D1_POINT_2F p) {
    if (button != aether::ui::PointerButton::Left) return;
    if (selectionAnchor_) { selectionAnchor_.reset(); return; }
    if (const auto hit = Hit(p)) Launch(*hit);
}
void SearchWindow::OnPointerCaptureLost() { selectionAnchor_.reset(); }
void SearchWindow::Launch(std::size_t index) {
    if (!IsOpen() || index >= results_.size()) return;
    const auto weak = std::weak_ptr<bool>(alive_);
    launcher_.LaunchAsync(results_[index].application, [this, weak](Result<void> result) {
        if (const auto live = weak.lock(); live && *live) {
            if (result) Close();
            else { error_ = L"Windows could not open this application."; EnsureVisible(); SetPanelTarget(true); MoveIndicator(false); Invalidate(); }
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
    if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) BeginClose(false);
    return std::nullopt;
}
void SearchWindow::OnDestroying() { StopTimers(); accessible_.Disconnect(); edit_ = nullptr; }
void SearchWindow::OnDpiChanged(UINT dpi, const RECT& suggested) {
    SurfaceWindow::OnDpiChanged(dpi, suggested);
    editFontPx_ = 0;
    appliedRegion_ = RECT{};
    Position();
    SetPanelTarget(false);
}

aether::ui::accessibility::AccessibleItem SearchWindow::Item(std::size_t index) const {
    aether::ui::accessibility::AccessibleItem item;
    if (index >= results_.size()) return item;
    item.id = IdFor(index); item.name = results_[index].application.displayName;
    item.helpText = results_[index].application.kind == apps::ApplicationKind::Packaged ? L"Installed application" : L"Desktop application";
    item.controlType = UIA_ListItemControlTypeId; item.supportsInvoke = true; item.isFocusable = true; item.isEnabled = IsOpen();
    item.hasKeyboardFocus = IsOpen() && ::GetFocus() == edit_ && index == selected_;
    item.isOffscreen = !IsOpen() || index < firstVisible_ || index >= firstVisible_ + VisibleRows();
    if (item.isOffscreen) return item;
    const auto b = BoundsPx();
    const float margin = Margin();
    const int top = b.top + static_cast<int>(ToPx(margin + kRowTop + static_cast<float>(index - firstVisible_) * kRowHeight));
    item.boundsPx = RECT{b.left + static_cast<int>(ToPx(margin + kRowInset)), top, b.right - static_cast<int>(ToPx(margin + kRowInset)),
                         top + static_cast<int>(ToPx(kRowPill))};
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
