// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "topbar/TopBarWindow.h"

#include "core/Strings.h"
#include "diagnostics/Logger.h"
#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "graphics/ScrollPhysics.h"
#include "graphics/SystemSymbols.h"
#include "core/Localization.h"
#include "graphics/TextCache.h"
#include "graphics/Transition.h"
#include "graphics/WeatherArt.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/WindowUtilities.h"
#include "status/ClockProvider.h"
#include "status/audio/EndpointReading.h"
#include "topbar/TopBarMenu.h"
#include "ui/GlassMenu.h"
#include "ui/TooltipWindow.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <shellapi.h>
#include <d2d1effects.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>

namespace aether::topbar {
namespace {

constexpr UINT kAppBarMessage = WM_APP + 101;
constexpr UINT kForegroundChanged = WM_APP + 102;
constexpr UINT kDeferredInvoke = WM_APP + 103;
constexpr UINT_PTR kTouchHoldTimer = 0x5448;
thread_local std::vector<HWND> watchedWindows;

void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    for (const HWND window : watchedWindows) (void)::PostMessageW(window, kForegroundChanged, 0, 0);
}

// Map only provider categories; graphics owns their shared proportional rounded geometry.
graphics::NetworkGlyphKind NetworkKind(status::ConnectionKind kind) noexcept {
    switch (kind) {
        case status::ConnectionKind::None: return graphics::NetworkGlyphKind::None;
        case status::ConnectionKind::WiFi: return graphics::NetworkGlyphKind::WiFi;
        case status::ConnectionKind::Ethernet: return graphics::NetworkGlyphKind::Ethernet;
        case status::ConnectionKind::Cellular: return graphics::NetworkGlyphKind::Cellular;
        case status::ConnectionKind::Other: return graphics::NetworkGlyphKind::Other;
        default: return graphics::NetworkGlyphKind::Unknown;
    }
}
graphics::WeatherSymbol WeatherKind(weather::WeatherGlyph category) noexcept {
    switch (category) {
        case weather::WeatherGlyph::Clear: return graphics::WeatherSymbol::Clear;
        case weather::WeatherGlyph::PartlyCloudy: return graphics::WeatherSymbol::PartlyCloudy;
        case weather::WeatherGlyph::Cloudy: return graphics::WeatherSymbol::Cloudy;
        case weather::WeatherGlyph::Fog: return graphics::WeatherSymbol::Fog;
        case weather::WeatherGlyph::Drizzle: return graphics::WeatherSymbol::Drizzle;
        case weather::WeatherGlyph::Rain: return graphics::WeatherSymbol::Rain;
        case weather::WeatherGlyph::Snow: return graphics::WeatherSymbol::Snow;
        case weather::WeatherGlyph::Thunder: return graphics::WeatherSymbol::Thunder;
        default: return graphics::WeatherSymbol::Unknown;
    }
}
void Glyph(ID2D1DeviceContext* dc, const DisplayItem& item, float x, float y,
           ID2D1SolidColorBrush* brush, const graphics::DesignTokens& tokens, bool updateAvailable,
           graphics::WeatherArtMotion weatherMotion = {}) {
    const float stroke = tokens.glyph.stroke;
    const auto box = D2D1::RectF(x,y,x+18,y+18);
    if (item.kind == ItemKind::Logo) {
        graphics::PaintBrandMark(dc,box,tokens);
        if (updateAvailable) {
            brush->SetColor(tokens.glass.tint.WithAlpha(1).ToD2D());
            dc->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x+18,y-2,x+28,y+11),3,3),brush);
            brush->SetColor(tokens.textPrimary.ToD2D());
            graphics::PaintGlyph(dc,graphics::Glyph::ArrowUp,D2D1::RectF(x+19,y,x+27,y+9),brush,stroke);
        }
    } else if (item.kind == ItemKind::Network) {
        const bool limited = item.connectivity == status::InternetConnectivity::Constrained ||
            item.connectivity == status::InternetConnectivity::LocalOnly;
        graphics::PaintNetworkGlyph(dc,box,brush,NetworkKind(item.connection),item.level,limited,stroke);
    } else if (item.kind == ItemKind::Volume) {
        graphics::PaintVolumeGlyph(dc,box,brush,item.level,item.muted ? std::optional<bool>(true) :
            item.unknown ? std::nullopt : std::optional<bool>(false),stroke);
    } else if (item.kind == ItemKind::Microphone) {
        graphics::PaintMicrophoneGlyph(dc,box,brush,item.unknown ? std::nullopt : std::optional<bool>(item.muted),stroke);
    } else if (item.kind == ItemKind::Battery) {
        graphics::PaintBatteryGlyph(dc,box,brush,tokens,item.level,item.lowOrCritical,
            item.charge == status::ChargeState::Charging,item.powerSource == status::PowerSource::External,stroke);
    } else if (item.kind == ItemKind::Weather) {
        // D78: the condition's own illustration, in the bar's grey; it moves only while the pointer is over it.
        const auto ink = brush->GetColor();
        if (item.weatherCode) graphics::PaintWeatherArt(dc,box,graphics::WeatherSubjectForCode(item.weatherCode,item.isDay),
            graphics::WeatherArtStyle::Monochrome,graphics::Color{ink.r,ink.g,ink.b,ink.a},weatherMotion,stroke,item.cached);
        else graphics::PaintWeatherGlyph(dc,box,brush,WeatherKind(item.weatherGlyph),item.isDay,item.cached,stroke);
    }
}

