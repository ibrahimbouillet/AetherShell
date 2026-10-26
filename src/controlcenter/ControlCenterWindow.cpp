// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "controlcenter/ControlCenterWindow.h"

#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
#include "graphics/SystemSymbols.h"
#include "graphics/TextCache.h"
#include "platform/windows/MonitorUtilities.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace aether::controlcenter {
namespace {

using graphics::Color;
using graphics::Symbol;

// Geometry in DIPs, measured from the owner's reference panel (relative proportions, not pixels).
constexpr float kMargin = 8.0f;          // transparent edge around the panel (entrance travel)
constexpr float kPanelWidth = 360.0f;
constexpr float kRadius = 8.0f;
constexpr float kGridHeight = 215.0f;    // two tile rows with labels and the page column
constexpr float kSliderHeight = 125.0f;  // brightness and volume
constexpr float kFooterHeight = 48.0f;   // battery and Settings
constexpr float kBodyHeight = kGridHeight + kSliderHeight + kFooterHeight;
constexpr float kMediaHeight = 72.0f;    // only while a real media session exists
constexpr float kTileWidth = 96.0f, kTileHeight = 48.0f, kTileLeft = 22.0f, kTileGap = 13.0f;
constexpr float kRowTop = 23.0f, kRowPitch = 97.0f, kSplitMain = 50.0f;
constexpr float kPageColumn = 348.0f;
constexpr float kTrackLeft = 56.0f, kTrackRight = 301.0f;
constexpr float kBrightnessRow = 37.5f, kVolumeRow = 90.5f;  // centres below the grid
constexpr float kDetailRowTop = 68.0f, kDetailRowHeight = 42.0f;

constexpr UINT kInvoke = WM_APP + 211;
constexpr UINT kRange = WM_APP + 212;
constexpr UINT_PTR kRefresh = 41, kVolumeCommit = 42, kPendingExpiry = 43;
constexpr double kPendingSeconds = 1.5;  // an unconfirmed slider request is shown at most this long

// Stable item identities (also UI Automation ids "item-N").
constexpr int kTile = 100, kChevron = 200;
constexpr int kBrightness = 300, kVolume = 301, kMute = 302, kOutput = 310;
constexpr int kPrevious = 400, kPlay = 401, kNext = 402, kSession = 403;
constexpr int kBattery = 500, kSettings = 501;
constexpr int kNextPage = 600, kPageDot = 601;
constexpr int kBack = 700, kDetailSwitch = 711, kDetailRow = 720;

constexpr std::array<SystemControl, 12> kTiles{SystemControl::WiFi, SystemControl::Bluetooth,
    SystemControl::FlightMode, SystemControl::Accessibility, SystemControl::EnergySaver,
    SystemControl::LiveCaptions, SystemControl::NightLight, SystemControl::MobileHotspot,
    SystemControl::NearbySharing, SystemControl::Cast, SystemControl::Project, SystemControl::Settings};
constexpr int kPages = 2;

enum class Detail { None, WiFi, Bluetooth, Accessibility, Output };

const wchar_t* Label(SystemControl value) noexcept {
    switch (value) {
        case SystemControl::WiFi: return L"Wi-Fi";
        case SystemControl::Bluetooth: return L"Bluetooth";
        case SystemControl::FlightMode: return L"Airplane mode";
        case SystemControl::Accessibility: return L"Accessibility";
        case SystemControl::EnergySaver: return L"Energy saver";
        case SystemControl::LiveCaptions: return L"Live captions";
        case SystemControl::NightLight: return L"Night light";
        case SystemControl::MobileHotspot: return L"Mobile hotspot";
        case SystemControl::NearbySharing: return L"Nearby sharing";
        case SystemControl::Cast: return L"Cast";
        case SystemControl::Project: return L"Project";
        case SystemControl::Settings: return L"All settings";
    }
    return L"Settings";
}

Symbol SymbolFor(SystemControl value) noexcept {
    switch (value) {
        case SystemControl::WiFi: return Symbol::WiFi;
        case SystemControl::Bluetooth: return Symbol::Bluetooth;
        case SystemControl::FlightMode: return Symbol::Airplane;
        case SystemControl::Accessibility: return Symbol::Accessibility;
        case SystemControl::EnergySaver: return Symbol::EnergySaver;
        case SystemControl::LiveCaptions: return Symbol::LiveCaptions;
        case SystemControl::NightLight: return Symbol::NightLight;
        case SystemControl::MobileHotspot: return Symbol::Hotspot;
        case SystemControl::NearbySharing: return Symbol::NearbySharing;
        case SystemControl::Cast: return Symbol::Cast;
        case SystemControl::Project: return Symbol::Project;
        case SystemControl::Settings: return Symbol::Settings;
    }
    return Symbol::Settings;
}

// Tiles whose only documented route is a Windows Settings page: never drawn as an on/off toggle.
bool OpensSettings(SystemControl value) noexcept {
    return value == SystemControl::FlightMode || value == SystemControl::EnergySaver || value == SystemControl::NightLight ||
           value == SystemControl::MobileHotspot || value == SystemControl::NearbySharing;
}

bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p) noexcept {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
D2D1_POINT_2F Center(const D2D1_RECT_F& r) noexcept { return D2D1::Point2F((r.left + r.right) / 2.0f, (r.top + r.bottom) / 2.0f); }
D2D1_RECT_F Box(float cx, float cy, float size) noexcept {
    return D2D1::RectF(cx - size / 2.0f, cy - size / 2.0f, cx + size / 2.0f, cy + size / 2.0f);
}
Color Mix(const Color& a, const Color& b, float t) noexcept {
    t = std::clamp(t, 0.0f, 1.0f);
    return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Color White(float alpha) noexcept { return Color{1.0f, 1.0f, 1.0f, alpha}; }
Color Black(float alpha) noexcept { return Color{0.0f, 0.0f, 0.0f, alpha}; }

// The neutral control colors of the panel per mode (Windows' quick-settings hierarchy over Aether's
// own frosted material). High contrast uses the opaque system roles.
struct Palette {
    Color tile, tileHover, tilePressed, tileStroke, tileDisabled;
    Color divider, footer, track, thumb, thumbStroke, focusFill;
};
Palette MakePalette(const graphics::DesignTokens& tokens) noexcept {
    if (tokens.mode == graphics::ThemeMode::HighContrast) {
        const auto& s = tokens.surface;
        return Palette{s.controlFill, s.controlFillHover, s.controlFillPressed, s.strongStroke, s.controlFillDisabled,
                       s.strongStroke, s.layer, s.strongStroke, s.controlFill, s.strongStroke, s.controlFill};
    }
    if (tokens.mode == graphics::ThemeMode::Light) {
        return Palette{White(0.70f), White(0.50f), White(0.30f), Black(0.06f), White(0.30f),
                       Black(0.08f), Black(0.035f), Black(0.45f), Color::FromHex(0xFFFFFF), Black(0.08f), White(0.5f)};
    }
    return Palette{White(0.061f), White(0.090f), White(0.040f), White(0.075f), White(0.030f),
                   White(0.083f), Black(0.14f), White(0.54f), Color::FromHex(0x454545), White(0.09f), White(0.06f)};
}

struct Target {
    int id = 0;
    D2D1_RECT_F rect{};  // window DIPs (panel scale applied by the render transform)
    std::wstring name;
    bool enabled = true;
    bool slider = false;
    std::optional<int> value;          // observed reading of a slider
    std::optional<ToggleState> toggle; // genuine two-state controls only
    std::wstring help;
    float radius = 4.0f;
};

struct Pending {
    int value = 0;
    double since = 0.0;
};

}  // namespace

struct ControlCenterWindow::Impl final : ui::accessibility::IAccessibleSurfaceModel {
    ControlCenterWindow& owner;
    IUiDispatcher& dispatcher;
    ControlCenterState state;
    graphics::DesignTokens tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    graphics::MotionPolicy motion;
    graphics::TextCache text;
    graphics::SymbolRenderer symbols;
    ui::accessibility::AccessibleSurface accessible;
    std::vector<Target> targets;
    std::vector<Target>* building = nullptr;          // Build() target
    const std::vector<Target>* painting = nullptr;    // an outgoing view being painted
    float scale = 1.0f;  // fit into a small work area
    int page = 0;
    Detail detail = Detail::None, fromView = Detail::None;  // the current view and the one it replaced
    std::optional<int> hovered, focused, pressedId, dragging, dragValue, volumePending;
    bool cues = false;  // keyboard focus cues
    std::map<int, Pending> requested;
    std::map<int, graphics::Tween> hover, press, on, thumbs;
    graphics::Tween pageMotion{0.0f};
    graphics::Tween viewMotion{1.0f};  // 0 just changed .. 1 settled on `detail`
    float viewDirection = 1.0f;        // +1 into a detail view, -1 back to the main view
    bool destroyed = false, volumeTimerArmed = false, closing = false;
    std::uint64_t renders = 0;

    explicit Impl(ControlCenterWindow& window, IUiDispatcher& ui) : owner(window), dispatcher(ui) {}

    // --- geometry ---
    [[nodiscard]] bool MediaShown() const { return Session() != nullptr; }
    [[nodiscard]] float BodyTop() const { return kMargin + (MediaShown() ? kMediaHeight : 0.0f); }
    [[nodiscard]] float PanelHeight() const { return (MediaShown() ? kMediaHeight : 0.0f) + kBodyHeight; }
    [[nodiscard]] D2D1_RECT_F PanelRect() const { return D2D1::RectF(kMargin, kMargin, kMargin + kPanelWidth, kMargin + PanelHeight()); }
    [[nodiscard]] float FooterTop() const { return BodyTop() + kGridHeight + kSliderHeight; }
    [[nodiscard]] D2D1_RECT_F TileRect(int slot, float pageShift = 0.0f) const {
        const float x = kMargin + kTileLeft + static_cast<float>(slot % 3) * (kTileWidth + kTileGap);
        const float y = BodyTop() + kRowTop + static_cast<float>(slot / 3) * kRowPitch + pageShift;
        return D2D1::RectF(x, y, x + kTileWidth, y + kTileHeight);
    }
    [[nodiscard]] float SliderCenter(int id) const { return BodyTop() + kGridHeight + (id == kBrightness ? kBrightnessRow : kVolumeRow); }
    [[nodiscard]] D2D1_RECT_F DetailRow(int index) const {
        const float top = BodyTop() + kDetailRowTop + static_cast<float>(index) * kDetailRowHeight;
        return D2D1::RectF(kMargin + 10.0f, top, kMargin + kPanelWidth - 10.0f, top + kDetailRowHeight - 2.0f);
    }

    // --- model ---
    const MediaSession* Session() const {
        if (!state.mediaAvailable || !state.media.selectedSession) return nullptr;
        const auto found = std::find_if(state.media.sessions.begin(), state.media.sessions.end(),
            [&](const auto& session) { return session.key == *state.media.selectedSession; });
        return found == state.media.sessions.end() ? nullptr : &*found;
    }
    std::optional<int> Volume() const {
        if (state.audio.state != status::ProviderState::Available || !state.audio.deviceAvailable ||
            !state.audio.volume || !std::isfinite(*state.audio.volume)) return std::nullopt;
        return static_cast<int>(std::lround(std::clamp(*state.audio.volume, 0.0f, 1.0f) * 100));
    }
    std::optional<bool> Muted() const {
        if (state.audio.state != status::ProviderState::Available || !state.audio.deviceAvailable) return std::nullopt;
        return state.audio.muted;
    }
    const RadioStatus* Radio(SystemControl control) const {
        if (control == SystemControl::WiFi) return &state.system.wifi;
        if (control == SystemControl::Bluetooth) return &state.system.bluetooth;
        return nullptr;
    }
    // A granted session permission or a consent request Windows can still show (the host asks first).
    static bool Switchable(const RadioStatus& radio) { return radio.canToggle || radio.canRequestAccess; }
    // Only a genuine reading is "on"; a settings-only tile shows its real state where Windows reports
    // one (energy saver) and is otherwise neutral.
    std::optional<bool> TileState(SystemControl control) const {
        if (const auto* radio = Radio(control)) return radio->enabled;
        if (control == SystemControl::EnergySaver) return state.system.energySaver;
        return std::nullopt;
    }
    bool TileEnabled(SystemControl control) const {
        if (control == SystemControl::LiveCaptions) return state.system.windowsBuild >= 22621;
        return true;
    }
    std::wstring TileCaption(SystemControl control) const {
        if (control == SystemControl::WiFi) {
            const auto& radio = state.system.wifi;
            const bool wireless = state.network.state == status::ProviderState::Available && state.network.kind == status::ConnectionKind::WiFi;
            // Privacy (FR-008): never the network name, only the connection state.
            if (radio.enabled == true) return wireless ? L"Connected" : L"Not connected";
            return L"Wi-Fi";
        }
        return Label(control);
    }
    std::wstring TileName(SystemControl control) const {
        std::wstring name = Label(control);
        const auto value = TileState(control);
        if (Radio(control)) name += value ? (*value ? L", on" : L", off") : L", state unknown";
        if (control == SystemControl::WiFi && value == true) name += L", " + TileCaption(control);
        if (control == SystemControl::EnergySaver && value) name += *value ? L", on" : L", off";
        if (OpensSettings(control)) name += L", opens Windows Settings";
        if (control == SystemControl::Accessibility) name += L", shows accessibility features";
        if (!TileEnabled(control)) name += L", requires Windows 11 version 22H2";
        return name;
    }
    std::optional<int> Displayed(int id, std::optional<int> observed, double now) const {
        if (dragging == id && dragValue) return dragValue;
        // An in-flight request is shown until Windows reports it (or for 1.5 s); never claimed done.
        if (const auto found = requested.find(id); found != requested.end() && now - found->second.since < kPendingSeconds) return found->second.value;
        return observed;
    }

    void Add(int id, D2D1_RECT_F rect, std::wstring name, bool enabled = true, float radius = 4.0f) {
        Target target;
        target.id = id;
        target.rect = rect;
        target.name = std::move(name);
        target.enabled = enabled;
        target.radius = radius;
        building->push_back(std::move(target));
    }

    // The targets of one view (None: the main view). Painting an outgoing view during a transition
    // uses its own list; input always uses `targets`, the current view.
    void Build(Detail view, std::vector<Target>& out) {
        building = &out;
        const float left = kMargin, right = kMargin + kPanelWidth;
        if (const auto* session = Session()) {
            Add(kSession, D2D1::RectF(left + 12.0f, kMargin + 10.0f, left + 232.0f, kMargin + 58.0f),
                L"Now playing: " + (session->title.empty() ? std::wstring(L"Untitled media") : session->title) +
                    (state.media.sessions.size() > 1 ? L", switch source" : L""),
                state.media.sessions.size() > 1);
            const float cy = kMargin + 34.0f;
            Add(kPrevious, Box(left + 258.0f, cy, 32.0f), L"Previous media item", session->canPrevious);
            Add(kPlay, Box(left + 296.0f, cy, 32.0f), session->playback == MediaPlayback::Playing ? L"Pause" : L"Play",
                session->canTogglePlayPause);
            Add(kNext, Box(left + 334.0f, cy, 32.0f), L"Next media item", session->canNext);
        }
        if (view == Detail::None) {
            for (int slot = 0; slot < 6; ++slot) {
                const int index = page * 6 + slot;
                const auto control = kTiles[static_cast<std::size_t>(index)];
                const auto tile = TileRect(slot);
                const bool split = control == SystemControl::WiFi || control == SystemControl::Bluetooth;
                Target main;
                main.id = kTile + index;
                main.rect = split ? D2D1::RectF(tile.left, tile.top, tile.left + kSplitMain, tile.bottom) : tile;
                main.name = TileName(control);
                main.enabled = TileEnabled(control);
                if (const auto value = TileState(control); Radio(control)) {
                    main.toggle = value ? (*value ? ToggleState_On : ToggleState_Off) : ToggleState_Indeterminate;
                }
                out.push_back(main);
                if (split) {
                    Add(kChevron + index, D2D1::RectF(tile.left + kSplitMain, tile.top, tile.right, tile.bottom),
                        std::wstring(Label(control)) + L" details");
                }
            }
            const float top = BodyTop();
            for (int dot = 0; dot < kPages; ++dot) {
                Add(kPageDot + dot, Box(left + kPageColumn, top + 99.0f + 13.0f * static_cast<float>(dot), 14.0f),
                    L"Page " + std::to_wstring(dot + 1) + L" of " + std::to_wstring(kPages));
            }
            Add(kNextPage, Box(left + kPageColumn, top + 130.0f, 20.0f), page + 1 < kPages ? L"Next page" : L"First page");
            const auto add = [&](int id, std::wstring name, bool enabled, std::optional<int> value) {
                const float cy = SliderCenter(id);
                Target slider;
                slider.id = id;
                slider.rect = D2D1::RectF(left + kTrackLeft - 10.0f, cy - 16.0f, left + kTrackRight + 10.0f, cy + 16.0f);
                slider.name = std::move(name);
                slider.enabled = enabled;
                slider.slider = true;
                slider.value = value;
                slider.radius = 16.0f;
                out.push_back(slider);
            };
            const bool brightness = state.brightness.canSet && state.brightness.percent.has_value();
            add(kBrightness, brightness ? L"Brightness" : L"Brightness, unavailable on this display", brightness, state.brightness.percent);
            add(kVolume, Volume() ? L"Volume" : L"Volume, unavailable", Volume().has_value(), Volume());
            const float vy = SliderCenter(kVolume);
            Add(kMute, Box(left + 32.0f, vy, 32.0f), Muted().value_or(false) ? L"Unmute" : L"Mute", Muted().has_value());
            Add(kOutput, D2D1::RectF(left + 304.0f, vy - 16.0f, left + 348.0f, vy + 16.0f), L"Sound output and volume mixer");
        } else {
            const float top = BodyTop();
            Add(kBack, D2D1::RectF(left + 10.0f, top + 12.0f, left + 42.0f, top + 44.0f), L"Back");
            const auto row = [&](int index, std::wstring name, bool enabled = true) {
                Add(kDetailRow + index, DetailRow(index), std::move(name), enabled, 6.0f);
            };
            if (view == Detail::WiFi || view == Detail::Bluetooth) {
                const auto control = view == Detail::WiFi ? SystemControl::WiFi : SystemControl::Bluetooth;
                const auto value = TileState(control);
                Target toggle;
                toggle.id = kDetailSwitch;
                toggle.rect = D2D1::RectF(right - 62.0f, top + 16.0f, right - 14.0f, top + 40.0f);
                toggle.name = Label(control);
                toggle.enabled = value.has_value() && Switchable(*Radio(control));
                toggle.toggle = value ? (*value ? ToggleState_On : ToggleState_Off) : ToggleState_Indeterminate;
                toggle.radius = 12.0f;
                out.push_back(toggle);
                if (view == Detail::WiFi) {
                    row(0, L"Show available networks");
                    row(1, L"More Wi-Fi settings");
                } else {
                    row(0, L"Bluetooth and devices settings");
                }
            } else if (view == Detail::Accessibility) {
                row(0, L"Magnifier");
                row(1, L"Narrator");
                row(2, L"Live captions", state.system.windowsBuild >= 22621);
                row(3, L"On-screen keyboard");
                Target sticky;
                sticky.id = kDetailRow + 4;
                sticky.rect = DetailRow(4);
                sticky.name = L"Sticky keys";
                sticky.enabled = state.system.stickyKeys.has_value();
                sticky.toggle = state.system.stickyKeys ? (*state.system.stickyKeys ? ToggleState_On : ToggleState_Off)
                                                        : ToggleState_Indeterminate;
                sticky.radius = 6.0f;
                out.push_back(sticky);
                row(5, L"More accessibility settings");
            } else if (view == Detail::Output) {
                Target mute;
                mute.id = kDetailRow;
                mute.rect = DetailRow(0);
                mute.name = L"Mute";
                mute.enabled = Muted().has_value();
                mute.toggle = Muted() ? (*Muted() ? ToggleState_On : ToggleState_Off) : ToggleState_Indeterminate;
                mute.radius = 6.0f;
                out.push_back(mute);
                row(1, L"Volume mixer");
                row(2, L"Choose output in Sound settings");
            }
        }
        const float footer = FooterTop();
        const bool batteryKnown = state.battery.state == status::ProviderState::Available;
        if (!batteryKnown || state.battery.batteryPresent) {
            std::wstring name = L"Power and battery settings";
            if (batteryKnown && state.battery.percent) name = L"Battery " + std::to_wstring(*state.battery.percent) + L" percent, " + name;
            Add(kBattery, D2D1::RectF(left + 12.0f, footer + 8.0f, left + 124.0f, footer + 40.0f), std::move(name));
        }
        Add(kSettings, Box(right - 30.0f, footer + kFooterHeight / 2.0f, 32.0f), L"All Windows settings");
        for (auto& target : out) {
            if (target.id >= kTile && target.id < kTile + 12) target.help = TileCaption(kTiles[static_cast<std::size_t>(target.id - kTile)]);
        }
        building = nullptr;
    }

    void Rebuild() {
        std::vector<int> previousIds;
        for (const auto& target : targets) previousIds.push_back(target.id);
        std::vector<Target> built;
        Build(detail, built);
        targets = std::move(built);
        if (focused && std::none_of(targets.begin(), targets.end(), [&](const auto& item) { return item.id == *focused && item.enabled; }))
            focused.reset();
        if (hovered && !Find(*hovered)) hovered.reset();
        std::vector<int> currentIds;
        for (const auto& target : targets) currentIds.push_back(target.id);
        if (previousIds != currentIds) (void)accessible.NotifyStructureChanged();
        for (const int id : currentIds) (void)accessible.NotifyPropertyChanged(id);
    }

    const Target* Find(int id) const {
        const auto& list = painting ? *painting : targets;
        const auto found = std::find_if(list.begin(), list.end(), [&](const auto& item) { return item.id == id; });
        return found == list.end() ? nullptr : &*found;
    }
    std::optional<int> Hit(D2D1_POINT_2F point) const {
        point.x /= scale;
        point.y /= scale;
        for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
            if (Contains(it->rect, point)) return it->id;
        }
        return std::nullopt;
    }

    // --- feedback ---
    float Amount(const std::map<int, graphics::Tween>& tweens, int id, double now) const {
        const auto found = tweens.find(id);
        return found == tweens.end() ? 0.0f : std::clamp(found->second.Value(now), 0.0f, 1.0f);
    }
    void Animate(std::map<int, graphics::Tween>& tweens, int id, float goal, double seconds) {
        const double now = graphics::FrameScheduler::Now();
        auto& tween = tweens.try_emplace(id, graphics::Tween(0.0f)).first->second;
        if (std::fabs(tween.Target() - goal) < 1e-4f && !tween.IsAnimating(now)) return;
        tween.AnimateTo(goal, now, motion.Scale(seconds), tokens.curves.standard);
        owner.RequestAnimationFrame();
    }
    void SetHovered(std::optional<int> id) {
        if (id == hovered) return;
        if (hovered) Animate(hover, *hovered, 0.0f, 0.16);  // leaves gently...
        hovered = id;
        if (hovered) Animate(hover, *hovered, 1.0f, 0.11);  // ...arrives at once
        owner.Invalidate();
    }
    void SetPressed(std::optional<int> id) {
        if (pressedId) Animate(press, *pressedId, 0.0f, 0.14);
        pressedId = id;
        if (pressedId) Animate(press, *pressedId, 1.0f, 0.07);
    }
    void SyncTileStates(bool snap) {
        for (std::size_t index = 0; index < kTiles.size(); ++index) {
            const auto value = TileState(kTiles[index]);
            const float goal = value.value_or(false) && !OpensSettings(kTiles[index]) ? 1.0f : 0.0f;
            const float energy = kTiles[index] == SystemControl::EnergySaver && value.value_or(false) ? 1.0f : 0.0f;
            const int id = kTile + static_cast<int>(index);
            if (snap || motion.reduced) on.insert_or_assign(id, graphics::Tween(std::max(goal, energy)));
            else Animate(on, id, std::max(goal, energy), 0.18);
        }
        for (const int id : {kDetailSwitch, kDetailRow, kDetailRow + 4}) {
            std::optional<bool> value;
            if (id == kDetailSwitch && (detail == Detail::WiFi || detail == Detail::Bluetooth))
                value = TileState(detail == Detail::WiFi ? SystemControl::WiFi : SystemControl::Bluetooth);
            if (id == kDetailRow && detail == Detail::Output) value = Muted();
            if (id == kDetailRow + 4 && detail == Detail::Accessibility) value = state.system.stickyKeys;
            const float goal = value.value_or(false) ? 1.0f : 0.0f;
            if (snap || motion.reduced) on.insert_or_assign(id, graphics::Tween(goal));
            else Animate(on, id, goal, 0.18);
        }
    }

    void Focus(int id, bool keyboard) {
        const auto* target = Find(id);
        if (!target || !target->enabled) return;
        focused = id;
        if (keyboard) cues = true;
        owner.Invalidate();
        (void)accessible.NotifyFocusChanged(id);
    }

    // --- actions ---
    bool Admitted() const { return !destroyed && owner.Handle() && owner.IsShown() && ::IsWindowEnabled(owner.Handle()); }
    void Emit(ControlIntent value) {
        if (!Admitted()) return;
        try {
            owner.Requested.Emit(value);
        } catch (...) {
            state.message = L"The requested action could not be dispatched";
            owner.Invalidate();
        }
    }
    // Main view <-> detail view: the outgoing view slides away while the incoming one arrives from the
    // side it belongs to; input moves to the new view at once (no waiting for the animation).
    void Open(Detail next) {
        if (next == detail) return;
        fromView = detail;
        viewDirection = next != Detail::None ? 1.0f : -1.0f;
        detail = next;
        const double now = graphics::FrameScheduler::Now();
        viewMotion.SnapTo(0.0f);
        viewMotion.AnimateTo(1.0f, now, motion.Scale(0.22), tokens.curves.decelerate);
        hover.clear();
        press.clear();
        hovered.reset();
        pressedId.reset();
        Rebuild();
        SyncTileStates(true);
        if (next != Detail::None) {
            Focus(kBack, cues);
        } else if (const auto origin = OpenerOf(fromView)) {
            Focus(*origin, cues);  // back where the detail was opened from
        }
        owner.RequestAnimationFrame();
        owner.Invalidate();
    }
    static std::optional<int> OpenerOf(Detail view) {
        switch (view) {
            case Detail::WiFi: return kChevron + 0;
            case Detail::Bluetooth: return kChevron + 1;
            case Detail::Accessibility: return kTile + 3;
            case Detail::Output: return kOutput;
            case Detail::None: break;
        }
        return std::nullopt;
    }
    void ShowPage(int next) {
        next = ((next % kPages) + kPages) % kPages;
        if (next == page) return;
        page = next;
        pageMotion.AnimateTo(static_cast<float>(page), graphics::FrameScheduler::Now(), motion.Scale(0.24), tokens.curves.standard);
        Rebuild();
        SyncTileStates(true);
        owner.RequestAnimationFrame();
        owner.Invalidate();
        (void)accessible.NotifyStructureChanged();
    }
    void RadioIntent(SystemControl control) {
        ControlIntent intent;
        intent.kind = ControlIntentKind::SystemTile;
        intent.control = control;
        if (const auto* radio = Radio(control); radio && radio->enabled) intent.enabled = !*radio->enabled;
        Emit(intent);
    }
    void Activate(int id) {
        if (!Admitted()) return;
        const auto* target = Find(id);
        if (!target || !target->enabled || target->slider) return;
        ControlIntent intent;
        if (id >= kTile && id < kTile + 12) {
            const auto control = kTiles[static_cast<std::size_t>(id - kTile)];
            if (control == SystemControl::Accessibility) {
                Open(Detail::Accessibility);
                return;
            }
            if (const auto* radio = Radio(control)) {
                // A state Windows does not report cannot be toggled: show the radio's details instead.
                if (!radio->enabled || !Switchable(*radio)) {
                    Open(control == SystemControl::WiFi ? Detail::WiFi : Detail::Bluetooth);
                    return;
                }
                RadioIntent(control);
                return;
            }
            intent.kind = ControlIntentKind::SystemTile;
            intent.control = control;
            Emit(intent);
            return;
        }
        if (id >= kChevron && id < kChevron + 12) {
            Open(id == kChevron ? Detail::WiFi : Detail::Bluetooth);
            return;
        }
        switch (id) {
            case kMute: intent.kind = ControlIntentKind::ToggleMute; break;
            case kOutput: Open(Detail::Output); return;
            case kBattery: intent.kind = ControlIntentKind::BatterySettings; break;
            case kSettings: intent.kind = ControlIntentKind::AllSettings; break;
            case kNextPage: ShowPage(page + 1); return;
            case kBack: Open(Detail::None); return;
            case kPrevious: intent.kind = ControlIntentKind::MediaPrevious; break;
            case kPlay: intent.kind = ControlIntentKind::MediaPlayPause; break;
            case kNext: intent.kind = ControlIntentKind::MediaNext; break;
            case kSession: {
                intent.kind = ControlIntentKind::SelectMedia;
                const auto* current = Session();
                const auto found = std::find_if(state.media.sessions.begin(), state.media.sessions.end(),
                    [&](const auto& value) { return current && value.key == current->key; });
                const auto offset = found == state.media.sessions.end() ? 0u
                    : (static_cast<std::size_t>(found - state.media.sessions.begin()) + 1) % state.media.sessions.size();
                intent.session = state.media.sessions[offset].key;
                Emit(intent);
                return;
            }
            case kDetailSwitch:
                RadioIntent(detail == Detail::WiFi ? SystemControl::WiFi : SystemControl::Bluetooth);
                return;
            default:
                break;
        }
        if (id >= kPageDot && id < kPageDot + kPages) {
            ShowPage(id - kPageDot);
            return;
        }
        if (id >= kDetailRow && id < kDetailRow + 8) {
            const int row = id - kDetailRow;
            if (detail == Detail::WiFi) {
                if (row == 0) intent.kind = ControlIntentKind::NetworkList;
                else {
                    intent.kind = ControlIntentKind::SystemSettings;
                    intent.control = SystemControl::WiFi;
                }
            } else if (detail == Detail::Bluetooth) {
                intent.kind = ControlIntentKind::SystemSettings;
                intent.control = SystemControl::Bluetooth;
            } else if (detail == Detail::Accessibility) {
                if (row == 4) {
                    if (!state.system.stickyKeys) return;
                    intent.kind = ControlIntentKind::StickyKeys;
                    intent.enabled = !*state.system.stickyKeys;
                } else {
                    intent.kind = ControlIntentKind::Accessibility;
                    static constexpr AccessibilityFeature features[] = {AccessibilityFeature::Magnifier, AccessibilityFeature::Narrator,
                        AccessibilityFeature::LiveCaptions, AccessibilityFeature::OnScreenKeyboard, AccessibilityFeature::Settings,
                        AccessibilityFeature::Settings};
                    intent.feature = features[std::clamp(row, 0, 5)];
                }
            } else if (detail == Detail::Output) {
                intent.kind = row == 0 ? ControlIntentKind::ToggleMute : row == 1 ? ControlIntentKind::VolumeMixer : ControlIntentKind::SoundSettings;
            } else {
                return;
            }
        } else if (intent.kind == ControlIntentKind::SystemTile) {
            return;  // no action for this id
        }
        if (const auto* session = Session(); session && intent.kind >= ControlIntentKind::MediaPrevious &&
                                              intent.kind <= ControlIntentKind::MediaNext) {
            intent.session = session->key;
        }
        Emit(intent);
    }

    void SetSlider(int id, int percent, bool commit) {
        const auto* target = Find(id);
        if (!Admitted() || !target || !target->enabled || !target->slider) return;
        percent = std::clamp(percent, 0, 100);
        dragValue = percent;
        if (commit) {
            requested.insert_or_assign(id, Pending{percent, graphics::FrameScheduler::Now()});
            // One timer ends the display of an unconfirmed request; no frames run while waiting.
            ::SetTimer(owner.Handle(), kPendingExpiry, static_cast<UINT>(kPendingSeconds * 1000.0) + 20, nullptr);
            ControlIntent intent;
            intent.kind = id == kVolume ? ControlIntentKind::Volume : ControlIntentKind::Brightness;
            intent.percent = percent;
            Emit(intent);
        }
        owner.Invalidate();
    }
    int PointerValue(int id, D2D1_POINT_2F point) const {
        (void)id;
        const float x = point.x / scale;
        const float left = kMargin + kTrackLeft, right = kMargin + kTrackRight;
        return static_cast<int>(std::lround(std::clamp((x - left) / (right - left), 0.0f, 1.0f) * 100.0f));
    }

    // Arrow keys move to the nearest target in that direction (the grid reads like Windows').
    void Move(UINT key) {
        if (targets.empty()) return;
        const auto* current = focused ? Find(*focused) : nullptr;
        if (!current) {
            for (const auto& target : targets)
                if (target.enabled) {
                    Focus(target.id, true);
                    return;
                }
            return;
        }
        const auto from = Center(current->rect);
        const Target* best = nullptr;
        float bestScore = 1e9f;
        for (const auto& target : targets) {
            if (!target.enabled || target.id == current->id) continue;
            const auto to = Center(target.rect);
            const float dx = to.x - from.x, dy = to.y - from.y;
            float along = 0.0f, across = 0.0f;
            if (key == VK_LEFT) { along = -dx; across = std::fabs(dy); }
            else if (key == VK_RIGHT) { along = dx; across = std::fabs(dy); }
            else if (key == VK_UP) { along = -dy; across = std::fabs(dx); }
            else { along = dy; across = std::fabs(dx); }
            if (along <= 1.0f) continue;
            const float score = along + across * 2.0f;
            if (score < bestScore) {
                bestScore = score;
                best = &target;
            }
        }
        if (best) Focus(best->id, true);
    }

    // --- UI Automation ---
    std::wstring RootName() const override { return L"Aether Shell quick controls"; }
    CONTROLTYPEID RootControlType() const override { return UIA_PaneControlTypeId; }
    std::size_t ItemCount() const override { return targets.size(); }
    ui::accessibility::AccessibleItem Item(std::size_t index) const override {
        const auto& target = targets.at(index);
        ui::accessibility::AccessibleItem item;
        item.id = target.id;
        item.name = target.name;
        item.helpText = target.help;
        if (target.id == kSession) item.helpText = Session() ? L"Media session active" : L"No media session";
        if (target.id >= kPageDot && target.id < kPageDot + kPages) item.itemStatus = target.id - kPageDot == page ? L"Current page" : L"";
        item.isEnabled = Admitted() && target.enabled;
        item.isFocusable = true;
        item.isOffscreen = !owner.IsShown();
        item.hasKeyboardFocus = focused == target.id;
        item.controlType = target.slider ? UIA_SliderControlTypeId : UIA_ButtonControlTypeId;
        item.supportsInvoke = !target.slider;
        if (target.toggle) {
            item.supportsToggle = true;
            item.toggleState = *target.toggle;
        }
        item.supportsRangeValue = target.slider && target.value.has_value();
        item.rangeValue = target.value.value_or(0);
        item.rangeMinimum = 0;
        item.rangeMaximum = 100;
        item.rangeSmallChange = 1;
        item.rangeLargeChange = 10;
        const auto bounds = owner.BoundsPx();
        const float factor = scale * static_cast<float>(owner.Dpi()) / 96.0f;
        const auto& r = target.rect;
        item.boundsPx = {bounds.left + static_cast<LONG>(std::lround(r.left * factor)), bounds.top + static_cast<LONG>(std::lround(r.top * factor)),
                         bounds.left + static_cast<LONG>(std::lround(r.right * factor)), bounds.top + static_cast<LONG>(std::lround(r.bottom * factor))};
        return item;
    }
    Result<void> Invoke(int id) override {
        if (!Admitted()) return MakeError(ErrorCode::Unavailable, "quick controls are closed");
        const auto* target = Find(id);
        if (!target || !target->enabled || target->slider) return MakeError(ErrorCode::Unsupported, "quick control action unavailable");
        return ::PostMessageW(owner.Handle(), kInvoke, static_cast<WPARAM>(id), 0) ? Ok()
            : Result<void>(MakeError(ErrorCode::Platform, "quick control could not be queued"));
    }
    Result<void> Toggle(int id) override { return Invoke(id); }
    Result<void> SetFocus(int id) override {
        if (!Admitted() || !Find(id) || !Find(id)->enabled) return MakeError(ErrorCode::Unavailable, "quick control focus unavailable");
        ::SetFocus(owner.Handle());
        Focus(id, true);
        return Ok();
    }
    Result<void> SetRangeValue(int id, double value) override {
        if (!std::isfinite(value) || value < 0 || value > 100) return MakeError(ErrorCode::InvalidArgument, "invalid quick control range");
        const auto* target = Find(id);
        if (!Admitted() || !target || !target->enabled || !target->slider) return MakeError(ErrorCode::Unavailable, "quick control range unavailable");
        return ::PostMessageW(owner.Handle(), kRange, static_cast<WPARAM>(id), static_cast<LPARAM>(std::lround(value))) ? Ok()
            : Result<void>(MakeError(ErrorCode::Platform, "quick control range could not be queued"));
    }

    // --- painting ---
    struct Painter {
        Impl& p;
        ID2D1DeviceContext* dc;
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        Palette palette;
        double now;
        ID2D1SolidColorBrush* Paint(const Color& color) {
            brush->SetColor(color.ToD2D());
            return brush.get();
        }
        void Fill(const D2D1_RECT_F& rect, float radius, const Color& color) {
            dc->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), Paint(color));
        }
        void Stroke(const D2D1_RECT_F& rect, float radius, const Color& color, float width = 1.0f) {
            const float h = width / 2.0f;
            dc->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(rect.left + h, rect.top + h, rect.right - h, rect.bottom - h), radius, radius),
                                     Paint(color), width);
        }
        void Text(std::wstring_view value, const D2D1_RECT_F& rect, const Color& color, float size = 12.0f, bool bold = false,
                  DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING, bool tabular = false) {
            graphics::TextStyle style;
            style.size = size * std::clamp(p.tokens.textScale, 1.0f, 1.5f);
            style.weight = bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
            style.alignment = align;
            style.opticalSmall = size <= 12.0f;
            style.tabularFigures = tabular;
            p.text.Draw(dc, value, rect, style, Paint(color));
        }
        void Glyph(Symbol symbol, float cx, float cy, float size, const Color& color) {
            p.symbols.Draw(dc, symbol, Box(cx, cy, size), Paint(color), size);
        }
        Color TextColor(bool enabled) const { return enabled ? p.tokens.textPrimary : p.tokens.textDisabled; }
        Color ControlFill(int id, bool enabled) {
            if (!enabled) return palette.tileDisabled;
            return Mix(Mix(palette.tile, palette.tileHover, p.Amount(p.hover, id, now)), palette.tilePressed, p.Amount(p.press, id, now));
        }
        Color AccentFill(int id) {
            const auto& a = p.tokens.accentRoles;
            return Mix(Mix(a.fill, a.fillHover, p.Amount(p.hover, id, now)), a.fillPressed, p.Amount(p.press, id, now));
        }
        // Hover/press feedback of a plain (fill-less) target: a soft rounded highlight.
        void Highlight(const Target& target) {
            const float hovering = p.Amount(p.hover, target.id, now), pressed = p.Amount(p.press, target.id, now);
            const float amount = std::max(hovering, pressed);
            if (amount <= 0.001f || !target.enabled) return;
            const bool light = p.tokens.mode == graphics::ThemeMode::Light;
            const Color color = Mix(light ? Black(0.06f) : White(0.08f), light ? Black(0.035f) : White(0.05f), pressed);
            Fill(target.rect, target.radius, color.WithAlpha(color.a * amount));
        }
        void Focus(const Target& target) {
            if (p.focused == target.id && p.cues && target.enabled) graphics::PaintFocusRing(dc, target.rect, target.radius, p.tokens);
        }
        void Switch(const D2D1_RECT_F& track, int id, bool enabled) {
            const float amount = p.Amount(p.on, id, now);
            const float h = track.bottom - track.top;
            const auto& a = p.tokens.accentRoles;
            const Color off = p.tokens.mode == graphics::ThemeMode::Light ? Black(0.45f) : White(0.6f);
            if (enabled) Fill(track, h / 2.0f, a.fill.WithAlpha(a.fill.a * amount));
            Stroke(track, h / 2.0f, enabled ? off.WithAlpha(off.a * (1.0f - amount)) : p.tokens.textDisabled, 1.0f);
            const float knob = 12.0f + 2.0f * p.Amount(p.hover, id, now);
            const float x = track.left + h / 2.0f + (track.right - track.left - h) * amount;
            const float cy = (track.top + track.bottom) / 2.0f;
            const Color knobColor = !enabled ? p.tokens.textDisabled : Mix(off, a.textOnAccent, amount);
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), knob / 2.0f, knob / 2.0f), Paint(knobColor));
        }
    };

    void PaintTile(Painter& g, int index, float shift, float opacity) {
        const auto control = kTiles[static_cast<std::size_t>(index)];
        const auto tile = TileRect(index % 6, shift);
        const int id = kTile + index;
        const bool enabled = TileEnabled(control);
        const bool split = control == SystemControl::WiFi || control == SystemControl::Bluetooth;
        const float onAmount = g.p.Amount(on, id, g.now);
        const auto main = split ? D2D1::RectF(tile.left, tile.top, tile.left + kSplitMain, tile.bottom) : tile;
        const auto chevron = D2D1::RectF(tile.left + kSplitMain, tile.top, tile.right, tile.bottom);
        // One joined tile: the main and chevron halves share its outline and corners.
        g.dc->PushAxisAlignedClip(tile, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const auto halfFill = [&](const D2D1_RECT_F& part, int partId) {
            const Color off = g.ControlFill(partId, enabled);
            const Color accent = g.AccentFill(partId);
            Color fill = Mix(off, accent, onAmount);
            fill.a *= opacity;
            g.dc->PushAxisAlignedClip(part, D2D1_ANTIALIAS_MODE_ALIASED);
            g.Fill(tile, 4.0f, fill);
            g.dc->PopAxisAlignedClip();
        };
        halfFill(main, id);
        if (split) halfFill(chevron, kChevron + index);
        g.dc->PopAxisAlignedClip();
        Color stroke = Mix(g.palette.tileStroke, Color{0, 0, 0, 0}, onAmount);
        stroke.a *= opacity;
        g.Stroke(tile, 4.0f, stroke);
        if (split) {
            Color divider = Mix(g.palette.tileStroke, g.p.tokens.accentRoles.textOnAccent.WithAlpha(0.25f), onAmount);
            divider.a *= opacity;
            g.dc->FillRectangle(D2D1::RectF(chevron.left, tile.top + 1.0f, chevron.left + 1.0f, tile.bottom - 1.0f), g.Paint(divider));
        }
        Color glyph = Mix(g.TextColor(enabled), g.p.tokens.accentRoles.textOnAccent, onAmount);
        glyph.a *= opacity;
        const auto mc = Center(main);
        if (control == SystemControl::Accessibility) {
            g.Glyph(Symbol::Accessibility, mc.x - 8.0f, mc.y, 16.0f, glyph);
            g.Glyph(Symbol::ChevronRight, mc.x + 11.0f, mc.y, 12.0f, glyph);
        } else if (control == SystemControl::WiFi) {
            const auto bars = state.network.state == status::ProviderState::Available && state.network.kind == status::ConnectionKind::WiFi
                                  ? state.network.signalBars : std::nullopt;
            if (state.system.wifi.enabled == true && bars) {
                // Real signal strength: the full arc set dimmed, the measured level on top.
                g.Glyph(Symbol::WiFi, mc.x, mc.y, 16.0f, glyph.WithAlpha(glyph.a * 0.35f));
                const int level = std::clamp(static_cast<int>(*bars), 0, 5);
                const Symbol strength = level >= 4 ? Symbol::WiFi : level >= 3 ? Symbol::WiFiLevel3 : level >= 2 ? Symbol::WiFiLevel2 : Symbol::WiFiLevel1;
                g.Glyph(strength, mc.x, mc.y, 16.0f, glyph);
            } else {
                g.Glyph(Symbol::WiFi, mc.x, mc.y, 16.0f, glyph);
            }
        } else {
            g.Glyph(SymbolFor(control), mc.x, mc.y, 16.0f, glyph);
        }
        if (split) {
            const auto cc = Center(chevron);
            g.Glyph(Symbol::ChevronRight, cc.x, cc.y, 12.0f, glyph);
        }
        if (OpensSettings(control)) {
            // Honest affordance: this tile opens Windows Settings rather than switching anything.
            g.Glyph(Symbol::OpenExternal, tile.right - 9.0f, tile.top + 9.0f, 9.0f, glyph.WithAlpha(glyph.a * 0.62f));
        }
        Color label = g.TextColor(enabled);
        label.a *= opacity;
        g.Text(TileCaption(control), D2D1::RectF(tile.left - 8.0f, tile.bottom + 4.0f, tile.right + 8.0f, tile.bottom + 28.0f), label, 12.0f,
               false, DWRITE_TEXT_ALIGNMENT_CENTER);
        if (opacity > 0.99f) {
            if (const auto* target = Find(id)) g.Focus(*target);
            if (split) if (const auto* target = Find(kChevron + index)) g.Focus(*target);
        }
    }

    void PaintGrid(Painter& g) {
        const float top = BodyTop();
        const auto zone = D2D1::RectF(kMargin, top, kMargin + kPanelWidth, top + kGridHeight);
        g.dc->PushAxisAlignedClip(zone, D2D1_ANTIALIAS_MODE_ALIASED);
        const float position = pageMotion.Value(g.now);
        const float pitch = 2.0f * kRowPitch;
        for (int shown = 0; shown < kPages; ++shown) {
            const float shift = (static_cast<float>(shown) - position) * pitch;
            if (std::fabs(shift) >= pitch) continue;
            const float opacity = std::clamp(1.0f - std::fabs(shift) / pitch * 1.4f, 0.0f, 1.0f);
            for (int slot = 0; slot < 6; ++slot) PaintTile(g, shown * 6 + slot, shift, opacity);
        }
        g.dc->PopAxisAlignedClip();
        // Page column: the current page's dot, the other page's dot and the next-page arrow.
        for (int dot = 0; dot < kPages; ++dot) {
            const auto* target = Find(kPageDot + dot);
            if (!target) continue;
            g.Highlight(*target);
            const auto c = Center(target->rect);
            const bool current = dot == page;
            const Color color = current ? tokens.textPrimary : tokens.textSecondary.WithAlpha(tokens.textSecondary.a * 0.75f);
            const float radius = current ? 2.6f : 2.1f;
            g.dc->FillEllipse(D2D1::Ellipse(c, radius, radius), g.Paint(color));
            g.Focus(*target);
        }
        if (const auto* target = Find(kNextPage)) {
            g.Highlight(*target);
            const auto c = Center(target->rect);
            g.Glyph(page + 1 < kPages ? Symbol::CaretDown : Symbol::CaretUp, c.x, c.y, 10.0f, tokens.textSecondary);
            g.Focus(*target);
        }
    }

    void PaintSlider(Painter& g, int id, Symbol icon) {
        const auto* target = Find(id);
        if (!target) return;
        const float cy = SliderCenter(id);
        const float left = kMargin + kTrackLeft, right = kMargin + kTrackRight;
        if (id == kVolume) {
            if (const auto* mute = Find(kMute)) {
                g.Highlight(*mute);
                g.Glyph(graphics::VolumeSymbol(Displayed(kVolume, Volume(), g.now), Muted()), kMargin + 32.0f, cy, 16.0f,
                        g.TextColor(mute->enabled));
                g.Focus(*mute);
            }
        } else {
            g.Glyph(icon, kMargin + 32.0f, cy, 16.0f, g.TextColor(target->enabled));
        }
        const float h = 4.0f;
        const auto track = D2D1::RectF(left, cy - h / 2.0f, right, cy + h / 2.0f);
        if (!target->enabled) {
            g.Fill(track, h / 2.0f, g.palette.track.WithAlpha(g.palette.track.a * 0.35f));
            g.Text(L"Unavailable", D2D1::RectF(left, cy - 22.0f, right, cy - 6.0f), tokens.textSecondary, 11.0f);
            return;
        }
        const auto shown = Displayed(id, target->value, g.now);
        auto& thumb = thumbs.try_emplace(id, graphics::Tween(static_cast<float>(shown.value_or(0)))).first->second;
        if (shown) {
            const float goal = static_cast<float>(*shown);
            if (dragging == id || motion.reduced) thumb.SnapTo(goal);
            else if (std::fabs(thumb.Target() - goal) > 0.01f) {
                thumb.AnimateTo(goal, g.now, motion.Scale(0.12), tokens.curves.decelerate);
                owner.RequestAnimationFrame();
            }
        }
        const float fraction = std::clamp(thumb.Value(g.now) / 100.0f, 0.0f, 1.0f);
        const float x = left + (right - left) * fraction;
        g.Fill(track, h / 2.0f, g.palette.track);
        const auto& a = tokens.accentRoles;
        g.Fill(D2D1::RectF(left, track.top, std::max(left + h, x), track.bottom), h / 2.0f, Mix(a.fill, a.fillHover, g.p.Amount(hover, id, g.now)));
        const float hoverAmount = g.p.Amount(hover, id, g.now);
        const float pressAmount = std::max(g.p.Amount(press, id, g.now), dragging == id ? 1.0f : 0.0f);
        const float outer = 10.0f;
        g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), outer, outer), g.Paint(g.palette.thumb));
        g.dc->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), outer - 0.5f, outer - 0.5f), g.Paint(g.palette.thumbStroke), 1.0f);
        const float inner = 5.0f + 1.5f * hoverAmount - 1.5f * pressAmount;  // grows on hover, settles on press
        g.dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, cy), inner, inner), g.Paint(a.fill));
        if (dragging == id && shown) {
            // The value being requested (not yet confirmed by Windows) while the thumb is held.
            const auto bubble = D2D1::RectF(x - 20.0f, cy - 40.0f, x + 20.0f, cy - 16.0f);
            g.Fill(bubble, 4.0f, tokens.mode == graphics::ThemeMode::Light ? Color::FromHex(0xF9F9F9) : Color::FromHex(0x2C2C2C));
            g.Stroke(bubble, 4.0f, g.palette.tileStroke);
            g.Text(std::to_wstring(*shown), bubble, tokens.textPrimary, 12.0f, false, DWRITE_TEXT_ALIGNMENT_CENTER, true);
        }
        if (focused == id && cues) graphics::PaintFocusRing(g.dc, target->rect, 16.0f, tokens);
    }

    void PaintSliders(Painter& g) {
        const float top = BodyTop() + kGridHeight;
        g.dc->FillRectangle(D2D1::RectF(kMargin, top, kMargin + kPanelWidth, top + 1.0f), g.Paint(g.palette.divider));
        PaintSlider(g, kBrightness, Symbol::Brightness);
        PaintSlider(g, kVolume, Symbol::Volume3);
        if (const auto* output = Find(kOutput)) {
            g.Highlight(*output);
            const float cy = SliderCenter(kVolume);
            g.Glyph(Symbol::Mixer, kMargin + 319.0f, cy, 16.0f, tokens.textPrimary);
            g.Glyph(Symbol::ChevronRight, kMargin + 338.0f, cy, 11.0f, tokens.textPrimary);
            g.Focus(*output);
        }
    }

    void PaintMain(Painter& g) {
        PaintGrid(g);
        PaintSliders(g);
    }

    void PaintDetail(Painter& g, Detail kind) {
        const float top = BodyTop();
        const float left = kMargin, right = kMargin + kPanelWidth;
        if (const auto* back = Find(kBack)) {
            g.Highlight(*back);
            const auto c = Center(back->rect);
            g.Glyph(Symbol::Back, c.x, c.y, 14.0f, tokens.textPrimary);
            g.Focus(*back);
        }
        std::wstring title, status;
        if (kind == Detail::WiFi) {
            title = L"Wi-Fi";
            const auto& radio = state.system.wifi;
            const bool wireless = state.network.state == status::ProviderState::Available && state.network.kind == status::ConnectionKind::WiFi;
            if (!radio.enabled) status = radio.hardwareDisabled ? L"Turned off by hardware or policy" : L"State unavailable - use Windows settings";
            else if (!*radio.enabled) status = L"Off";
            else if (wireless) {
                status = L"Connected";
                if (state.network.signalBars) status += L" - signal " + std::to_wstring(*state.network.signalBars) + L" of 5";
                if (state.network.connectivity == status::InternetConnectivity::LocalOnly) status += L", no internet";
            } else status = L"On - not connected";
        } else if (kind == Detail::Bluetooth) {
            title = L"Bluetooth";
            const auto& radio = state.system.bluetooth;
            status = !radio.enabled ? (radio.hardwareDisabled ? L"Turned off by hardware or policy" : L"State unavailable - use Windows settings")
                                    : *radio.enabled ? L"On" : L"Off";
        } else if (kind == Detail::Accessibility) {
            title = L"Accessibility";
            status = state.system.screenReader == true ? L"A screen reader is running" : L"Windows accessibility features";
        } else if (kind == Detail::Output) {
            title = L"Sound output";
            if (state.audio.state == status::ProviderState::Available && state.audio.deviceAvailable)
                status = state.audio.deviceName.empty() ? L"Default output device" : state.audio.deviceName;
            else status = L"No output device available";
        }
        g.Text(title, D2D1::RectF(left + 50.0f, top + 14.0f, right - 70.0f, top + 42.0f), tokens.textPrimary, 15.0f, true);
        g.Text(status, D2D1::RectF(left + 20.0f, top + 44.0f, right - 20.0f, top + 64.0f), tokens.textSecondary, 12.0f);
        if (const auto* toggle = Find(kDetailSwitch)) {
            g.Switch(D2D1::RectF(toggle->rect.left + 6.0f, toggle->rect.top + 2.0f, toggle->rect.right - 2.0f, toggle->rect.bottom - 2.0f),
                     kDetailSwitch, toggle->enabled);
            g.Focus(*toggle);
        }
        for (const auto& target : painting ? *painting : targets) {
            if (target.id < kDetailRow || target.id >= kDetailRow + 8) continue;
            const int row = target.id - kDetailRow;
            g.Highlight(target);
            const auto r = target.rect;
            const float cy = (r.top + r.bottom) / 2.0f;
            Symbol icon = Symbol::Settings;
            bool external = true;
            if (kind == Detail::WiFi) icon = row == 0 ? Symbol::WiFi : Symbol::Settings;
            else if (kind == Detail::Bluetooth) icon = Symbol::Bluetooth;
            else if (kind == Detail::Accessibility) {
                static constexpr Symbol icons[] = {Symbol::Magnifier, Symbol::Narrator, Symbol::LiveCaptions, Symbol::Keyboard,
                                                   Symbol::Keyboard, Symbol::Accessibility};
                icon = icons[std::clamp(row, 0, 5)];
                external = row == 5;
            } else if (kind == Detail::Output) {
                static constexpr Symbol icons[] = {Symbol::VolumeMuted, Symbol::Mixer, Symbol::Volume3};
                icon = icons[std::clamp(row, 0, 2)];
                external = row != 0;
            }
            g.Glyph(icon, r.left + 20.0f, cy, 16.0f, g.TextColor(target.enabled));
            g.Text(target.name, D2D1::RectF(r.left + 42.0f, r.top, r.right - 60.0f, r.bottom), g.TextColor(target.enabled), 13.0f);
            if (target.toggle) {
                g.Switch(D2D1::RectF(r.right - 52.0f, cy - 10.0f, r.right - 12.0f, cy + 10.0f), target.id, target.enabled);
            } else if (external) {
                g.Glyph(Symbol::OpenExternal, r.right - 20.0f, cy, 12.0f, tokens.textSecondary);
            } else if (target.enabled) {
                g.Text(L"Start", D2D1::RectF(r.right - 70.0f, r.top, r.right - 14.0f, r.bottom), tokens.textSecondary, 12.0f, false,
                       DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
            if (!target.enabled && kind == Detail::Accessibility && row == 2) {
                g.Text(L"Windows 11 22H2", D2D1::RectF(r.right - 140.0f, r.top, r.right - 14.0f, r.bottom), tokens.textDisabled, 11.0f, false,
                       DWRITE_TEXT_ALIGNMENT_TRAILING);
            }
            g.Focus(target);
        }
    }

    void PaintMedia(Painter& g) {
        const auto* session = Session();
        if (!session) return;
        const float left = kMargin, right = kMargin + kPanelWidth;
        if (const auto* source = Find(kSession)) g.Highlight(*source);
        g.Text(session->title.empty() ? std::wstring(L"Untitled media") : session->title,
               D2D1::RectF(left + 20.0f, kMargin + 14.0f, left + 232.0f, kMargin + 34.0f), tokens.textPrimary, 13.0f, true);
        g.Text(session->artist, D2D1::RectF(left + 20.0f, kMargin + 34.0f, left + 232.0f, kMargin + 54.0f), tokens.textSecondary, 12.0f);
        for (const int id : {kPrevious, kPlay, kNext}) {
            const auto* target = Find(id);
            if (!target) continue;
            g.Highlight(*target);
            const auto c = Center(target->rect);
            const Symbol symbol = id == kPrevious ? Symbol::Previous : id == kNext ? Symbol::Next
                                  : session->playback == MediaPlayback::Playing ? Symbol::Pause : Symbol::Play;
            g.Glyph(symbol, c.x, c.y, 16.0f, g.TextColor(target->enabled));
            g.Focus(*target);
        }
        if (const auto* source = Find(kSession)) g.Focus(*source);
        g.dc->FillRectangle(D2D1::RectF(left, kMargin + kMediaHeight - 1.0f, right, kMargin + kMediaHeight), g.Paint(g.palette.divider));
    }

    void PaintFooter(Painter& g) {
        const float top = FooterTop();
        const auto panel = PanelRect();
        // A slightly distinguished band with the panel's own bottom corners.
        g.dc->PushAxisAlignedClip(D2D1::RectF(panel.left, top, panel.right, panel.bottom), D2D1_ANTIALIAS_MODE_ALIASED);
        g.Fill(panel, kRadius, g.palette.footer);
        g.dc->PopAxisAlignedClip();
        g.dc->FillRectangle(D2D1::RectF(panel.left, top, panel.right, top + 1.0f), g.Paint(g.palette.divider));
        const float cy = top + kFooterHeight / 2.0f;
        if (const auto* battery = Find(kBattery)) {
            g.Highlight(*battery);
            const auto& b = state.battery;
            const bool known = b.state == status::ProviderState::Available && b.batteryPresent;
            const auto symbol = graphics::BatterySymbol(known ? b.percent : std::nullopt, known && b.charge == status::ChargeState::Charging,
                                                         state.system.energySaver.value_or(false));
            const Color color = known && b.lowOrCritical.value_or(false) ? graphics::BatteryStatusColor(tokens, b.percent, b.lowOrCritical)
                                                                        : tokens.textPrimary;
            g.Glyph(symbol, kMargin + 30.0f, cy, 18.0f, color);
            std::wstring label = known && b.percent ? std::to_wstring(*b.percent) + L"%" : L"Battery unknown";
            g.Text(label, D2D1::RectF(kMargin + 44.0f, top, kMargin + 140.0f, top + kFooterHeight), tokens.textPrimary, 12.5f, false,
                   DWRITE_TEXT_ALIGNMENT_LEADING, true);
            g.Focus(*battery);
        }
        if (!state.message.empty()) {
            g.Text(state.message, D2D1::RectF(kMargin + 130.0f, top, kMargin + kPanelWidth - 52.0f, top + kFooterHeight), tokens.textSecondary,
                   11.0f, false, DWRITE_TEXT_ALIGNMENT_TRAILING);
        }
        if (const auto* settings = Find(kSettings)) {
            g.Highlight(*settings);
            const auto c = Center(settings->rect);
            g.Glyph(Symbol::Settings, c.x, c.y, 16.0f, tokens.textPrimary);
            g.Focus(*settings);
        }
    }
};

ControlCenterWindow::ControlCenterWindow(graphics::GraphicsDevice& graphics, graphics::FrameScheduler& scheduler, IUiDispatcher& dispatcher)
    : SurfaceWindow(graphics, scheduler), impl_(std::make_unique<Impl>(*this, dispatcher)) {}
ControlCenterWindow::~ControlCenterWindow() { Destroy(); }

Result<void> ControlCenterWindow::Initialize() {
    if (!impl_->dispatcher.IsUiThread()) return MakeError(ErrorCode::InvalidState, "quick controls require UI thread");
    ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.ControlCenter";
    options.title = L"Aether Shell quick controls";
    options.noActivate = false;
    if (auto result = Create(options, {0, 0, 376, 404}); !result) return result;
    impl_->text.Initialize(Device().DWriteFactory(), impl_->tokens.fontFamily, impl_->tokens.displayFontFamily);
    impl_->symbols.Initialize(Device().DWriteFactory());
    impl_->Rebuild();
    impl_->SyncTileStates(true);
    return impl_->accessible.Connect(Handle(), *impl_);
}

Result<void> ControlCenterWindow::Toggle(const RECT& anchor, bool keyboard) {
    if (!impl_->dispatcher.IsUiThread() || impl_->destroyed) return MakeError(ErrorCode::InvalidState, "quick controls unavailable");
    if (IsShown()) {
        Close();
        return Ok();
    }
    auto& p = *impl_;
    const auto monitor = platform::QueryMonitor(::MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST)).value_or(platform::PrimaryMonitor());
    // Move the hidden HWND to the anchor monitor first so PMv2 updates Dpi() before layout.
    SetBoundsPx({anchor.left, anchor.top, anchor.left + 1, anchor.top + 1});
    p.detail = Detail::None;
    p.fromView = Detail::None;
    p.page = 0;
    p.pageMotion.SnapTo(0.0f);
    p.viewMotion.SnapTo(1.0f);
    p.hover.clear();
    p.press.clear();
    p.hovered.reset();
    p.pressedId.reset();
    p.requested.clear();
    p.cues = keyboard;
    p.Rebuild();
    p.SyncTileStates(true);
    const float dpiScale = static_cast<float>(Dpi()) / 96.0f;
    const auto area = monitor.workArea;
    const float width = kPanelWidth + 2.0f * kMargin, height = p.PanelHeight() + 2.0f * kMargin;
    const float fit = std::min({1.0f, static_cast<float>(area.right - area.left - 16) / (width * dpiScale),
                                static_cast<float>(area.bottom - area.top - 16) / (height * dpiScale)});
    if (fit < 0.55f) return MakeError(ErrorCode::Unavailable, "screen area is too small for quick controls");
    p.scale = fit;
    const LONG w = static_cast<LONG>(std::ceil(width * fit * dpiScale)), h = static_cast<LONG>(std::ceil(height * fit * dpiScale));
    const LONG margin = static_cast<LONG>(std::lround(kMargin * fit * dpiScale));
    const LONG gap = static_cast<LONG>(std::lround(6.0f * dpiScale));
    // The visible panel's right edge meets the group's right edge, its top sits just below it; both
    // are clamped to the monitor's work area (negative coordinates included).
    const LONG edge = static_cast<LONG>(std::lround(8.0f * dpiScale));
    const LONG x = std::clamp(anchor.right + margin - w, area.left + edge - margin, std::max(area.left + edge - margin, area.right - edge + margin - w));
    const LONG y = std::clamp(anchor.bottom + gap - margin, area.top - margin, std::max(area.top - margin, area.bottom - edge + margin - h));
    SetBoundsPx({x, y, x + w, y + h});
    const auto panel = p.PanelRect();
    const RECT region{static_cast<LONG>(std::floor(panel.left * fit * dpiScale)), static_cast<LONG>(std::floor(panel.top * fit * dpiScale)),
                      static_cast<LONG>(std::ceil(panel.right * fit * dpiScale)), static_cast<LONG>(std::ceil(panel.bottom * fit * dpiScale))};
    SetInputRegionPx(std::span<const RECT>(&region, 1));
    SetVisualRegionPx(std::span<const RECT>(&region, 1));
    SetBackdropPanel(D2D1::RectF(panel.left * fit, panel.top * fit, panel.right * fit, panel.bottom * fit), kRadius * fit,
                     p.tokens.transparencyEnabled, p.tokens.glass.backdropOpacity);
    // Opens from the Top Bar: a short settle downwards, never a jump from another place.
    graphics::TransitionShape from;
    from.offsetY = -6.0f;
    from.scale = 0.985f;
    from.centerX = (panel.left + panel.right) / 2.0f * fit;
    from.centerY = panel.top * fit;
    for (const auto& target : p.targets) {
        if (target.enabled) {
            p.focused = target.id;
            break;
        }
    }
    // The first composed frame is this opening's own state (view, page, focus), rendered before
    // the window becomes visible.
    Invalidate();
    ShowAnimated(graphics::MakeEnterTransition(from, p.tokens, p.motion), true);
    ::SetTimer(Handle(), kRefresh, 1500, nullptr);
    try {
        RefreshRequested.Emit();
    } catch (...) {
        p.state.message = L"Status refresh could not be started";
    }
    return Ok();
}

