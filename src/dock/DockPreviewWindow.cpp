// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockPreviewWindow.h"

#include "core/Strings.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MonitorUtilities.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aether::dock {
namespace {

constexpr UINT kPreviewInvoke = WM_APP + 115;
constexpr UINT_PTR kCloseTimer = 1;
constexpr std::size_t kMaxTiles = 6;
constexpr double kCloseRequestTimeout = 2.5;  // seconds before a still-open window counts as refused
constexpr double kFadeOut = 0.18;
constexpr float kAnchorGap = 10.0f;  // DIPs between the magnified icon and the card

bool Instant(const graphics::MotionPolicy& motion, const graphics::DesignTokens& tokens) noexcept {
    return !motion.AllowsMovement() || tokens.mode == graphics::ThemeMode::HighContrast;
}
bool Contains(const D2D1_RECT_F& rect, D2D1_POINT_2F point) noexcept {
    return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

}  // namespace

DockPreviewWindow::DockPreviewWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler)
    : SurfaceWindow(device, scheduler), tokens_(graphics::MakeDesignTokens(graphics::ThemeMode::Dark)), bitmaps_(device) {}

DockPreviewWindow::~DockPreviewWindow() { Destroy(); }

Result<void> DockPreviewWindow::Initialize(HWND owner, Callbacks callbacks) {
    callbacks_ = std::move(callbacks);
    text_.Initialize(Device().DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
    ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.DockPreview";
    options.title = L"Aether Shell window previews";
    options.owner = owner;  // stays above the Dock without being part of its region
    if (auto created = Create(options, RECT{0, 0, 1, 1}); !created) return created;
    return accessible_.Connect(Handle(), *this);
}

void DockPreviewWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion, float glassOpacity) {
    tokens_ = tokens;
    motion_ = motion;
    glassOpacity_ = std::isfinite(glassOpacity) ? std::clamp(glassOpacity, 0.3f, 1.0f) : 0.8f;
    text_.Initialize(Device().DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
    if (Instant(motion_, tokens_)) {
        if (dismissing_) FinishDismiss();
        else if (IsShown()) { opacity_.SnapTo(1.0f); Show(false); UpdateThumbnails(1.0f); }
    }
    if (IsShown() && !tiles_.empty()) Layout();  // material, backdrop and metrics follow the theme
    Invalidate();
}

bool DockPreviewWindow::Present(const DockPreviewContent& content, const RECT& anchorPx) {
    if (!Handle()) return false;
    if (!text::EqualsIgnoreCase(content.applicationId, applicationId_)) {
        ReleaseThumbnails();
        tiles_.clear();
        hover_.reset(); pressed_.reset(); focus_ = 0;
    }
    applicationId_ = content.applicationId;
    applicationName_ = content.applicationName;
    if (icon_ != content.icon) {
        if (!iconKey_.empty()) bitmaps_.Remove(iconKey_);
        icon_ = content.icon;
        iconKey_ = icon_ ? L"preview-icon#" + std::to_wstring(reinterpret_cast<std::uintptr_t>(icon_.get())) : L"";
    }
    anchorPx_ = anchorPx;
    const POINT center{(anchorPx.left + anchorPx.right) / 2, (anchorPx.top + anchorPx.bottom) / 2};
    const auto monitor = platform::QueryMonitor(::MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST));
    const UINT dpi = platform::DpiForPoint(center);
    const float monitorDip = monitor ? platform::PixelsToDips(platform::Width(monitor->bounds), dpi) : 1024.0f;
    const auto capacity = PreviewTileCapacity(monitorDip - 32.0f, kMaxTiles);

    // Tracker order (most recent first). Existing tiles keep their thumbnails, so a refresh does not
    // flash; only a live identity can produce a tile.
    std::vector<Tile> next;
    std::size_t live = 0;
    for (const auto& window : content.windows) {
        const auto identity = CaptureWindowIdentity(window.handle);
        if (!identity) continue;
        ++live;
        if (next.size() >= capacity) continue;
        Tile tile;
        const auto existing = std::find_if(tiles_.begin(), tiles_.end(), [&](const Tile& old) {
            return old.identity.handle == identity->handle && SameWindowIdentity(old.identity, *identity);
        });
        if (existing != tiles_.end()) {
            tile = *existing;
            existing->thumbnail = nullptr;  // ownership moves to the refreshed tile
        } else {
            tile.identity = *identity;
        }
        tile.title = window.title;
        tile.minimized = ::IsIconic(window.handle) != FALSE;
        auto& ids = ids_[window.handle];
        if (!ids.first) { ids.first = nextId_++; ids.second = nextId_++; }
        tile.bodyId = ids.first; tile.closeId = ids.second;
        next.push_back(std::move(tile));
    }
    for (auto& old : tiles_) Unregister(old);
    tiles_ = std::move(next);
    omitted_ = live - tiles_.size();
    std::erase_if(ids_, [&](const auto& entry) {
        return std::none_of(tiles_.begin(), tiles_.end(), [&](const Tile& tile) { return tile.identity.handle == entry.first; });
    });
    if (tiles_.empty()) { Dismiss(true); return false; }
    for (auto& tile : tiles_) Register(tile);
    if (hover_ && hover_->tile >= tiles_.size()) hover_.reset();
    if (pressed_ && pressed_->tile >= tiles_.size()) pressed_.reset();
    focus_ = std::min(focus_, tiles_.size() * 2 - 1);

    const bool appearing = !IsShown() || dismissing_;
    dismissing_ = false;
    Layout();
    if (appearing) {
        // The compositor fades the glass and its blurred backdrop; DWM thumbnails (composed above
        // this window's visuals) follow the same duration and curve frame by frame. An interrupted
        // dismissal reverses from its current opacity instead of restarting from transparent.
        const double now = graphics::FrameScheduler::Now();
        const float from = IsShown() ? opacity_.Value(now) : 0.0f;
        graphics::Transition fade;
        fade.opacityFrom = from;
        fade.opacityTo = 1.0f;
        fade.curve = tokens_.curves.decelerate;
        fade.duration = Instant(motion_, tokens_) ? 0.0 : motion_.Scale(tokens_.durationNormal) * (1.0 - from);
        opacity_.SnapTo(from);
        if (fade.IsInstant()) opacity_.SnapTo(1.0f);
        else opacity_.AnimateTo(1.0f, now, fade.duration, fade.curve);
        UpdateThumbnails(opacity_.Value(now));
        ShowAnimated(fade, false);
        RequestAnimationFrame();
    }
    (void)accessible_.NotifyStructureChanged();
    Invalidate();
    return true;
}

