// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "controlcenter/ControlCenterWindow.h"

#include "graphics/GlassPainter.h"
#include "graphics/Glyphs.h"
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
constexpr float kWidth = 432, kHeight = 560;
constexpr UINT kInvoke = WM_APP + 211;
constexpr UINT kRange = WM_APP + 212;
constexpr UINT_PTR kRefresh = 41, kVolumeCommit = 42;
constexpr int kBrightness = 300, kVolume = 301, kMute = 302, kSound = 310, kDisplay = 311;
constexpr int kPrevious = 400, kPlay = 401, kNext = 402, kSession = 403;
constexpr int kBattery = 500, kSettings = 501, kPage = 600;
constexpr std::array<SystemControl, 12> kTiles{SystemControl::WiFi, SystemControl::Bluetooth,
    SystemControl::FlightMode, SystemControl::Accessibility, SystemControl::EnergySaver,
    SystemControl::LiveCaptions, SystemControl::NightLight, SystemControl::MobileHotspot,
    SystemControl::NearbySharing, SystemControl::Cast, SystemControl::Project, SystemControl::Settings};

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
bool Contains(const D2D1_RECT_F& r, D2D1_POINT_2F p) noexcept {
    return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom;
}
struct Target {
    int id = 0;
    D2D1_RECT_F rect{};
    std::wstring name;
    bool enabled = true;
    bool slider = false;
    std::optional<int> value;
};
}

struct ControlCenterWindow::Impl final : ui::accessibility::IAccessibleSurfaceModel {
    ControlCenterWindow& owner;
    IUiDispatcher& dispatcher;
    ControlCenterState state;
    graphics::DesignTokens tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    graphics::MotionPolicy motion;
    graphics::TextCache text;
    ui::accessibility::AccessibleSurface accessible;
    std::vector<Target> targets;
    float scale = 1;
    int page = 0;
    std::optional<int> hovered, focused, dragging, dragValue, volumePending;
    std::map<int,int> requestedRanges;
    graphics::Tween sun{0};
    bool destroyed = false, volumeTimerArmed = false;
    explicit Impl(ControlCenterWindow& window, IUiDispatcher& ui) : owner(window), dispatcher(ui) {}

