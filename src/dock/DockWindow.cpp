// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockWindow.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "dock/DockModel.h"
#include "dock/DockMotion.h"
#include "dock/DockPreview.h"
#include "dock/DockPreviewWindow.h"
#include "graphics/BitmapCache.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Win32Error.h"
#include "settings/SettingsSerialization.h"
#include "ui/TooltipWindow.h"
#include "ui/GlassMenu.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <set>

namespace aether::dock {
namespace {

constexpr UINT kAppBarMessage = WM_APP + 113;
constexpr UINT kDeferredInvoke = WM_APP + 114;
constexpr UINT_PTR kTooltipTimer = 1;
constexpr UINT_PTR kErrorTimer = 2;
constexpr UINT_PTR kLongPressTimer = 3;
constexpr UINT_PTR kHoverTimer = 4;

template <class F> Result<void> Guard(F&& function) {
    try { return function(); }
    catch (...) { return MakeError(ErrorCode::Internal, "Dock operation failed"); }
}
RECT Pixels(const D2D1_RECT_F& rect, float scale) {
    return {static_cast<LONG>(std::floor(rect.left * scale)), static_cast<LONG>(std::floor(rect.top * scale)),
        static_cast<LONG>(std::ceil(rect.right * scale)), static_cast<LONG>(std::ceil(rect.bottom * scale))};
}
std::wstring MenuText(std::wstring text) {
    for (std::size_t at = 0; (at = text.find(L'&', at)) != std::wstring::npos; at += 2) text.insert(at, 1, L'&');
    return text;
}
bool SamePresentation(const std::vector<DockItem>& first, const std::vector<DockItem>& second) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& a = first[index]; const auto& b = second[index];
        if (a.kind != b.kind || !text::EqualsIgnoreCase(a.id, b.id) || a.displayName != b.displayName ||
            a.running != b.running || a.foreground != b.foreground || a.attentionRequested != b.attentionRequested || a.windowCount != b.windowCount ||
            apps::IconSourceFingerprint(a.application) != apps::IconSourceFingerprint(b.application)) return false;
    }
    return true;
}

}  // namespace

struct DockWindow::Impl final : ui::accessibility::IAccessibleSurfaceModel {
    Impl(DockWindow& surface, DockWindowContext dependencies)
        : window(surface), context(dependencies), bitmaps(dependencies.graphics),
          tooltip(dependencies.graphics, dependencies.scheduler), preview(dependencies.graphics, dependencies.scheduler) {}

    DockWindow& window;
    DockWindowContext context;
    DockWindowOptions options;
    graphics::DesignTokens tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    graphics::MotionPolicy motion;
    settings::DockSettings settings;
    std::vector<apps::RunningApplication> running;
    apps::ApplicationIndex catalog;
    std::vector<std::wstring> sessionOrder;
    std::shared_ptr<ui::GlassMenuPresenter> menus;
    FittedDock fitted;
    DockLayout layout;
    std::vector<graphics::Spring> springs;
    std::optional<D2D1_POINT_2F> pointer;
    std::optional<std::size_t> hovered;
    std::optional<std::size_t> pressed;
    D2D1_POINT_2F pressPoint{};
    std::wstring dragId;
    float dragCenter = 0;
    float grabOffsetX = 0;
    bool dragging = false;
    bool suppressed = false;
    bool keyboard = false;
    std::size_t focused = 0;
    HWND previousForeground = nullptr;
    bool appBar = false;
    bool updatingBar = false;
    RECT appBarRect{};
    bool appBarPositioned = false;
    UINT explorerMessage = ::RegisterWindowMessageW(L"TaskbarCreated");
    float width = 500;
    double lastFrame = 0;
    std::uint64_t frames = 0;
    std::map<std::wstring, double> bounces;
    std::map<std::wstring, graphics::Spring> reorderOffsets;
    std::map<std::wstring, graphics::Spring> presses;
    std::map<std::wstring, graphics::Spring> lifts;
    std::map<std::wstring, DockIndicatorMotion> indicators;
    std::map<std::wstring, float> baseCenters;
    std::map<std::wstring, double> attentionPulses;
    std::set<std::wstring> attentionLatched;
    std::set<std::wstring> pendingAttention;
    std::map<std::wstring, std::shared_ptr<const apps::IconImage>> images;
    std::set<std::wstring> requested;
    graphics::BitmapCache bitmaps;
    ui::TooltipWindow tooltip;
    DockPreviewWindow preview;
    DockHoverSession hover;
    std::optional<double> armedHover;
    RECT previewAnchor{};
    bool applyingHover = false;
    ui::accessibility::AccessibleSurface accessible;
    Connection settingsChanged;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::map<std::wstring, int> accessibleIds;
    int nextAccessibleId = 1;

    bool AllowsDecoration() const noexcept {
        return !motion.reduced && tokens.mode != graphics::ThemeMode::HighContrast;
    }
    void UpdateIndicators() {
        auto policy = motion;
        policy.reduced = !AllowsDecoration();
        std::set<std::wstring> current;
        bool moving = false;
        bool changed = false;
        for (std::size_t index = 0; index < fitted.items.size(); ++index) {
            const auto& item = fitted.items[index];
            if (!item.running) continue;
            const auto id = text::ToLower(item.id);
            current.insert(id);
            auto& marker = indicators[id];
            const float before = marker.Width();
            // Emphasis covers the icon, its card and the crossing between them. It never changes the
            // observed foreground: only item.foreground selects the active styling when drawn.
            marker.Retarget(true, item.foreground, hover.Emphasizes(item.id), policy);
            changed = changed || before != marker.Width();
            moving = moving || marker.IsAnimating();
        }
        changed = std::erase_if(indicators, [&](const auto& entry) { return !current.contains(entry.first); }) > 0 || changed;
        if (changed) window.Invalidate();
        if (moving) window.RequestAnimationFrame();
    }
    std::optional<std::size_t> DragIndex() const {
        for (std::size_t i = 0; i < fitted.items.size(); ++i)
            if (text::EqualsIgnoreCase(fitted.items[i].id, dragId)) return i;
        return std::nullopt;
    }
    void FinishDrag(bool settle = true) {
        ::KillTimer(window.Handle(), kLongPressTimer);
        if (!dragId.empty() && settle && AllowsDecoration()) {
            if (const auto index = DragIndex(); index && baseCenters.contains(fitted.items[*index].id)) {
                auto spring = graphics::Spring(dragCenter - baseCenters.at(fitted.items[*index].id),
                    motion.ScaleSpring(graphics::Motion().slide));
                spring.SetTarget(0); reorderOffsets.insert_or_assign(fitted.items[*index].id, spring);
            }
        }
        pressed.reset(); dragId.clear(); dragging = false;
        for (auto& [id, spring] : presses) { (void)id; spring.SetTarget(0); }
        if (settle && AllowsDecoration()) {
            for (auto& [id, spring] : lifts) { (void)id; spring.SetTarget(0); }
            window.RequestAnimationFrame();
        } else { lifts.clear(); presses.clear(); }
    }
    void BeginDrag() {
        if (dragging || dragId.empty() || !DragIndex()) return;
        dragging = true;
        if (const auto press = presses.find(dragId); press != presses.end()) press->second.SetTarget(0);
        if (AllowsDecoration()) {
            auto& spring = lifts.try_emplace(dragId, 0.0f, motion.ScaleSpring(graphics::Motion().lift)).first->second;
            spring.SetTarget(1); window.RequestAnimationFrame();
        }
        tooltip.HideTooltip(); ::KillTimer(window.Handle(), kTooltipTimer);
        ResetHover();
    }
    void UpdateAttention() {
        std::set<std::wstring> requestedNow;
        for (const auto& item : fitted.items) if (item.attentionRequested && !item.foreground) {
            requestedNow.insert(item.id);
            if (!attentionLatched.contains(item.id)) pendingAttention.insert(item.id);
        }
        attentionLatched = std::move(requestedNow);
        std::erase_if(pendingAttention, [&](const auto& id) { return !attentionLatched.contains(id); });
        if (!suppressed && window.IsShown()) {
            if (AllowsDecoration()) for (const auto& id : pendingAttention)
                attentionPulses[id] = graphics::FrameScheduler::Now();
            pendingAttention.clear();
        }
        std::erase_if(attentionPulses, [&](const auto& entry) {
            return !attentionLatched.contains(entry.first) || !AllowsDecoration() || suppressed || !window.IsShown();
        });
        if (!attentionPulses.empty()) window.RequestAnimationFrame();
    }