// D71: the items written on the desktop instead of on a bar. High contrast keeps the bar: its text must sit on the
// system's own window colour.
bool IsTextPresentation(const settings::TopBarSettings& settings, const graphics::DesignTokens& tokens) noexcept {
    return settings.presentation == settings::TopBarPresentation::Text && tokens.mode != graphics::ThemeMode::HighContrast;
}
// How far the halo's shape is widened around the strokes before it is blurred, DIPs.
constexpr UINT32 kHaloSpreadDip = 3;
// One weight for measuring and drawing. Written over a picture, the labels are a little heavier, as a menu bar's are.
DWRITE_FONT_WEIGHT ItemWeight(ItemKind kind, bool written) noexcept {
    return kind == ItemKind::Clock ? DWRITE_FONT_WEIGHT_SEMI_BOLD : written ? DWRITE_FONT_WEIGHT_MEDIUM : DWRITE_FONT_WEIGHT_NORMAL;
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
    // The logo and item menus share one glass presenter for the window's lifetime (shared so a
    // menu callback that destroys the window cannot destroy the presenter under its own Show).
    std::shared_ptr<ui::GlassMenuPresenter> menus;
    bool keyboardMenu = false;
    graphics::TextCache textCache;
    graphics::SymbolRenderer symbols;  // installed Windows icon fonts (the hidden-icons chevron)
    ui::TooltipWindow tooltip;
    ui::accessibility::AccessibleSurface accessibility;
    settings::TopBarSettings settings;
    bool weatherEnabled = false;
    bool updateAvailable = false;
    TopBarSnapshots snapshots;
    std::vector<PlacedItem> placed;
    std::vector<DisplayItem> displayed;
    D2D1_RECT_F panel{};
    bool layoutDirty = true;
    TaskbarReplacementState taskbar;
    graphics::Tween transition{1};
    std::shared_ptr<Gate> gate;
    Connection settingsChanged;
    std::vector<std::function<void()>> stops;
    std::optional<RECT> testBounds;
    std::optional<int> hover;
    std::optional<int> focused;
    // D78: the weather illustration's motion: it starts while the pointer is over the weather item and settles when it
    // leaves (a third of a second each way), never with reduced motion or high contrast. Idle: no frames.
    double weatherClock = 0.0;
    float weatherEnergy = 0.0f;
    double weatherTick = -1.0;
    std::optional<float> pendingVolume;
    bool focusedWindow = false;
    bool keyboardCues = false;     // the focus ring follows keyboard navigation only
    bool autoHidden = false;       // D47: slid away by Auto-Hide (independent of full-screen suppression)
    bool pointerInside = false;    // the pointer is over the bar's window
    bool menuOpen = false;         // one of the bar's menus is running its modal loop
    bool hiddenIconsOpen = false;  // Windows' overflow is open: the chevron points up
    graphics::PressTarget press;   // AS4-11: a release activates only the item its press started on
    graphics::TouchGesture touch{{}, graphics::TouchAxis::Both};
    std::optional<int> touchItem;
    bool registered = false;
    std::optional<RECT> reservedBounds;
    bool relayouting = false;
    bool fullscreen = false;
    bool heldBack = false;
    bool destroyed = false;
    HWINEVENTHOOK foregroundHook = nullptr;
    UINT explorerMessage = 0;
    std::uint64_t frames = 0;
    // D71, the Text presentation: what lies behind the bar on the desktop, the screen it was measured across, whether
    // an application window is under the bar, and what draws the halo under the ink (kept while the device lives).
    std::optional<BackdropProfile> backdrop;
    RECT screen{};
    bool overWindows = false;
    winrt::com_ptr<ID2D1Device> haloDevice;
    winrt::com_ptr<ID2D1DeviceContext> haloRecorder;
    winrt::com_ptr<ID2D1Effect> haloSpread;
    std::array<winrt::com_ptr<ID2D1Effect>, 2> haloEffects;

    void ReleaseHalo() noexcept {
        for (auto& effect : haloEffects) effect = nullptr;
        haloSpread = nullptr;
        haloRecorder = nullptr;
        haloDevice = nullptr;
    }
    // 0: the items laid out from the left (the logo, weather when it is placed there); 1: the status items and the clock.
    [[nodiscard]] int ClusterOf(const PlacedItem& entry) const noexcept {
        return entry.item.kind == ItemKind::Logo ||
               (entry.item.kind == ItemKind::Weather && settings.weatherPlacement == aether::settings::WeatherPlacement::Left) ? 0 : 1;
    }
    // The items in `tokens`' colours at opacity `shown`; `cluster` 0 or 1 draws only that side of the bar.
    void PaintItems(ID2D1DeviceContext* dc, const graphics::DesignTokens& colors, float shown, bool focus, std::optional<int> cluster = {}) {
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        if (FAILED(dc->CreateSolidColorBrush(colors.textPrimary.ToD2D(), brush.put()))) return;
        const bool written = IsTextPresentation(settings, tokens);
        for (const auto& entry : placed) {
            if (cluster && ClusterOf(entry) != *cluster) continue;
            if (focus && focusedWindow && keyboardCues && focused == entry.item.id) {
                const auto inset = D2D1::RectF(entry.rect.left+2,entry.rect.top+3,entry.rect.right-2,entry.rect.bottom-3);
                graphics::PaintFocusRing(dc,inset,6,colors);
            }
            const auto colorOf = [&](bool unknown) { return (unknown ? colors.textSecondary : colors.textPrimary).WithAlpha(shown); };
            const auto color = colorOf(entry.item.unknown);
            brush->SetColor(color.ToD2D());
            if (entry.item.kind == ItemKind::NotificationArea) {
                const float cx = (entry.rect.left + entry.rect.right) * 0.5f, cy = (entry.rect.top + entry.rect.bottom) * 0.5f;
                const auto chevron = hiddenIconsOpen ? graphics::Symbol::ChevronUp : graphics::Symbol::ChevronDown;
                symbols.Draw(dc, chevron, D2D1::RectF(cx - 8, cy - 8, cx + 8, cy + 8), brush.get(), 12.0f);
                continue;
            }
            const bool glyph = entry.item.kind != ItemKind::Keyboard && entry.item.kind != ItemKind::Clock;
            if(entry.item.kind==ItemKind::QuickControls){
                float x=entry.rect.left+10;
                for(const auto& component:entry.item.components){brush->SetColor(colorOf(component.unknown).ToD2D());
                    Glyph(dc,component,x,(entry.rect.top+entry.rect.bottom-18)*0.5f,brush.get(),colors,false);x+=27;}
            }else if (glyph) Glyph(dc, entry.item, entry.rect.left + 10, (entry.rect.top + entry.rect.bottom - 18) * 0.5f,
                brush.get(), colors, updateAvailable, WeatherMotion());
            brush->SetColor(color.ToD2D());
            graphics::TextStyle style; style.size = tokens.fontSizeBar * tokens.textScale;
            style.weight = ItemWeight(entry.item.kind, written);
            style.tabularFigures = entry.item.kind == ItemKind::Clock || entry.item.kind == ItemKind::Battery;
            const float textInset=entry.item.kind==ItemKind::QuickControls?10.0f+static_cast<float>(entry.item.components.size())*27.0f:(glyph?34.0f:10.0f);
            auto textBounds = D2D1::RectF(entry.rect.left + textInset, entry.rect.top,
                entry.rect.right - 10, entry.rect.bottom);
            if (!entry.item.secondaryText.empty()) {
                style.alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
                style.opticalSmall = true;
                style.size = std::min(12.0f * tokens.textScale, (entry.rect.bottom - entry.rect.top) * 0.36f);
                textBounds.bottom = (entry.rect.top + entry.rect.bottom) * 0.5f;
                textCache.Draw(dc, entry.item.text, textBounds, style, brush.get());
                textBounds.top = textBounds.bottom; textBounds.bottom = entry.rect.bottom;
                textCache.Draw(dc, entry.item.secondaryText, textBounds, style, brush.get());
            } else textCache.Draw(dc, entry.item.text, textBounds, style, brush.get());
        }
    }
    // The soft halo under one side's items: their own shapes, widened then blurred, in the tone opposite to the ink.
    // Widening first is what makes it hold: a stroke a pixel or two wide, only blurred, leaves almost nothing around
    // itself. Close to the strokes it carries the contrast asked for; wider, it lifts them off a busy picture.
    // Recorded once per frame and drawn by Direct2D; where the device cannot, the ink is simply drawn without it.
    void PaintHalo(ID2D1DeviceContext* dc, const graphics::DesignTokens& written, int cluster, const graphics::Color& tone, float strength) {
        if (!(strength > 0.01f)) return;
        winrt::com_ptr<ID2D1Device> device;
        dc->GetDevice(device.put());
        if (!device) return;
        if (device != haloDevice || !haloRecorder) {
            ReleaseHalo();
            if (FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, haloRecorder.put()))) return;
            haloDevice = device;
        }
        winrt::com_ptr<ID2D1CommandList> list;
        if (FAILED(haloRecorder->CreateCommandList(list.put()))) return;
        float dpiX = 96.0f, dpiY = 96.0f;
        dc->GetDpi(&dpiX, &dpiY);
        haloRecorder->SetDpi(dpiX, dpiY);
        haloRecorder->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        haloRecorder->SetTarget(list.get());
        haloRecorder->BeginDraw();
        haloRecorder->SetTransform(D2D1::Matrix3x2F::Identity());
        PaintItems(haloRecorder.get(), written, 1.0f, false, cluster);
        const bool recorded = SUCCEEDED(haloRecorder->EndDraw()) && SUCCEEDED(list->Close());
        haloRecorder->SetTarget(nullptr);
        if (!recorded) { ReleaseHalo(); return; }
        if (!haloSpread) {
            if (FAILED(haloRecorder->CreateEffect(CLSID_D2D1Morphology, haloSpread.put()))) { haloSpread = nullptr; return; }
            (void)haloSpread->SetValue(D2D1_MORPHOLOGY_PROP_MODE, D2D1_MORPHOLOGY_MODE_DILATE);
            (void)haloSpread->SetValue(D2D1_MORPHOLOGY_PROP_WIDTH, kHaloSpreadDip);
            (void)haloSpread->SetValue(D2D1_MORPHOLOGY_PROP_HEIGHT, kHaloSpreadDip);
        }
        haloSpread->SetInput(0, list.get());
        // Near the strokes the first layer reaches about the strength asked; the second only softens its end.
        struct Layer { float deviation, gain; };
        static constexpr Layer kLayers[] = {{1.3f, 1.25f}, {4.0f, 0.45f}};
        for (std::size_t index = 0; index < haloEffects.size(); ++index) {
            auto& effect = haloEffects[index];
            if (!effect && FAILED(haloRecorder->CreateEffect(CLSID_D2D1Shadow, effect.put()))) { effect = nullptr; break; }
            effect->SetInputEffect(0, haloSpread.get());
            (void)effect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, kLayers[index].deviation);
            (void)effect->SetValue(D2D1_SHADOW_PROP_COLOR, D2D1::Vector4F(tone.r, tone.g, tone.b, std::clamp(strength * kLayers[index].gain, 0.0f, 1.0f)));
            dc->DrawImage(effect.get());
        }
        haloSpread->SetInput(0, nullptr);   // the frame's recording is not kept
    }
    // D71: the items written straight on what lies behind the bar. Each side takes the ink that reads over its own
    // part of the desktop picture, and a halo of the opposite tone as strong as that part needs.
    void PaintWritten(ID2D1DeviceContext* dc, float shown) {
        const auto bounds = owner.BoundsPx();
        const float screenWidth = static_cast<float>(std::max<LONG>(1, screen.right - screen.left));
        const auto across = [&](float dip) { return (static_cast<float>(bounds.left - screen.left) + owner.ToPx(dip)) / screenWidth; };
        for (int cluster = 0; cluster < 2; ++cluster) {
            float left = 0.0f, right = 0.0f;
            bool any = false;
            for (const auto& entry : placed) {
                if (ClusterOf(entry) != cluster) continue;
                left = any ? std::min(left, entry.rect.left) : entry.rect.left;
                right = any ? std::max(right, entry.rect.right) : entry.rect.right;
                any = true;
            }
            if (!any) continue;
            const auto ink = ChooseTextInk(backdrop ? &*backdrop : nullptr, across(left), across(right), tokens.mode == graphics::ThemeMode::Light);
            const auto colour = ink.dark ? graphics::Color::FromHex(0x0B0C0E) : graphics::Color::FromHex(0xFFFFFF);
            const auto tone = ink.dark ? graphics::Color::FromHex(0xFFFFFF) : graphics::Color::FromHex(0x000000);
            auto written = tokens;
            written.textPrimary = colour;
            written.textSecondary = ink.dark ? graphics::Color::FromHex(0x33353A) : graphics::Color::FromHex(0xD9D9DC);
            written.brandPrimary = written.brandSecondary = colour;   // the mark is written like the rest
            written.glass.tint = tone;                                // what glyphs knock out of one another
            written.glyph.halo = tone;
            PaintHalo(dc, written, cluster, tone, ink.halo * shown);
            PaintItems(dc, written, shown, true, cluster);
        }
    }

    void EmitRequest(Signal<>& request, std::optional<HWND> inputTarget = {}) noexcept {
        try {
            if (inputTarget) {
                auto& ops = context.menuWindowOps ? *context.menuWindowOps : ui::SystemMenuWindowOps();
                const auto restored = RestoreVoiceTypingTarget(ops, owner.Handle(), *inputTarget);
                if (!restored) { ErrorTooltip(restored, static_cast<int>(ItemKind::Microphone)); return; }
            }
            request.Emit();
        }
        catch (...) { AETHER_LOG_ERROR("topbar", "request listener threw an exception"); }
    }

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
        // A control completion may arrive after fullscreen suppression or shutdown admission closed.
        // Keep its model cleanup, but never resurrect a tooltip above a hidden/disabled surface.
        if (fullscreen || !owner.Handle() || !owner.IsShown() || !::IsWindowEnabled(owner.Handle())) return;
        const auto found = std::find_if(placed.begin(), placed.end(), [id](const auto& item) { return item.item.id == id; });
        if (found != placed.end()) tooltip.ShowFor(i18n::TrFormat(L"Action failed: {}", text::Widen(ToString(result.error().code))),
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
    [[nodiscard]] static float Ease(float t) noexcept { return t * t * (3.0f - 2.0f * t); }
    [[nodiscard]] graphics::WeatherArtMotion WeatherMotion() const noexcept { return {weatherClock, Ease(weatherEnergy)}; }
    [[nodiscard]] bool WeatherMotionWanted() const noexcept {
        return pointerInside && hover == static_cast<int>(ItemKind::Weather) && !motion.reduced &&
               tokens.mode != graphics::ThemeMode::HighContrast && !fullscreen && !autoHidden && !heldBack;
    }
    // Advances the weather motion to `now`; true while it still moves or is about to.
    bool StepWeather(double now) noexcept {
        const float target = WeatherMotionWanted() ? 1.0f : 0.0f;
        const double step = weatherTick < 0.0 ? 0.0 : std::clamp(now - weatherTick, 0.0, 0.1);
        weatherTick = now;
        const float change = static_cast<float>(step / 0.33);
        weatherEnergy = target > weatherEnergy ? std::min(target, weatherEnergy + change) : std::max(target, weatherEnergy - change);
        weatherClock += step * Ease(weatherEnergy);
        const bool moving = weatherEnergy > 0.0f || target > 0.0f;
        if (!moving) weatherTick = -1.0;
        return moving;
    }
    // The weather illustration's own pixels (and its halo, written on the desktop): all a motion step redraws.
    [[nodiscard]] std::optional<D2D1_RECT_F> WeatherArtArea() const noexcept {
        const auto found = std::find_if(placed.begin(), placed.end(), [](const auto& entry) { return entry.item.kind == ItemKind::Weather; });
        if (found == placed.end()) return std::nullopt;
        const float margin = IsTextPresentation(settings, tokens) ? 18.0f : 2.0f;
        const float top = (found->rect.top + found->rect.bottom - 18.0f) * 0.5f;
        return D2D1::RectF(found->rect.left + 10.0f - margin, top - margin, found->rect.left + 28.0f + margin, top + 18.0f + margin);
    }
    std::wstring RootName() const override { return std::wstring(i18n::Tr(L"Aether Shell status bar")); }
    CONTROLTYPEID RootControlType() const override { return UIA_ToolBarControlTypeId; }
    std::size_t ItemCount() const override { return placed.size(); }
    ui::accessibility::AccessibleItem Item(std::size_t index) const override {
        const auto& entry = placed.at(index);
        ui::accessibility::AccessibleItem item;
        item.id = entry.item.id; item.name = entry.item.accessibleName; item.helpText = entry.item.tooltip;
        item.itemStatus = i18n::Tr(entry.item.kind == ItemKind::Logo && updateAvailable ? L"Update available" :
            entry.item.cached ? L"Cached" : entry.item.unknown ? L"Unavailable" : L"");
        item.controlType = entry.item.actionable ? UIA_ButtonControlTypeId : UIA_TextControlTypeId;
        item.boundsPx = ItemBounds(entry.rect); item.isFocusable = entry.item.actionable;
        item.isEnabled = !destroyed && owner.IsShown() && !fullscreen && ::IsWindowEnabled(owner.Handle());
        item.hasKeyboardFocus = focusedWindow && focused == item.id;
        item.isOffscreen = !owner.IsShown() || fullscreen;
        item.supportsInvoke = entry.item.actionable;
        return item;
    }
    Result<void> Invoke(int id) override {
        if (destroyed || !owner.IsShown() || fullscreen || !::IsWindowEnabled(owner.Handle()))
            return MakeError(ErrorCode::Unavailable, "top bar is not available for actions");
        const auto found = std::find_if(placed.begin(), placed.end(), [id](const auto& entry) { return entry.item.id == id; });
        if (found == placed.end() || !found->item.actionable)
            return MakeError(ErrorCode::Unsupported, "top bar item has no action");
        if (!::PostMessageW(owner.Handle(), kDeferredInvoke, static_cast<WPARAM>(id), 0))
            return MakeError(ErrorCode::Unavailable, "top bar action could not be queued");
        return Ok();
    }
    Result<void> Toggle(int) override { return MakeError(ErrorCode::Unsupported, "top bar uses invoke actions"); }
    Result<void> SetFocus(int id) override {
        if (destroyed || !owner.IsShown() || fullscreen || !::IsWindowEnabled(owner.Handle()))
            return MakeError(ErrorCode::Unavailable, "top bar is not available for focus");
        if (std::none_of(placed.begin(), placed.end(), [id](const auto& item) { return item.item.id == id && item.item.actionable; }))
            return MakeError(ErrorCode::Unsupported, "top bar item cannot receive focus");
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
        impl_->symbols.Initialize(Device().DWriteFactory());
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
                        alive->owner->ApplyObservedAudio(snapshot);
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
    if (!impl_->context.dispatcher.IsUiThread() || impl_->destroyed) return;
    auto audio = std::move(snapshots.audio);
    // Keep the previous observed output value for the shared change comparison below.
    snapshots.audio = std::move(impl_->snapshots.audio);
    impl_->pendingVolume.reset(); impl_->snapshots = std::move(snapshots);
    ApplyObservedAudio(std::move(audio));
}
void TopBarWindow::ApplyObservedAudio(status::AudioSnapshot snapshot) {
    if (!impl_->context.dispatcher.IsUiThread() || impl_->destroyed) return;
    const bool changed = !status::audio::SameSnapshot(impl_->snapshots.audio, snapshot);
    impl_->snapshots.audio = std::move(snapshot);
    Rebuild();
    // Outbound signals may synchronously destroy this window. Touch no member afterward.
    if (changed) {
        try { AudioObservedChanged.Emit(); }
        catch (...) { AETHER_LOG_ERROR("topbar", "observed audio listener threw an exception"); }
    }
}
Result<void> TopBarWindow::SetUpdateAvailable(bool available) {
    if (!impl_->context.dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "top bar requires UI thread");
    if (impl_->destroyed) return MakeError(ErrorCode::InvalidState, "top bar was destroyed");
    if (impl_->updateAvailable == available) return Ok();
    try { impl_->updateAvailable = available; Rebuild(); return Ok(); }
    catch (...) { return MakeError(ErrorCode::Internal, "update indicator could not be changed"); }
}
void TopBarWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    impl_->tokens = tokens; impl_->motion = motion; impl_->tooltip.SetTheme(tokens, motion);
    if (impl_->menus) impl_->menus->SetTheme(tokens, motion);
    impl_->layoutDirty = true;
    Relayout();
}
Result<void> TopBarWindow::SetTaskbarReplacementState(bool available, bool active, bool suspended) {
    if (!impl_->context.dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "top bar requires UI thread");
    if (impl_->destroyed) return MakeError(ErrorCode::InvalidState, "top bar was destroyed");
    const TaskbarReplacementState next{available, active, suspended};
    if (next == impl_->taskbar) return Ok();
    try { impl_->taskbar = next; Rebuild(); return Ok(); }
    catch (...) { return MakeError(ErrorCode::Internal, "taskbar action state could not be changed"); }
}
const std::vector<PlacedItem>& TopBarWindow::Items() const { return impl_->placed; }
std::uint64_t TopBarWindow::RenderCount() const noexcept { return impl_->frames; }

