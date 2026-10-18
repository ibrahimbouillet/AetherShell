// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "topbar/TopBarWindow.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "graphics/GlassPainter.h"
#include "graphics/TextCache.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/WindowUtilities.h"
#include "status/ClockProvider.h"
#include "ui/TooltipWindow.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace aether::topbar {
namespace {

constexpr UINT kAppBarMessage = WM_APP + 101;
constexpr UINT kForegroundChanged = WM_APP + 102;
constexpr UINT kDeferredInvoke = WM_APP + 103;
constexpr float kHeight = 32.0f;
thread_local std::vector<HWND> watchedWindows;

void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    for (const HWND window : watchedWindows) (void)::PostMessageW(window, kForegroundChanged, 0, 0);
}

// All marks are original vector geometry; no icon font or third-party icon set is used.
void Glyph(ID2D1DeviceContext* dc, const DisplayItem& item, float x, float y,
           ID2D1SolidColorBrush* brush, const graphics::DesignTokens& tokens, bool updateAvailable) {
    constexpr float stroke = 1.5f;
    const auto line = [&](float a, float b, float c, float d) {
        dc->DrawLine(D2D1::Point2F(x + a, y + b), D2D1::Point2F(x + c, y + d), brush, stroke);
    };
    const auto circle = [&](float a, float b, float radius) {
        dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x + a, y + b), radius, radius), brush, stroke);
    };
    if (item.kind == ItemKind::Logo) {
        // Same two cubic ribbons and small-size optical correction as render_logo.py.
        winrt::com_ptr<ID2D1Factory> factory; dc->GetFactory(factory.put());
        winrt::com_ptr<ID2D1PathGeometry> geometry;
        winrt::com_ptr<ID2D1GeometrySink> sink;
        if (FAILED(factory->CreatePathGeometry(geometry.put())) || FAILED(geometry->Open(sink.put()))) return;
        const auto p = [&](float a, float b) { return D2D1::Point2F(x + 9 + (a - 500) * 0.02124f, y + 9 + (b - 500) * 0.02124f); };
        sink->BeginFigure(p(150, 610), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddBezier(D2D1::BezierSegment(p(260, 340), p(445, 170), p(810, 240)));
        sink->AddLine(p(700, 390));
        sink->AddBezier(D2D1::BezierSegment(p(500, 350), p(365, 425), p(280, 640)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->BeginFigure(p(245, 770), D2D1_FIGURE_BEGIN_FILLED); sink->AddLine(p(345, 600));
        sink->AddBezier(D2D1::BezierSegment(p(485, 655), p(615, 575), p(725, 375)));
        sink->AddLine(p(850, 375));
        sink->AddBezier(D2D1::BezierSegment(p(715, 700), p(515, 850), p(245, 770)));
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        if (FAILED(sink->Close())) return;
        brush->SetColor(tokens.textPrimary.ToD2D());
        winrt::com_ptr<ID2D1GradientStopCollection> stops;
        winrt::com_ptr<ID2D1LinearGradientBrush> gradient;
        const D2D1_GRADIENT_STOP colors[] = {{0, graphics::Color::FromHex(0x5B6CFF).ToD2D()}, {1, graphics::Color::FromHex(0x3DD9E8).ToD2D()}};
        if (tokens.mode != graphics::ThemeMode::HighContrast && SUCCEEDED(dc->CreateGradientStopCollection(colors, 2, stops.put())))
            dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(x, y), D2D1::Point2F(x + 18, y + 6.3f)), stops.get(), gradient.put());
        dc->FillGeometry(geometry.get(), gradient ? static_cast<ID2D1Brush*>(gradient.get()) : brush);
        if (updateAvailable) {
            brush->SetColor(tokens.glass.tint.WithAlpha(1).ToD2D());
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 18, y - 2, x + 28, y + 11), 3, 3), brush);
            brush->SetColor(tokens.textPrimary.ToD2D());
            line(23, 1, 23, 8); line(20, 4, 23, 1); line(23, 1, 26, 4);
        }
        return;
    }
    if (item.kind == ItemKind::Network) {
        if (item.connection == status::ConnectionKind::WiFi || item.connection == status::ConnectionKind::Cellular) {
            // Signal bars are drawn only when Windows supplied a value; never synthesize full bars.
            for (int index = 0; item.level && index < 5; ++index) {
                const float top = 15.0f - static_cast<float>(index + 1) * 2.3f;
                const auto rect = D2D1::RectF(x + 1 + index * 3.3f, y + top, x + 3 + index * 3.3f, y + 16);
                if (item.level && index < *item.level) dc->FillRectangle(rect, brush);
                else dc->DrawRectangle(rect, brush, 0.65f);
            }
            if (!item.level) { circle(9, 9, 6); line(6, 12, 12, 6); }
        } else if (item.connection == status::ConnectionKind::Ethernet) {
            dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 1, y + 2, x + 17, y + 12), 2, 2), brush, stroke);
            line(9, 12, 9, 16); line(4, 16, 14, 16);
        } else { circle(9, 9, 7); line(4, 14, 14, 4); }
        if (item.connectivity == status::InternetConnectivity::Constrained ||
            item.connectivity == status::InternetConnectivity::LocalOnly) {
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + 16, y + 3), 3, 3), brush);
        }
    } else if (item.kind == ItemKind::Volume) {
        line(1, 7, 5, 7); line(5, 7, 10, 3); line(10, 3, 10, 15); line(10, 15, 5, 11); line(5, 11, 1, 11); line(1, 11, 1, 7);
        if (item.muted) { line(13, 6, 18, 11); line(13, 11, 18, 6); }
        else if (item.level) {
            const int waves = *item.level == 0 ? 0 : *item.level <= 33 ? 1 : *item.level <= 66 ? 2 : 3;
            for (int wave = 0; wave < waves; ++wave) line(12.5f + wave * 2, 7.0f - wave, 12.5f + wave * 2, 11.0f + wave);
        }
    } else if (item.kind == ItemKind::Microphone) {
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x + 6, y + 1, x + 12, y + 11), 3, 3), brush, stroke);
        line(3, 8, 3, 11); line(3, 11, 6, 14); line(6, 14, 12, 14); line(12, 14, 15, 11); line(15, 11, 15, 8);
        line(9, 14, 9, 18); line(5, 18, 13, 18);
        if (item.muted) line(1, 17, 17, 1);
    } else if (item.kind == ItemKind::Battery) {
        if (!item.level && item.powerSource == status::PowerSource::External) {
            line(6, 2, 6, 7); line(12, 2, 12, 7); line(4, 7, 14, 7);
            line(4, 7, 4, 11); line(4, 11, 8, 14); line(14, 7, 14, 11); line(14, 11, 10, 14);
            line(9, 14, 9, 18); return;
        }
        dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y + 4, x + 17, y + 14), 2, 2), brush, stroke);
        line(19, 7, 19, 11);
        if (item.level && *item.level > 0)
            dc->FillRectangle(D2D1::RectF(x + 2, y + 6, x + 2 + 13.0f * *item.level / 100.0f, y + 12), brush);
        if (item.charge == status::ChargeState::Charging) {
            brush->SetColor(tokens.glass.tint.ToD2D());
            line(10, 4, 7, 10); line(7, 10, 11, 10); line(11, 10, 8, 15);
        }
    } else if (item.kind == ItemKind::Weather) {
        const auto category = item.weatherGlyph;
        if (category == weather::WeatherGlyph::Clear) {
            if (item.isDay == false) { circle(9, 9, 6); line(9, 3, 6, 9); line(6, 9, 12, 14); }
            else {
                circle(9, 9, 4);
                for (int ray = 0; ray < 8; ++ray) {
                    const float angle = static_cast<float>(ray) * 0.78539816f;
                    line(9 + std::cos(angle) * 6, 9 + std::sin(angle) * 6,
                         9 + std::cos(angle) * 8, 9 + std::sin(angle) * 8);
                }
            }
        } else if (category != weather::WeatherGlyph::Unknown) {
            circle(6, 9, 4); circle(11, 6, 5); circle(15, 10, 3); line(4, 13, 16, 13);
            if (category == weather::WeatherGlyph::Rain || category == weather::WeatherGlyph::Drizzle) {
                line(6, 16, 5, 18); line(11, 16, 10, 18); line(16, 16, 15, 18);
            } else if (category == weather::WeatherGlyph::Snow) { line(7, 16, 7, 19); line(5, 17.5f, 9, 17.5f); }
            else if (category == weather::WeatherGlyph::Fog) { line(2, 16, 18, 16); line(4, 19, 16, 19); }
            else if (category == weather::WeatherGlyph::Thunder) { line(11, 13, 8, 17); line(8, 17, 12, 17); line(12, 17, 9, 21); }
        } else { circle(9, 9, 6); line(6, 12, 12, 6); }
        if (item.cached) circle(18, 17, 2);
    }
    if (item.unknown && item.kind != ItemKind::Logo) { line(15, 15, 18, 18); line(15, 18, 18, 15); }
}

}  // namespace

