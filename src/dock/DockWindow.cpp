// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "dock/DockWindow.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "dock/DockModel.h"
#include "graphics/BitmapCache.h"
#include "graphics/GlassPainter.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Win32Error.h"
#include "settings/SettingsSerialization.h"
#include "ui/TooltipWindow.h"
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
struct Menu {
    HMENU value = ::CreatePopupMenu();
    ~Menu() { if (value) ::DestroyMenu(value); }
};
bool SamePresentation(const std::vector<DockItem>& first, const std::vector<DockItem>& second) {
    if (first.size() != second.size()) return false;
    for (std::size_t index = 0; index < first.size(); ++index) {
        const auto& a = first[index]; const auto& b = second[index];
        if (a.kind != b.kind || !text::EqualsIgnoreCase(a.id, b.id) || a.displayName != b.displayName ||
            a.running != b.running || a.foreground != b.foreground || a.windowCount != b.windowCount) return false;
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
    FittedDock fitted;
    DockLayout layout;
    std::vector<graphics::Spring> springs;
    std::optional<D2D1_POINT_2F> pointer;
    std::optional<std::size_t> hovered;
    std::optional<std::size_t> pressed;
    D2D1_POINT_2F pressPoint{};
    std::wstring dragId;
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
        if (appBar && !options.workAreaPx) area.bottom = appBarRect.bottom;
        return area;
    }
    Result<void> Reserve() {
        const bool wanted = options.allowWorkAreaReservation && settings.reserveSpace && !options.workAreaPx;
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
        const LONG height = static_cast<LONG>(std::ceil(window.ToPx(fitted.iconSize + 30 + static_cast<float>(settings.bottomMargin))));
        data.rc.top = data.rc.bottom - height;
        if (!appBarPositioned || !::EqualRect(&appBarRect, &data.rc)) {
            ::SHAppBarMessage(ABM_SETPOS, &data); appBarRect = data.rc; appBarPositioned = true;
        }
        updatingBar = false;
        return Ok();
    }
    std::wstring ImageKey(const DockItem& item) const {
        const auto pixels = static_cast<unsigned>(std::ceil(window.ToPx(fitted.iconSize * fitted.magnification)));
        return text::ToLower(item.id) + L"#" + std::to_wstring(pixels);
    }
    void RequestIcons() {
        for (const auto& item : fitted.items) {
            if (item.kind == DockItemKind::Search || item.kind == DockItemKind::Overflow) continue;
            const auto key = ImageKey(item);
            if (!requested.insert(key).second) continue;
            std::weak_ptr<bool> lifetime = alive;
            try {
                context.icons.RequestIcon({key, item.application,
                    static_cast<unsigned>(std::ceil(window.ToPx(fitted.iconSize * fitted.magnification)))},
                    [this, lifetime](const std::wstring& keyValue, std::shared_ptr<const apps::IconImage> image) {
                        const auto valid = lifetime.lock(); if (!valid || !*valid) return;
                        images[keyValue] = std::move(image); window.Invalidate();
                    });
            } catch (...) { AETHER_LOG_WARN("dock", "an icon request failed"); }
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
                const float phase = static_cast<float>((now - bounce->second) / 0.5);
                if (phase >= 0 && phase < 1 && !motion.reduced) {
                    const float shift = std::sin(phase * std::numbers::pi_v<float>) * 10;
                    rect.top -= shift; rect.bottom -= shift;
                }
            }
            if (pressed && *pressed == index && !dragging) {
                const float inset = (rect.right - rect.left) * 0.03f;
                rect.left += inset; rect.right -= inset; rect.top += inset; rect.bottom -= inset;
            }
        }
        std::vector<RECT> regions{Pixels(layout.panel, static_cast<float>(window.Dpi()) / 96)};
        for (const auto& item : layout.items) regions.push_back(Pixels(item.icon, static_cast<float>(window.Dpi()) / 96));
        window.SetInputRegionPx(regions);
        window.SetBackdropPanel(layout.panel, tokens.dockCornerRadius, tokens.mode != graphics::ThemeMode::HighContrast);
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
        auto model = BuildDockItems(settings, running); if (!model) return model.error();
        auto fit = FitDockItems(*model, settings, width); if (!fit) return fit.error();
        std::map<std::wstring, graphics::Spring> old;
        for (std::size_t index = 0; index < springs.size(); ++index) old.emplace(fitted.items[index].id, springs[index]);
        fitted = std::move(*fit); springs.clear();
        for (const auto& item : fitted.items) {
            auto found = old.find(item.id);
            springs.push_back(found == old.end() ? graphics::Spring(1) : found->second);
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
                    auto& spring = reorderOffsets.try_emplace(fitted.items[index].id, offset).first->second;
                    spring.SetTarget(0);
                }
            }
            if (!reorderOffsets.empty()) { (void)Arrange(); window.RequestAnimationFrame(); }
        }
        const int pixelWidth = static_cast<int>(std::lround(window.ToPx(width)));
        const int pixelHeight = static_cast<int>(std::lround(window.ToPx(layout.windowHeight)));
        const int x = (area.left + area.right - pixelWidth) / 2;
        const int bottom = area.bottom - static_cast<int>(window.ToPx(static_cast<float>(settings.bottomMargin)));
        window.SetBoundsPx({x, bottom - pixelHeight, x + pixelWidth, bottom});
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
        if (index >= layout.items.size()) return;
        tooltip.ShowFor(text::Widen(error.message), Anchor(index), ui::TooltipPlacement::Above);
        ::SetTimer(window.Handle(), kErrorTimer, 3000, nullptr);
    }
    void Emit(Signal<>& signal) {
        try { signal.Emit(); } catch (...) { AETHER_LOG_ERROR("dock", "Dock action listener threw an exception"); }
    }
    Result<void> ActivateItem(const DockItem& item, bool forceNew, std::size_t anchor) {
        if (item.kind == DockItemKind::Search) { Emit(window.SearchRequested); return Ok(); }
        if (item.kind == DockItemKind::Overflow) { MenuAt(anchor); return Ok(); }
        if (!forceNew && item.runningApplication) {
            auto result = context.launcher.Activate(*item.runningApplication);
            if (!result) ErrorTooltip(result.error(), anchor);
            return result;
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
    void SavePins(std::vector<settings::PinnedApp> pins) {
        std::weak_ptr<bool> lifetime = alive;
        auto result = context.settings.Update([pins = std::move(pins)](settings::Settings& value) { value.dock.pinned = pins; },
            [this, lifetime](Result<void> saved) {
                const auto valid = lifetime.lock(); if (!valid || !*valid) return;
                if (!saved) ErrorTooltip(saved.error(), focused);
            });
        if (!result) ErrorTooltip(result.error(), focused);
    }
    void MenuAt(std::size_t index) {
        if (index >= fitted.items.size()) return;
        const DockItem item = fitted.items[index];
        const auto hidden = fitted.overflow;
        Menu menu; if (!menu.value) return;
        const bool overflow = item.kind == DockItemKind::Overflow || (item.kind == DockItemKind::Search && !hidden.empty());
        if (overflow) {
            for (std::size_t i = 0; i < hidden.size(); ++i) ::AppendMenuW(menu.value, MF_STRING, 1000 + i, MenuText(hidden[i].displayName).c_str());
            ::AppendMenuW(menu.value, MF_SEPARATOR, 0, nullptr);
        } else {
            ::AppendMenuW(menu.value, MF_STRING, 1, item.kind == DockItemKind::Search ? L"Search applications" : L"Open");
            if (item.kind != DockItemKind::Search) {
                if (item.application.kind == apps::ApplicationKind::Desktop) ::AppendMenuW(menu.value, MF_STRING, 2, L"New instance");
                ::AppendMenuW(menu.value, MF_STRING, 3, item.kind == DockItemKind::Pinned ? L"Unpin from Dock" : L"Pin to Dock");
                if (item.kind == DockItemKind::Pinned) {
                    ::AppendMenuW(menu.value, MF_STRING, 4, L"Move left");
                    ::AppendMenuW(menu.value, MF_STRING, 5, L"Move right");
                }
            }
            ::AppendMenuW(menu.value, MF_SEPARATOR, 0, nullptr);
        }
        ::AppendMenuW(menu.value, MF_STRING, 6, L"Dock settings...");
        const auto anchor = Anchor(index);
        const auto chosen = ::TrackPopupMenuEx(menu.value, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
            (anchor.left + anchor.right) / 2, anchor.top, window.Handle(), nullptr);
        if (chosen >= 1000 && static_cast<std::size_t>(chosen - 1000) < hidden.size()) {
            (void)ActivateItem(hidden[chosen - 1000], false, index); return;
        }
        if (chosen == 1 || chosen == 2) { (void)ActivateItem(item, chosen == 2, index); return; }
        if (chosen == 6) { Emit(window.SettingsRequested); return; }
        const auto& pins = context.settings.Current().dock.pinned;
        if (chosen == 3) {
            auto changed = item.kind == DockItemKind::Pinned ? UnpinApplication(pins, item.id) : PinApplication(pins, item.application);
            if (changed) SavePins(std::move(*changed)); else ErrorTooltip(changed.error(), index);
        }
        if (chosen == 4 || chosen == 5) {
            const auto found = std::find_if(pins.begin(), pins.end(), [&](const auto& pin) { return text::EqualsIgnoreCase(pin.id, item.id); });
            if (found == pins.end()) return;
            const auto current = static_cast<std::size_t>(found - pins.begin());
            const auto destination = chosen == 4 ? (current == 0 ? 0 : current - 1) : std::min(current + 1, pins.size() - 1);
            auto changed = MovePin(pins, item.id, destination); if (changed) SavePins(std::move(*changed));
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
        if (item.kind == DockItemKind::Pinned) result.itemStatus = L"Pinned";
        if (item.running) { if (!result.itemStatus.empty()) result.itemStatus += L", "; result.itemStatus += L"Running"; }
        return result;
    }
    Result<void> Invoke(int id) override {
        return Guard([&]() -> Result<void> {
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
        auto oldModel = BuildDockItems(impl_->settings, impl_->running); if (!oldModel) return oldModel.error();
        auto newModel = BuildDockItems(impl_->settings, applications); if (!newModel) return newModel.error();
        impl_->running = std::move(applications);
        if (SamePresentation(*oldModel, *newModel)) {
            // Titles/window ordering affect activation but do not request a frame when the visible
            // Dock presentation is unchanged.
            const auto update = [&](std::vector<DockItem>& items) {
                for (auto& item : items) for (const auto& fresh : *newModel) {
                    if (text::EqualsIgnoreCase(item.id, fresh.id)) item.runningApplication = fresh.runningApplication;
                }
            };
            update(impl_->fitted.items); update(impl_->fitted.overflow); return Ok();
        }
        return Relayout();
    });
}
Result<void> DockWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion,
                                 const settings::DockSettings& settings) {
    return Guard([&]() -> Result<void> {
        settings::Settings normalized; normalized.dock = settings;
        if (auto validated = settings::Validate(normalized); !validated) return validated.error();
        impl_->tokens = tokens; impl_->motion = motion; impl_->settings = std::move(normalized.dock);
        impl_->tooltip.SetTheme(tokens, motion);
        if (!Handle()) return Ok();
        auto result = Relayout(); impl_->SetTargets(); return result;
    });
}
Result<void> DockWindow::SetSuppressed(bool suppressed) {
    return Guard([&]() -> Result<void> {
        impl_->suppressed = suppressed;
        if (suppressed) { Hide(); impl_->tooltip.HideTooltip(); } else Show(false);
        return Ok();
    });
}
Result<void> DockWindow::OnExplorerRestarted() {
    const bool registered = impl_->appBar;
    impl_->updatingBar = true; impl_->appBar = false; impl_->appBarPositioned = false;
    if (registered) { APPBARDATA data{sizeof(data)}; data.hWnd = Handle(); ::SHAppBarMessage(ABM_REMOVE, &data); }
    impl_->updatingBar = false; return Relayout();
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
    graphics::GlassStyle style; style.cornerRadius = impl_->tokens.dockCornerRadius;
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
        if (item.kind == DockItemKind::Search) {
            context.d2d->FillRoundedRectangle({rect, size * 0.24f, size * 0.24f}, accent.get());
            const auto center = D2D1::Point2F(rect.left + size * 0.44f, rect.top + size * 0.43f);
            context.d2d->DrawEllipse({center, size * 0.19f, size * 0.19f}, primary.get(), std::max(1.6f, size * 0.045f));
            context.d2d->DrawLine({center.x + size * 0.14f, center.y + size * 0.14f},
                {rect.left + size * 0.73f, rect.top + size * 0.73f}, primary.get(), std::max(1.6f, size * 0.045f));
        } else if (item.kind == DockItemKind::Overflow) {
            for (int dot = 0; dot < 3; ++dot) context.d2d->FillEllipse({{rect.left + size * (0.3f + 0.2f * dot), rect.top + size / 2}, 2.5f, 2.5f}, primary.get());
        } else {
            const auto key = impl_->ImageKey(item);
            auto* bitmap = impl_->bitmaps.Find(key);
            if (!bitmap) {
                if (const auto image = impl_->images.find(key); image != impl_->images.end() && image->second) {
                    bitmap = impl_->bitmaps.Put(context.d2d, key, image->second->width, image->second->height, image->second->pixels);
                }
            }
            if (bitmap) context.d2d->DrawBitmap(bitmap, rect, 1, D2D1_INTERPOLATION_MODE_LINEAR);
            else graphics::PaintFallbackIcon(context.d2d, Device().DWriteFactory(), rect,
                item.displayName.empty() ? L'?' : item.displayName.front(), impl_->tokens);
        }
        if (item.running) context.d2d->FillEllipse({{(rect.left + rect.right) / 2, impl_->layout.panel.bottom - 9}, 2.2f, 2.2f},
            item.foreground ? accent.get() : indicator.get());
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
    std::erase_if(impl_->bounces, [&](const auto& bounce) { return impl_->motion.reduced || now - bounce.second >= 0.5; });
    moving = moving || !impl_->bounces.empty();
    (void)impl_->Arrange(); if (!moving) impl_->lastFrame = 0; return moving;
}
void DockWindow::OnDpiChanged(UINT, const RECT&) { impl_->requested.clear(); (void)Relayout(); }
void DockWindow::OnPointerMove(D2D1_POINT_2F point) {
    impl_->pointer = point;
    if (impl_->pressed && !impl_->dragId.empty() && std::hypot(point.x - impl_->pressPoint.x, point.y - impl_->pressPoint.y) >= 6) impl_->dragging = true;
    const auto hit = HitTest(impl_->layout, point);
    if (impl_->dragging && hit && impl_->fitted.items[*hit].kind == DockItemKind::Pinned &&
        !text::EqualsIgnoreCase(impl_->fitted.items[*hit].id, impl_->dragId)) {
        const auto pins = impl_->context.settings.Current().dock.pinned;
        const auto found = std::find_if(pins.begin(), pins.end(), [&](const auto& pin) { return text::EqualsIgnoreCase(pin.id, impl_->fitted.items[*hit].id); });
        if (found != pins.end()) {
            auto moved = MovePin(pins, impl_->dragId, static_cast<std::size_t>(found - pins.begin()));
            if (moved) impl_->SavePins(std::move(*moved));
        }
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
    const auto hit = HitTest(impl_->layout, point);
    if (button != ui::PointerButton::Left || !hit) return;
    impl_->pressed = hit; impl_->pressPoint = point; impl_->dragging = false;
    impl_->dragId = impl_->fitted.items[*hit].kind == DockItemKind::Pinned ? impl_->fitted.items[*hit].id : L"";
    (void)impl_->Arrange(); Invalidate();
}
void DockWindow::OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) {
    const auto hit = HitTest(impl_->layout, point);
    const bool click = button != ui::PointerButton::Left || (impl_->pressed && hit == impl_->pressed && !impl_->dragging);
    impl_->pressed.reset(); impl_->dragId.clear(); impl_->dragging = false;
    (void)impl_->Arrange(); Invalidate();
    if (!click || !hit) return;
    if (button == ui::PointerButton::Right) impl_->MenuAt(*hit);
    else { const auto item = impl_->fitted.items[*hit]; (void)impl_->ActivateItem(item, button == ui::PointerButton::Middle, *hit); }
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
        if (const auto index = impl_->Index(static_cast<int>(wParam)); index && !impl_->suppressed) {
            const auto item = impl_->fitted.items[*index]; (void)impl_->ActivateItem(item, false, *index);
        }
        return 0;
    }
    if (message == impl_->explorerMessage) { (void)OnExplorerRestarted(); return 0; }
    if (message == WM_DISPLAYCHANGE || (message == WM_SETTINGCHANGE && wParam == SPI_SETWORKAREA)) { (void)Relayout(); return 0; }
    if (message == kAppBarMessage) {
        if (wParam == ABN_POSCHANGED && !impl_->updatingBar) (void)Relayout();
        if (wParam == ABN_FULLSCREENAPP) (void)SetSuppressed(lParam != 0);
        return 0;
    }
    if (message == WM_TIMER && (wParam == kTooltipTimer || wParam == kErrorTimer)) {
        ::KillTimer(Handle(), wParam);
        if (wParam == kErrorTimer) impl_->tooltip.HideTooltip();
        else if (impl_->hovered) impl_->tooltip.ShowFor(impl_->fitted.items[*impl_->hovered].displayName,
            impl_->Anchor(*impl_->hovered), ui::TooltipPlacement::Above);
        return 0;
    }
    if (message == WM_CONTEXTMENU) { impl_->MenuAt(impl_->focused); return 0; }
    if (message == WM_CANCELMODE) { impl_->pressed.reset(); impl_->dragId.clear(); impl_->dragging = false; (void)impl_->Arrange(); Invalidate(); }
    return std::nullopt;
}
void DockWindow::OnCreated() {}
void DockWindow::OnDestroying() {
    ::KillTimer(Handle(), kTooltipTimer); ::KillTimer(Handle(), kErrorTimer);
    impl_->accessible.Disconnect(); impl_->tooltip.Destroy();
    const bool registered = impl_->appBar;
    impl_->options.allowWorkAreaReservation = false;
    impl_->appBar = false; impl_->appBarPositioned = false;
    if (registered) { APPBARDATA data{sizeof(data)}; data.hWnd = Handle(); ::SHAppBarMessage(ABM_REMOVE, &data); }
}

}  // namespace aether::dock