void DockPreviewWindow::Register(Tile& tile) {
    tile.unavailable = false;
    tile.sourceCrop.reset();
    if (!tile.thumbnail) {
        HTHUMBNAIL thumbnail = nullptr;
        if (FAILED(::DwmRegisterThumbnail(Handle(), tile.identity.handle, &thumbnail)) || !thumbnail) {
            tile.unavailable = true;  // shown as an explicit placeholder; never fabricated
            return;
        }
        tile.thumbnail = thumbnail;
    }
    SIZE size{};
    if (FAILED(::DwmQueryThumbnailSourceSize(tile.thumbnail, &size)) || size.cx <= 0 || size.cy <= 0) {
        Unregister(tile);
        tile.unavailable = true;
        return;
    }
    // A minimized window's source can be only its minimized caption: say so instead of stretching it.
    if (tile.minimized && (size.cx < 80 || size.cy < 80)) {
        Unregister(tile);
        tile.unavailable = true;
        return;
    }
    tile.sourcePx = size;
    // Drop the invisible resize borders that DWM reports outside the visible frame, so the aspect
    // ratio and edges match the window as seen. Only when both measurements describe the same box.
    RECT window{}, frame{};
    if (!tile.minimized && ::GetWindowRect(tile.identity.handle, &window) &&
        SUCCEEDED(::DwmGetWindowAttribute(tile.identity.handle, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))) &&
        window.right - window.left == size.cx && window.bottom - window.top == size.cy &&
        frame.left >= window.left && frame.top >= window.top && frame.right <= window.right &&
        frame.bottom <= window.bottom && frame.right > frame.left && frame.bottom > frame.top) {
        tile.sourceCrop = RECT{frame.left - window.left, frame.top - window.top, frame.right - window.left, frame.bottom - window.top};
        tile.sourcePx = SIZE{frame.right - frame.left, frame.bottom - frame.top};
    }
}