struct TopBarWindow::Impl final : ui::accessibility::IAccessibleSurfaceModel {
    struct Gate { TopBarWindow* owner = nullptr; };
    Impl(TopBarWindow& window, TopBarContext input)
        : owner(window), context(input), tooltip(input.device, input.scheduler), gate(std::make_shared<Gate>()) {
        gate->owner = &window;
    }
    TopBarWindow& owner;
    TopBarContext context;
    graphics::DesignTokens tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    graphics::MotionPolicy motion;
    graphics::TextCache textCache;
    ui::TooltipWindow tooltip;
    ui::accessibility::AccessibleSurface accessibility;
    settings::TopBarSettings settings;
    bool weatherEnabled = false;
    bool updateAvailable = false;
    TopBarSnapshots snapshots;
    std::vector<PlacedItem> placed;
    graphics::Tween transition{1};
    std::shared_ptr<Gate> gate;
    Connection settingsChanged;
    std::vector<std::function<void()>> stops;
    std::optional<RECT> testBounds;
    std::optional<int> hover;
    std::optional<int> focused;
    std::optional<float> pendingVolume;
    bool focusedWindow = false;
    bool registered = false;
    std::optional<RECT> reservedBounds;
    bool relayouting = false;
    bool fullscreen = false;
    bool destroyed = false;
    HWINEVENTHOOK foregroundHook = nullptr;
    UINT explorerMessage = 0;
    std::uint64_t frames = 0;