void TopBarWindow::Rebuild() {
    if (impl_->destroyed || !Handle()) return;
    auto items = BuildDisplayItems(impl_->snapshots, impl_->settings, impl_->weatherEnabled, i18n::FormattingLocale(), impl_->taskbar);
    if (!items) return;
    if (impl_->context.groupQuickControls) { items=GroupQuickControlItems(std::move(*items));if(!items)return; }
    for (auto& item : items.value()) if (!item.secondaryText.empty() &&
        24.0f * impl_->tokens.textScale > impl_->panel.bottom - impl_->panel.top) item.secondaryText.clear();
    if (impl_->updateAvailable) for (auto& item : items.value()) if (item.kind == ItemKind::Logo) {
        item.tooltip += L" \u00B7 " + std::wstring(i18n::Tr(L"Update available")); item.accessibleName = item.tooltip;
    }
    if (!impl_->layoutDirty && items.value() == impl_->displayed) return;
    // The same items in the same order, only some of their contents changed (the clock's digits, every second; a level):
    // no entrance fade over the whole bar, and only what changed is drawn again.
    const auto sameItems = [](const std::vector<DisplayItem>& a, const std::vector<DisplayItem>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](const DisplayItem& x, const DisplayItem& y) {
            return x.id == y.id && x.kind == y.kind;
        });
    };
    const bool structural = impl_->layoutDirty || !sameItems(items.value(), impl_->displayed);
    const auto previous = impl_->placed;
    impl_->displayed = items.value();
    impl_->layoutDirty = false;
    std::vector<float> widths;
    graphics::TextStyle style; style.size = impl_->tokens.fontSizeBar * impl_->tokens.textScale;
    const bool written = IsTextPresentation(impl_->settings, impl_->tokens);
    for (const auto& item : items.value()) {
        if (item.kind == ItemKind::NotificationArea) {
            widths.push_back(30.0f);  // a compact chevron, like Windows' own
            continue;
        }
        const bool glyph = item.kind != ItemKind::Keyboard && item.kind != ItemKind::Clock;
        style.weight = ItemWeight(item.kind, written);
        style.tabularFigures = item.kind == ItemKind::Clock || item.kind == ItemKind::Battery;
        const auto primary = impl_->textCache.Measure(item.text, style).width;
        const auto secondary = item.secondaryText.empty() ? 0.0f :
            std::min(80.0f, impl_->textCache.Measure(item.secondaryText, style).width);
        const float glyphWidth=item.kind==ItemKind::QuickControls?static_cast<float>(item.components.size())*27.0f:(glyph?24.0f:0.0f);
        widths.push_back(std::max(primary, secondary) + glyphWidth + 22.0f);
    }
    impl_->placed = LayoutItems(items.value(), widths, impl_->panel.right - impl_->panel.left,
        impl_->panel.bottom - impl_->panel.top, impl_->settings.weatherPlacement);
    for (auto& item : impl_->placed) {
        item.rect.left += impl_->panel.left; item.rect.right += impl_->panel.left;
        item.rect.top += impl_->panel.top; item.rect.bottom += impl_->panel.top;
    }
    // Only where every item kept its place can the rest of the bar stay as drawn.
    const bool samePlaces = !structural && previous.size() == impl_->placed.size() &&
        std::equal(previous.begin(), previous.end(), impl_->placed.begin(), [](const PlacedItem& a, const PlacedItem& b) {
            return a.item.id == b.item.id && a.rect.left == b.rect.left && a.rect.top == b.rect.top && a.rect.right == b.rect.right &&
                   a.rect.bottom == b.rect.bottom;
        });
    if (structural) {
        if (impl_->motion.reduced || impl_->tokens.mode == graphics::ThemeMode::HighContrast) impl_->transition.SnapTo(1.0f);
        else {
            impl_->transition.SnapTo(0.70f);
            impl_->transition.AnimateTo(1.0f, graphics::FrameScheduler::Now(), impl_->motion.Scale(impl_->tokens.durationFast),
                impl_->tokens.curves.standard);
            RequestAnimationFrame();
        }
        const auto notified = impl_->accessibility.NotifyStructureChanged(); (void)notified;
    }
    std::optional<D2D1_RECT_F> changed;
    for (std::size_t index = 0; index < impl_->placed.size(); ++index) {
        const auto& item = impl_->placed[index];
        const bool itemChanged = !samePlaces || !(previous[index].item == item.item);
        if (!itemChanged) continue;   // its name and value are what assistive technology already has
        const auto property = impl_->accessibility.NotifyPropertyChanged(item.item.id); (void)property;
        if (impl_->hover == item.item.id && Handle() && IsShown() && !impl_->fullscreen && ::IsWindowEnabled(Handle()))
            impl_->tooltip.ShowFor(item.item.tooltip, impl_->ItemBounds(item.rect), ui::TooltipPlacement::Below);
        if (samePlaces) changed = changed ? D2D1::RectF(std::min(changed->left, item.rect.left), std::min(changed->top, item.rect.top),
                                                        std::max(changed->right, item.rect.right), std::max(changed->bottom, item.rect.bottom))
                                          : item.rect;
    }
    if (!samePlaces) Invalidate();
    else if (changed) InvalidateArea(*changed);
}