    int Id(std::size_t index) const {
        return accessibleIds.at(text::ToLower(fitted.items[index].id));
    }
    std::optional<std::size_t> Index(int id) const {
        for (std::size_t index = 0; index < fitted.items.size(); ++index) if (Id(index) == id) return index;
        return std::nullopt;
    }
    RECT WorkArea() const {
        RECT area = options.workAreaPx.value_or(platform::PrimaryMonitor().workArea);
        // Before the first ABM_SETPOS the AppBar rectangle is still empty; using it would put the
        // lower edge at 0 and reject every placement (found on the real desktop, 2026-10-18).
        if (appBar && appBarPositioned && !options.workAreaPx) area.bottom = appBarRect.bottom;
        return area;
    }
    Result<void> Reserve() {
        const bool wanted = options.allowWorkAreaReservation &&
            (settings.reserveSpace || options.replacementTaskbarMode) && !options.workAreaPx;
        if (!wanted && appBar) {
            APPBARDATA data{sizeof(data)}; data.hWnd = window.Handle();
            appBar = false; appBarPositioned = false;
            ::SHAppBarMessage(ABM_REMOVE, &data);
        }
        if (!wanted || updatingBar) return Ok();
        updatingBar = true;
        APPBARDATA data{sizeof(data)}; data.hWnd = window.Handle(); data.uCallbackMessage = kAppBarMessage;
        if (!appBar) {
            if (!::SHAppBarMessage(ABM_NEW, &data)) { updatingBar = false; return MakeError(ErrorCode::Platform, "Dock AppBar registration failed"); }
            appBar = true;
        }
        const auto monitor = platform::PrimaryMonitor();
        data.uEdge = ABE_BOTTOM; data.rc = monitor.bounds;
        ::SHAppBarMessage(ABM_QUERYPOS, &data);
        const auto placement = PlaceDock(layout, WorkArea(), window.Dpi(), fitted.iconSize,
            fitted.magnification, static_cast<float>(settings.bottomMargin), motion.reduced, settings.reserveAnimationSpace);
        if (!placement) { updatingBar = false; return placement.error(); }
        // Honor the shell's returned lower edge so another AppBar or the native auto-hide reveal
        // strip is preserved. The application releases the ordinary native-taskbar reservation
        // before enabling replacement mode; the Dock never changes another AppBar's allocation.
        data.rc.top = data.rc.bottom - placement->reservationHeightPx;
        if (!appBarPositioned || !::EqualRect(&appBarRect, &data.rc)) {
            ::SHAppBarMessage(ABM_SETPOS, &data); appBarRect = data.rc; appBarPositioned = true;
        }
        updatingBar = false;
        return Ok();
    }
    std::wstring ImageKey(const DockItem& item) const {
        const auto pixels = static_cast<unsigned>(std::ceil(window.ToPx(fitted.iconSize * fitted.magnification)));
        return text::ToLower(item.id) + L"#" + apps::IconSourceFingerprint(item.application) + L"#" + std::to_wstring(pixels);
    }
    void RequestIcons() {
        std::set<std::wstring> current;
        for (const auto& item : fitted.items) if (IsApplicationItem(item.kind)) current.insert(ImageKey(item));
        for (const auto& key : requested) if (!current.contains(key)) { images.erase(key); bitmaps.Remove(key); }
        std::erase_if(requested, [&](const auto& key) { return !current.contains(key); });
        for (const auto& item : fitted.items) {
            if (!IsApplicationItem(item.kind)) continue;
            const auto key = ImageKey(item);
            if (!requested.insert(key).second) continue;
            std::weak_ptr<bool> lifetime = alive;
            try {
                context.icons.RequestIcon({key, item.application,
                    static_cast<unsigned>(std::ceil(window.ToPx(fitted.iconSize * fitted.magnification)))},
                    [this, lifetime](const std::wstring& keyValue, std::shared_ptr<const apps::IconImage> image) {
                        const auto valid = lifetime.lock(); if (!valid || !*valid || !requested.contains(keyValue)) return;
                        images[keyValue] = std::move(image); window.Invalidate();
                    });
            } catch (...) { requested.erase(key); images.erase(key); bitmaps.Remove(key); AETHER_LOG_WARN("dock", "an icon request failed"); }
        }
    }
    Result<void> Arrange() {
        std::vector<float> scales;
        for (const auto& spring : springs) scales.push_back(std::clamp(spring.Value(), 1.0f, fitted.magnification));
        auto geometry = ArrangeDockLayout(fitted.iconSize, static_cast<float>(settings.iconSpacing), fitted.magnification, width, scales);
        if (!geometry) return geometry.error();
        layout = std::move(*geometry);
        baseCenters.clear();
        const double now = graphics::FrameScheduler::Now();
        for (std::size_t index = 0; index < fitted.items.size(); ++index) {
            auto& rect = layout.items[index].icon;
            baseCenters[fitted.items[index].id] = (rect.left + rect.right) / 2;
            if (const auto moving = reorderOffsets.find(fitted.items[index].id); moving != reorderOffsets.end()) {
                const float shift = motion.reduced ? 0 : moving->second.Value();
                rect.left += shift; rect.right += shift;
            }
            if (const auto bounce = bounces.find(fitted.items[index].id); bounce != bounces.end()) {
                if (AllowsDecoration()) {
                    const float shift = graphics::LaunchBounce((now - bounce->second) / motion.EffectiveScale(),
                        graphics::MaxBounceHeight(fitted.iconSize));
                    rect.top -= shift; rect.bottom -= shift;
                }
            }
            if (const auto attention = attentionPulses.find(fitted.items[index].id); attention != attentionPulses.end() && AllowsDecoration()) {
                const auto shift = DockAttentionAt((now - attention->second) / motion.EffectiveScale()).height;
                rect.top -= shift; rect.bottom -= shift;
            }
            if (!dragId.empty() && text::EqualsIgnoreCase(fitted.items[index].id, dragId)) {
                const float shift = dragCenter - (rect.left + rect.right) / 2;
                rect.left += shift; rect.right += shift;
            }
            if (const auto lift = lifts.find(fitted.items[index].id); lift != lifts.end() && AllowsDecoration()) {
                layout.raisedItem = index;
                const float progress = std::clamp(lift->second.Value(), 0.0f, 1.1f);
                const float grow = (rect.right - rect.left) * (graphics::Motion().liftScale - 1) * progress / 2;
                rect.left -= grow; rect.right += grow; rect.top -= grow + graphics::Motion().liftOffset * progress;
                rect.bottom += grow - graphics::Motion().liftOffset * progress;
            }
            if (const auto press = presses.find(fitted.items[index].id); press != presses.end() && AllowsDecoration()) {
                const float inset = (rect.right - rect.left) * (1 - graphics::Motion().pressScale) *
                    std::clamp(press->second.Value(), 0.0f, 1.1f) / 2;
                rect.left += inset; rect.right -= inset; rect.top += inset; rect.bottom -= inset;
            }
        }
        if (dragging) layout.raisedItem = DragIndex();
        std::vector<RECT> regions{Pixels(layout.panel, static_cast<float>(window.Dpi()) / 96)};
        for (const auto& item : layout.items) regions.push_back(Pixels(item.icon, static_cast<float>(window.Dpi()) / 96));
        window.SetInputRegionPx(regions);
        // HTTRANSPARENT forwards only within one thread. Keep empty margins outside the native
        // HWND region so other processes receive clicks; outer shadows/entrance pixels are clipped.
        window.SetVisualRegionPx({});
        window.SetBackdropPanel(layout.panel, graphics::DockCornerRadius(tokens, layout.panel.bottom - layout.panel.top),
            tokens.transparencyEnabled && tokens.mode != graphics::ThemeMode::HighContrast, tokens.glass.backdropOpacity);
        return Ok();
    }
    Result<void> Rebuild() {
        const auto previousCenters = baseCenters;
        const auto pressedId = pressed && *pressed < fitted.items.size() ? fitted.items[*pressed].id : L"";
        const auto hoveredId = hovered && *hovered < fitted.items.size() ? fitted.items[*hovered].id : L"";
        const auto focusedId = focused < fitted.items.size() ? fitted.items[focused].id : L"";
        const auto area = WorkArea();
        const float fullWidth = window.ToDip(static_cast<float>(area.right - area.left));
        const float margin = std::clamp(static_cast<float>(settings.sideMargin), 0.0f, std::max(0.0f, (fullWidth - 56) / 2));
        width = fullWidth - margin * 2;
        auto model = BuildDockItems(DockModelInputs{settings, running, sessionOrder, &catalog}); if (!model) return model.error();
        auto fit = FitDockItems(*model, settings, std::max(56.0f, width - 2 * kDockShadowMargin)); if (!fit) return fit.error();
        std::map<std::wstring, graphics::Spring> old;
        for (std::size_t index = 0; index < springs.size(); ++index) old.emplace(fitted.items[index].id, springs[index]);
        fitted = std::move(*fit); springs.clear();
        for (const auto& item : fitted.items) {
            auto found = old.find(item.id);
            springs.push_back(found == old.end() ? graphics::Spring(1, motion.ScaleSpring(graphics::Motion().magnify)) : found->second);
            springs.back().SetParameters(motion.ScaleSpring(graphics::Motion().magnify));
            const auto id = text::ToLower(item.id);
            if (!accessibleIds.contains(id)) accessibleIds[id] = nextAccessibleId++;
        }
        pressed.reset(); hovered.reset();
        focused = fitted.items.empty() ? 0 : std::min(focused, fitted.items.size() - 1);
        for (std::size_t index = 0; index < fitted.items.size(); ++index) {
            if (text::EqualsIgnoreCase(fitted.items[index].id, pressedId)) pressed = index;
            if (text::EqualsIgnoreCase(fitted.items[index].id, hoveredId)) hovered = index;
            if (text::EqualsIgnoreCase(fitted.items[index].id, focusedId)) focused = index;
        }
        if (!dragId.empty() && !DragIndex()) {
            FinishDrag(false);
            if (::GetCapture() == window.Handle()) ::ReleaseCapture();
        }
        std::erase_if(reorderOffsets, [&](const auto& entry) {
            return std::none_of(fitted.items.begin(), fitted.items.end(), [&](const auto& item) { return item.id == entry.first; });
        });
        std::erase_if(presses, [&](const auto& entry) {
            return std::none_of(fitted.items.begin(), fitted.items.end(), [&](const auto& item) { return item.id == entry.first; });
        });
        std::erase_if(lifts, [&](const auto& entry) {
            return std::none_of(fitted.items.begin(), fitted.items.end(), [&](const auto& item) { return item.id == entry.first; });
        });
        auto result = Arrange(); if (!result) return result;
        if (!AllowsDecoration()) reorderOffsets.clear();
        else for (const auto& item : fitted.items) {
            const auto before = previousCenters.find(item.id);
            if (before == previousCenters.end()) continue;
            const float shift = before->second - baseCenters.at(item.id);
            auto moving = reorderOffsets.find(item.id);
            if (moving == reorderOffsets.end() && std::abs(shift) > 0.01f)
                moving = reorderOffsets.try_emplace(item.id, 0.0f, motion.ScaleSpring(graphics::Motion().slide)).first;
            if (moving != reorderOffsets.end()) {
                // Rebase the spring rather than restart it: the drawn center and velocity survive
                // another crossed slot or a running-status/catalog refresh mid-gesture.
                moving->second.ShiftBy(shift); moving->second.SetTarget(0);
                moving->second.SetParameters(motion.ScaleSpring(graphics::Motion().slide));
            }
        }
        (void)Arrange(); UpdateAttention(); UpdateIndicators();
        if (!reorderOffsets.empty()) window.RequestAnimationFrame();
        const auto placement = PlaceDock(layout, area, window.Dpi(), fitted.iconSize,
            fitted.magnification, static_cast<float>(settings.bottomMargin), motion.reduced);
        if (!placement) return placement.error();
        window.SetBoundsPx(placement->windowPx);
        RequestIcons();
        (void)accessible.NotifyStructureChanged();
        window.Invalidate();
        return Ok();
    }
    void SetTargets() {
        UpdateIndicators();
        auto targets = DockMagnification(fitted.items.size(), fitted.iconSize, static_cast<float>(settings.iconSpacing),
            fitted.magnification, width, pointer ? std::optional<float>(pointer->x) : std::nullopt);
        if (!targets) return;
        bool changed = false;
        for (std::size_t i = 0; i < springs.size(); ++i) {
            changed = changed || std::abs(springs[i].Target() - (*targets)[i]) > 0.0001f || !springs[i].IsSettled();
            if (motion.reduced) springs[i].SnapTo((*targets)[i]); else springs[i].SetTarget((*targets)[i]);
        }
        if (!changed) return;
        if (motion.reduced) { (void)Arrange(); window.Invalidate(); }
        else window.RequestAnimationFrame();
    }
    void MoveDrag(float pointerX) {
        const auto source = DragIndex();
        if (!source || !std::isfinite(pointerX)) return;
        const float half = fitted.iconSize * fitted.magnification * graphics::Motion().liftScale / 2;
        dragCenter = std::clamp(pointerX - grabOffsetX, kDockShadowMargin + half,
            std::max(kDockShadowMargin + half, width - kDockShadowMargin - half));
        std::vector<float> centers;
        std::vector<std::size_t> slots;
        const float baseWidth = static_cast<float>(fitted.items.size()) * fitted.iconSize +
            static_cast<float>(fitted.items.empty() ? 0 : fitted.items.size() - 1) * static_cast<float>(settings.iconSpacing);
        const float start = (width - baseWidth) / 2;
        std::size_t current = 0;
        for (std::size_t index = 0; index < fitted.items.size(); ++index) if (IsReorderable(fitted.items[index].kind)) {
            if (index == *source) current = centers.size();
            centers.push_back(start + static_cast<float>(index) * (fitted.iconSize + static_cast<float>(settings.iconSpacing)) + fitted.iconSize / 2);
            slots.push_back(index);
        }
        const auto destination = CrossedDockSlot(centers, current, dragCenter);
        if (!destination || *destination == current) return;
        auto moved = ReorderDockItems(DockModelInputs{context.settings.Current().dock, running, sessionOrder, &catalog},
            dragId, fitted.items[slots[*destination]].id);
        if (!moved) { ErrorTooltip(moved.error(), *source); return; }
        sessionOrder = std::move(moved->sessionOrder);
        if (moved->pinsChanged) SavePins(std::move(moved->pins));
        else (void)window.Relayout();
    }
    RECT Anchor(std::size_t index) const {
        auto rect = Pixels(layout.items[index].icon, static_cast<float>(window.Dpi()) / 96);
        const auto bounds = window.BoundsPx(); ::OffsetRect(&rect, bounds.left, bounds.top); return rect;
    }
    void ErrorTooltip(const Error& error, std::size_t index) {
        // A launch completion can arrive after fullscreen suppression or shutdown has closed input.
        if (!window.Handle() || suppressed || dragging || !window.IsShown() || window.IsTransitioning() ||
            !::IsWindowEnabled(window.Handle()) || index >= layout.items.size()) return;
        tooltip.ShowFor(text::Widen(error.message), Anchor(index), ui::TooltipPlacement::Above);
        ::SetTimer(window.Handle(), kErrorTimer, 3000, nullptr);
    }
    void Emit(Signal<>& signal) {
        try { signal.Emit(); } catch (...) { AETHER_LOG_ERROR("dock", "Dock action listener threw an exception"); }
    }