    template <class Snapshot, class Provider>
    void Start(Provider* provider, Snapshot TopBarSnapshots::* member) {
        if (!provider) return;
        std::weak_ptr<Gate> weak = gate;
        auto* dispatcher = &context.dispatcher;
        const auto result = provider->Start([weak, dispatcher, member](const Snapshot& snapshot) {
            (void)dispatcher->Post([weak, snapshot, member] {
                if (const auto alive = weak.lock(); alive && alive->owner) {
                    alive->owner->impl_->snapshots.*member = snapshot;
                    alive->owner->Rebuild();
                }
            });
        });
        if (result) stops.push_back([provider] { provider->Stop(); });
        else AETHER_LOG_WARN("topbar", "status provider could not start ({})", ToString(result.error().code));
    }
    RECT ItemBounds(const D2D1_RECT_F& rect) const {
        const auto bounds = owner.BoundsPx();
        return RECT{bounds.left + static_cast<LONG>(std::lround(owner.ToPx(rect.left))),
                    bounds.top + static_cast<LONG>(std::lround(owner.ToPx(rect.top))),
                    bounds.left + static_cast<LONG>(std::lround(owner.ToPx(rect.right))),
                    bounds.top + static_cast<LONG>(std::lround(owner.ToPx(rect.bottom)))};
    }
    void ErrorTooltip(const Result<void>& result, int id) {
        if (result || destroyed) return;
        if (id == static_cast<int>(ItemKind::Volume)) pendingVolume.reset();
        const auto found = std::find_if(placed.begin(), placed.end(), [id](const auto& item) { return item.item.id == id; });
        if (found != placed.end()) tooltip.ShowFor(L"Action failed: " + text::Widen(ToString(result.error().code)),
            ItemBounds(found->rect), ui::TooltipPlacement::Below);
    }
    status::audio::ControlCompletion Completion(int id) {
        std::weak_ptr<Gate> weak = gate;
        auto* dispatcher = &context.dispatcher;
        return [weak, dispatcher, id](Result<void> result) {
            (void)dispatcher->Post([weak, result = std::move(result), id] {
                if (const auto alive = weak.lock(); alive && alive->owner) alive->owner->impl_->ErrorTooltip(result, id);
            });
        };
    }
    void RemoveAppBar() {
        if (!registered) return;
        APPBARDATA data{sizeof(data)}; data.hWnd = owner.Handle();
        registered = false;
        reservedBounds.reset();
        (void)::SHAppBarMessage(ABM_REMOVE, &data);
    }
    std::wstring RootName() const override { return L"Aether Shell status bar"; }
    CONTROLTYPEID RootControlType() const override { return UIA_ToolBarControlTypeId; }
    std::size_t ItemCount() const override { return placed.size(); }
    ui::accessibility::AccessibleItem Item(std::size_t index) const override {
        const auto& entry = placed.at(index);
        ui::accessibility::AccessibleItem item;
        item.id = entry.item.id; item.name = entry.item.accessibleName; item.helpText = entry.item.tooltip;
        item.itemStatus = entry.item.kind == ItemKind::Logo && updateAvailable ? L"Update available" :
            entry.item.cached ? L"Cached" : entry.item.unknown ? L"Unavailable" : L"";
        item.controlType = entry.item.actionable ? UIA_ButtonControlTypeId : UIA_TextControlTypeId;
        item.boundsPx = ItemBounds(entry.rect); item.isFocusable = entry.item.actionable;
        item.hasKeyboardFocus = focusedWindow && focused == item.id;
        item.isOffscreen = !owner.IsShown() || fullscreen;
        item.supportsInvoke = entry.item.actionable;
        return item;
    }
    Result<void> Invoke(int id) override {
        if (!::PostMessageW(owner.Handle(), kDeferredInvoke, static_cast<WPARAM>(id), 0))
            return MakeError(ErrorCode::Unavailable, "top bar action could not be queued");
        return Ok();
    }
    Result<void> Toggle(int) override { return MakeError(ErrorCode::Unsupported, "top bar uses invoke actions"); }
    Result<void> SetFocus(int id) override {
        focused = id; ::SetFocus(owner.Handle()); owner.Invalidate();
        return accessibility.NotifyFocusChanged(id);
    }
};