void TopBarWindow::Relayout() {
    if (!Handle() || impl_->destroyed || impl_->relayouting) return;
    impl_->relayouting = true;
    const auto monitor = platform::PrimaryMonitor();
    const auto dpi = impl_->testBounds ? Dpi() : monitor.dpi;
    const int textHeight = static_cast<int>(std::ceil(impl_->tokens.fontSizeBar * impl_->tokens.textScale * 1.3f + 6.0f));
    const int height = std::clamp(std::max(impl_->settings.height, textHeight), 24, 48);
    // D71: written on the desktop, the bar is flush with the top of the screen and spans it: no margin above it, no
    // shadow around it, no space at its ends.
    const bool written = IsTextPresentation(impl_->settings, impl_->tokens);
    const int margin = impl_->testBounds || written ? 0 : std::clamp(impl_->settings.topMargin, 0, 100);
    const float shadow = written ? 0.0f : std::max(4.0f, std::ceil(graphics::ShadowExtent(impl_->tokens.elevation.bar)));
    const float sides = written ? 0.0f : 12.0f;
    impl_->screen = monitor.bounds;
    RECT base = monitor.workArea;
    if (impl_->testBounds) {
        base = *impl_->testBounds;
        const int side = static_cast<int>(std::lround(sides * static_cast<float>(dpi) / 96.0f));
        base.left -= side; base.right += side;
    } else {
        const auto footprint = ComputeTopBarPlacement(monitor.bounds, dpi, height, margin, shadow, sides);
        if (!footprint) { impl_->relayouting = false; return; }
        // D47: an auto-hidden bar reserves nothing; maximized windows take the screen's top edge.
        // D71: text with no bar under it must not lie over an application: its strip is always kept free of windows.
        if (impl_->context.allowAppBar && (impl_->settings.reserveSpace || written) && !impl_->settings.autoHide) {
            APPBARDATA data{sizeof(data)}; data.hWnd = Handle(); data.uCallbackMessage = kAppBarMessage;
            if (!impl_->registered) impl_->registered = ::SHAppBarMessage(ABM_NEW, &data) != 0;
            if (impl_->registered) {
                data.uEdge = ABE_TOP; data.rc = monitor.bounds;
                data.rc.bottom = data.rc.top + footprint->reservationHeightPx;
                (void)::SHAppBarMessage(ABM_QUERYPOS, &data);
                data.rc.bottom = data.rc.top + footprint->reservationHeightPx;
                if (!impl_->reservedBounds || !::EqualRect(&data.rc, &*impl_->reservedBounds)) {
                    (void)::SHAppBarMessage(ABM_SETPOS, &data);
                    impl_->reservedBounds = data.rc;
                }
                base = data.rc;
            }
        } else impl_->RemoveAppBar();
    }
    const auto placed = ComputeTopBarPlacement(base, dpi, height, margin, shadow, sides);
    if (!placed) { impl_->relayouting = false; return; }
    SetBoundsPx(placed->windowBoundsPx);
    const auto& panel = placed->panelBoundsPx;
    impl_->panel = D2D1::RectF(ToDip(static_cast<float>(panel.left)), ToDip(static_cast<float>(panel.top)),
        ToDip(static_cast<float>(panel.right)), ToDip(static_cast<float>(panel.bottom)));
    SetInputRegionPx(std::span<const RECT>(&panel, 1));
    // HTTRANSPARENT forwards only within one thread. Keep empty margins outside the native
    // HWND region so other processes receive clicks; the outer shadow is deliberately clipped.
    SetVisualRegionPx({});
    impl_->layoutDirty = true;
    impl_->relayouting = false;
    Rebuild();
}