    const MediaSession* Session() const {
        if (!state.media.selectedSession) return nullptr;
        const auto found = std::find_if(state.media.sessions.begin(), state.media.sessions.end(),
            [&](const auto& session) { return session.key == *state.media.selectedSession; });
        return found == state.media.sessions.end() ? nullptr : &*found;
    }
    std::optional<int> Volume() const {
        if (state.audio.state != status::ProviderState::Available || !state.audio.deviceAvailable ||
            !state.audio.volume || !std::isfinite(*state.audio.volume)) return {};
        return static_cast<int>(std::lround(std::clamp(*state.audio.volume, 0.0f, 1.0f) * 100));
    }
    const RadioStatus* Radio(SystemControl control) const {
        if (control == SystemControl::WiFi) return &state.system.wifi;
        if (control == SystemControl::Bluetooth) return &state.system.bluetooth;
        return nullptr;
    }
    bool On(SystemControl control) const {
        if (const auto* radio = Radio(control)) return radio->enabled.value_or(false);
        return control == SystemControl::EnergySaver && state.system.energySaver.value_or(false);
    }
    std::wstring StateLabel(SystemControl control) const {
        if (const auto* radio = Radio(control)) {
            if (!radio->enabled) return L"State unavailable";
            return *radio->enabled ? L"On" : L"Off";
        }
        if (control == SystemControl::EnergySaver && state.system.energySaver)
            return *state.system.energySaver ? L"On · Settings" : L"Off · Settings";
        return control == SystemControl::Cast || control == SystemControl::Project ||
            control == SystemControl::LiveCaptions ? L"Open" : L"Settings";
    }
    void Rebuild() {
        std::vector<int> previousIds;for(const auto& target:targets)previousIds.push_back(target.id);
        targets.clear();
        const auto* media = Session();
        targets.push_back({kPrevious,{126,82,174,120},L"Previous media item",media && media->canPrevious});
        targets.push_back({kPlay,{190,82,238,120},media && media->playback == MediaPlayback::Playing ? L"Pause" : L"Play",
            media && media->canTogglePlayPause});
        targets.push_back({kNext,{254,82,302,120},L"Next media item",media && media->canNext});
        targets.push_back({kSession,{28,16,366,42},L"Choose next media session",state.media.sessions.size() > 1});
        for (int slot = 0; slot < 6; ++slot) {
            const int index = page * 6 + slot;
            const auto control = kTiles[static_cast<std::size_t>(index)];
            const float x = 28.0f + static_cast<float>(slot % 3) * 132;
            const float y = 184.0f + static_cast<float>(slot / 3) * 78;
            bool enabled = true;
            if (control == SystemControl::LiveCaptions) enabled = state.system.windowsBuild >= 22621;
            targets.push_back({100 + index,{x,y,x+108,y+52},std::wstring(Label(control)) + L" · " + StateLabel(control),enabled});
            if (Radio(control)) targets.push_back({200+index,{x+78,y,x+108,y+52},std::wstring(Label(control))+L" settings"});
        }
        targets.push_back({kPage,{340,342,404,370},page == 0 ? L"More controls" : L"Previous controls"});
        targets.push_back({kBrightness,{74,382,380,414},L"Brightness",state.brightness.canSet && state.brightness.percent.has_value(),true,state.brightness.percent});
        targets.push_back({kVolume,{74,434,380,466},L"Volume",Volume().has_value(),true,Volume()});
        targets.push_back({kMute,{24,434,64,466},state.audio.muted.value_or(false) ? L"Unmute output" : L"Mute output",state.audio.deviceAvailable && state.audio.muted.has_value()});
        targets.push_back({kDisplay,{384,382,416,414},L"Display brightness settings"});
        targets.push_back({kSound,{384,434,416,466},L"Audio output settings"});
        targets.push_back({kBattery,{24,502,230,542},L"Power and battery settings"});
        targets.push_back({kSettings,{346,502,408,542},L"All Windows settings"});
        if (focused && std::none_of(targets.begin(),targets.end(),[&](const auto& item){return item.id==*focused && item.enabled;})) focused.reset();
        std::vector<int> currentIds;for(const auto& target:targets)currentIds.push_back(target.id);
        if(previousIds!=currentIds)(void)accessible.NotifyStructureChanged();
        // Copy IDs before outbound events: clients may reenter and rebuild the model.
        for(const int id:currentIds)(void)accessible.NotifyPropertyChanged(id);
    }
    void Focus(int id) {
        const auto* target=Find(id);if(!target||!target->enabled)return;
        focused=id;owner.Invalidate();(void)accessible.NotifyFocusChanged(id);
    }
    const Target* Find(int id) const {
        const auto found = std::find_if(targets.begin(),targets.end(),[&](const auto& item){return item.id==id;});
        return found == targets.end() ? nullptr : &*found;
    }
    std::optional<int> Hit(D2D1_POINT_2F point) const {
        point.x /= scale; point.y /= scale;
        // Split-button arrows take priority over the enclosing radio tile.
        for (auto it = targets.rbegin(); it != targets.rend(); ++it) if (Contains(it->rect,point)) return it->id;
        return {};
    }
    bool Admitted() const { return !destroyed && owner.Handle() && owner.IsShown() && ::IsWindowEnabled(owner.Handle()); }
    void Emit(ControlIntent value) {
        if (!Admitted()) return;
        try { owner.Requested.Emit(value); } catch (...) { state.message=L"The requested action could not be dispatched";owner.Invalidate(); }
    }
    void Activate(int id) {
        if (!Admitted()) return;
        const auto* target=Find(id); if (!target || !target->enabled) return;
        if (id==kPage) { page=1-page;Rebuild();owner.Invalidate();return; }
        ControlIntent intent;
        if (id>=100 && id<112) {
            intent.control=kTiles[static_cast<std::size_t>(id-100)]; intent.kind=ControlIntentKind::SystemTile;
            if (const auto* radio=Radio(intent.control); radio && radio->enabled) intent.enabled=!*radio->enabled;
        } else if (id>=200 && id<212) { intent.kind=ControlIntentKind::SystemSettings;intent.control=kTiles[static_cast<std::size_t>(id-200)]; }
        else if (id==kMute) intent.kind=ControlIntentKind::ToggleMute;
        else if (id==kSound) intent.kind=ControlIntentKind::SoundSettings;
        else if (id==kDisplay) intent.kind=ControlIntentKind::DisplaySettings;
        else if (id==kBattery) intent.kind=ControlIntentKind::BatterySettings;
        else if (id==kSettings) intent.kind=ControlIntentKind::AllSettings;
        else if (id==kPrevious) intent.kind=ControlIntentKind::MediaPrevious;
        else if (id==kPlay) intent.kind=ControlIntentKind::MediaPlayPause;
        else if (id==kNext) intent.kind=ControlIntentKind::MediaNext;
        else if (id==kSession) {
            intent.kind=ControlIntentKind::SelectMedia;
            const auto* current=Session();
            const auto found=std::find_if(state.media.sessions.begin(),state.media.sessions.end(),[&](const auto& value){return current && value.key==current->key;});
            const auto offset=found==state.media.sessions.end()?0u:(static_cast<std::size_t>(found-state.media.sessions.begin())+1)%state.media.sessions.size();
            intent.session=state.media.sessions[offset].key;
        } else return;
        if (const auto* session=Session(); session && intent.kind!=ControlIntentKind::SelectMedia) intent.session=session->key;
        Emit(intent);
    }
    void SetSlider(int id,int percent,bool commit) {
        const auto* target=Find(id);if(!Admitted() || !target || !target->enabled || !target->slider)return;
        percent=std::clamp(percent,0,100);dragValue=percent;
        if(commit)requestedRanges[id]=percent;
        if(id==kBrightness) {
            const float value=static_cast<float>(percent)/100;
            if(motion.reduced)sun.SnapTo(value);else {sun.AnimateTo(value,graphics::FrameScheduler::Now(),motion.Scale(0.12));owner.RequestAnimationFrame();}
        }
        if(commit) {ControlIntent intent;intent.kind=id==kVolume?ControlIntentKind::Volume:ControlIntentKind::Brightness;intent.percent=percent;Emit(intent);}
        owner.Invalidate();
    }
    int PointerValue(int id,D2D1_POINT_2F point) const {
        const auto* target=Find(id);if(!target)return 0;
        const float x=point.x/scale;
        return static_cast<int>(std::lround(std::clamp((x-target->rect.left-8)/(target->rect.right-target->rect.left-16),0.0f,1.0f)*100));
    }
    std::wstring RootName() const override {return L"Aether Shell quick controls";}
    CONTROLTYPEID RootControlType() const override {return UIA_PaneControlTypeId;}
    std::size_t ItemCount() const override {return targets.size();}
    ui::accessibility::AccessibleItem Item(std::size_t index) const override {
        const auto& target=targets.at(index);ui::accessibility::AccessibleItem item;
        item.id=target.id;item.name=target.name;item.isEnabled=Admitted()&&target.enabled;item.isFocusable=true;
        item.isOffscreen=!owner.IsShown();item.hasKeyboardFocus=focused==target.id;
        item.controlType=target.slider?UIA_SliderControlTypeId:UIA_ButtonControlTypeId;
        item.supportsInvoke=!target.slider;item.supportsRangeValue=target.slider&&target.value.has_value();
        item.rangeValue=target.value.value_or(0);item.rangeMinimum=0;item.rangeMaximum=100;item.rangeSmallChange=1;item.rangeLargeChange=10;
        auto rect=target.rect;
        if(target.id>=100&&target.id<112&&Radio(kTiles[static_cast<std::size_t>(target.id-100)]))rect.right-=30;
        const auto bounds=owner.BoundsPx();const float factor=scale*static_cast<float>(owner.Dpi())/96;
        item.boundsPx={bounds.left+static_cast<LONG>(std::lround(rect.left*factor)),bounds.top+static_cast<LONG>(std::lround(rect.top*factor)),
            bounds.left+static_cast<LONG>(std::lround(rect.right*factor)),bounds.top+static_cast<LONG>(std::lround(rect.bottom*factor))};
        return item;
    }
    Result<void> Invoke(int id) override {
        if(!Admitted())return MakeError(ErrorCode::Unavailable,"quick controls are closed");
        const auto* target=Find(id);if(!target||!target->enabled||target->slider)return MakeError(ErrorCode::Unsupported,"quick control action unavailable");
        return ::PostMessageW(owner.Handle(),kInvoke,static_cast<WPARAM>(id),0)?Ok():Result<void>(MakeError(ErrorCode::Platform,"quick control could not be queued"));
    }
    Result<void> Toggle(int id) override {return Invoke(id);}
    Result<void> SetFocus(int id) override {
        if(!Admitted()||!Find(id)||!Find(id)->enabled)return MakeError(ErrorCode::Unavailable,"quick control focus unavailable");
        ::SetFocus(owner.Handle());Focus(id);return Ok();
    }
    Result<void> SetRangeValue(int id,double value) override {
        if(!std::isfinite(value)||value<0||value>100)return MakeError(ErrorCode::InvalidArgument,"invalid quick control range");
        const auto* target=Find(id);if(!Admitted()||!target||!target->enabled||!target->slider)return MakeError(ErrorCode::Unavailable,"quick control range unavailable");
        return ::PostMessageW(owner.Handle(),kRange,static_cast<WPARAM>(id),static_cast<LPARAM>(std::lround(value)))?Ok():Result<void>(MakeError(ErrorCode::Platform,"quick control range could not be queued"));
    }
};