TopBarWindow::TopBarWindow(TopBarContext context)
    : SurfaceWindow(context.device, context.scheduler), impl_(std::make_unique<Impl>(*this, context)) {}
TopBarWindow::~TopBarWindow() { Destroy(); }

Result<void> TopBarWindow::Create(std::optional<RECT> testBoundsPx) {
    try {
        if (impl_->destroyed || Handle()) return MakeError(ErrorCode::InvalidState, "top bar already created or destroyed");
        if (!impl_->context.dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "top bar requires UI thread");
        if (testBoundsPx && impl_->context.allowAppBar)
            return MakeError(ErrorCode::InvalidArgument, "test bounds require AppBar operations disabled");
        impl_->testBounds = testBoundsPx;
        if (auto* service = impl_->context.settings; service && service->Loaded()) {
            impl_->settings = service->Current().topBar;
            impl_->weatherEnabled = service->Current().weather.enabled;
            if (impl_->context.weather) {
                const auto& source = service->Current().weather;
                weather::WeatherConfig config;
                config.enabled = source.enabled; config.latitude = source.latitude; config.longitude = source.longitude;
                config.locationName = source.locationName;
                config.unit = source.unit == settings::TemperatureUnit::Celsius ? status::TemperatureUnit::Celsius : status::TemperatureUnit::Fahrenheit;
                config.refreshInterval = std::chrono::minutes(source.refreshMinutes);
                if (auto configured = impl_->context.weather->UpdateConfig(std::move(config)); !configured) return configured;
            }
        }
        if (auto* clock = dynamic_cast<status::ClockProvider*>(impl_->context.clock))
            clock->SetResolution(impl_->settings.showSeconds ? status::ClockResolution::Seconds : status::ClockResolution::Minutes);
        if (!impl_->context.clock) impl_->snapshots.clock.state = status::ProviderState::Unavailable;
        if (!impl_->context.battery) impl_->snapshots.battery.state = status::ProviderState::Error;
        if (!impl_->context.keyboard) impl_->snapshots.keyboard.state = status::ProviderState::Unavailable;
        if (!impl_->context.network) impl_->snapshots.network.state = status::ProviderState::Unavailable;
        if (!impl_->context.audio) impl_->snapshots.audio.state = status::ProviderState::Unavailable;
        if (!impl_->context.microphone) impl_->snapshots.microphone.state = status::ProviderState::Unavailable;
        if (!impl_->context.weather) impl_->snapshots.weather.snapshot.state = status::WeatherState::Unsupported;
        impl_->textCache.Initialize(Device().DWriteFactory(), impl_->tokens.fontFamily, impl_->tokens.displayFontFamily);
        const auto monitor = platform::PrimaryMonitor();
        RECT bounds = testBoundsPx.value_or(RECT{monitor.workArea.left + 12, monitor.workArea.top + 6,
            monitor.workArea.right - 12, monitor.workArea.top + 38});
        ui::SurfaceWindowOptions options;
        options.className = L"AetherShell.TopBar"; options.title = L"Aether Shell status bar";
        if (auto created = SurfaceWindow::Create(options, bounds); !created) return created;
        if (auto tooltip = impl_->tooltip.Initialize(); !tooltip) { Destroy(); return tooltip; }
        impl_->tooltip.SetTheme(impl_->tokens, impl_->motion);
        Rebuild();
        if (auto connected = impl_->accessibility.Connect(Handle(), *impl_); !connected) { Destroy(); return connected; }
        auto& context = impl_->context;
        impl_->Start(context.clock, &TopBarSnapshots::clock);
        impl_->Start(context.battery, &TopBarSnapshots::battery);
        impl_->Start(context.keyboard, &TopBarSnapshots::keyboard);
        impl_->Start(context.network, &TopBarSnapshots::network);
        if (context.audio) {
            std::weak_ptr<Impl::Gate> weak = impl_->gate;
            auto* dispatcher = &context.dispatcher;
            auto started = context.audio->StartWithOrigin([weak, dispatcher](const status::AudioSnapshot& snapshot,
                status::audio::ChangeOrigin origin) {
                (void)dispatcher->Post([weak, snapshot, origin] {
                    if (const auto alive = weak.lock(); alive && alive->owner) {
                        auto& state = *alive->owner->impl_;
                        if (origin == status::audio::ChangeOrigin::External ||
                            (state.pendingVolume && snapshot.volume && std::abs(*state.pendingVolume - *snapshot.volume) < 0.001f))
                            state.pendingVolume.reset();
                        state.snapshots.audio = snapshot;
                        alive->owner->Rebuild();
                    }
                });
            });
            if (started) impl_->stops.push_back([provider = context.audio] { provider->Stop(); });
        }
        impl_->Start(context.microphone, &TopBarSnapshots::microphone);
        if (context.weather) {
            std::weak_ptr<Impl::Gate> weak = impl_->gate;
            auto* dispatcher = &context.dispatcher;
            auto started = context.weather->StartWithReports([weak, dispatcher](const weather::WeatherReport& report) {
                (void)dispatcher->Post([weak, report] {
                    if (const auto alive = weak.lock(); alive && alive->owner) {
                        alive->owner->impl_->snapshots.weather = report; alive->owner->Rebuild();
                    }
                });
            });
            if (started) impl_->stops.push_back([provider = context.weather] { provider->Stop(); });
        }
        if (context.settings) impl_->settingsChanged = context.settings->Changed().Connect([this](const settings::Settings& value) {
            SetSettings(value.topBar, value.weather.enabled);
            if (impl_->context.weather) {
                weather::WeatherConfig config;
                config.enabled = value.weather.enabled; config.latitude = value.weather.latitude; config.longitude = value.weather.longitude;
                config.locationName = value.weather.locationName;
                config.unit = value.weather.unit == settings::TemperatureUnit::Celsius ? status::TemperatureUnit::Celsius : status::TemperatureUnit::Fahrenheit;
                config.refreshInterval = std::chrono::minutes(value.weather.refreshMinutes);
                const auto updated = impl_->context.weather->UpdateConfig(std::move(config));
                if (!updated) AETHER_LOG_WARN("topbar", "weather settings rejected");
            }
        });
        impl_->explorerMessage = ::RegisterWindowMessageW(L"TaskbarCreated");
        if (context.allowAppBar) {
            watchedWindows.push_back(Handle());
            impl_->foregroundHook = ::SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                ForegroundEvent, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        }
        Relayout();
        if (context.allowAppBar) (void)::PostMessageW(Handle(), kForegroundChanged, 0, 0);
        return Ok();
    } catch (...) { Destroy(); return MakeError(ErrorCode::Internal, "top bar creation failed"); }
}