    // --- Hover emphasis and window previews (T42/T43) ---
    std::optional<std::size_t> IndexOf(std::wstring_view id) const {
        if (id.empty()) return std::nullopt;
        for (std::size_t index = 0; index < fitted.items.size(); ++index)
            if (text::EqualsIgnoreCase(fitted.items[index].id, id)) return index;
        return std::nullopt;
    }
    // A card can only represent a live window the tracker currently lists for this group.
    static bool Previewable(const DockItem& item) {
        if (!IsApplicationItem(item.kind) || !item.running || !item.runningApplication) return false;
        return std::any_of(item.runningApplication->windows.begin(), item.runningApplication->windows.end(),
            [](const auto& entry) { return CaptureWindowIdentity(entry.handle).has_value(); });
    }
    bool HoverAllowed() const {
        return window.Handle() && !suppressed && !dragging && window.IsShown() && !window.IsTransitioning() &&
            ::IsWindowEnabled(window.Handle());
    }
    DockPreviewContent PreviewContent(const DockItem& item) const {
        DockPreviewContent content;
        content.applicationId = item.id;
        content.applicationName = item.displayName;
        if (item.runningApplication) content.windows = item.runningApplication->windows;
        if (const auto image = images.find(ImageKey(item)); image != images.end()) content.icon = image->second;
        return content;
    }
    void PointerHover(std::optional<std::size_t> hit) {
        const double now = graphics::FrameScheduler::Now();
        if (hit && *hit < fitted.items.size()) {
            const auto& item = fitted.items[*hit];
            const bool application = IsApplicationItem(item.kind);
            hover.PointerOnItem(item.id, application && item.running, application && Previewable(item), now);
        } else {
            hover.PointerOnItem({}, false, false, now);
        }
        ApplyHover();
    }
    void ArmHoverTimer() {
        const auto deadline = hover.Deadline();
        if (deadline == armedHover || !window.Handle()) return;
        armedHover = deadline;
        if (!deadline) { ::KillTimer(window.Handle(), kHoverTimer); return; }
        const double delay = std::max(0.0, *deadline - graphics::FrameScheduler::Now());
        ::SetTimer(window.Handle(), kHoverTimer, static_cast<UINT>(std::ceil(delay * 1000)) + 1, nullptr);
    }
    // Brings markers and the card in line with the session. Presenting happens only when the
    // previewed application changes; tracker refreshes go through RefreshPreview.
    void ApplyHover() {
        if (applyingHover) return;
        applyingHover = true;
        const std::wstring wanted = hover.Previewed();
        if (wanted.empty()) {
            if (preview.IsPresenting()) preview.Dismiss();
        } else if (!HoverAllowed()) {
            hover.PreviewUnavailable(wanted);
            preview.Dismiss(true);
        } else if (!preview.IsPresenting() || !text::EqualsIgnoreCase(preview.ApplicationId(), wanted)) {
            const auto index = IndexOf(wanted);
            bool shown = false;
            if (index && *index < layout.items.size() && Previewable(fitted.items[*index])) {
                previewAnchor = Anchor(*index);
                shown = preview.Present(PreviewContent(fitted.items[*index]), previewAnchor);
            }
            if (shown) {
                tooltip.HideTooltip(); ::KillTimer(window.Handle(), kTooltipTimer);
            } else {
                hover.PreviewUnavailable(wanted);
                preview.Dismiss(true);
                // Honest fallback: the name tooltip, never an invented preview.
                if (index && *index < layout.items.size() && text::EqualsIgnoreCase(hover.ItemUnderPointer(), wanted))
                    tooltip.ShowFor(fitted.items[*index].displayName, Anchor(*index), ui::TooltipPlacement::Above);
            }
        }
        UpdateIndicators();
        ArmHoverTimer();
        applyingHover = false;
    }
    // The tracker changed: the card drops destroyed windows and adds new ones in place, and the item
    // under the pointer may have gained or lost its last live window.
    void RefreshPreview() {
        if (hovered && *hovered < fitted.items.size() && pointer) {
            const auto& item = fitted.items[*hovered];
            const bool application = IsApplicationItem(item.kind);
            hover.PointerOnItem(item.id, application && item.running, application && Previewable(item),
                graphics::FrameScheduler::Now());
        }
        if (preview.IsPresenting()) {
            const std::wstring id = preview.ApplicationId();
            const auto index = IndexOf(id);
            if (!index || !Previewable(fitted.items[*index]) || !preview.Present(PreviewContent(fitted.items[*index]), previewAnchor)) {
                hover.PreviewUnavailable(id);
                preview.Dismiss();
            }
        }
        ApplyHover();
    }
    void ResetHover() {
        hover.Reset();
        armedHover.reset();
        if (window.Handle()) ::KillTimer(window.Handle(), kHoverTimer);
        preview.Dismiss(true);
        UpdateIndicators();
    }
    bool OpenKeyboardPreview() {
        if (focused >= fitted.items.size() || !Previewable(fitted.items[focused])) return false;
        hover.OpenForKeyboard(fitted.items[focused].id);
        ApplyHover();
        if (!preview.IsPresenting()) return false;
        preview.BeginKeyboard();
        return true;
    }
    void CloseKeyboardPreview() {
        const std::wstring id = hover.Previewed();
        hover.PreviewUnavailable(id);
        preview.EndKeyboard();
        preview.Dismiss();
        ApplyHover();
    }
    // A tile was clicked or invoked: activate exactly that window through the existing launcher path.
    void ActivateFromPreview(const PreviewWindowIdentity& identity) {
        const std::wstring id = preview.ApplicationId();
        const auto index = IndexOf(id);
        if (!index || !fitted.items[*index].runningApplication ||
            !IsCurrentPreviewWindow(identity, fitted.items[*index].runningApplication->windows)) {
            RefreshPreview();  // stale tile: never act on a replacement handle or a sibling
            return;
        }
        if (!window.Handle() || suppressed || !window.IsShown() || !::IsWindowEnabled(window.Handle())) return;
        const double now = graphics::FrameScheduler::Now();
        const HWND foreground = ::GetForegroundWindow();
        if (foreground && (identity.handle == foreground || identity.handle == ::GetAncestor(foreground, GA_ROOTOWNER))) {
            hover.Activated(id, now);  // already in front: nothing to switch, and never minimize it
            ApplyHover();
            return;
        }
        apps::RunningApplication target = *fitted.items[*index].runningApplication;
        std::erase_if(target.windows, [&](const auto& entry) { return entry.handle != identity.handle; });
        hover.Activated(id, now);
        auto result = context.launcher.Activate(target);
        if (!result) { hover.ActivationFailed(); ErrorTooltip(result.error(), *index); }
        ApplyHover();
    }
    // The tile's close button: a normal close request for that selected window only.
    void CloseFromPreview(const PreviewWindowIdentity& identity) {
        const std::wstring id = preview.ApplicationId();
        const auto index = IndexOf(id);
        if (!index || !fitted.items[*index].runningApplication ||
            !IsCurrentPreviewWindow(identity, fitted.items[*index].runningApplication->windows)) {
            RefreshPreview();
            return;
        }
        auto result = RequestNormalClose(identity);
        if (result) preview.MarkCloseRequested(identity.handle);
        else if (result.error().code == ErrorCode::NotFound) RefreshPreview();
        else ErrorTooltip(result.error(), *index);
    }
    Result<void> ActivateItem(const DockItem& item, bool forceNew, std::size_t anchor) {
        if (!window.Handle() || suppressed || !window.IsShown() || window.IsTransitioning() || !::IsWindowEnabled(window.Handle()))
            return MakeError(ErrorCode::Unavailable, "Dock is not available for actions");
        if (item.kind == DockItemKind::Search) { Emit(window.SearchRequested); return Ok(); }
        if (item.kind == DockItemKind::Overflow) { MenuAt(anchor); return Ok(); }
        if (item.kind == DockItemKind::Start) { Emit(window.StartRequested); return Ok(); }
        if (item.kind == DockItemKind::TaskView) { Emit(window.TaskViewRequested); return Ok(); }
        if (!IsApplicationItem(item.kind)) return MakeError(ErrorCode::Unsupported, "this Dock entry is not available yet");
        if (!forceNew && item.runningApplication) {
            // The card closes; an observed activation then continues the expanded marker.
            hover.Activated(item.id, graphics::FrameScheduler::Now());
            auto result = context.launcher.Activate(*item.runningApplication);
            if (!result) { hover.ActivationFailed(); ErrorTooltip(result.error(), anchor); }
            ApplyHover();
            return result;
        }
        if (const std::wstring shown = hover.Previewed(); !shown.empty()) {
            hover.PreviewUnavailable(shown);  // a launch closes an open card without emphasizing anything
            ApplyHover();
        }
        if (!apps::HasLaunchRoute(item.application)) {
            auto error = MakeError(ErrorCode::Unsupported, "This application can be activated but has no trusted launch route.");
            ErrorTooltip(error, anchor); return error;
        }
        if (!motion.reduced) { bounces[item.id] = graphics::FrameScheduler::Now(); window.RequestAnimationFrame(); }
        std::weak_ptr<bool> lifetime = alive;
        try {
            context.launcher.LaunchAsync(item.application, [this, lifetime, anchor](Result<void> result) {
                const auto valid = lifetime.lock(); if (!valid || !*valid) return;
                if (!result) ErrorTooltip(result.error(), anchor);
            });
        } catch (...) { return MakeError(ErrorCode::Internal, "application launch could not be queued"); }
        return Ok();
    }
    void SavePins(std::vector<settings::PinnedApp> pins, std::wstring changedId = {}, bool dismissed = false) {
        std::weak_ptr<bool> lifetime = alive;
        auto result = context.settings.Update([pins = std::move(pins), changedId = std::move(changedId), dismissed](settings::Settings& value) {
            value.dock.pinned = pins;
            if (!changedId.empty()) {
                std::erase_if(value.dock.dismissedPins, [&](const auto& id) { return text::EqualsIgnoreCase(id, changedId); });
                if (dismissed) {
                    if (value.dock.dismissedPins.size() >= 128) value.dock.dismissedPins.erase(value.dock.dismissedPins.begin());
                    value.dock.dismissedPins.push_back(changedId);
                }
            }
        },
            [this, lifetime](Result<void> saved) {
                const auto valid = lifetime.lock(); if (!valid || !*valid) return;
                if (!saved) ErrorTooltip(saved.error(), focused);
            });
        if (!result) ErrorTooltip(result.error(), focused);
    }
    void MenuAt(std::size_t index, std::size_t overflowOffset = 0) {
        if (index >= fitted.items.size() || suppressed || !window.IsShown() || !::IsWindowEnabled(window.Handle())) return;
        DockItem item = fitted.items[index];
        const auto originalKind = item.kind;
        const auto hidden = fitted.overflow;
        std::vector<ui::MenuEntry> entries;
        const bool overflow = item.kind == DockItemKind::Overflow || (item.kind == DockItemKind::Search && !hidden.empty());
        if (overflow) {
            // Keep the shared menu's bounded entry count even for a very large tracker snapshot.
            for (std::size_t i = overflowOffset; i < std::min(hidden.size(), overflowOffset + 240); ++i)
                entries.push_back(ui::MenuEntry::Command(static_cast<std::uint32_t>(1000 + i), MenuText(hidden[i].displayName)));
            if (overflowOffset) entries.push_back(ui::MenuEntry::Command(10, L"Previous applications"));
            if (hidden.size() > overflowOffset + 240) entries.push_back(ui::MenuEntry::Command(9, L"More applications"));
            entries.push_back(ui::MenuEntry::Separator());
        } else {
            entries.push_back(ui::MenuEntry::Command(1, IsApplicationItem(item.kind) ? L"Open" : item.displayName,
                !IsApplicationItem(item.kind) || item.runningApplication.has_value() || apps::HasLaunchRoute(item.application)));
            if (IsApplicationItem(item.kind)) {
                const bool launchable = apps::HasLaunchRoute(item.application);
                entries.push_back(ui::MenuEntry::Command(2, L"New instance", launchable));
                entries.push_back(ui::MenuEntry::Command(3, item.kind == DockItemKind::Pinned ? L"Unpin from Dock" : L"Pin to Dock",
                    item.kind == DockItemKind::Pinned || launchable));
                if (item.kind == DockItemKind::Pinned) {
                    const auto& pins = context.settings.Current().dock.pinned;
                    const auto found = std::find_if(pins.begin(), pins.end(), [&](const auto& pin) { return text::EqualsIgnoreCase(pin.id, item.id); });
                    entries.push_back(ui::MenuEntry::Command(4, L"Move left", found != pins.end() && found != pins.begin()));
                    entries.push_back(ui::MenuEntry::Command(5, L"Move right", found != pins.end() && std::next(found) != pins.end()));
                }
            }
            entries.push_back(ui::MenuEntry::Separator());
        }
        entries.push_back(ui::MenuEntry::Command(6, L"Dock settings..."));
        entries.push_back(ui::MenuEntry::Command(7, L"Import Windows taskbar pins"));
        entries.push_back(ui::MenuEntry::Command(8, L"Open Windows Start"));
        const auto presenterOwner = menus; // modal reentrancy must not destroy the active presenter
        auto* presenter = context.menuPresenter ? context.menuPresenter : presenterOwner.get();
        if (!presenter) return;
        const std::weak_ptr<bool> lifetime = alive;
        ui::MenuRequest request; request.owner = window.Handle(); request.anchor = Anchor(index);
        request.placement = ui::MenuPlacement::AboveAnchor;
        request.restore = ui::ForegroundRestore::Always;
        auto selection = presenter->Show(entries, request);
        const auto live = lifetime.lock();
        if (!live || !*live) return; // do not access this after a nested loop destroyed the owner
        if (!window.Handle() || suppressed || !window.IsShown() || !::IsWindowEnabled(window.Handle())) return;
        const auto currentItem = std::find_if(fitted.items.begin(), fitted.items.end(), [&](const auto& entry) {
            return text::EqualsIgnoreCase(entry.id, item.id);
        });
        if (currentItem == fitted.items.end()) return;
        index = static_cast<std::size_t>(currentItem - fitted.items.begin());
        item = *currentItem;
        if (!selection) { ErrorTooltip(selection.error(), index); return; }
        const auto chosen = selection->command;
        // Fakes and a nested modal loop cannot invoke a disabled or obsolete action.
        const auto* action = ui::FindMenuCommand(entries, chosen);
        if (!action || !action->enabled) return;
        if (chosen == 9) { MenuAt(index, overflowOffset + 240); return; }
        if (chosen == 10) { MenuAt(index, overflowOffset >= 240 ? overflowOffset - 240 : 0); return; }
        if (chosen >= 1000 && static_cast<std::size_t>(chosen - 1000) < hidden.size()) {
            const auto& wanted = hidden[chosen - 1000].id;
            const auto invoke = [&](const std::vector<DockItem>& items) {
                const auto found = std::find_if(items.begin(), items.end(), [&](const auto& entry) { return text::EqualsIgnoreCase(entry.id, wanted); });
                if (found == items.end()) return false;
                (void)ActivateItem(*found, false, index); return true;
            };
            if (!invoke(fitted.items)) (void)invoke(fitted.overflow);
            return;
        }
        if (chosen == 1 || chosen == 2) { (void)ActivateItem(item, chosen == 2, index); return; }
        if (chosen == 6) { Emit(window.SettingsRequested); return; }
        if (chosen == 7) { Emit(window.ImportWindowsPinsRequested); return; }
        if (chosen == 8) { Emit(window.StartRequested); return; }
        const auto& pins = context.settings.Current().dock.pinned;
        if (chosen == 3) {
            if (item.kind != originalKind) return; // never invert Pin/Unpin after a concurrent change
            auto changed = item.kind == DockItemKind::Pinned ? UnpinApplication(pins, item.id) : PinApplication(pins, item.application);
            if (changed) SavePins(std::move(*changed), item.id, item.kind == DockItemKind::Pinned); else ErrorTooltip(changed.error(), index);
        }
        if (chosen == 4 || chosen == 5) {
            const auto found = std::find_if(pins.begin(), pins.end(), [&](const auto& pin) { return text::EqualsIgnoreCase(pin.id, item.id); });
            if (found == pins.end()) return;
            const auto current = static_cast<std::size_t>(found - pins.begin());
            const auto destination = chosen == 4 ? (current == 0 ? 0 : current - 1) : std::min(current + 1, pins.size() - 1);
            auto changed = MovePin(pins, item.id, destination);
            if (changed) SavePins(std::move(*changed));
        }
    }