void DockPreviewWindow::Unregister(Tile& tile) noexcept {
    if (tile.thumbnail) (void)::DwmUnregisterThumbnail(tile.thumbnail);
    tile.thumbnail = nullptr;
}

void DockPreviewWindow::ReleaseThumbnails() noexcept {
    for (auto& tile : tiles_) Unregister(tile);
}

std::size_t DockPreviewWindow::LiveThumbnailCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(tiles_.begin(), tiles_.end(), [](const Tile& tile) { return tile.thumbnail != nullptr; }));
}

void DockPreviewWindow::Layout() {
    if (laying_ || tiles_.empty()) return;
    laying_ = true;
    const POINT center{(anchorPx_.left + anchorPx_.right) / 2, (anchorPx_.top + anchorPx_.bottom) / 2};
    const UINT dpi = platform::DpiForPoint(center);
    std::vector<SIZE> sources;
    for (const auto& tile : tiles_) sources.push_back(tile.unavailable ? SIZE{} : tile.sourcePx);
    layout_ = LayoutPreviewCard(sources, dpi, omitted_);
    layoutDpi_ = dpi;
    const float scale = platform::ScaleForDpi(dpi);
    const int width = static_cast<int>(std::ceil(layout_.card.width * scale));
    const int height = static_cast<int>(std::ceil(layout_.card.height * scale));
    RECT bounds{center.x - width / 2, anchorPx_.top - platform::DipsToPixels(kAnchorGap, dpi) - height, 0, 0};
    if (const auto monitor = platform::QueryMonitor(::MonitorFromPoint(center, MONITOR_DEFAULTTONEAREST))) {
        bounds.left = std::clamp<LONG>(bounds.left, monitor->bounds.left, std::max<LONG>(monitor->bounds.left, monitor->bounds.right - width));
        bounds.top = std::max<LONG>(bounds.top, monitor->workArea.top);
    }
    bounds.right = bounds.left + width;
    bounds.bottom = bounds.top + height;
    SetBoundsPx(bounds);  // may deliver WM_DPICHANGED; the guard defers that relayout to here
    laying_ = false;
    // Aether's neutral translucent material: the blurred backdrop under the whole card.
    SetBackdropPanel(D2D1::RectF(0, 0, layout_.card.width, layout_.card.height), CornerRadius(),
        tokens_.transparencyEnabled && tokens_.mode != graphics::ThemeMode::HighContrast, tokens_.glass.backdropOpacity);
    if (Dpi() != layoutDpi_ && Dpi() == platform::DpiForPoint(center)) { Layout(); return; }
    UpdateThumbnails(opacity_.Value(graphics::FrameScheduler::Now()));
}

void DockPreviewWindow::UpdateThumbnails(float opacity) {
    bool lost = false;
    for (std::size_t index = 0; index < tiles_.size() && index < layout_.tiles.size(); ++index) {
        auto& tile = tiles_[index];
        if (!tile.thumbnail) continue;
        DWM_THUMBNAIL_PROPERTIES properties{};
        properties.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        properties.rcDestination = PreviewPixelsInside(layout_.tiles[index].thumbnail, layoutDpi_);
        if (tile.sourceCrop) { properties.dwFlags |= DWM_TNP_RECTSOURCE; properties.rcSource = *tile.sourceCrop; }
        properties.fVisible = TRUE;
        properties.fSourceClientAreaOnly = FALSE;
        properties.opacity = static_cast<BYTE>(std::lround(std::clamp(opacity, 0.0f, 1.0f) * 255.0f));
        if (FAILED(::DwmUpdateThumbnailProperties(tile.thumbnail, &properties))) {
            Unregister(tile);
            tile.unavailable = true;
            lost = true;
        }
    }
    if (lost) Invalidate();
}

void DockPreviewWindow::Dismiss(bool immediate) {
    keyboard_ = false;
    hover_.reset(); pressed_.reset();
    if (!IsShown()) { FinishDismiss(); return; }
    if (immediate || Instant(motion_, tokens_)) { FinishDismiss(); return; }
    if (dismissing_) return;
    dismissing_ = true;
    const double now = graphics::FrameScheduler::Now();
    graphics::Transition fade;
    fade.opacityFrom = opacity_.Value(now);
    fade.opacityTo = 0.0f;
    fade.curve = tokens_.curves.accelerate;
    fade.duration = motion_.Scale(kFadeOut) * fade.opacityFrom;
    opacity_.SnapTo(fade.opacityFrom);
    opacity_.AnimateTo(0.0f, now, fade.duration, fade.curve);
    HideAnimated(fade);  // the thumbnails and the window are released when the tween completes
    RequestAnimationFrame();
}