std::uint64_t ControlCenterWindow::RenderCount() const noexcept { return impl_->renders; }

RECT ControlCenterWindow::PanelBoundsPx() const noexcept {
    if (!IsShown()) return RECT{};
    const auto bounds = BoundsPx();
    const float factor = impl_->scale * static_cast<float>(Dpi()) / 96.0f;
    const auto panel = impl_->PanelRect();
    return RECT{bounds.left + static_cast<LONG>(std::lround(panel.left * factor)), bounds.top + static_cast<LONG>(std::lround(panel.top * factor)),
                bounds.left + static_cast<LONG>(std::lround(panel.right * factor)), bounds.top + static_cast<LONG>(std::lround(panel.bottom * factor))};
}

void ControlCenterWindow::Close() {
    // Hiding deactivates the window, which closes again from WM_ACTIVATE: announce one close only.
    if (impl_->closing) return;
    impl_->closing = true;
    const bool shown = IsShown();
    if (Handle()) {
        ::KillTimer(Handle(), kRefresh);
        ::KillTimer(Handle(), kVolumeCommit);
        ::KillTimer(Handle(), kPendingExpiry);
        impl_->volumeTimerArmed = false;
    }
    impl_->dragging.reset();
    impl_->dragValue.reset();
    impl_->volumePending.reset();
    impl_->requested.clear();
    impl_->hovered.reset();
    impl_->pressedId.reset();
    Hide();
    impl_->closing = false;
    if (shown) {
        try {
            Closed.Emit();
        } catch (...) {
        }
    }
}

void ControlCenterWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    impl_->tokens = tokens;
    impl_->motion = motion;
    impl_->text.Initialize(Device().DWriteFactory(), tokens.fontFamily, tokens.displayFontFamily);
    if (motion.reduced) {
        for (auto* tweens : {&impl_->hover, &impl_->press, &impl_->on, &impl_->thumbs})
            for (auto& [id, tween] : *tweens) tween.SnapTo(tween.Target());
        impl_->pageMotion.SnapTo(impl_->pageMotion.Target());
        impl_->viewMotion.SnapTo(impl_->viewMotion.Target());
    }
    if (IsShown()) {
        const float fit = impl_->scale;
        const auto panel = impl_->PanelRect();
        SetBackdropPanel(D2D1::RectF(panel.left * fit, panel.top * fit, panel.right * fit, panel.bottom * fit), kRadius * fit,
                         tokens.transparencyEnabled, tokens.glass.backdropOpacity);
    }
    Invalidate();
}

void ControlCenterWindow::SetState(ControlCenterState state) {
    auto& p = *impl_;
    const bool mediaBefore = p.MediaShown();
    p.state = std::move(state);
    const double now = graphics::FrameScheduler::Now();
    // A request is settled once Windows reports it (or its readback is overdue).
    std::erase_if(p.requested, [&](const auto& pending) {
        const std::optional<int> observed = pending.first == kVolume ? p.Volume() : p.state.brightness.percent;
        return observed == pending.second.value || now - pending.second.since >= kPendingSeconds || !p.state.message.empty();
    });
    p.Rebuild();
    p.SyncTileStates(false);
    if (mediaBefore != p.MediaShown() && IsShown()) {
        // The media card appeared or left: grow/shrink downwards from the same top-right anchor.
        const auto bounds = BoundsPx();
        const float factor = p.scale * static_cast<float>(Dpi()) / 96.0f;
        const LONG h = static_cast<LONG>(std::ceil((p.PanelHeight() + 2.0f * kMargin) * factor));
        SetBoundsPx({bounds.left, bounds.top, bounds.right, bounds.top + h});
        const auto panel = p.PanelRect();
        const RECT region{static_cast<LONG>(std::floor(panel.left * factor)), static_cast<LONG>(std::floor(panel.top * factor)),
                          static_cast<LONG>(std::ceil(panel.right * factor)), static_cast<LONG>(std::ceil(panel.bottom * factor))};
        SetInputRegionPx(std::span<const RECT>(&region, 1));
        SetVisualRegionPx(std::span<const RECT>(&region, 1));
        SetBackdropPanel(D2D1::RectF(panel.left * p.scale, panel.top * p.scale, panel.right * p.scale, panel.bottom * p.scale),
                         kRadius * p.scale, p.tokens.transparencyEnabled, p.tokens.glass.backdropOpacity);
    }
    Invalidate();
}