    std::wstring RootName() const override { return L"Aether Shell Dock"; }
    CONTROLTYPEID RootControlType() const override { return UIA_ToolBarControlTypeId; }
    std::size_t ItemCount() const override { return fitted.items.size(); }
    ui::accessibility::AccessibleItem Item(std::size_t index) const override {
        ui::accessibility::AccessibleItem result;
        const auto& item = fitted.items.at(index);
        result.id = Id(index); result.name = item.displayName; result.supportsInvoke = true;
        result.boundsPx = Anchor(index); result.hasKeyboardFocus = keyboard && focused == index;
        result.isOffscreen = suppressed || !window.IsShown();
        result.isEnabled = !result.isOffscreen && !window.IsTransitioning() && ::IsWindowEnabled(window.Handle());
        if (item.kind == DockItemKind::Pinned) result.itemStatus = L"Pinned";
        if (item.running) { if (!result.itemStatus.empty()) result.itemStatus += L", "; result.itemStatus += L"Running"; }
        if (item.attentionRequested) { if (!result.itemStatus.empty()) result.itemStatus += L", "; result.itemStatus += L"Needs attention"; }
        if (Previewable(item)) result.helpText = L"Up arrow shows window previews";
        return result;
    }
    Result<void> Invoke(int id) override {
        return Guard([&]() -> Result<void> {
            if (!window.Handle() || suppressed || !window.IsShown() || window.IsTransitioning() || !::IsWindowEnabled(window.Handle()))
                return MakeError(ErrorCode::Unavailable, "Dock is not available for actions");
            const auto index = Index(id); if (!index) return MakeError(ErrorCode::NotFound, "Dock item no longer exists");
            if (!::PostMessageW(window.Handle(), kDeferredInvoke, static_cast<WPARAM>(id), 0))
                return MakeError(ErrorCode::Unavailable, "Dock action could not be queued");
            return Ok();
        });
    }
    Result<void> Toggle(int) override { return MakeError(ErrorCode::Unsupported, "Dock items do not support toggle"); }
    Result<void> SetFocus(int id) override {
        const auto index = Index(id); if (!index) return MakeError(ErrorCode::NotFound, "Dock item no longer exists");
        auto result = window.FocusDock(); if (!result) return result;
        focused = *index; window.Invalidate(); return accessible.NotifyFocusChanged(id);
    }
};