void DockPreviewWindow::FinishDismiss() {
    dismissing_ = false;
    inside_ = false;
    keyboard_ = false;
    opacity_.SnapTo(0.0f);
    ReleaseThumbnails();
    tiles_.clear();
    ids_.clear();
    applicationId_.clear();
    if (Handle()) {
        ::KillTimer(Handle(), kCloseTimer);
        Hide();
        (void)accessible_.NotifyStructureChanged();
    }
}

void DockPreviewWindow::MarkCloseRequested(HWND window) {
    for (auto& tile : tiles_) {
        if (tile.identity.handle != window) continue;
        tile.closeRequestedAt = graphics::FrameScheduler::Now();
        ::SetTimer(Handle(), kCloseTimer, static_cast<UINT>(kCloseRequestTimeout * 1000), nullptr);
        (void)accessible_.NotifyPropertyChanged(tile.closeId);
        Invalidate();
    }
}

void DockPreviewWindow::BeginKeyboard() {
    if (tiles_.empty()) return;
    keyboard_ = true;
    Focus(0);
}
void DockPreviewWindow::EndKeyboard() { keyboard_ = false; Invalidate(); }

void DockPreviewWindow::Focus(std::size_t element) {
    if (tiles_.empty()) return;
    focus_ = std::min(element, tiles_.size() * 2 - 1);
    const auto& tile = tiles_[focus_ / 2];
    (void)accessible_.NotifyFocusChanged(focus_ % 2 ? tile.closeId : tile.bodyId);
    Invalidate();
}

bool DockPreviewWindow::HandleKey(UINT key) {
    if (!keyboard_ || tiles_.empty()) return false;
    const std::size_t last = tiles_.size() * 2 - 1;
    const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (key == VK_RIGHT || (key == VK_TAB && !shift)) Focus(std::min(focus_ + 1, last));
    else if (key == VK_LEFT || (key == VK_TAB && shift)) Focus(focus_ ? focus_ - 1 : 0);
    else if (key == VK_HOME) Focus(0);
    else if (key == VK_END) Focus(last);
    else if (key == VK_RETURN || key == VK_SPACE) Invoke(Element{focus_ / 2, focus_ % 2 ? Part::Close : Part::Body});
    else if (key == VK_DELETE) Invoke(Element{focus_ / 2, Part::Close});
    else return false;
    return true;
}

std::optional<DockPreviewWindow::Element> DockPreviewWindow::HitElement(D2D1_POINT_2F point) const {
    for (std::size_t index = 0; index < tiles_.size() && index < layout_.tiles.size(); ++index) {
        if (Contains(layout_.tiles[index].close, point)) return Element{index, Part::Close};
        if (Contains(layout_.tiles[index].tile, point)) return Element{index, Part::Body};
    }
    return std::nullopt;
}

RECT DockPreviewWindow::ScreenRect(const D2D1_RECT_F& rect) const {
    const float scale = platform::ScaleForDpi(layoutDpi_);
    const RECT bounds = BoundsPx();
    return RECT{bounds.left + static_cast<LONG>(std::floor(rect.left * scale)), bounds.top + static_cast<LONG>(std::floor(rect.top * scale)),
        bounds.left + static_cast<LONG>(std::ceil(rect.right * scale)), bounds.top + static_cast<LONG>(std::ceil(rect.bottom * scale))};
}

void DockPreviewWindow::Invoke(const Element& element) {
    if (!IsPresenting() || element.tile >= tiles_.size()) return;
    const auto identity = tiles_[element.tile].identity;  // callbacks may refresh or dismiss the card
    if (element.part == Part::Close) {
        if (tiles_[element.tile].closeRequestedAt >= 0) return;  // one request at a time per window
        if (callbacks_.close) callbacks_.close(identity);
    } else if (callbacks_.activate) {
        callbacks_.activate(identity);
    }
}