void TopBarWindow::OnExplorerRestarted() { impl_->RemoveAppBar(); Relayout(); }
void TopBarWindow::SetAutoHidden(bool hidden) {
    auto& p = *impl_;
    if (p.autoHidden == hidden || p.destroyed) return;
    p.autoHidden = hidden;
    if (hidden) p.press.Cancel();
    if (!Handle() || p.fullscreen || p.heldBack) return;  // something else already hides it; it decides showing
    // Slides by the panel's own extent so it leaves (and returns over) the screen's top edge.
    graphics::TransitionShape shape;
    shape.offsetY = -(p.panel.bottom + 8.0f);
    if (hidden) {
        p.hover.reset(); p.weatherEnergy = 0.0f; p.pointerInside = false; p.tooltip.HideTooltip();
        HideAnimated(graphics::MakeExitTransition(shape, p.tokens, p.motion));
    } else if (IsHiding()) {
        Show(false);  // reversed mid-way: back at once rather than replaying the entrance
    } else if (!IsShown()) {
        ShowAnimated(graphics::MakeEnterTransition(shape, p.tokens, p.motion));
    }
}
bool TopBarWindow::AutoHidden() const noexcept { return impl_->autoHidden; }
D2D1_RECT_F TopBarWindow::PanelDip() const noexcept { return impl_->panel; }
void TopBarWindow::SetHeldBack(bool held, bool entrance) {
    auto& p = *impl_;
    if (p.heldBack == held || p.destroyed) return;
    p.heldBack = held;
    if (!Handle() || p.fullscreen || p.autoHidden) return;
    if (held) {
        p.press.Cancel(); p.hover.reset(); p.weatherEnergy = 0.0f; p.pointerInside = false; p.tooltip.HideTooltip();
        Hide();
    } else if (!entrance) {
        Show(false);
    } else {
        graphics::TransitionShape shape;
        shape.offsetY = -(p.panel.bottom + 8.0f);
        ShowAnimated(graphics::MakeEnterTransition(shape, p.tokens, p.motion));
    }
}
bool TopBarWindow::Engaged() const noexcept {
    return impl_->pointerInside || impl_->menuOpen || (impl_->focusedWindow && impl_->keyboardCues) ||
           impl_->touch.CurrentPhase() != graphics::TouchGesture::Phase::Idle;
}