DockWindow::DockWindow(DockWindowContext context)
    : SurfaceWindow(context.graphics, context.scheduler), impl_(std::make_unique<Impl>(*this, context)) {}
DockWindow::~DockWindow() { *impl_->alive = false; impl_->settingsChanged.Disconnect(); Destroy(); }

Result<void> DockWindow::Create(DockWindowOptions options) {
    return Guard([&]() -> Result<void> {
        if (!impl_->context.dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "Dock requires the UI thread");
        impl_->options = options; impl_->settings = impl_->context.settings.Current().dock;
        impl_->menus = std::make_shared<ui::GlassMenuPresenter>(impl_->context.graphics, impl_->context.scheduler,
                                                                impl_->tokens, impl_->motion);
        const auto work = options.workAreaPx.value_or(platform::PrimaryMonitor().workArea);
        ui::SurfaceWindowOptions native;
        native.className = L"AetherShell.Dock"; native.title = L"Aether Shell Dock"; native.owner = options.owner;
        if (auto created = SurfaceWindow::Create(native, {work.left, work.bottom - 160, work.right, work.bottom}); !created) return created;
        if (auto tip = impl_->tooltip.Initialize(); !tip) return tip;
        impl_->tooltip.SetTheme(impl_->tokens, impl_->motion);
        if (options.hoverTiming) impl_->hover.SetTiming(*options.hoverTiming);
        DockPreviewWindow::Callbacks callbacks;
        callbacks.pointerEntered = [this] { impl_->hover.PointerOnPreview(graphics::FrameScheduler::Now()); impl_->ApplyHover(); };
        callbacks.pointerLeft = [this] { impl_->hover.PointerOffPreview(graphics::FrameScheduler::Now()); impl_->ApplyHover(); };
        callbacks.activate = [this](const PreviewWindowIdentity& identity) { impl_->ActivateFromPreview(identity); };
        callbacks.close = [this](const PreviewWindowIdentity& identity) { impl_->CloseFromPreview(identity); };
        if (auto card = impl_->preview.Initialize(Handle(), std::move(callbacks)); !card) return card;
        impl_->preview.SetTheme(impl_->tokens, impl_->motion, static_cast<float>(impl_->settings.opacity));
        if (auto connected = impl_->accessible.Connect(Handle(), *impl_); !connected) return connected;
        impl_->settingsChanged = impl_->context.settings.Changed().Connect([this](const settings::Settings& value) {
            (void)SetTheme(impl_->tokens, impl_->motion, value.dock);
        });
        return Relayout();
    });
}