ControlCenterWindow::ControlCenterWindow(graphics::GraphicsDevice& graphics,graphics::FrameScheduler& scheduler,IUiDispatcher& dispatcher)
    :SurfaceWindow(graphics,scheduler),impl_(std::make_unique<Impl>(*this,dispatcher)){}
ControlCenterWindow::~ControlCenterWindow(){Destroy();}
Result<void> ControlCenterWindow::Initialize(){
    if(!impl_->dispatcher.IsUiThread())return MakeError(ErrorCode::InvalidState,"quick controls require UI thread");
    ui::SurfaceWindowOptions options;options.className=L"AetherShell.ControlCenter";options.title=L"Aether Shell quick controls";options.noActivate=false;
    if(auto result=Create(options,{0,0,432,560});!result)return result;
    impl_->text.Initialize(Device().DWriteFactory(),impl_->tokens.fontFamily,impl_->tokens.displayFontFamily);
    impl_->Rebuild();return impl_->accessible.Connect(Handle(),*impl_);
}
Result<void> ControlCenterWindow::Toggle(const RECT& anchor){
    if(!impl_->dispatcher.IsUiThread()||impl_->destroyed)return MakeError(ErrorCode::InvalidState,"quick controls unavailable");
    if(IsShown()){Close();return Ok();}
    const auto monitor=platform::QueryMonitor(::MonitorFromRect(&anchor,MONITOR_DEFAULTTONEAREST)).value_or(platform::PrimaryMonitor());
    // Move the hidden HWND to the anchor monitor first so PMv2 updates Dpi() before layout.
    SetBoundsPx({anchor.left,anchor.top,anchor.left+1,anchor.top+1});
    const float dpiScale=static_cast<float>(Dpi())/96;
    const auto area=monitor.workArea;
    const float fit=std::min({1.0f,static_cast<float>(area.right-area.left-24)/(kWidth*dpiScale),static_cast<float>(area.bottom-area.top-16)/(kHeight*dpiScale)});
    if(fit<0.55f)return MakeError(ErrorCode::Unavailable,"screen area is too small for quick controls");
    impl_->scale=fit;
    const LONG width=static_cast<LONG>(std::ceil(kWidth*fit*dpiScale)),height=static_cast<LONG>(std::ceil(kHeight*fit*dpiScale));
    const LONG x=std::clamp(anchor.right-width,area.left+8,std::max(area.left+8,area.right-width-8));
    const LONG y=std::clamp(anchor.bottom+8,area.top+8,std::max(area.top+8,area.bottom-height-8));
    SetBoundsPx({x,y,x+width,y+height});
    const RECT regions[]={{static_cast<LONG>(8*fit*dpiScale),static_cast<LONG>(8*fit*dpiScale),width-static_cast<LONG>(8*fit*dpiScale),static_cast<LONG>(128*fit*dpiScale)},
        {static_cast<LONG>(8*fit*dpiScale),static_cast<LONG>(138*fit*dpiScale),width-static_cast<LONG>(8*fit*dpiScale),height-static_cast<LONG>(8*fit*dpiScale)}};
    SetInputRegionPx(regions);SetVisualRegionPx(regions);
    SetBackdropPanel({8*fit,8*fit,(kWidth-8)*fit,(kHeight-8)*fit},12*fit,impl_->tokens.transparencyEnabled,impl_->tokens.glass.backdropOpacity);
    Show(true);
    for(const auto& target:impl_->targets)if(target.enabled){impl_->Focus(target.id);break;}
    ::SetTimer(Handle(),kRefresh,1500,nullptr);Invalidate();
    try{RefreshRequested.Emit();}catch(...){impl_->state.message=L"Status refresh could not be started";}
    return Ok();
}
void ControlCenterWindow::Close(){const bool shown=IsShown();if(Handle()){::KillTimer(Handle(),kRefresh);::KillTimer(Handle(),kVolumeCommit);impl_->volumeTimerArmed=false;}impl_->dragging.reset();impl_->dragValue.reset();impl_->volumePending.reset();impl_->requestedRanges.clear();Hide();if(shown)try{Closed.Emit();}catch(...){} }
void ControlCenterWindow::SetTheme(const graphics::DesignTokens& tokens,const graphics::MotionPolicy& motion){
    impl_->tokens=tokens;impl_->motion=motion;impl_->text.Initialize(Device().DWriteFactory(),tokens.fontFamily,tokens.displayFontFamily);
    if(motion.reduced)impl_->sun.SnapTo(impl_->sun.Target());
    const float fit=impl_->scale;SetBackdropPanel({8*fit,8*fit,(kWidth-8)*fit,(kHeight-8)*fit},12*fit,tokens.transparencyEnabled,tokens.glass.backdropOpacity);Invalidate();
}
void ControlCenterWindow::SetState(ControlCenterState state){
    impl_->state=std::move(state);
    if(!impl_->dragging && impl_->state.brightness.percent){const float value=static_cast<float>(*impl_->state.brightness.percent)/100;
        if(impl_->motion.reduced)impl_->sun.SnapTo(value);else if(std::abs(impl_->sun.Target()-value)>0.001f){impl_->sun.AnimateTo(value,graphics::FrameScheduler::Now(),impl_->motion.Scale(0.12));RequestAnimationFrame();}}
    impl_->Rebuild();
    std::erase_if(impl_->requestedRanges,[&](const auto& pending){const auto* target=impl_->Find(pending.first);
        return !impl_->state.message.empty()||!target||!target->enabled||target->value==pending.second;});
    Invalidate();
}