std::optional<RECT> DockPreviewWindow::TileBodyPx(std::size_t index) const {
    if (index >= layout_.tiles.size() || index >= tiles_.size()) return std::nullopt;
    return ScreenRect(layout_.tiles[index].thumbnailBox);
}
std::optional<RECT> DockPreviewWindow::CloseButtonPx(std::size_t index) const {
    if (index >= layout_.tiles.size() || index >= tiles_.size()) return std::nullopt;
    return ScreenRect(layout_.tiles[index].close);
}
std::optional<RECT> DockPreviewWindow::ThumbnailPx(std::size_t index) const {
    if (index >= layout_.tiles.size() || index >= tiles_.size()) return std::nullopt;
    RECT rect = PreviewPixelsInside(layout_.tiles[index].thumbnail, layoutDpi_);
    const RECT bounds = BoundsPx();
    ::OffsetRect(&rect, bounds.left, bounds.top);
    return rect;
}
HWND DockPreviewWindow::TileWindow(std::size_t index) const noexcept {
    return index < tiles_.size() ? tiles_[index].identity.handle : nullptr;
}
bool DockPreviewWindow::TileUnavailable(std::size_t index) const noexcept {
    return index < tiles_.size() && tiles_[index].unavailable;
}
bool DockPreviewWindow::ClosePending(std::size_t index) const noexcept {
    return index < tiles_.size() && tiles_[index].closeRequestedAt >= 0;
}

void DockPreviewWindow::OnRender(const ui::RenderContext& context) {
    ++frames_;
    if (tiles_.empty() || layout_.tiles.size() != tiles_.size()) return;
    auto* d2d = context.d2d;
    const D2D1_RECT_F card{0, 0, layout_.card.width, layout_.card.height};
    // DWM composes the live thumbnails above this window's visuals (verified with own windows, S-041),
    // so the glass is painted beneath them and nothing here may overlap a thumbnail rectangle.
    graphics::GlassStyle glass;
    glass.cornerRadius = CornerRadius();
    glass.opacity = glassOpacity_;
    glass.backdropBlur = BackdropBlurActive();
    glass.shadow = false;  // the window is exactly the card: no margin over other applications
    glass.elevation = graphics::Elevation::Popup;
    glass.neutral = true;
    glass.highlight = false;
    graphics::PaintGlassPanel(d2d, card, tokens_, glass);

    const bool contrast = tokens_.mode == graphics::ThemeMode::HighContrast;
    winrt::com_ptr<ID2D1SolidColorBrush> primary, secondary, disabled, onFill, closeFill, placeholder;
    if (FAILED(d2d->CreateSolidColorBrush(tokens_.textPrimary.ToD2D(), primary.put())) ||
        FAILED(d2d->CreateSolidColorBrush(tokens_.textSecondary.ToD2D(), secondary.put())) ||
        FAILED(d2d->CreateSolidColorBrush(tokens_.textDisabled.ToD2D(), disabled.put())) ||
        FAILED(d2d->CreateSolidColorBrush(contrast ? tokens_.surface.selectedText.ToD2D() : D2D1::ColorF(1, 1, 1, 1), onFill.put())) ||
        FAILED(d2d->CreateSolidColorBrush(contrast ? tokens_.surface.selectedFill.ToD2D() : tokens_.critical.ToD2D(), closeFill.put())) ||
        FAILED(d2d->CreateSolidColorBrush(tokens_.hoverOverlay.ToD2D(), placeholder.put()))) return;
    ID2D1Bitmap1* icon = nullptr;
    if (icon_ && !iconKey_.empty()) {
        icon = bitmaps_.Find(iconKey_);
        if (!icon && icon_->width && icon_->height) icon = bitmaps_.Put(d2d, iconKey_, icon_->width, icon_->height, icon_->pixels);
    }
    graphics::TextStyle titleStyle;
    titleStyle.size = tokens_.type.caption;
    titleStyle.opticalSmall = true;
    graphics::TextStyle centered = titleStyle;
    centered.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
    for (std::size_t index = 0; index < tiles_.size(); ++index) {
        const auto& tile = tiles_[index];
        const auto& geometry = layout_.tiles[index];
        const bool hoverBody = hover_ && hover_->tile == index;
        const bool pressedBody = pressed_ && pressed_->tile == index && pressed_->part == Part::Body;
        graphics::PaintSelectionPill(d2d, geometry.tile, tokens_.radii.item, tokens_, pressedBody, hoverBody);
        if (icon) graphics::DrawIconBitmap(d2d, icon, geometry.icon);
        const std::wstring title = tile.title.empty() ? applicationName_ : tile.title;
        text_.Draw(d2d, title, geometry.title, titleStyle, primary.get());
        const bool pending = tile.closeRequestedAt >= 0;
        const bool hoverClose = hover_ && hover_->tile == index && hover_->part == Part::Close && !pending;
        if (hoverClose) d2d->FillRoundedRectangle(D2D1::RoundedRect(geometry.close, tokens_.radii.control, tokens_.radii.control), closeFill.get());
        const float glyph = 12.0f;
        const float gx = (geometry.close.left + geometry.close.right) / 2, gy = (geometry.close.top + geometry.close.bottom) / 2;
        graphics::PaintGlyph(d2d, graphics::Glyph::Close, {gx - glyph / 2, gy - glyph / 2, gx + glyph / 2, gy + glyph / 2},
            pending ? disabled.get() : hoverClose ? onFill.get() : secondary.get(), tokens_.glyph.stroke);
        if (!tile.thumbnail) {
            d2d->FillRoundedRectangle(D2D1::RoundedRect(geometry.thumbnailBox, tokens_.radii.card, tokens_.radii.card), placeholder.get());
            text_.Draw(d2d, tile.minimized ? L"Minimized \x2014 preview unavailable" : L"Preview unavailable",
                geometry.thumbnailBox, centered, secondary.get());
        }
        if (keyboard_ && focus_ / 2 == index) {
            if (focus_ % 2) graphics::PaintFocusRing(d2d, geometry.close, tokens_.radii.control, tokens_);
            else graphics::PaintFocusRing(d2d, geometry.tile, tokens_.radii.item, tokens_);
        }
    }
    if (layout_.more && omitted_ > 0) {
        text_.Draw(d2d, L"+" + std::to_wstring(omitted_), *layout_.more, centered, secondary.get());
    }
}