void ControlCenterWindow::OnRender(const ui::RenderContext& context) {
    auto& p = *impl_;
    ++p.renders;
    auto* dc = context.d2d;
    D2D1_MATRIX_3X2_F old{};
    dc->GetTransform(&old);
    dc->SetTransform(D2D1::Matrix3x2F::Scale(p.scale, p.scale) * old);
    graphics::GlassStyle glass;
    glass.elevation = graphics::Elevation::None;
    glass.cornerRadius = kRadius;
    glass.opacity = 0.86f;
    glass.backdropBlur = BackdropBlurActive();
    glass.shadow = false;
    glass.neutral = true;
    glass.highlight = false;
    const auto panel = p.PanelRect();
    graphics::PaintGlassPanel(dc, panel, p.tokens, glass);
    Impl::Painter g{p, dc, nullptr, MakePalette(p.tokens), context.now};
    if (FAILED(dc->CreateSolidColorBrush(p.tokens.textPrimary.ToD2D(), g.brush.put()))) {
        dc->SetTransform(old);
        return;
    }
    dc->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    p.PaintMedia(g);
    // Main view and detail views slide past each other inside the body; the footer stays.
    const float body = p.BodyTop();
    const auto zone = D2D1::RectF(panel.left, body, panel.right, body + kGridHeight + kSliderHeight);
    const float amount = std::clamp(p.viewMotion.Value(context.now), 0.0f, 1.0f);
    dc->PushAxisAlignedClip(zone, D2D1_ANTIALIAS_MODE_ALIASED);
    const auto layer = [&](float opacity, float offset, const auto& paint) {
        if (opacity <= 0.001f) return;
        dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                             opacity, nullptr, D2D1_LAYER_OPTIONS1_NONE),
                      nullptr);
        D2D1_MATRIX_3X2_F base{};
        dc->GetTransform(&base);
        dc->SetTransform(D2D1::Matrix3x2F::Translation(offset, 0.0f) * base);
        paint();
        dc->SetTransform(base);
        dc->PopLayer();
    };
    const auto paintView = [&](Detail view) {
        if (view == Detail::None) p.PaintMain(g);
        else p.PaintDetail(g, view);
    };
    if (amount < 0.999f) {
        // The view being left keeps drawing from its own targets while it slides out.
        std::vector<Target> outgoing;
        p.Build(p.fromView, outgoing);
        p.painting = &outgoing;
        // Fade-through: the outgoing view is gone by 55 % of the slide and the incoming one appears from
        // 20 %, so they overlap only briefly and faintly instead of reading as one double-exposed page.
        layer(std::clamp(1.0f - amount / 0.55f, 0.0f, 1.0f), -p.viewDirection * 40.0f * amount, [&] { paintView(p.fromView); });
        p.painting = nullptr;
        layer(std::clamp((amount - 0.2f) / 0.8f, 0.0f, 1.0f), p.viewDirection * 40.0f * (1.0f - amount), [&] { paintView(p.detail); });
    } else {
        paintView(p.detail);
    }
    dc->PopAxisAlignedClip();
    p.PaintFooter(g);
    dc->SetTransform(old);
}