void ControlCenterWindow::OnRender(const ui::RenderContext& context){
    auto& p=*impl_;auto* dc=context.d2d;const auto old= [&]{D2D1_MATRIX_3X2_F value;dc->GetTransform(&value);return value;}();
    dc->SetTransform(D2D1::Matrix3x2F::Scale(p.scale,p.scale)*old);
    graphics::GlassStyle glass;glass.elevation=graphics::Elevation::Bar;glass.cornerRadius=14;glass.opacity=0.86f;glass.backdropBlur=BackdropBlurActive();glass.shadow=false;
    graphics::PaintGlassPanel(dc,{8,8,424,128},p.tokens,glass);graphics::PaintGlassPanel(dc,{8,138,424,552},p.tokens,glass);
    winrt::com_ptr<ID2D1SolidColorBrush> brush; if(FAILED(dc->CreateSolidColorBrush(p.tokens.textPrimary.ToD2D(),brush.put()))){dc->SetTransform(old);return;}
    auto color=[&](graphics::Color value){brush->SetColor(value.ToD2D());};
    auto text=[&](std::wstring_view value,D2D1_RECT_F rect,float size=13,bool bold=false,bool center=false){graphics::TextStyle style;style.size=size*p.tokens.textScale;style.weight=bold?DWRITE_FONT_WEIGHT_SEMI_BOLD:DWRITE_FONT_WEIGHT_NORMAL;style.alignment=center?DWRITE_TEXT_ALIGNMENT_CENTER:DWRITE_TEXT_ALIGNMENT_LEADING;p.text.Draw(dc,value,rect,style,brush.get());};
    winrt::com_ptr<ID2D1Factory> factory;dc->GetFactory(factory.put());
    auto* round=graphics::RoundStrokeStyle(factory.get());
    auto line=[&](float x,float y,float a,float b,float width=1.6f){dc->DrawLine({x,y},{a,b},brush.get(),width,round);};
    const auto* session=p.Session();color(p.tokens.textSecondary);
    text(session?L"Now playing · switch source":L"Media controls",{28,16,366,42},12);
    color(p.tokens.textPrimary);text(session?(session->title.empty()?L"Untitled media":session->title):(p.state.loading?L"Reading media sessions…":L"No compatible media session"),{28,40,404,65},15,true);
    color(p.tokens.textSecondary);text(session?session->artist:L"Play media in an application to see its controls",{28,63,404,83},12);
    for(const auto& target:p.targets){
        if(target.slider||target.id==kSession)continue;
        if(target.id>=200&&target.id<212)continue;
        if(p.hovered==target.id && target.enabled){color(p.tokens.surface.subtleFill);dc->FillRoundedRectangle(D2D1::RoundedRect(target.rect,6,6),brush.get());}
        if(target.id>=100&&target.id<112){
            const auto control=kTiles[static_cast<std::size_t>(target.id-100)];
            color(p.On(control)?p.tokens.accentRoles.fill:target.enabled?p.tokens.surface.controlFill:p.tokens.surface.controlFillDisabled);
            dc->FillRoundedRectangle(D2D1::RoundedRect(target.rect,7,7),brush.get());color(p.tokens.surface.cardStroke);dc->DrawRoundedRectangle(D2D1::RoundedRect(target.rect,7,7),brush.get(),1);
            color(p.On(control)?p.tokens.accentRoles.textOnAccent:target.enabled?p.tokens.textPrimary:p.tokens.textDisabled);
            const float cx=(target.rect.left+target.rect.right)/2-(p.Radio(control)?12:0),cy=(target.rect.top+target.rect.bottom)/2;
            if(control==SystemControl::WiFi){
                std::optional<int> bars;
                if(p.state.network.state==status::ProviderState::Available&&p.state.network.kind==status::ConnectionKind::WiFi&&p.state.network.signalBars)bars=*p.state.network.signalBars;
                graphics::PaintNetworkGlyph(dc,{cx-11,cy-11,cx+11,cy+11},brush.get(),graphics::NetworkGlyphKind::WiFi,bars);
            }
            else if(control==SystemControl::Bluetooth){line(cx,cy-12,cx,cy+12);line(cx,cy-12,cx+8,cy-5);line(cx+8,cy-5,cx-7,cy+7);line(cx-7,cy-7,cx+8,cy+5);line(cx+8,cy+5,cx,cy+12);}
            else if(control==SystemControl::FlightMode){line(cx-12,cy+3,cx+12,cy-3,2.2f);line(cx,cy,cx-4,cy-11,2.2f);line(cx,cy,cx-4,cy+10,2.2f);}
            else if(control==SystemControl::EnergySaver){dc->DrawRoundedRectangle(D2D1::RoundedRect({cx-12,cy-6,cx+9,cy+6},2,2),brush.get(),1.6f);line(cx+12,cy-2,cx+12,cy+2);line(cx-4,cy+3,cx+3,cy-4,2);}
            else if(control==SystemControl::LiveCaptions){dc->DrawRoundedRectangle(D2D1::RoundedRect({cx-13,cy-9,cx+13,cy+9},3,3),brush.get(),1.6f);text(L"CC",{cx-12,cy-9,cx+12,cy+9},11,true,true);}
            else if(control==SystemControl::NightLight){dc->DrawEllipse({{cx,cy},9,9},brush.get(),1.6f);line(cx+2,cy-8,cx-3,cy+1);line(cx-3,cy+1,cx+7,cy+6);}
            else if(control==SystemControl::NearbySharing){graphics::PaintGlyph(dc,graphics::Glyph::ArrowUp,{cx-10,cy-12,cx+10,cy+8},brush.get());line(cx-11,cy+9,cx+11,cy+9);}
            else if(control==SystemControl::Accessibility){dc->DrawEllipse({{cx,cy-9},3,3},brush.get(),1.6f);line(cx-11,cy-2,cx+11,cy-2);line(cx,cy-3,cx,cy+5);line(cx,cy+5,cx-7,cy+12);line(cx,cy+5,cx+7,cy+12);}
            else if(control==SystemControl::Settings){graphics::PaintGlyph(dc,graphics::Glyph::General,{cx-12,cy-12,cx+12,cy+12},brush.get());}
            else {dc->DrawRoundedRectangle(D2D1::RoundedRect({cx-13,cy-9,cx+13,cy+7},2,2),brush.get(),1.6f);line(cx-5,cy+11,cx+5,cy+11);if(control==SystemControl::MobileHotspot)dc->DrawEllipse({{cx,cy},4,4},brush.get(),1.5f);}
            if(p.Radio(control)){line(target.rect.right-22,cy-4,target.rect.right-17,cy);line(target.rect.right-17,cy,target.rect.right-22,cy+4);}
            color(p.tokens.textPrimary);text(Label(control),{target.rect.left-6,target.rect.bottom+1,target.rect.right+6,target.rect.bottom+20},11,false,true);
        }else if(target.id==kPrevious||target.id==kPlay||target.id==kNext){
            color(target.enabled?p.tokens.textPrimary:p.tokens.textDisabled);const float x=(target.rect.left+target.rect.right)/2,y=(target.rect.top+target.rect.bottom)/2;
            if(target.id==kPlay&&session&&session->playback==MediaPlayback::Playing){line(x-4,y-7,x-4,y+7,3);line(x+4,y-7,x+4,y+7,3);}
            else if(target.id==kPlay){line(x-4,y-8,x+7,y);line(x+7,y,x-4,y+8);line(x-4,y+8,x-4,y-8);}
            else{const float sign=target.id==kNext?1.0f:-1.0f;line(x-5*sign,y-7,x+4*sign,y);line(x+4*sign,y,x-5*sign,y+7);line(x+7*sign,y-7,x+7*sign,y+7);}
        }
        if(p.focused==target.id&&target.enabled)graphics::PaintFocusRing(dc,target.rect,6,p.tokens);
    }
    color(p.tokens.textSecondary);text(L"Quick controls",{28,146,310,176},14,true);text(p.page==0?L"1 / 2   ›":L"‹   2 / 2",{340,342,404,370},12,false,true);
    for(const int id:{kBrightness,kVolume}){
        const auto* target=p.Find(id);if(!target)continue;const auto r=target->rect;
        const auto value=p.dragging==id&&p.dragValue?p.dragValue:target->value;
        color(target->enabled?p.tokens.textSecondary:p.tokens.textDisabled);line(r.left+8,(r.top+r.bottom)/2,r.right-8,(r.top+r.bottom)/2,4);
        if(value){const float x=r.left+8+(r.right-r.left-16)*static_cast<float>(*value)/100;const float y=(r.top+r.bottom)/2;
            color(p.tokens.accentRoles.fill);line(r.left+8,y,x,y,4);color(p.tokens.textPrimary);dc->FillEllipse({{x,y},7,7},brush.get());color(p.tokens.accentRoles.fill);dc->FillEllipse({{x,y},3.5f,3.5f},brush.get());}
        color(p.tokens.textSecondary);text(value?std::to_wstring(*value)+L"%":L"Unavailable",{74,r.top-17,380,r.top},11);
        if(p.focused==id&&target->enabled)graphics::PaintFocusRing(dc,r,6,p.tokens);
    }
    color(p.tokens.textPrimary);const float sun=p.sun.Value(context.now),sunSize=16+sun*8;
    graphics::PaintStatusGlyph(dc,graphics::StatusGlyph::Sun,{44-sunSize/2,398-sunSize/2,44+sunSize/2,398+sunSize/2},brush.get());
    const bool audioKnown=p.state.audio.state==status::ProviderState::Available&&p.state.audio.deviceAvailable;
    graphics::PaintVolumeGlyph(dc,{28,438,60,462},brush.get(),p.Volume(),audioKnown?p.state.audio.muted:std::nullopt);
    text(L"›",{384,434,416,466},20,false,true);text(L"›",{384,382,416,414},20,false,true);
    color(p.tokens.textSecondary);text(p.state.message.empty()?(p.state.loading?L"Reading Windows status…":p.state.audio.deviceName):p.state.message,{28,470,404,496},11);
    color(p.tokens.surface.divider);line(9,498,423,498,1);
    color(p.tokens.textPrimary);std::wstring battery=L"Power and battery";if(p.state.battery.percent)battery=std::to_wstring(*p.state.battery.percent)+L"% · Power and battery";
    const bool batteryKnown=p.state.battery.state==status::ProviderState::Available&&p.state.battery.batteryPresent;
    graphics::PaintBatteryGlyph(dc,{28,512,50,534},brush.get(),p.tokens,batteryKnown?p.state.battery.percent:std::nullopt,
        batteryKnown?p.state.battery.lowOrCritical:std::nullopt,batteryKnown&&p.state.battery.charge==status::ChargeState::Charging,
        p.state.battery.source==status::PowerSource::External);
    text(battery,{58,502,330,542},12);graphics::PaintGlyph(dc,graphics::Glyph::General,{365,512,389,536},brush.get());
    dc->SetTransform(old);
}
bool ControlCenterWindow::OnAnimate(double now){return impl_->sun.IsAnimating(now);}
void ControlCenterWindow::OnPointerMove(D2D1_POINT_2F point){
    if(impl_->dragging){impl_->SetSlider(*impl_->dragging,impl_->PointerValue(*impl_->dragging,point),false);
        if(*impl_->dragging==kVolume){impl_->volumePending=impl_->dragValue;if(!impl_->volumeTimerArmed)impl_->volumeTimerArmed=::SetTimer(Handle(),kVolumeCommit,40,nullptr)!=0;}return;}
    const auto hit=impl_->Hit(point);if(hit!=impl_->hovered){impl_->hovered=hit;Invalidate();}
}
void ControlCenterWindow::OnPointerDown(ui::PointerButton button,D2D1_POINT_2F point){
    if(button!=ui::PointerButton::Left)return;const auto hit=impl_->Hit(point);if(!hit)return;const auto* target=impl_->Find(*hit);
    if(!target||!target->enabled)return;impl_->Focus(*hit);if(target->slider){impl_->dragging=*hit;impl_->SetSlider(*hit,impl_->PointerValue(*hit,point),false);}Invalidate();
}
void ControlCenterWindow::OnPointerUp(ui::PointerButton button,D2D1_POINT_2F point){
    if(button!=ui::PointerButton::Left)return;
    if(impl_->dragging){const int id=*impl_->dragging;::KillTimer(Handle(),kVolumeCommit);impl_->volumeTimerArmed=false;impl_->volumePending.reset();impl_->SetSlider(id,impl_->PointerValue(id,point),true);impl_->dragging.reset();impl_->dragValue.reset();return;}
    if(const auto hit=impl_->Hit(point);hit&&impl_->focused==hit)impl_->Activate(*hit);
}
void ControlCenterWindow::OnPointerCaptureLost(){impl_->dragging.reset();impl_->dragValue.reset();impl_->volumePending.reset();if(Handle())::KillTimer(Handle(),kVolumeCommit);impl_->volumeTimerArmed=false;Invalidate();}
bool ControlCenterWindow::OnKeyDown(UINT key){
    if(key==VK_ESCAPE){Close();return true;}
    if(key==VK_TAB){std::vector<int> ids;for(const auto& target:impl_->targets)if(target.enabled)ids.push_back(target.id);if(ids.empty())return true;
        const auto found=impl_->focused?std::find(ids.begin(),ids.end(),*impl_->focused):ids.end();const auto index=found==ids.end()?0:static_cast<int>(found-ids.begin());
        const int count=static_cast<int>(ids.size()),delta=(::GetKeyState(VK_SHIFT)&0x8000)?-1:1;impl_->Focus(ids[static_cast<std::size_t>((index+count+delta)%count)]);return true;}
    if(!impl_->focused)return false;const auto* target=impl_->Find(*impl_->focused);if(!target||!target->enabled)return false;
    if(target->slider&&target->value&&(key==VK_LEFT||key==VK_RIGHT||key==VK_HOME||key==VK_END||key==VK_PRIOR||key==VK_NEXT)){
        const int current=impl_->requestedRanges.contains(target->id)?impl_->requestedRanges.at(target->id):*target->value;
        const int value=key==VK_HOME?0:key==VK_END?100:current+(key==VK_RIGHT?1:key==VK_LEFT?-1:key==VK_PRIOR?10:-10);impl_->SetSlider(target->id,value,true);impl_->dragValue.reset();return true;}
    if(key==VK_RETURN||key==VK_SPACE){impl_->Activate(*impl_->focused);return true;}return false;
}
void ControlCenterWindow::OnFocusChanged(bool focused){if(!focused)Invalidate();}
void ControlCenterWindow::OnDpiChanged(UINT dpi,const RECT& suggested){SurfaceWindow::OnDpiChanged(dpi,suggested);Close();SetBoundsPx(suggested);Invalidate();}
std::optional<LRESULT> ControlCenterWindow::OnMessage(UINT message,WPARAM wParam,LPARAM lParam){
    if(message==WM_GETOBJECT){LRESULT result=0;if(impl_->accessible.HandleGetObject(Handle(),wParam,lParam,result))return result;}
    if(message==kInvoke){impl_->Activate(static_cast<int>(wParam));return 0;}
    if(message==kRange){impl_->SetSlider(static_cast<int>(wParam),static_cast<int>(lParam),true);impl_->dragValue.reset();return 0;}
    if(message==WM_TIMER&&wParam==kRefresh){if(IsShown())try{RefreshRequested.Emit();}catch(...){impl_->state.message=L"Status refresh unavailable";}return 0;}
    if(message==WM_TIMER&&wParam==kVolumeCommit){::KillTimer(Handle(),kVolumeCommit);impl_->volumeTimerArmed=false;if(impl_->dragging==kVolume&&impl_->volumePending){const int value=*impl_->volumePending;impl_->volumePending.reset();impl_->SetSlider(kVolume,value,true);}return 0;}
    if(message==WM_ACTIVATE&&LOWORD(wParam)==WA_INACTIVE){Close();return 0;}
    if(message==WM_CLOSE){Close();return 0;}return std::nullopt;
}
void ControlCenterWindow::OnDestroying(){impl_->destroyed=true;impl_->accessible.Disconnect();::KillTimer(Handle(),kRefresh);::KillTimer(Handle(),kVolumeCommit);impl_->volumeTimerArmed=false;}

} // namespace aether::controlcenter