float DockPreviewWindow::CornerRadius() const noexcept { return tokens_.popupCornerRadius + 2.0f; }

bool DockPreviewWindow::OnAnimate(double now) {
    const bool animating = opacity_.IsAnimating(now);
    UpdateThumbnails(opacity_.Value(now));
    if (!animating && dismissing_) FinishDismiss();
    return animating;
}

void DockPreviewWindow::OnDpiChanged(UINT dpi, const RECT& suggestedPx) {
    if (tiles_.empty()) { SurfaceWindow::OnDpiChanged(dpi, suggestedPx); return; }
    if (laying_) return;  // Layout() re-checks the DPI after its own move
    Layout();
}

void DockPreviewWindow::OnPointerMove(D2D1_POINT_2F point) {
    if (!IsPresenting() || !std::isfinite(point.x) || !std::isfinite(point.y)) return;
    if (!inside_) {
        inside_ = true;
        if (callbacks_.pointerEntered) callbacks_.pointerEntered();
        if (!IsPresenting()) return;
    }
    const auto hit = HitElement(point);
    const bool changed = hit.has_value() != hover_.has_value() ||
        (hit && hover_ && (hit->tile != hover_->tile || hit->part != hover_->part));
    hover_ = hit;
    if (changed) Invalidate();
}

void DockPreviewWindow::OnPointerLeave() {
    hover_.reset();
    Invalidate();
    if (!inside_) return;
    inside_ = false;
    if (callbacks_.pointerLeft) callbacks_.pointerLeft();
}

void DockPreviewWindow::OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) {
    if (button != ui::PointerButton::Left || !IsPresenting()) return;
    pressed_ = HitElement(point);
    Invalidate();
}

void DockPreviewWindow::OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) {
    if (button != ui::PointerButton::Left) return;
    const auto pressed = pressed_;
    pressed_.reset();
    Invalidate();
    const auto hit = HitElement(point);
    if (pressed && hit && pressed->tile == hit->tile && pressed->part == hit->part) Invoke(*hit);
}

void DockPreviewWindow::OnDeviceReplaced() {
    bitmaps_.Clear();
    Invalidate();
}

