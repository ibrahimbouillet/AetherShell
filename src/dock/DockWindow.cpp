// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockWindow.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "dock/DockModel.h"
#include "graphics/BitmapCache.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Win32Error.h"
#include "settings/SettingsSerialization.h"
#include "ui/TooltipWindow.h"
#include "ui/ThemedMenu.h"
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
            a.running != b.running || a.foreground != b.foreground || a.windowCount != b.windowCount ||
            apps::IconSourceFingerprint(a.application) != apps::IconSourceFingerprint(b.application)) return false;
    }
    return true;
}

}  // namespace

struct DockWindow::Impl final : ui::accessibility::IAccessibleSurfaceModel {
    Impl(DockWindow& surface, DockWindowContext dependencies)
        : window(surface), context(dependencies), bitmaps(dependencies.graphics),
          tooltip(dependencies.graphics, dependencies.scheduler) {}

    DockWindow& window;
    DockWindowContext context;
    DockWindowOptions options;
    graphics::DesignTokens tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    graphics::MotionPolicy motion;
    settings::DockSettings settings;
    std::vector<apps::RunningApplication> running;
    apps::ApplicationIndex catalog;
    std::vector<std::wstring> sessionOrder;
    std::shared_ptr<ui::ThemedMenuPresenter> menus;
    FittedDock fitted;
    DockLayout layout;
    std::vector<graphics::Spring> springs;
    std::optional<D2D1_POINT_2F> pointer;
    std::optional<std::size_t> hovered;
    std::optional<std::size_t> pressed;
    D2D1_POINT_2F pressPoint{};
    std::wstring dragId;
    std::wstring lastDragTarget;
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
    std::map<std::wstring, std::shared_ptr<const apps::IconImage>> images;
    std::set<std::wstring> requested;
    graphics::BitmapCache bitmaps;
    ui::TooltipWindow tooltip;
    ui::accessibility::AccessibleSurface accessible;
    Connection settingsChanged;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    std::map<std::wstring, int> accessibleIds;
    int nextAccessibleId = 1;

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
        const double now = graphics::FrameScheduler::Now();
        for (std::size_t index = 0; index < fitted.items.size(); ++index) {
            auto& rect = layout.items[index].icon;
            if (const auto moving = reorderOffsets.find(fitted.items[index].id); moving != reorderOffsets.end()) {
                const float shift = motion.reduced ? 0 : moving->second.Value();
                rect.left += shift; rect.right += shift;
            }
            if (const auto bounce = bounces.find(fitted.items[index].id); bounce != bounces.end()) {
                if (!motion.reduced) {
                    const float shift = graphics::LaunchBounce((now - bounce->second) / motion.EffectiveScale(),
                        graphics::MaxBounceHeight(fitted.iconSize));
                    rect.top -= shift; rect.bottom -= shift;
                }
            }
            if (dragging && text::EqualsIgnoreCase(fitted.items[index].id, dragId) && !motion.reduced) {
                const float grow = (rect.right - rect.left) * (graphics::Motion().liftScale - 1) / 2;
                rect.left -= grow; rect.right += grow; rect.top -= grow + graphics::Motion().liftOffset;
                rect.bottom += grow - graphics::Motion().liftOffset;
            }
            if (pressed && *pressed == index && !dragging) {
                const float inset = motion.reduced ? 0 : (rect.right - rect.left) * (1 - graphics::Motion().pressScale) / 2;
                rect.left += inset; rect.right -= inset; rect.top += inset; rect.bottom -= inset;
            }
        }
        std::vector<RECT> regions{Pixels(layout.panel, static_cast<float>(window.Dpi()) / 96)};
        for (const auto& item : layout.items) regions.push_back(Pixels(item.icon, static_cast<float>(window.Dpi()) / 96));
        window.SetInputRegionPx(regions);
        // Preserve the shadow/entry only near actual content. A full monitor-wide HWND region
        // could intercept other processes' clicks in otherwise empty side/headroom margins.
        const float shadow = tokens.mode == graphics::ThemeMode::HighContrast ? 0 : kDockShadowMargin;
        std::vector<RECT> visual;
        const auto expand = [&](const D2D1_RECT_F& rect, float extent) {
            return Pixels({rect.left - extent, rect.top - extent, rect.right + extent,
                rect.bottom + extent + kDockEntranceOffset}, static_cast<float>(window.Dpi()) / 96);
        };
        visual.push_back(expand(layout.panel, shadow));
        for (const auto& item : layout.items) visual.push_back(expand(item.icon, shadow));
        window.SetVisualRegionPx(visual);
        window.SetBackdropPanel(layout.panel, graphics::DockCornerRadius(tokens, layout.panel.bottom - layout.panel.top),
            tokens.transparencyEnabled && tokens.mode != graphics::ThemeMode::HighContrast, tokens.glass.backdropOpacity);
        return Ok();
    }
    Result<void> Rebuild() {
        std::map<std::wstring, float> previousPositions;
        std::vector<std::wstring> previousOrder;
        for (std::size_t index = 0; index < fitted.items.size(); ++index) {
            previousOrder.push_back(fitted.items[index].id);
            if (index < layout.items.size()) previousPositions.emplace(fitted.items[index].id, layout.items[index].icon.left);
        }
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
        focused = fitted.items.empty() ? 0 : std::min(focused, fitted.items.size() - 1);
        pressed.reset(); hovered.reset(); reorderOffsets.clear();
        auto result = Arrange(); if (!result) return result;
        std::vector<std::wstring> newOrder;
        for (const auto& item : fitted.items) newOrder.push_back(item.id);
        if (!motion.reduced && previousOrder != newOrder) {
            for (std::size_t index = 0; index < fitted.items.size(); ++index) {
                const auto found = previousPositions.find(fitted.items[index].id);
                if (found == previousPositions.end()) continue;
                const float offset = found->second - layout.items[index].icon.left;
                if (std::abs(offset) > 0.01f) {
                    auto& spring = reorderOffsets.try_emplace(fitted.items[index].id, offset,
                        motion.ScaleSpring(graphics::Motion().slide)).first->second;
                    spring.SetTarget(0);
                }
            }
            if (!reorderOffsets.empty()) { (void)Arrange(); window.RequestAnimationFrame(); }
        }
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
    RECT Anchor(std::size_t index) const {
        auto rect = Pixels(layout.items[index].icon, static_cast<float>(window.Dpi()) / 96);
        const auto bounds = window.BoundsPx(); ::OffsetRect(&rect, bounds.left, bounds.top); return rect;
    }
    void ErrorTooltip(const Error& error, std::size_t index) {
        // A launch completion can arrive after fullscreen suppression or shutdown has closed input.
        if (!window.Handle() || suppressed || !window.IsShown() || window.IsTransitioning() ||
            !::IsWindowEnabled(window.Handle()) || index >= layout.items.size()) return;
        tooltip.ShowFor(text::Widen(error.message), Anchor(index), ui::TooltipPlacement::Above);
        ::SetTimer(window.Handle(), kErrorTimer, 3000, nullptr);
    }
    void Emit(Signal<>& signal) {
        try { signal.Emit(); } catch (...) { AETHER_LOG_ERROR("dock", "Dock action listener threw an exception"); }
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
            auto result = context.launcher.Activate(*item.runningApplication);
            if (!result) ErrorTooltip(result.error(), anchor);
            return result;
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
        impl_->menus = std::make_shared<ui::ThemedMenuPresenter>(impl_->tokens);
        const auto work = options.workAreaPx.value_or(platform::PrimaryMonitor().workArea);
        ui::SurfaceWindowOptions native;
        native.className = L"AetherShell.Dock"; native.title = L"Aether Shell Dock"; native.owner = options.owner;
        if (auto created = SurfaceWindow::Create(native, {work.left, work.bottom - 160, work.right, work.bottom}); !created) return created;
        if (auto tip = impl_->tooltip.Initialize(); !tip) return tip;
        impl_->tooltip.SetTheme(impl_->tokens, impl_->motion);
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
            update(impl_->fitted.items); update(impl_->fitted.overflow); return Ok();
        }
        return Relayout();
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
        impl_->tooltip.SetTheme(tokens, motion);
        if (impl_->menus) impl_->menus->SetTheme(tokens);
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
            impl_->pointer.reset(); impl_->hovered.reset(); impl_->pressed.reset();
            impl_->dragId.clear(); impl_->lastDragTarget.clear(); impl_->dragging = false;
            Hide(); impl_->tooltip.HideTooltip(); impl_->SetTargets();
        } else Show(false);
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