bool ControlCenterWindow::OnAnimate(double now) {
    auto& p = *impl_;
    bool running = p.pageMotion.IsAnimating(now) || p.viewMotion.IsAnimating(now);
    for (const auto* tweens : {&p.hover, &p.press, &p.on, &p.thumbs})
        for (const auto& [id, tween] : *tweens) running = running || tween.IsAnimating(now);
    return running;
}

void ControlCenterWindow::OnPointerMove(D2D1_POINT_2F point) {
    auto& p = *impl_;
    if (p.dragging) {
        p.SetSlider(*p.dragging, p.PointerValue(*p.dragging, point), false);
        if (*p.dragging == kVolume) {
            p.volumePending = p.dragValue;
            if (!p.volumeTimerArmed) p.volumeTimerArmed = ::SetTimer(Handle(), kVolumeCommit, 40, nullptr) != 0;
        }
        return;
    }
    const auto hit = p.Hit(point);
    const auto* target = hit ? p.Find(*hit) : nullptr;
    p.SetHovered(target && target->enabled ? hit : std::nullopt);
}

void ControlCenterWindow::OnPointerLeave() {
    impl_->SetHovered(std::nullopt);
    if (!impl_->dragging) impl_->SetPressed(std::nullopt);
}

void ControlCenterWindow::OnPointerDown(ui::PointerButton button, D2D1_POINT_2F point) {
    auto& p = *impl_;
    if (button != ui::PointerButton::Left) return;
    const auto hit = p.Hit(point);
    if (!hit) return;
    const auto* target = p.Find(*hit);
    if (!target || !target->enabled) return;
    p.cues = false;  // pointer interaction: no keyboard focus ring
    p.Focus(*hit, false);
    p.SetPressed(*hit);
    if (target->slider) {
        p.dragging = *hit;
        p.SetSlider(*hit, p.PointerValue(*hit, point), false);
    }
    Invalidate();
}