Result<void> DockWindow::Relayout() {
    return Guard([&]() -> Result<void> {
        if (!Handle()) return MakeError(ErrorCode::InvalidState, "Dock has not been created");
        if (auto rebuilt = impl_->Rebuild(); !rebuilt) return rebuilt;
        if (auto reserve = impl_->Reserve(); !reserve) return reserve;
        if (impl_->appBar) return impl_->Rebuild();
        return Ok();
    });
}
Result<void> DockWindow::SetRunningApplications(std::vector<apps::RunningApplication> applications) {
    return Guard([&]() -> Result<void> {
        auto oldModel = BuildDockItems(DockModelInputs{impl_->settings, impl_->running, impl_->sessionOrder, &impl_->catalog}); if (!oldModel) return oldModel.error();
        auto newModel = BuildDockItems(DockModelInputs{impl_->settings, applications, impl_->sessionOrder, &impl_->catalog}); if (!newModel) return newModel.error();
        impl_->running = std::move(applications);
        std::erase_if(impl_->sessionOrder, [&](const auto& id) {
            return std::none_of(newModel->begin(), newModel->end(), [&](const auto& item) { return text::EqualsIgnoreCase(item.id, id); });
        });
        if (SamePresentation(*oldModel, *newModel)) {
            // Titles/window ordering affect activation but do not request a frame when the visible
            // Dock presentation is unchanged.
            const auto update = [&](std::vector<DockItem>& items) {
                for (auto& item : items) for (const auto& fresh : *newModel) {
                    if (text::EqualsIgnoreCase(item.id, fresh.id)) {
                        item.runningApplication = fresh.runningApplication; item.application = fresh.application;
                    }
                }
            };
            update(impl_->fitted.items); update(impl_->fitted.overflow);
            impl_->RefreshPreview();
            return Ok();
        }
        auto result = Relayout();
        impl_->RefreshPreview();
        return result;
    });
}
Result<void> DockWindow::SetApplicationCatalog(std::vector<apps::ApplicationInfo> applications) {
    return Guard([&]() -> Result<void> {
        if (!impl_->context.dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "Dock requires the UI thread");
        impl_->catalog = apps::ApplicationIndex(std::move(applications));
        return Handle() ? Relayout() : Ok();
    });
}
Result<void> DockWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
                                 const settings::DockSettings& settings) {
    return Guard([&]() -> Result<void> {
        settings::Settings normalized; normalized.dock = settings;
        if (auto validated = settings::Validate(normalized); !validated) return validated.error();
        auto order = AlignSessionPins(impl_->sessionOrder, normalized.dock.pinned);
        if (!order) return order.error();
        impl_->sessionOrder = std::move(*order);
        impl_->tokens = tokens; impl_->motion = motion; impl_->settings = std::move(normalized.dock);
        for (auto& [id, spring] : impl_->lifts) { (void)id; spring.SetParameters(motion.ScaleSpring(graphics::Motion().lift)); }
        for (auto& [id, spring] : impl_->presses) { (void)id; spring.SetParameters(motion.ScaleSpring(graphics::Motion().press)); }
        if (!impl_->AllowsDecoration()) { impl_->lifts.clear(); impl_->presses.clear(); impl_->bounces.clear(); impl_->attentionPulses.clear(); }
        impl_->tooltip.SetTheme(tokens, motion);
        impl_->preview.SetTheme(tokens, motion, static_cast<float>(impl_->settings.opacity));
        if (impl_->menus) impl_->menus->SetTheme(tokens, motion);
        if (IsShown() && IsTransitioning() && (motion.reduced || tokens.mode == graphics::ThemeMode::HighContrast))
            SurfaceWindow::Show(false); // cancel decorative motion immediately when policy changes
        if (!Handle()) return Ok();
        auto result = Relayout(); impl_->SetTargets(); return result;
    });
}
void DockWindow::Show(bool activate) {
    if (IsShown()) { SurfaceWindow::Show(activate); return; }
    graphics::TransitionShape shape; shape.offsetY = kDockEntranceOffset;
    SurfaceWindow::ShowAnimated(graphics::MakeEnterTransition(shape, impl_->tokens, impl_->motion), activate);
}
Result<void> DockWindow::SetSuppressed(bool suppressed) {
    return Guard([&]() -> Result<void> {
        impl_->suppressed = suppressed;
        if (suppressed) {
            ::KillTimer(Handle(), kTooltipTimer); ::KillTimer(Handle(), kLongPressTimer);
            impl_->FinishDrag(false);
            if (::GetCapture() == Handle()) ::ReleaseCapture();
            impl_->pointer.reset(); impl_->hovered.reset(); impl_->attentionPulses.clear();
            impl_->ResetHover();
            if (impl_->menus) impl_->menus->Cancel();
            Hide(); impl_->tooltip.HideTooltip(); impl_->SetTargets();
        } else { Show(false); impl_->UpdateAttention(); }
        return Ok();
    });
}
Result<void> DockWindow::OnExplorerRestarted() {
    const bool registered = impl_->appBar;
    impl_->updatingBar = true; impl_->appBar = false; impl_->appBarPositioned = false;
    if (registered) { APPBARDATA data{sizeof(data)}; data.hWnd = Handle(); ::SHAppBarMessage(ABM_REMOVE, &data); }
    impl_->updatingBar = false; return Relayout();
}
Result<void> DockWindow::SetTaskbarReplacement(bool active) {
    return Guard([&]() -> Result<void> {
        if (!impl_->context.dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "Dock requires the UI thread");
        if (impl_->options.replacementTaskbarMode == active) return Ok();
        impl_->options.replacementTaskbarMode = active;
        return Handle() ? Relayout() : Ok();
    });
}
Result<void> DockWindow::FocusDock() {
    return Guard([&]() -> Result<void> {
        if (!Handle() || impl_->suppressed) return MakeError(ErrorCode::Unavailable, "Dock is not currently visible");
        if (!impl_->keyboard) impl_->previousForeground = ::GetForegroundWindow();
        ::SetWindowLongPtrW(Handle(), GWL_EXSTYLE, ::GetWindowLongPtrW(Handle(), GWL_EXSTYLE) & ~WS_EX_NOACTIVATE);
        Show(true);
        if (!::SetForegroundWindow(Handle())) {
            ::SetWindowLongPtrW(Handle(), GWL_EXSTYLE, ::GetWindowLongPtrW(Handle(), GWL_EXSTYLE) | WS_EX_NOACTIVATE);
            return MakeError(ErrorCode::AccessDenied, "Windows refused to focus the Dock");
        }
        ::SetFocus(Handle()); impl_->keyboard = true; impl_->focused = 0;
        Invalidate(); return impl_->accessible.NotifyFocusChanged(impl_->Id(0));
    });
}
Result<DockLayout> DockWindow::Geometry() const {
    try { return impl_->layout; } catch (...) { return MakeError(ErrorCode::Internal, "Dock geometry could not be copied"); }
}
std::uint64_t DockWindow::RenderedFrames() const noexcept { return impl_->frames; }
float DockWindow::MarkerWidth(std::wstring_view applicationId) const {
    const auto marker = impl_->indicators.find(text::ToLower(applicationId));
    return marker == impl_->indicators.end() ? 0.0f : marker->second.Width();
}
std::wstring DockWindow::EmphasizedApplication() const { return impl_->hover.Emphasized(); }
const DockPreviewWindow* DockWindow::PreviewWindow() const noexcept { return &impl_->preview; }