void TopBarWindow::SetSettings(const settings::TopBarSettings& settings, bool weatherEnabled) {
    impl_->settings = settings; impl_->weatherEnabled = weatherEnabled;
    if (auto* clock = dynamic_cast<status::ClockProvider*>(impl_->context.clock))
        clock->SetResolution(settings.showSeconds ? status::ClockResolution::Seconds : status::ClockResolution::Minutes);
    Relayout(); Rebuild();
}
void TopBarWindow::SetSnapshots(TopBarSnapshots snapshots) {
    impl_->pendingVolume.reset(); impl_->snapshots = std::move(snapshots); Rebuild();
}
Result<void> TopBarWindow::SetUpdateAvailable(bool available) {
    if (!impl_->context.dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "top bar requires UI thread");
    if (impl_->destroyed) return MakeError(ErrorCode::InvalidState, "top bar was destroyed");
    if (impl_->updateAvailable == available) return Ok();
    try { impl_->updateAvailable = available; Rebuild(); return Ok(); }
    catch (...) { return MakeError(ErrorCode::Internal, "update indicator could not be changed"); }
}
void TopBarWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    impl_->tokens = tokens; impl_->motion = motion; impl_->tooltip.SetTheme(tokens, motion); Rebuild();
}
const std::vector<PlacedItem>& TopBarWindow::Items() const { return impl_->placed; }
std::uint64_t TopBarWindow::RenderCount() const noexcept { return impl_->frames; }