bool TopBarWindow::TextPresentation() const noexcept { return IsTextPresentation(impl_->settings, impl_->tokens); }
void TopBarWindow::SetBackdropProfile(std::optional<BackdropProfile> profile) {
    if (impl_->destroyed || impl_->backdrop == profile) return;
    impl_->backdrop = std::move(profile);
    if (Handle() && TextPresentation()) Invalidate();
}
void TopBarWindow::SetOverWindows(bool over) {
    if (impl_->destroyed || impl_->overWindows == over) return;
    impl_->overWindows = over;
    if (Handle() && TextPresentation()) Invalidate();
}
void TopBarWindow::OnDeviceReplaced() { impl_->ReleaseHalo(); Invalidate(); }

void TopBarWindow::SetHiddenIconsOpen(bool open) {
    if (impl_->hiddenIconsOpen == open) return;
    impl_->hiddenIconsOpen = open;
    if (Handle()) Invalidate();
}
bool TopBarWindow::HiddenIconsOpen() const noexcept { return impl_->hiddenIconsOpen; }
bool TopBarWindow::FocusCuesVisible() const noexcept { return impl_->focusedWindow && impl_->keyboardCues && impl_->focused.has_value(); }

void TopBarWindow::ReportItemFailure(ItemKind item, std::wstring_view message) {
    auto& p = *impl_;
    if (p.destroyed || p.fullscreen || !Handle() || !IsShown() || !::IsWindowEnabled(Handle()) || message.empty()) return;
    const int id = static_cast<int>(item);
    const auto found = std::find_if(p.placed.begin(), p.placed.end(), [id](const auto& entry) { return entry.item.id == id; });
    if (found != p.placed.end()) p.tooltip.ShowFor(std::wstring(message), p.ItemBounds(found->rect), ui::TooltipPlacement::Below);
}
void TopBarWindow::SetFullscreenHidden(bool hidden) {
    if (impl_->fullscreen == hidden) return;
    impl_->fullscreen = hidden;
    // State transitions only: never log foreground identities, titles, paths or repeated probes.
    AETHER_LOG_INFO("topbar", "fullscreen suppression {}", hidden);
    if (hidden) { impl_->press.Cancel(); impl_->hover.reset(); impl_->weatherEnergy = 0.0f; impl_->pointerInside = false; impl_->tooltip.HideTooltip(); if (impl_->menus) impl_->menus->Cancel(); Hide(); }
    else if (!impl_->autoHidden && !impl_->heldBack) Show(false);
    try { FullscreenChanged.Emit(hidden); }
    catch (...) { AETHER_LOG_ERROR("topbar", "fullscreen listener threw an exception"); }
}

void TopBarWindow::OnRender(const ui::RenderContext& context) {
    ++impl_->frames;
    const auto panel = impl_->panel;
    // D71: written on the desktop, the bar has no material at all: only its items are drawn. Where Auto-Hide brings
    // it back over an application window, they sit on a square strip of the style's glass, from edge to edge.
    const bool written = IsTextPresentation(impl_->settings, impl_->tokens);
    const bool plate = !written || impl_->overWindows;
    const float radius = written ? 0.0f : impl_->tokens.barCornerRadius;
    // D66: Liquid is the regular glass (the live backdrop adapted by the compositor) under its light; Grain is the
    // frosted glass. Without transparency, or where the compositor offers no glass, Grain.
    const bool blur = plate && impl_->tokens.mode != graphics::ThemeMode::HighContrast && impl_->tokens.transparencyEnabled;
    const bool wantGlass = blur && impl_->tokens.liquid;
    SetBackdropPanel(panel, radius, blur, wantGlass ? impl_->tokens.liquidGlass.blurOpacity : impl_->tokens.glass.backdropOpacity,
                     wantGlass ? &impl_->tokens.liquidGlass : nullptr);
    const float shown = impl_->transition.Value(context.now);
    if (!plate) {
        impl_->PaintWritten(context.d2d, shown);
        return;
    }
    // No exterior shadow: the window's region ends at the panel and would cut it into square corners.
    const bool liquid = wantGlass && BackdropGlassActive() && PaintLiquidPanel(context, panel, radius, 0.0f);
    if (!liquid) {
        graphics::GlassStyle glass;
        glass.cornerRadius = radius; glass.opacity = static_cast<float>(impl_->settings.opacity);
        glass.backdropBlur = BackdropBlurActive(); glass.elevation = graphics::Elevation::Bar;
        graphics::PaintGlassPanel(context.d2d, panel, impl_->tokens, glass);
    }
    // D66: no shadow under the text in Liquid any more: the regular glass keeps its brightness where the text reads,
    // and the labels stay sharp.
    impl_->PaintItems(context.d2d, impl_->tokens, shown, true);
}
bool TopBarWindow::OnAnimate(double now) {
    const bool transition = impl_->transition.IsAnimating(now);
    const bool weather = impl_->StepWeather(now);
    // D78: the weather illustration moving on its own redraws only itself.
    if (weather && !transition) {
        if (const auto area = impl_->WeatherArtArea()) LimitAnimationFrameTo(*area);
    }
    return transition || weather;
}