void ControlCenterWindow::OnPointerUp(ui::PointerButton button, D2D1_POINT_2F point) {
    auto& p = *impl_;
    if (button != ui::PointerButton::Left) return;
    p.SetPressed(std::nullopt);
    if (p.dragging) {
        const int id = *p.dragging;
        ::KillTimer(Handle(), kVolumeCommit);
        p.volumeTimerArmed = false;
        p.volumePending.reset();
        p.SetSlider(id, p.PointerValue(id, point), true);
        p.dragging.reset();
        p.dragValue.reset();
        return;
    }
    // One action per gesture, on release over the target that was pressed.
    if (const auto hit = p.Hit(point); hit && p.focused == hit) p.Activate(*hit);
}

void ControlCenterWindow::OnPointerCaptureLost() {
    auto& p = *impl_;
    p.dragging.reset();
    p.dragValue.reset();
    p.volumePending.reset();
    p.SetPressed(std::nullopt);
    if (Handle()) ::KillTimer(Handle(), kVolumeCommit);
    p.volumeTimerArmed = false;
    Invalidate();
}

bool ControlCenterWindow::OnKeyDown(UINT key) {
    auto& p = *impl_;
    if (key == VK_ESCAPE) {
        if (p.detail != Detail::None) p.Open(Detail::None);
        else Close();
        return true;
    }
    if (key == VK_TAB) {
        std::vector<int> ids;
        for (const auto& target : p.targets)
            if (target.enabled) ids.push_back(target.id);
        if (ids.empty()) return true;
        const auto found = p.focused ? std::find(ids.begin(), ids.end(), *p.focused) : ids.end();
        const int index = found == ids.end() ? -1 : static_cast<int>(found - ids.begin());
        const int count = static_cast<int>(ids.size());
        const int delta = (::GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1;
        p.Focus(ids[static_cast<std::size_t>(((index < 0 ? (delta > 0 ? -1 : 0) : index) + count + delta) % count)], true);
        return true;
    }
    const auto* target = p.focused ? p.Find(*p.focused) : nullptr;
    if (target && target->enabled && target->slider && target->value &&
        (key == VK_LEFT || key == VK_RIGHT || key == VK_HOME || key == VK_END || key == VK_PRIOR || key == VK_NEXT)) {
        p.cues = true;
        const auto pending = p.requested.find(target->id);
        const int current = pending != p.requested.end() ? pending->second.value : *target->value;
        const int value = key == VK_HOME ? 0 : key == VK_END ? 100
            : current + (key == VK_RIGHT ? 1 : key == VK_LEFT ? -1 : key == VK_PRIOR ? 10 : -10);
        p.SetSlider(target->id, value, true);
        p.dragValue.reset();
        return true;
    }
    if (key == VK_LEFT || key == VK_RIGHT || key == VK_UP || key == VK_DOWN) {
        p.Move(key);
        return true;
    }
    if ((key == VK_NEXT || key == VK_PRIOR) && p.detail == Detail::None) {
        p.ShowPage(p.page + (key == VK_NEXT ? 1 : -1));
        return true;
    }
    if ((key == VK_RETURN || key == VK_SPACE) && target && target->enabled) {
        p.cues = true;
        p.Activate(target->id);
        return true;
    }
    return false;
}

void ControlCenterWindow::OnFocusChanged(bool focused) {
    // Hiding takes the focus too: a closing panel needs no new frame.
    if (!focused && !impl_->closing && IsShown()) Invalidate();
}

void ControlCenterWindow::OnDpiChanged(UINT dpi, const RECT& suggested) {
    SurfaceWindow::OnDpiChanged(dpi, suggested);
    Close();
    SetBoundsPx(suggested);
    Invalidate();
}

std::optional<LRESULT> ControlCenterWindow::OnMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    auto& p = *impl_;
    if (message == WM_GETOBJECT) {
        LRESULT result = 0;
        if (p.accessible.HandleGetObject(Handle(), wParam, lParam, result)) return result;
    }
    if (message == kInvoke) {
        p.Activate(static_cast<int>(wParam));
        return 0;
    }
    if (message == kRange) {
        p.SetSlider(static_cast<int>(wParam), static_cast<int>(lParam), true);
        p.dragValue.reset();
        return 0;
    }
    if (message == WM_TIMER && wParam == kRefresh) {
        if (IsShown()) {
            try {
                RefreshRequested.Emit();
            } catch (...) {
                p.state.message = L"Status refresh unavailable";
            }
        }
        return 0;
    }
    if (message == WM_TIMER && wParam == kVolumeCommit) {
        ::KillTimer(Handle(), kVolumeCommit);
        p.volumeTimerArmed = false;
        if (p.dragging == kVolume && p.volumePending) {
            const int value = *p.volumePending;
            p.volumePending.reset();
            p.SetSlider(kVolume, value, true);
        }
        return 0;
    }
    if (message == WM_TIMER && wParam == kPendingExpiry) {
        ::KillTimer(Handle(), kPendingExpiry);
        const double now = graphics::FrameScheduler::Now();
        std::erase_if(p.requested, [&](const auto& pending) { return now - pending.second.since >= kPendingSeconds; });
        if (!p.requested.empty()) {
            double oldest = now;
            for (const auto& entry : p.requested) oldest = std::min(oldest, entry.second.since);
            ::SetTimer(Handle(), kPendingExpiry, static_cast<UINT>((kPendingSeconds - (now - oldest)) * 1000.0) + 20, nullptr);
        }
        Invalidate();  // the observed reading returns
        return 0;
    }
    if (message == WM_MOUSEWHEEL && p.detail == Detail::None && IsShown()) {
        // The wheel over the tiles turns pages, like Windows' panel; over a slider it adjusts it.
        POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        ::ScreenToClient(Handle(), &point);
        const float dip = 96.0f / static_cast<float>(Dpi());
        const auto hit = p.Hit(D2D1::Point2F(static_cast<float>(point.x) * dip, static_cast<float>(point.y) * dip));
        const short delta = GET_WHEEL_DELTA_WPARAM(wParam);
        if (hit && (*hit == kVolume || *hit == kBrightness)) {
            if (const auto* target = p.Find(*hit); target && target->enabled && target->value) {
                const auto pending = p.requested.find(*hit);
                const int current = pending != p.requested.end() ? pending->second.value : *target->value;
                p.SetSlider(*hit, current + (delta > 0 ? 2 : -2), true);
                p.dragValue.reset();
            }
        } else if (std::abs(delta) >= WHEEL_DELTA / 2) {
            p.ShowPage(p.page + (delta < 0 ? 1 : -1));
        }
        return 0;
    }
    if (message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) {
        Close();
        return 0;
    }
    if (message == WM_CLOSE) {
        Close();
        return 0;
    }
    return std::nullopt;
}

void ControlCenterWindow::OnDestroying() {
    impl_->destroyed = true;
    impl_->accessible.Disconnect();
    ::KillTimer(Handle(), kRefresh);
    ::KillTimer(Handle(), kVolumeCommit);
    ::KillTimer(Handle(), kPendingExpiry);
    impl_->volumeTimerArmed = false;
}

}  // namespace aether::controlcenter