void TopBarWindow::Rebuild() {
    if (impl_->destroyed || !Handle()) return;
    auto items = BuildDisplayItems(impl_->snapshots, impl_->settings, impl_->weatherEnabled);
    if (!items) return;
    if (impl_->updateAvailable) for (auto& item : items.value()) if (item.kind == ItemKind::Logo) {
        item.tooltip += L" · Update available"; item.accessibleName = item.tooltip;
    }
    std::vector<float> widths;
    graphics::TextStyle style; style.size = impl_->tokens.fontSizeBar;
    for (const auto& item : items.value()) {
        const bool glyph = item.kind != ItemKind::Keyboard && item.kind != ItemKind::Clock;
        style.weight = item.kind == ItemKind::Clock ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        widths.push_back(impl_->textCache.Measure(item.text, style).width + (glyph ? 24.0f : 0.0f) + 22.0f);
    }
    impl_->placed = LayoutItems(items.value(), widths, ToDip(static_cast<float>(BoundsPx().right - BoundsPx().left)));
    impl_->transition.SnapTo(0.70f);
    impl_->transition.AnimateTo(1.0f, graphics::FrameScheduler::Now(), impl_->motion.Scale(0.12));
    if (!impl_->motion.reduced) RequestAnimationFrame();
    const auto notified = impl_->accessibility.NotifyStructureChanged(); (void)notified;
    for (const auto& item : impl_->placed) {
        const auto property = impl_->accessibility.NotifyPropertyChanged(item.item.id); (void)property;
        if (impl_->hover == item.item.id)
            impl_->tooltip.ShowFor(item.item.tooltip, impl_->ItemBounds(item.rect), ui::TooltipPlacement::Below);
    }
    Invalidate();
}

void TopBarWindow::Relayout() {
    if (!Handle() || impl_->destroyed || impl_->relayouting) return;
    impl_->relayouting = true;
    if (impl_->testBounds) SetBoundsPx(*impl_->testBounds);
    else {
        const auto monitor = platform::PrimaryMonitor();
        const float scale = static_cast<float>(monitor.dpi) / 96.0f;
        const int margin = static_cast<int>(std::lround(impl_->settings.topMargin * scale));
        const int height = static_cast<int>(std::lround(kHeight * scale));
        const int side = static_cast<int>(std::lround(12 * scale));
        RECT base = monitor.workArea;
        if (impl_->context.allowAppBar && impl_->settings.reserveSpace) {
            APPBARDATA data{sizeof(data)}; data.hWnd = Handle(); data.uCallbackMessage = kAppBarMessage;
            if (!impl_->registered) impl_->registered = ::SHAppBarMessage(ABM_NEW, &data) != 0;
            if (impl_->registered) {
                data.uEdge = ABE_TOP; data.rc = monitor.bounds;
                data.rc.bottom = data.rc.top + margin + height + static_cast<int>(4 * scale);
                (void)::SHAppBarMessage(ABM_QUERYPOS, &data);
                data.rc.bottom = data.rc.top + margin + height + static_cast<int>(4 * scale);
                if (!impl_->reservedBounds || !::EqualRect(&data.rc, &*impl_->reservedBounds)) {
                    (void)::SHAppBarMessage(ABM_SETPOS, &data);
                    impl_->reservedBounds = data.rc;
                }
                base = data.rc;
            }
        } else impl_->RemoveAppBar();
        SetBoundsPx(RECT{base.left + side, base.top + margin, base.right - side, base.top + margin + height});
    }
    impl_->relayouting = false;
    Rebuild();
}

void TopBarWindow::OnExplorerRestarted() { impl_->RemoveAppBar(); Relayout(); }
void TopBarWindow::SetFullscreenHidden(bool hidden) {
    if (impl_->fullscreen == hidden) return;
    impl_->fullscreen = hidden;
    if (hidden) { impl_->tooltip.HideTooltip(); Hide(); }
    else Show(false);
    try { FullscreenChanged.Emit(hidden); }
    catch (...) { AETHER_LOG_ERROR("topbar", "fullscreen listener threw an exception"); }
}