void TopBarWindow::OnPointerMove(D2D1_POINT_2F point) {
    if (impl_->destroyed || !Handle() || !IsShown() || impl_->fullscreen || !::IsWindowEnabled(Handle())) return;
    impl_->pointerInside = true;
    const auto hovered = HitTest(impl_->placed, point);
    if (hovered == impl_->hover) return;
    const bool weatherInvolved = hovered == static_cast<int>(ItemKind::Weather) || impl_->hover == static_cast<int>(ItemKind::Weather);
    impl_->hover = hovered; Invalidate();
    if (weatherInvolved && (impl_->WeatherMotionWanted() || impl_->weatherEnergy > 0.0f)) RequestAnimationFrame();
    if (!hovered) { impl_->tooltip.HideTooltip(); return; }
    const auto found = std::find_if(impl_->placed.begin(), impl_->placed.end(), [&](const auto& item) { return item.item.id == *hovered; });
    if (found != impl_->placed.end()) impl_->tooltip.ShowFor(found->item.tooltip, impl_->ItemBounds(found->rect), ui::TooltipPlacement::Below);
}
void TopBarWindow::OnPointerLeave() {
    impl_->press.Cancel(); impl_->hover.reset(); impl_->pointerInside = false; impl_->tooltip.HideTooltip(); Invalidate();
    if (impl_->weatherEnergy > 0.0f) RequestAnimationFrame();   // D78: the illustration settles to its still pose
}
void TopBarWindow::OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) {
    impl_->press.Press(static_cast<int>(button), HitTest(impl_->placed, point));
}
void TopBarWindow::OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) {
    if (impl_->keyboardCues) { impl_->keyboardCues = false; Invalidate(); }  // a click hides the focus ring
    if (const auto id = impl_->press.Release(static_cast<int>(button), HitTest(impl_->placed, point))) {
        if (button == ui::PointerButton::Left) Activate(*id);
        else if (button == ui::PointerButton::Right) ShowItemMenu(*id);
    }
}
void TopBarWindow::OnPointerCaptureLost() { impl_->press.Cancel(); }
// FR-040: a tap activates the item it started on, a hold opens that item's menu, any pan does nothing.
bool TopBarWindow::OnTouchDown(const ui::TouchContact& contact) {
    auto& p = *impl_;
    if (p.destroyed || !Handle() || !IsShown() || p.fullscreen || !::IsWindowEnabled(Handle())) return false;
    const bool first = p.touch.CurrentPhase() == graphics::TouchGesture::Phase::Idle;
    (void)p.touch.Down(contact.id, contact.dip.x, contact.dip.y, contact.time);
    if (first) {
        p.touchItem = HitTest(p.placed, contact.dip);
        ::SetTimer(Handle(), kTouchHoldTimer, 550, nullptr);  // past the 0.5 s hold: timers can fire a tick early
    } else ::KillTimer(Handle(), kTouchHoldTimer);
    return true;
}
void TopBarWindow::OnTouchMove(const ui::TouchContact& contact) {
    (void)impl_->touch.Move(contact.id, contact.dip.x, contact.dip.y, contact.time);
    if (impl_->touch.CurrentPhase() != graphics::TouchGesture::Phase::Pending) ::KillTimer(Handle(), kTouchHoldTimer);
}
void TopBarWindow::OnTouchUp(const ui::TouchContact& contact) {
    auto& p = *impl_;
    ::KillTimer(Handle(), kTouchHoldTimer);
    const auto event = p.touch.Up(contact.id, contact.dip.x, contact.dip.y, contact.time);
    const auto started = p.touchItem;
    p.touchItem.reset();
    if (event != graphics::TouchGesture::Event::Tap || !started || started != HitTest(p.placed, contact.dip)) return;
    if (p.keyboardCues) { p.keyboardCues = false; Invalidate(); }
    Activate(*started);
}
void TopBarWindow::OnTouchCancel() {
    (void)impl_->touch.Cancel();
    impl_->touchItem.reset();
    if (Handle()) ::KillTimer(Handle(), kTouchHoldTimer);
}
void TopBarWindow::OnWheel(float notches, D2D1_POINT_2F point) {
    if (impl_->destroyed || !IsShown() || impl_->fullscreen || !::IsWindowEnabled(Handle()) ||
        HitTest(impl_->placed, point) != static_cast<int>(ItemKind::Volume) || !impl_->context.audio ||
        impl_->snapshots.audio.state != status::ProviderState::Available || !impl_->snapshots.audio.deviceAvailable ||
        !impl_->snapshots.audio.volume || !std::isfinite(*impl_->snapshots.audio.volume)) return;
    const auto target = WheelVolumeTarget(impl_->snapshots.audio.volume, impl_->pendingVolume, notches);
    if (!target) return;
    impl_->pendingVolume = target.value();
    impl_->context.audio->SetVolumeAsync(target.value(),
        impl_->Completion(static_cast<int>(ItemKind::Volume)));
}
void TopBarWindow::Activate(int id) {
    if (impl_->destroyed || !IsShown() || impl_->fullscreen || !::IsWindowEnabled(Handle())) return;
    const auto item = std::find_if(impl_->placed.begin(), impl_->placed.end(), [id](const auto& entry) { return entry.item.id == id; });
    if (item == impl_->placed.end()) return;
    if(id==static_cast<int>(ItemKind::QuickControls)){impl_->tooltip.HideTooltip();try{QuickControlsRequested.Emit(impl_->ItemBounds(item->rect));}catch(...){impl_->ErrorTooltip(MakeError(ErrorCode::Internal,"quick controls unavailable"),id);}return;}
    if (id == static_cast<int>(ItemKind::Keyboard)) impl_->EmitRequest(KeyboardRequested);
    else if (id == static_cast<int>(ItemKind::Network)) impl_->EmitRequest(NetworkRequested);
    else if (id == static_cast<int>(ItemKind::Weather)) {
        impl_->tooltip.HideTooltip();
        try { WeatherPanelRequested.Emit(impl_->ItemBounds(item->rect)); }
        catch (...) { impl_->ErrorTooltip(MakeError(ErrorCode::Internal, "weather unavailable"), id); }
    } else if (id == static_cast<int>(ItemKind::Volume)) {
        if (impl_->context.audio) impl_->context.audio->ToggleMuteAsync(impl_->Completion(id));
        else impl_->ErrorTooltip(MakeError(ErrorCode::Unavailable, "audio unavailable"), id);
    } else if (id == static_cast<int>(ItemKind::Microphone)) {
        if (impl_->context.microphone) impl_->context.microphone->ToggleMuteAsync(impl_->Completion(id));
        else impl_->ErrorTooltip(MakeError(ErrorCode::Unavailable, "microphone unavailable"), id);
    } else if (id == static_cast<int>(ItemKind::Clock)) impl_->EmitRequest(ClockRequested);
    else if (id == static_cast<int>(ItemKind::Battery)) impl_->EmitRequest(PowerSettingsRequested);
    else if (id == static_cast<int>(ItemKind::NotificationArea)) {
        // Windows' overflow only (recovery lives in the logo menu), placed below this chevron.
        impl_->tooltip.HideTooltip();
        try { HiddenIconsRequested.Emit(impl_->ItemBounds(item->rect)); }
        catch (...) { AETHER_LOG_ERROR("topbar", "request listener threw an exception"); }
    } else if (id == static_cast<int>(ItemKind::Logo)) ShowItemMenu(id);
}
void TopBarWindow::ShowItemMenu(int id) { ShowItemMenu(id, impl_->keyboardMenu); }
void TopBarWindow::ShowItemMenu(int id, bool keyboard) {
    if (impl_->destroyed || !IsShown() || impl_->fullscreen || !::IsWindowEnabled(Handle())) return;
    if(id==static_cast<int>(ItemKind::QuickControls)){Activate(id);return;}
    const auto found = std::find_if(impl_->placed.begin(), impl_->placed.end(), [id](const auto& entry) {
        return entry.item.id == id;
    });
    if (found == impl_->placed.end()) return;
    auto entries = BuildTopBarMenu(found->item.kind, impl_->taskbar, impl_->updateAvailable);
    if (entries.empty()) return;
    const auto bound = impl_->ItemBounds(found->rect);
    ui::MenuRequest request; request.owner = Handle(); request.anchor = bound;
    request.placement = ui::MenuPlacement::BelowAnchor; request.restore = ui::ForegroundRestore::Always;
    request.keyboard = keyboard;
    if (!impl_->context.menu && !impl_->menus)
        impl_->menus = std::make_shared<ui::GlassMenuPresenter>(impl_->context.device, impl_->context.scheduler, impl_->tokens, impl_->motion);
    const auto presenterOwner = impl_->menus;  // modal reentrancy must not destroy the active presenter
    ui::IMenuPresenter* presenter = impl_->context.menu ? impl_->context.menu : presenterOwner.get();
    std::weak_ptr<Impl::Gate> weak = impl_->gate;
    impl_->tooltip.HideTooltip();
    impl_->menuOpen = true;  // keeps an auto-hiding bar in place while its menu runs
    const auto selected = presenter->Show(entries, request);
    const auto alive = weak.lock();
    if (alive && alive->owner) alive->owner->impl_->menuOpen = false;
    if (!alive || !alive->owner || !selected) return;
    auto& owner = *alive->owner;
    auto& state = *owner.impl_;
    if (state.destroyed || !owner.IsShown() || state.fullscreen || !::IsWindowEnabled(owner.Handle())) return;
    if (std::none_of(state.placed.begin(), state.placed.end(), [id](const auto& item) {
        return item.item.id == id && item.item.actionable;
    })) return;
    // A callback may have changed availability while the native menu pumped messages. Rebuild the
    // command model and reject stale commands before emitting any request.
    const auto current = BuildTopBarMenu(static_cast<ItemKind>(id), state.taskbar, state.updateAvailable);
    const auto* command = ui::FindMenuCommand(current, selected->command);
    if (!command || !command->enabled) return;
    switch (static_cast<BarCommand>(selected->command)) {
        case BarCommand::Search: state.EmitRequest(owner.SearchRequested); break;
        case BarCommand::Settings: state.EmitRequest(owner.SettingsRequested); break;
        case BarCommand::CheckUpdates: state.EmitRequest(owner.CheckForUpdatesRequested); break;
        case BarCommand::About: state.EmitRequest(owner.AboutRequested); break;
        case BarCommand::Quit: state.EmitRequest(owner.QuitRequested); break;
        case BarCommand::ViewUpdate: state.EmitRequest(owner.UpdatesRequested); break;
        case BarCommand::Keyboard: state.EmitRequest(owner.KeyboardRequested); break;
        case BarCommand::Network: state.EmitRequest(owner.NetworkRequested); break;
        case BarCommand::Weather: state.EmitRequest(owner.WeatherRequested); break;
        case BarCommand::ToggleVolume: owner.Activate(static_cast<int>(ItemKind::Volume)); break;
        case BarCommand::SoundSettings: state.EmitRequest(owner.SoundSettingsRequested); break;
        case BarCommand::ToggleMicrophone: owner.Activate(static_cast<int>(ItemKind::Microphone)); break;
        case BarCommand::MicrophoneSettings: state.EmitRequest(owner.MicrophoneSettingsRequested); break;
        case BarCommand::VoiceTyping:
            state.EmitRequest(owner.VoiceTypingRequested, std::optional<HWND>{selected->previousForeground}); break;
        case BarCommand::PowerSettings: state.EmitRequest(owner.PowerSettingsRequested); break;
        case BarCommand::Clock: state.EmitRequest(owner.ClockRequested); break;
        case BarCommand::Calendar: state.EmitRequest(owner.CalendarRequested); break;
        case BarCommand::HiddenIcons: owner.Activate(static_cast<int>(ItemKind::NotificationArea)); break;
        case BarCommand::RevealWindowsTaskbar: state.EmitRequest(owner.RevealWindowsTaskbarRequested); break;
        case BarCommand::ResumeReplacement: state.EmitRequest(owner.ResumeTaskbarReplacementRequested); break;
    }
}
bool TopBarWindow::OnKeyDown(UINT key) {
    if ((key == VK_APPS || (key == VK_F10 && (::GetKeyState(VK_SHIFT) & 0x8000))) &&
        impl_->focused) {
        ShowItemMenu(*impl_->focused, true); return true;
    }
    std::vector<int> actions;
    for (const auto& item : impl_->placed) if (item.item.actionable) actions.push_back(item.item.id);
    if (actions.empty()) return false;
    if (key == VK_ESCAPE) { impl_->focused.reset(); impl_->keyboardCues = false; ::SetFocus(nullptr); impl_->tooltip.HideTooltip(); Invalidate(); return true; }
    if (key == VK_RETURN || key == VK_SPACE) {
        impl_->keyboardCues = true;
        if (impl_->focused) {
            // A menu opened from the keyboard (the logo) starts with its first entry selected.
            impl_->keyboardMenu = true;
            Activate(*impl_->focused);
            impl_->keyboardMenu = false;
        }
        return true;
    }
    if (key != VK_LEFT && key != VK_RIGHT && key != VK_TAB && key != VK_HOME && key != VK_END) return false;
    impl_->keyboardCues = true;
    auto found = std::find(actions.begin(), actions.end(), impl_->focused.value_or(0));
    int index = found == actions.end() ? 0 : static_cast<int>(found - actions.begin());
    if (key == VK_HOME) index = 0;
    else if (key == VK_END) index = static_cast<int>(actions.size()) - 1;
    else index = (index + ((key == VK_LEFT || (key == VK_TAB && (::GetKeyState(VK_SHIFT) & 0x8000))) ? -1 : 1) +
        static_cast<int>(actions.size())) % static_cast<int>(actions.size());
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
        // The shell's geometric notification may include a maximized framed window while the
        // native taskbar auto-hides. It is a trigger, never an independent suppression authority.
        // Queue the same current-foreground classification used by the WinEvent hook; lParam may
        // already describe an old foreground window by the time this message is dispatched.
        else if (wParam == ABN_FULLSCREENAPP) (void)::PostMessageW(Handle(), kForegroundChanged, 0, 0);
        return 0;
    }
    if (message == kForegroundChanged) {
        const bool fullscreen = impl_->context.fullscreenProbe ? impl_->context.fullscreenProbe() :
            impl_->context.allowAppBar && platform::IsFullscreenWindow(::GetForegroundWindow(), platform::MonitorForWindow(Handle()));
        SetFullscreenHidden(fullscreen);
        return 0;
    }
    if (message == kDeferredInvoke) { Activate(static_cast<int>(wParam)); return 0; }
    if (message == WM_TIMER && wParam == kTouchHoldTimer) {
        ::KillTimer(Handle(), kTouchHoldTimer);
        if (impl_->touch.Tick(graphics::FrameScheduler::Now()) == graphics::TouchGesture::Event::HoldStarted && impl_->touchItem)
            ShowItemMenu(*impl_->touchItem);
        return 0;
    }
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
    if (impl_->menus) impl_->menus->Cancel();
    impl_->settingsChanged.Disconnect();
    if (impl_->foregroundHook) { ::UnhookWinEvent(impl_->foregroundHook); impl_->foregroundHook = nullptr; }
    std::erase(watchedWindows, Handle());
    for (auto& stop : impl_->stops) stop();
    impl_->stops.clear();
    impl_->ReleaseHalo();
    impl_->RemoveAppBar(); impl_->accessibility.Disconnect(); impl_->tooltip.Destroy();
}

}  // namespace aether::topbar