void DockWindow::OnRender(const ui::RenderContext& context) {
    ++impl_->frames;
    graphics::GlassStyle style;
    style.cornerRadius = graphics::DockCornerRadius(impl_->tokens, impl_->layout.panel.bottom - impl_->layout.panel.top);
    style.opacity = static_cast<float>(impl_->settings.opacity); style.backdropBlur = BackdropBlurActive();
    // Exact native hit regions clip exterior shadows into the rectangular slots of raised icons.
    // Keep those transparent bitmap corners clear instead of painting square shadow fragments.
    style.shadow = false;
    graphics::PaintGlassPanel(context.d2d, impl_->layout.panel, impl_->tokens, style);
    winrt::com_ptr<ID2D1SolidColorBrush> primary, attention;
    if (FAILED(context.d2d->CreateSolidColorBrush(impl_->tokens.textPrimary.ToD2D(), primary.put())) ||
        FAILED(context.d2d->CreateSolidColorBrush(impl_->tokens.critical.ToD2D(), attention.put()))) return;
    const auto drawItem = [&](std::size_t index) {
        const auto& item = impl_->fitted.items[index];
        const auto rect = impl_->layout.items[index].icon;
        const float size = rect.right - rect.left;
        if (const auto pulse = impl_->attentionPulses.find(item.id); pulse != impl_->attentionPulses.end() && impl_->AllowsDecoration()) {
            const float opacity = DockAttentionAt((graphics::FrameScheduler::Now() - pulse->second) / impl_->motion.EffectiveScale()).opacity;
            const D2D1_POINT_2F center{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2};
            for (int layer = 3; layer >= 1; --layer) {
                attention->SetOpacity(opacity * 0.07f);
                const float radius = size / 2 + static_cast<float>(layer) * 3;
                context.d2d->FillEllipse({center, radius, radius}, attention.get());
            }
        }
        // Shared glyphs already include optical padding in their 16-unit grid. Use the same
        // animated icon slot as application bitmaps rather than shrinking fixed actions twice.
        const auto& glyphBox = rect;
        if (item.kind == DockItemKind::Start) {
            graphics::PaintStartMark(context.d2d, glyphBox, impl_->tokens);
        } else if (item.kind == DockItemKind::Search || item.kind == DockItemKind::TaskView) {
            graphics::PaintGlyph(context.d2d, item.kind == DockItemKind::Search ? graphics::Glyph::Search : graphics::Glyph::TaskView,
                glyphBox, primary.get(), impl_->tokens.glyph.stroke);
        } else if (item.kind == DockItemKind::Overflow) {
            for (int dot = 0; dot < 3; ++dot) context.d2d->FillEllipse({{rect.left + size * (0.3f + 0.2f * dot), rect.top + size / 2}, 2.5f, 2.5f}, primary.get());
        } else if (IsApplicationItem(item.kind)) {
            const auto key = impl_->ImageKey(item);
            auto* bitmap = impl_->bitmaps.Find(key);
            if (!bitmap) {
                if (const auto image = impl_->images.find(key); image != impl_->images.end() && image->second) {
                    bitmap = impl_->bitmaps.Put(context.d2d, key, image->second->width, image->second->height, image->second->pixels);
                }
            }
            if (bitmap) graphics::DrawIconBitmap(context.d2d, bitmap, rect);
            else graphics::PaintFallbackIcon(context.d2d, Device().DWriteFactory(), rect,
                item.displayName.empty() ? L'?' : item.displayName.front(), impl_->tokens);
        }
        if (item.running) {
            const float center = (rect.left + rect.right) / 2;
            const auto marker = impl_->indicators.find(text::ToLower(item.id));
            const float half = marker == impl_->indicators.end() ? (item.foreground ? 6.0f : 2.5f) : marker->second.Width() / 2;
            graphics::PaintRunningIndicator(context.d2d, {center - half, impl_->layout.panel.bottom - 11,
                center + half, impl_->layout.panel.bottom - 8}, impl_->tokens,
                item.foreground ? graphics::IndicatorState::Active : graphics::IndicatorState::Running);
        }
        if (item.attentionRequested && !item.foreground) {
            attention->SetColor(impl_->tokens.mode == graphics::ThemeMode::HighContrast ? impl_->tokens.textPrimary.ToD2D() : impl_->tokens.critical.ToD2D());
            attention->SetOpacity(1);
            context.d2d->FillEllipse({{(rect.left + rect.right) / 2, impl_->layout.panel.bottom - 9.5f}, 3, 2}, attention.get());
        }
        if (impl_->keyboard && index == impl_->focused) graphics::PaintFocusRing(context.d2d, rect, impl_->tokens.itemCornerRadius, impl_->tokens);
    };
    for (std::size_t index = 0; index < impl_->fitted.items.size(); ++index)
        if (impl_->layout.raisedItem != index) drawItem(index);
    if (impl_->layout.raisedItem) drawItem(*impl_->layout.raisedItem);
}
bool DockWindow::OnAnimate(double now) {
    const double dt = impl_->lastFrame == 0 ? Scheduler().RefreshInterval() : now - impl_->lastFrame;
    impl_->lastFrame = now;
    bool moving = false;
    for (auto& [id, marker] : impl_->indicators) { (void)id; marker.Step(dt); if (marker.IsAnimating()) moving = true; }
    for (auto& spring : impl_->springs) { spring.Step(dt); if (!spring.IsSettled()) moving = true; }
    for (auto& [id, spring] : impl_->reorderOffsets) { (void)id; spring.Step(dt); if (!spring.IsSettled()) moving = true; }
    for (auto& [id, spring] : impl_->presses) { (void)id; spring.Step(dt); if (!spring.IsSettled()) moving = true; }
    std::erase_if(impl_->presses, [](const auto& entry) { return entry.second.Target() == 0 && entry.second.IsSettled(); });
    for (auto& [id, spring] : impl_->lifts) { (void)id; spring.Step(dt); if (!spring.IsSettled()) moving = true; }
    std::erase_if(impl_->lifts, [](const auto& entry) { return entry.second.Target() == 0 && entry.second.IsSettled(); });
    std::erase_if(impl_->reorderOffsets, [](const auto& pair) { return pair.second.IsSettled(); });
    std::erase_if(impl_->bounces, [&](const auto& bounce) {
        return !impl_->AllowsDecoration() || now - bounce.second >= graphics::LaunchBounceDuration() * impl_->motion.EffectiveScale();
    });
    moving = moving || !impl_->bounces.empty();
    std::erase_if(impl_->attentionPulses, [&](const auto& pulse) {
        return !impl_->AllowsDecoration() || impl_->suppressed || !IsShown() ||
            now - pulse.second >= kDockAttentionDuration * impl_->motion.EffectiveScale();
    });
    moving = moving || !impl_->attentionPulses.empty();
    (void)impl_->Arrange(); if (!moving) impl_->lastFrame = 0; return moving;
}
void DockWindow::OnDpiChanged(UINT, const RECT&) {
    impl_->ResetHover();  // the card and its thumbnails are rebuilt by the next deliberate hover
    impl_->FinishDrag(false);
    if (::GetCapture() == Handle()) ::ReleaseCapture();
    impl_->reorderOffsets.clear(); impl_->baseCenters.clear();
    (void)Relayout();
}
void DockWindow::OnPointerMove(D2D1_POINT_2F point) {
    if (!Handle() || impl_->suppressed || !IsShown() || IsTransitioning() || !::IsWindowEnabled(Handle()) ||
        !std::isfinite(point.x) || !std::isfinite(point.y)) return;
    impl_->pointer = point;
    if (impl_->pressed && !impl_->dragId.empty() && std::hypot(point.x - impl_->pressPoint.x, point.y - impl_->pressPoint.y) >= 6)
        impl_->BeginDrag();
    if (impl_->dragging) {
        impl_->MoveDrag(point.x); impl_->SetTargets();
        (void)impl_->Arrange(); Invalidate(); return;
    }
    auto hit = HitTest(impl_->layout, point);
    if (hit != impl_->hovered) {
        impl_->hovered = hit; impl_->tooltip.HideTooltip(); ::KillTimer(Handle(), kTooltipTimer);
        // A running application with a live window shows its preview card after the dwell instead.
        if (hit && !(*hit < impl_->fitted.items.size() && Impl::Previewable(impl_->fitted.items[*hit])))
            ::SetTimer(Handle(), kTooltipTimer, static_cast<UINT>(std::max(1.0, impl_->tokens.tooltipDelay * 1000)), nullptr);
    }
    impl_->PointerHover(hit);
    impl_->SetTargets();
}
void DockWindow::OnPointerLeave() {
    if (impl_->dragging && ::GetCapture() == Handle()) return;
    impl_->pointer.reset(); impl_->hovered.reset(); impl_->tooltip.HideTooltip(); ::KillTimer(Handle(), kTooltipTimer);
    impl_->hover.PointerOffDock(graphics::FrameScheduler::Now()); impl_->ApplyHover();
    impl_->SetTargets();
}
void DockWindow::OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) {
    if (!Handle() || impl_->suppressed || !IsShown() || IsTransitioning() || !::IsWindowEnabled(Handle())) return;
    const auto hit = HitTest(impl_->layout, point);
    if (button != ui::PointerButton::Left || !hit) return;
    impl_->FinishDrag();
    impl_->pressed = hit; impl_->pressPoint = point; impl_->pointer = point;
    const auto kind = impl_->fitted.items[*hit].kind;
    impl_->dragId = IsReorderable(kind) ? impl_->fitted.items[*hit].id : L"";
    if (impl_->AllowsDecoration()) {
        auto& spring = impl_->presses.try_emplace(impl_->fitted.items[*hit].id, 0.0f,
            impl_->motion.ScaleSpring(graphics::Motion().press)).first->second;
        spring.SetTarget(1); RequestAnimationFrame();
    }
    const auto& rect = impl_->layout.items[*hit].icon;
    impl_->dragCenter = (rect.left + rect.right) / 2; impl_->grabOffsetX = point.x - impl_->dragCenter;
    if (!impl_->dragId.empty()) ::SetTimer(Handle(), kLongPressTimer,
        static_cast<UINT>(graphics::Motion().longPress * 1000), nullptr);
    (void)impl_->Arrange(); Invalidate();
}
void DockWindow::OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) {
    if (!Handle() || impl_->suppressed || !IsShown() || IsTransitioning() || !::IsWindowEnabled(Handle())) {
        impl_->FinishDrag(false); return;
    }
    const auto hit = HitTest(impl_->layout, point);
    ::KillTimer(Handle(), kLongPressTimer);
    const bool click = button != ui::PointerButton::Left || (impl_->pressed && hit == impl_->pressed && !impl_->dragging);
    impl_->FinishDrag();
    (void)impl_->Arrange(); Invalidate();
    if (!click || !hit) return;
    if (button == ui::PointerButton::Right) impl_->MenuAt(*hit);
    else { const auto item = impl_->fitted.items[*hit]; (void)impl_->ActivateItem(item, button == ui::PointerButton::Middle, *hit); }
}
void DockWindow::OnPointerCaptureLost() {
    impl_->FinishDrag();
    (void)impl_->Arrange(); Invalidate();
}
bool DockWindow::OnKeyDown(UINT key) {
    if (impl_->fitted.items.empty()) return false;
    if (impl_->preview.KeyboardActive()) {
        // The Dock keeps keyboard focus; the open card receives navigation and invocation keys.
        if (key == VK_ESCAPE || key == VK_DOWN) { impl_->CloseKeyboardPreview(); return true; }
        return impl_->preview.HandleKey(key);
    }
    if (key == VK_UP && impl_->keyboard && impl_->OpenKeyboardPreview()) return true;
    if (key == VK_APPS || (key == VK_F10 && (::GetKeyState(VK_SHIFT) & 0x8000))) { impl_->MenuAt(impl_->focused); return true; }
    if (key == VK_RETURN || key == VK_SPACE) { const auto item = impl_->fitted.items[impl_->focused]; (void)impl_->ActivateItem(item, false, impl_->focused); return true; }
    if (key == VK_ESCAPE) {
        impl_->keyboard = false; OnFocusChanged(false);
        if (::IsWindow(impl_->previousForeground)) ::SetForegroundWindow(impl_->previousForeground);
        Invalidate(); return true;
    }
    if (key == VK_LEFT && impl_->focused > 0) --impl_->focused;
    else if (key == VK_RIGHT && impl_->focused + 1 < impl_->fitted.items.size()) ++impl_->focused;
    else if (key == VK_HOME) impl_->focused = 0;
    else if (key == VK_END) impl_->focused = impl_->fitted.items.size() - 1;
    else return false;
    impl_->keyboard = true; (void)impl_->accessible.NotifyFocusChanged(impl_->Id(impl_->focused)); Invalidate(); return true;
}
void DockWindow::OnFocusChanged(bool focused) {
    if (!focused) {
        if (impl_->preview.KeyboardActive()) impl_->CloseKeyboardPreview();
        impl_->keyboard = false;
        ::SetWindowLongPtrW(Handle(), GWL_EXSTYLE, ::GetWindowLongPtrW(Handle(), GWL_EXSTYLE) | WS_EX_NOACTIVATE);
    }
    Invalidate();
}
void DockWindow::OnDeviceReplaced() { impl_->bitmaps.Clear(); Invalidate(); }
std::optional<LRESULT> DockWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_GETOBJECT) {
        LRESULT result = 0; if (impl_->accessible.HandleGetObject(Handle(), wParam, lParam, result)) return result;
    }
    if (message == kDeferredInvoke) {
        if (const auto index = impl_->Index(static_cast<int>(wParam)); index && !impl_->suppressed && !IsTransitioning()) {
            const auto item = impl_->fitted.items[*index]; (void)impl_->ActivateItem(item, false, *index);
        }
        return 0;
    }
    if (message == impl_->explorerMessage) { (void)OnExplorerRestarted(); return 0; }
    if (message == WM_DISPLAYCHANGE || (message == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA)) {
        impl_->ResetHover();
        impl_->FinishDrag(false);
        if (::GetCapture() == Handle()) ::ReleaseCapture();
        (void)Relayout(); return 0;
    }
    if (message == WM_ENABLE && !wParam) {
        impl_->ResetHover();
        impl_->FinishDrag(false); impl_->tooltip.HideTooltip();
        if (::GetCapture() == Handle()) ::ReleaseCapture();
        (void)impl_->Arrange();
    }
    if (message == kAppBarMessage) {
        if (wParam == ABN_POSCHANGED && !impl_->updatingBar) (void)Relayout();
        // The host combines foreground fullscreen with temporary native-taskbar reveal state.
        // Raw shell fullscreen notifications must not independently undo that suppression policy.
        return 0;
    }
    if (message == WM_TIMER && (wParam == kTooltipTimer || wParam == kErrorTimer)) {
        ::KillTimer(Handle(), wParam);
        if (wParam == kErrorTimer) impl_->tooltip.HideTooltip();
        else if (Handle() && IsShown() && !impl_->suppressed && !impl_->dragging && !IsTransitioning() && ::IsWindowEnabled(Handle()) &&
            !impl_->preview.IsPresenting() &&
            impl_->hovered && *impl_->hovered < impl_->fitted.items.size() && *impl_->hovered < impl_->layout.items.size())
            impl_->tooltip.ShowFor(impl_->fitted.items[*impl_->hovered].displayName,
            impl_->Anchor(*impl_->hovered), ui::TooltipPlacement::Above);
        return 0;
    }
    if (message == WM_TIMER && wParam == kHoverTimer) {
        ::KillTimer(Handle(), kHoverTimer);
        impl_->armedHover.reset();
        impl_->hover.Advance(graphics::FrameScheduler::Now());
        impl_->ApplyHover();
        return 0;
    }
    if (message == WM_TIMER && wParam == kLongPressTimer) {
        ::KillTimer(Handle(), kLongPressTimer);
        if (Handle() && IsShown() && !impl_->suppressed && !IsTransitioning() && ::IsWindowEnabled(Handle()) &&
            ::GetCapture() == Handle() && impl_->pressed && !impl_->dragId.empty()) {
            impl_->BeginDrag(); (void)impl_->Arrange(); Invalidate();
        }
        return 0;
    }
    if (message == WM_CONTEXTMENU) { if (!IsTransitioning()) impl_->MenuAt(impl_->focused); return 0; }
    if (message == WM_CANCELMODE) OnPointerCaptureLost();
    if (impl_->appBar && !impl_->updatingBar && (message == WM_ACTIVATE || message == WM_WINDOWPOSCHANGED)) {
        // Complete the registered AppBar lifecycle without recursively re-entering reservation
        // when the shell sends notifications during this call. Preserve default window handling.
        impl_->updatingBar = true;
        APPBARDATA data{sizeof(data)}; data.hWnd = Handle();
        (void)::SHAppBarMessage(message == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &data);
        impl_->updatingBar = false;
    }
    return std::nullopt;
}
void DockWindow::OnCreated() {}
void DockWindow::OnDestroying() {
    if (impl_->menus) impl_->menus->Cancel();
    ::KillTimer(Handle(), kTooltipTimer); ::KillTimer(Handle(), kErrorTimer); ::KillTimer(Handle(), kLongPressTimer);
    ::KillTimer(Handle(), kHoverTimer); impl_->hover.Reset(); impl_->armedHover.reset();
    impl_->accessible.Disconnect(); impl_->tooltip.Destroy(); impl_->preview.Destroy();
    const bool registered = impl_->appBar;
    impl_->options.allowWorkAreaReservation = false;
    impl_->appBar = false; impl_->appBarPositioned = false;
    if (registered) { APPBARDATA data{sizeof(data)}; data.hWnd = Handle(); ::SHAppBarMessage(ABM_REMOVE, &data); }
}

}  // namespace aether::dock