void TopBarWindow::OnRender(const ui::RenderContext& context) {
    ++impl_->frames;
    const auto panel = D2D1::RectF(0, 0, context.widthDip, context.heightDip);
    const bool blur = impl_->tokens.mode != graphics::ThemeMode::HighContrast && platform::SystemTransparencyEnabled();
    SetBackdropPanel(panel, impl_->tokens.barCornerRadius, blur);
    graphics::GlassStyle glass;
    glass.cornerRadius = impl_->tokens.barCornerRadius; glass.opacity = static_cast<float>(impl_->settings.opacity);
    glass.backdropBlur = BackdropBlurActive(); glass.shadow = false;
    graphics::PaintGlassPanel(context.d2d, panel, impl_->tokens, glass);
    winrt::com_ptr<ID2D1SolidColorBrush> brush;
    if (FAILED(context.d2d->CreateSolidColorBrush(impl_->tokens.textPrimary.ToD2D(), brush.put()))) return;
    for (const auto& entry : impl_->placed) {
        if ((impl_->hover == entry.item.id && entry.item.actionable) ||
            (impl_->focusedWindow && impl_->focused == entry.item.id)) {
            brush->SetColor(impl_->tokens.hoverOverlay.ToD2D());
            const auto inset = D2D1::RectF(entry.rect.left + 2, 3, entry.rect.right - 2, kHeight - 3);
            context.d2d->FillRoundedRectangle(D2D1::RoundedRect(inset, 7, 7), brush.get());
            if (impl_->focusedWindow && impl_->focused == entry.item.id)
                graphics::PaintFocusRing(context.d2d, inset, 6, impl_->tokens);
        }
        const auto color = entry.item.unknown ? impl_->tokens.textSecondary : impl_->tokens.textPrimary;
        brush->SetColor(color.WithAlpha(impl_->transition.Value(context.now)).ToD2D());
        const bool glyph = entry.item.kind != ItemKind::Keyboard && entry.item.kind != ItemKind::Clock;
        if (glyph) Glyph(context.d2d, entry.item, entry.rect.left + 10, 7, brush.get(), impl_->tokens, impl_->updateAvailable);
        brush->SetColor(color.WithAlpha(impl_->transition.Value(context.now)).ToD2D());
        graphics::TextStyle style; style.size = impl_->tokens.fontSizeBar;
        style.weight = entry.item.kind == ItemKind::Clock ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
        impl_->textCache.Draw(context.d2d, entry.item.text,
            D2D1::RectF(entry.rect.left + (glyph ? 34.0f : 10.0f), 0, entry.rect.right - 10, kHeight), style, brush.get());
    }
}
bool TopBarWindow::OnAnimate(double now) { return impl_->transition.IsAnimating(now); }