void DockWindow::OnRender(const ui::RenderContext& context) {
    ++impl_->frames;
    graphics::GlassStyle style;
    style.cornerRadius = graphics::DockCornerRadius(impl_->tokens, impl_->layout.panel.bottom - impl_->layout.panel.top);
    style.opacity = static_cast<float>(impl_->settings.opacity); style.backdropBlur = BackdropBlurActive();
    graphics::PaintGlassPanel(context.d2d, impl_->layout.panel, impl_->tokens, style);
    winrt::com_ptr<ID2D1SolidColorBrush> primary, accent, indicator;
    if (FAILED(context.d2d->CreateSolidColorBrush(impl_->tokens.textPrimary.ToD2D(), primary.put())) ||
        FAILED(context.d2d->CreateSolidColorBrush(impl_->tokens.accent.ToD2D(), accent.put())) ||
        FAILED(context.d2d->CreateSolidColorBrush(impl_->tokens.runningIndicator.ToD2D(), indicator.put()))) return;
    for (std::size_t index = 0; index < impl_->fitted.items.size(); ++index) {
        const auto& item = impl_->fitted.items[index];
        const auto rect = impl_->layout.items[index].icon;
        const float size = rect.right - rect.left;
        graphics::PaintItemHighlight(context.d2d, rect, impl_->tokens.radii.item, impl_->tokens,
            impl_->hovered == index ? 1.0f : 0.0f, impl_->pressed == index ? 1.0f : 0.0f);
        if (impl_->dragging && text::EqualsIgnoreCase(item.id, impl_->dragId))
            graphics::PaintLiftShadow(context.d2d, rect, impl_->tokens.radii.item, impl_->tokens, 1);
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
            const float half = item.foreground ? 6.0f : 2.5f;
            graphics::PaintRunningIndicator(context.d2d, {center - half, impl_->layout.panel.bottom - 11,
                center + half, impl_->layout.panel.bottom - 8}, impl_->tokens,
                item.foreground ? graphics::IndicatorState::Active : graphics::IndicatorState::Running);
        }
        if (impl_->keyboard && index == impl_->focused) graphics::PaintFocusRing(context.d2d, rect, impl_->tokens.itemCornerRadius, impl_->tokens);
        if (index > 0 && item.kind == DockItemKind::Running && impl_->fitted.items[index - 1].kind == DockItemKind::Pinned) {
            const float x = rect.left - static_cast<float>(impl_->settings.iconSpacing) / 2;
            context.d2d->DrawLine({x, impl_->layout.panel.top + 14}, {x, impl_->layout.panel.bottom - 14}, indicator.get(), 1);
        }
    }
}
bool DockWindow::OnAnimate(double now) {
    const double dt = impl_->lastFrame == 0 ? Scheduler().RefreshInterval() : now - impl_->lastFrame;
    impl_->lastFrame = now;
    bool moving = false;
    for (auto& spring : impl_->springs) { spring.Step(dt); if (!spring.IsSettled()) moving = true; }
    for (auto& [id, spring] : impl_->reorderOffsets) { (void)id; spring.Step(dt); if (!spring.IsSettled()) moving = true; }
    std::erase_if(impl_->reorderOffsets, [](const auto& pair) { return pair.second.IsSettled(); });
    std::erase_if(impl_->bounces, [&](const auto& bounce) {
        return impl_->motion.reduced || now - bounce.second >= graphics::LaunchBounceDuration() * impl_->motion.EffectiveScale();
    });
    moving = moving || !impl_->bounces.empty();
    (void)impl_->Arrange(); if (!moving) impl_->lastFrame = 0; return moving;
}
void DockWindow::OnDpiChanged(UINT, const RECT&) { (void)Relayout(); }
void DockWindow::OnPointerMove(D2D1_POINT_2F point) {
    if (!Handle() || impl_->suppressed || !IsShown() || IsTransitioning() || !::IsWindowEnabled(Handle())) return;
    impl_->pointer = point;
    if (impl_->pressed && !impl_->dragId.empty() && std::hypot(point.x - impl_->pressPoint.x, point.y - impl_->pressPoint.y) >= 6) impl_->dragging = true;
    auto hit = HitTest(impl_->layout, point);
    if (hit && text::EqualsIgnoreCase(impl_->fitted.items[*hit].id, impl_->dragId)) impl_->lastDragTarget.clear();
    if (impl_->dragging && hit && IsReorderable(impl_->fitted.items[*hit].kind) &&
        !text::EqualsIgnoreCase(impl_->fitted.items[*hit].id, impl_->dragId) &&
        !text::EqualsIgnoreCase(impl_->fitted.items[*hit].id, impl_->lastDragTarget)) {
        const auto targetId = impl_->fitted.items[*hit].id;
        auto moved = ReorderDockItems(DockModelInputs{impl_->context.settings.Current().dock, impl_->running,
            impl_->sessionOrder, &impl_->catalog},
            impl_->dragId, impl_->fitted.items[*hit].id);
        if (moved) {
            impl_->lastDragTarget = targetId;
            impl_->sessionOrder = std::move(moved->sessionOrder);
            if (moved->pinsChanged) impl_->SavePins(std::move(moved->pins));
            else (void)Relayout();
        } else impl_->ErrorTooltip(moved.error(), *hit);
        hit = HitTest(impl_->layout, point);
    }
    if (hit != impl_->hovered) {
        impl_->hovered = hit; impl_->tooltip.HideTooltip(); ::KillTimer(Handle(), kTooltipTimer);
        if (hit) ::SetTimer(Handle(), kTooltipTimer, static_cast<UINT>(std::max(1.0, impl_->tokens.tooltipDelay * 1000)), nullptr);
    }
    impl_->SetTargets();
}
void DockWindow::OnPointerLeave() {
    impl_->pointer.reset(); impl_->hovered.reset(); impl_->tooltip.HideTooltip(); ::KillTimer(Handle(), kTooltipTimer); impl_->SetTargets();
}
void DockWindow::OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) {
    if (!Handle() || impl_->suppressed || !IsShown() || IsTransitioning() || !::IsWindowEnabled(Handle())) return;
    const auto hit = HitTest(impl_->layout, point);
    if (button != ui::PointerButton::Left || !hit) return;
    impl_->pressed = hit; impl_->pressPoint = point; impl_->dragging = false; impl_->lastDragTarget.clear();
    const auto kind = impl_->fitted.items[*hit].kind;
    impl_->dragId = IsReorderable(kind) ? impl_->fitted.items[*hit].id : L"";
    if (!impl_->dragId.empty()) ::SetTimer(Handle(), kLongPressTimer,
        static_cast<UINT>(graphics::Motion().longPress * 1000), nullptr);
    (void)impl_->Arrange(); Invalidate();
}
void DockWindow::OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) {
    if (!Handle() || impl_->suppressed || !IsShown() || IsTransitioning() || !::IsWindowEnabled(Handle())) return;
    const auto hit = HitTest(impl_->layout, point);
    ::KillTimer(Handle(), kLongPressTimer);
    const bool click = button != ui::PointerButton::Left || (impl_->pressed && hit == impl_->pressed && !impl_->dragging);
    impl_->pressed.reset(); impl_->dragId.clear(); impl_->lastDragTarget.clear(); impl_->dragging = false;
    (void)impl_->Arrange(); Invalidate();
    if (!click || !hit) return;
    if (button == ui::PointerButton::Right) impl_->MenuAt(*hit);
    else { const auto item = impl_->fitted.items[*hit]; (void)impl_->ActivateItem(item, button == ui::PointerButton::Middle, *hit); }
}
void DockWindow::OnPointerCaptureLost() {
    ::KillTimer(Handle(), kLongPressTimer);
    impl_->pressed.reset(); impl_->dragId.clear(); impl_->lastDragTarget.clear(); impl_->dragging = false;
    (void)impl_->Arrange(); Invalidate();
}
bool DockWindow::OnKeyDown(UINT key) {
    if (impl_->fitted.items.empty()) return false;
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
    if (message == WM_DISPLAYCHANGE || (message == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA)) { (void)Relayout(); return 0; }
    if (message == kAppBarMessage) {
        if (wParam == ABN_POSCHANGED && !impl_->updatingBar) (void)Relayout();
        // The host combines foreground fullscreen with temporary native-taskbar reveal state.
        // Raw shell fullscreen notifications must not independently undo that suppression policy.
        return 0;
    }
    if (message == WM_TIMER && (wParam == kTooltipTimer || wParam == kErrorTimer)) {
        ::KillTimer(Handle(), wParam);
        if (wParam == kErrorTimer) impl_->tooltip.HideTooltip();
        else if (Handle() && IsShown() && !impl_->suppressed && !IsTransitioning() && ::IsWindowEnabled(Handle()) &&
            impl_->hovered && *impl_->hovered < impl_->fitted.items.size() && *impl_->hovered < impl_->layout.items.size())
            impl_->tooltip.ShowFor(impl_->fitted.items[*impl_->hovered].displayName,
            impl_->Anchor(*impl_->hovered), ui::TooltipPlacement::Above);
        return 0;
    }
    if (message == WM_TIMER && wParam == kLongPressTimer) {
        ::KillTimer(Handle(), kLongPressTimer);
        if (impl_->pressed && !impl_->dragId.empty()) { impl_->dragging = true; (void)impl_->Arrange(); Invalidate(); }
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
    ::KillTimer(Handle(), kTooltipTimer); ::KillTimer(Handle(), kErrorTimer); ::KillTimer(Handle(), kLongPressTimer);
    impl_->accessible.Disconnect(); impl_->tooltip.Destroy();
    const bool registered = impl_->appBar;
    impl_->options.allowWorkAreaReservation = false;
    impl_->appBar = false; impl_->appBarPositioned = false;
    if (registered) { APPBARDATA data{sizeof(data)}; data.hWnd = Handle(); ::SHAppBarMessage(ABM_REMOVE, &data); }
}

}  // namespace aether::dock