std::optional<LRESULT> DockPreviewWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_GETOBJECT) {
        LRESULT result = 0;
        if (accessible_.HandleGetObject(Handle(), wParam, lParam, result)) return result;
    }
    if (message == kPreviewInvoke) {
        if (const auto element = ElementForId(static_cast<int>(wParam))) Invoke(*element);
        return 0;
    }
    if (message == WM_TIMER && wParam == kCloseTimer) {
        const double now = graphics::FrameScheduler::Now();
        bool waiting = false;
        for (auto& tile : tiles_) {
            if (tile.closeRequestedAt < 0) continue;
            // Still open after the request: refused, cancelled or waiting on the app's own prompt.
            if (now - tile.closeRequestedAt >= kCloseRequestTimeout - 0.05) {
                tile.closeRequestedAt = -1;
                (void)accessible_.NotifyPropertyChanged(tile.closeId);
            } else {
                waiting = true;
            }
        }
        if (!waiting) ::KillTimer(Handle(), kCloseTimer);
        Invalidate();
        return 0;
    }
    return std::nullopt;
}

void DockPreviewWindow::OnDestroying() {
    ::KillTimer(Handle(), kCloseTimer);
    accessible_.Disconnect();
    ReleaseThumbnails();
    tiles_.clear();
}

std::wstring DockPreviewWindow::RootName() const {
    return (applicationName_.empty() ? std::wstring(L"Application") : applicationName_) + L" windows";
}

ui::accessibility::AccessibleItem DockPreviewWindow::Item(std::size_t index) const {
    ui::accessibility::AccessibleItem item;
    // UIA can ask during a refresh; never throw across the provider boundary.
    if (index / 2 >= tiles_.size() || index / 2 >= layout_.tiles.size()) { item.isOffscreen = true; item.isEnabled = false; return item; }
    const auto& tile = tiles_[index / 2];
    const auto& geometry = layout_.tiles[index / 2];
    const std::wstring title = tile.title.empty() ? applicationName_ + L" window" : tile.title;
    item.isOffscreen = !IsPresenting();
    item.isEnabled = !item.isOffscreen;
    item.supportsInvoke = true;
    item.hasKeyboardFocus = keyboard_ && focus_ == index;
    if (index % 2 == 0) {
        item.id = tile.bodyId;
        item.name = title;
        item.helpText = L"Switches to this window";
        if (tile.thumbnail == nullptr) item.itemStatus = tile.minimized ? L"Minimized, preview unavailable" : L"Preview unavailable";
        else if (tile.minimized) item.itemStatus = L"Minimized";
        item.boundsPx = ScreenRect(geometry.tile);
    } else {
        item.id = tile.closeId;
        item.name = L"Close " + title;
        item.helpText = L"Asks this window to close; the application can still ask to save or cancel";
        if (tile.closeRequestedAt >= 0) item.itemStatus = L"Close requested";
        item.boundsPx = ScreenRect(geometry.close);
    }
    return item;
}

std::optional<DockPreviewWindow::Element> DockPreviewWindow::ElementForId(int id) const {
    for (std::size_t index = 0; index < tiles_.size(); ++index) {
        if (tiles_[index].bodyId == id) return Element{index, Part::Body};
        if (tiles_[index].closeId == id) return Element{index, Part::Close};
    }
    return std::nullopt;
}

Result<void> DockPreviewWindow::Invoke(int id) {
    if (!Handle() || !IsPresenting()) return MakeError(ErrorCode::Unavailable, "Window previews are not shown");
    if (!ElementForId(id)) return MakeError(ErrorCode::NotFound, "This window preview no longer exists");
    if (!::PostMessageW(Handle(), kPreviewInvoke, static_cast<WPARAM>(id), 0))
        return MakeError(ErrorCode::Unavailable, "Window preview action could not be queued");
    return Ok();
}

Result<void> DockPreviewWindow::SetFocus(int id) {
    const auto element = ElementForId(id);
    if (!element) return MakeError(ErrorCode::NotFound, "This window preview no longer exists");
    // Keyboard focus stays with the Dock; previews are explored from it (Up arrow on a running app).
    if (!keyboard_) return MakeError(ErrorCode::Unsupported, "Window previews are reached from the focused Dock item");
    Focus(element->tile * 2 + (element->part == Part::Close ? 1 : 0));
    return Ok();
}

}  // namespace aether::dock