void TopBarWindow::OnPointerMove(D2D1_POINT_2F point) {
    const auto hovered = HitTest(impl_->placed, point);
    if (hovered == impl_->hover) return;
    impl_->hover = hovered; Invalidate();
    if (!hovered) { impl_->tooltip.HideTooltip(); return; }
    const auto found = std::find_if(impl_->placed.begin(), impl_->placed.end(), [&](const auto& item) { return item.item.id == *hovered; });
    if (found != impl_->placed.end()) impl_->tooltip.ShowFor(found->item.tooltip, impl_->ItemBounds(found->rect), ui::TooltipPlacement::Below);
}
void TopBarWindow::OnPointerLeave() { impl_->hover.reset(); impl_->tooltip.HideTooltip(); Invalidate(); }
void TopBarWindow::OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) {
    if (button == ui::PointerButton::Left) if (const auto id = HitTest(impl_->placed, point)) Activate(*id);
}
void TopBarWindow::OnWheel(float notches, D2D1_POINT_2F point) {
    if (HitTest(impl_->placed, point) != static_cast<int>(ItemKind::Volume) || !impl_->context.audio ||
        !impl_->snapshots.audio.volume || !std::isfinite(*impl_->snapshots.audio.volume)) return;
    const auto target = WheelVolumeTarget(impl_->snapshots.audio.volume, impl_->pendingVolume, notches);
    if (!target) return;
    impl_->pendingVolume = target.value();
    impl_->context.audio->SetVolumeAsync(target.value(),
        impl_->Completion(static_cast<int>(ItemKind::Volume)));
}
void TopBarWindow::Activate(int id) {
    if (id == static_cast<int>(ItemKind::Volume)) {
        if (impl_->context.audio) impl_->context.audio->ToggleMuteAsync(impl_->Completion(id));
        else impl_->ErrorTooltip(MakeError(ErrorCode::Unavailable, "audio unavailable"), id);
    } else if (id == static_cast<int>(ItemKind::Microphone)) {
        if (impl_->context.microphone) impl_->context.microphone->ToggleMuteAsync(impl_->Completion(id));
        else impl_->ErrorTooltip(MakeError(ErrorCode::Unavailable, "microphone unavailable"), id);
    } else if (id == static_cast<int>(ItemKind::Clock)) {
        const auto found = std::find_if(impl_->placed.begin(), impl_->placed.end(), [id](const auto& item) { return item.item.id == id; });
        if (found != impl_->placed.end()) impl_->tooltip.ShowFor(found->item.tooltip, impl_->ItemBounds(found->rect), ui::TooltipPlacement::Below);
    } else if (id == static_cast<int>(ItemKind::Logo)) {
        HMENU menu = ::CreatePopupMenu();
        if (!menu) return;
        ::AppendMenuW(menu, MF_STRING, 1, L"Search…"); ::AppendMenuW(menu, MF_STRING, 2, L"Settings…");
        if (impl_->updateAvailable) ::AppendMenuW(menu, MF_STRING, 6, L"View available update…");
        ::AppendMenuW(menu, MF_STRING, 3, L"Check for updates…"); ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, 4, L"About Aether Shell"); ::AppendMenuW(menu, MF_STRING, 5, L"Quit Aether Shell");
        const auto bounds = BoundsPx();
        ::SetForegroundWindow(Handle());
        const auto command = ::TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
            bounds.left + static_cast<int>(ToPx(10)), bounds.bottom + 4, Handle(), nullptr);
        ::DestroyMenu(menu);
        switch (command) {
            case 1: SearchRequested.Emit(); break; case 2: SettingsRequested.Emit(); break;
            case 3: CheckForUpdatesRequested.Emit(); break; case 4: AboutRequested.Emit(); break;
            case 5: QuitRequested.Emit(); break;
            case 6: if (impl_->updateAvailable) UpdatesRequested.Emit(); break;
            default: break;
        }
    }
}
bool TopBarWindow::OnKeyDown(UINT key) {
    std::vector<int> actions;
    for (const auto& item : impl_->placed) if (item.item.actionable) actions.push_back(item.item.id);
    if (actions.empty()) return false;
    if (key == VK_ESCAPE) { impl_->focused.reset(); ::SetFocus(nullptr); impl_->tooltip.HideTooltip(); Invalidate(); return true; }
    if (key == VK_RETURN || key == VK_SPACE) { if (impl_->focused) Activate(*impl_->focused); return true; }
    if (key != VK_LEFT && key != VK_RIGHT && key != VK_TAB && key != VK_HOME && key != VK_END) return false;
    auto found = std::find(actions.begin(), actions.end(), impl_->focused.value_or(0));
    int index = found == actions.end() ? 0 : static_cast<int>(found - actions.begin());
    if (key == VK_HOME) index = 0;
    else if (key == VK_END) index = static_cast<int>(actions.size()) - 1;
    else index = (index + (key == VK_LEFT ? -1 : 1) + static_cast<int>(actions.size())) % static_cast<int>(actions.size());
    impl_->focused = actions[static_cast<std::size_t>(index)];
    const auto notified = impl_->accessibility.NotifyFocusChanged(*impl_->focused); (void)notified;
    Invalidate(); return true;
}
void TopBarWindow::OnFocusChanged(bool focused) {
    impl_->focusedWindow = focused;
    if (focused && !impl_->focused) impl_->focused = static_cast<int>(ItemKind::Logo);
    Invalidate();
}
void TopBarWindow::OnDpiChanged(UINT, const RECT&) { Relayout(); }
std::optional<LRESULT> TopBarWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    LRESULT result = 0;
    if (message == WM_GETOBJECT && impl_->accessibility.HandleGetObject(Handle(), wParam, lParam, result)) return result;
    if (message == kAppBarMessage) {
        if (wParam == ABN_POSCHANGED) Relayout();
        else if (wParam == ABN_FULLSCREENAPP) SetFullscreenHidden(lParam != 0);
        return 0;
    }
    if (message == kForegroundChanged) {
        SetFullscreenHidden(platform::IsFullscreenWindow(::GetForegroundWindow(), platform::MonitorForWindow(Handle())));
        return 0;
    }
    if (message == kDeferredInvoke) { Activate(static_cast<int>(wParam)); return 0; }
    if (impl_->explorerMessage && message == impl_->explorerMessage) { OnExplorerRestarted(); return 0; }
    if (message == WM_DISPLAYCHANGE || message == WM_SETTINGCHANGE) Relayout();
    if (impl_->registered && (message == WM_ACTIVATE || message == WM_WINDOWPOSCHANGED)) {
        APPBARDATA data{sizeof(data)}; data.hWnd = Handle();
        (void)::SHAppBarMessage(message == WM_ACTIVATE ? ABM_ACTIVATE : ABM_WINDOWPOSCHANGED, &data);
    }
    return std::nullopt;
}
void TopBarWindow::OnDestroying() {
    if (impl_->destroyed) return;
    impl_->destroyed = true; impl_->gate->owner = nullptr;
    impl_->settingsChanged.Disconnect();
    if (impl_->foregroundHook) { ::UnhookWinEvent(impl_->foregroundHook); impl_->foregroundHook = nullptr; }
    std::erase(watchedWindows, Handle());
    for (auto& stop : impl_->stops) stop();
    impl_->stops.clear();
    impl_->RemoveAppBar(); impl_->accessibility.Disconnect(); impl_->tooltip.Destroy();
}

}  // namespace aether::topbar
