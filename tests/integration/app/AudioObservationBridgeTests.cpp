// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "topbar/TopBarWindow.h"
#include "controlcenter/ControlCenterWindow.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Win32UiDispatcher.h"
#include <UIAutomation.h>
#include <atomic>
#include <memory>
#include <thread>

using namespace aether;
namespace {
void Pump(platform::MessageLoop& loop,DWORD milliseconds) {
    HANDLE timer=::CreateWaitableTimerW(nullptr,TRUE,nullptr);REQUIRE(timer!=nullptr);
    LARGE_INTEGER due{};due.QuadPart=-static_cast<LONGLONG>(milliseconds)*10000;
    REQUIRE(::SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE));
    const auto id=loop.AddWaitHandle(timer,[]{::PostQuitMessage(0);});
    loop.Run();loop.RemoveWaitHandle(id);::CloseHandle(timer);
}
struct AudioBridgeFixture final {
    platform::ComApartment apartment;
    platform::MessageLoop loop;
    platform::Win32UiDispatcher dispatcher;
    graphics::GraphicsDevice device;
    graphics::FrameScheduler scheduler;
    std::unique_ptr<topbar::TopBarWindow> bar;
    std::unique_ptr<controlcenter::ControlCenterWindow> controls;
    controlcenter::ControlCenterState state;
    status::AudioSnapshot observed{status::ProviderState::Available,true,0.76f,false,L"Synthetic output",{}};
    Connection bridge,intents;
    int publications=0,audioIntents=0,events=0;
    bool wrongThread=false;
    ~AudioBridgeFixture(){
        bridge.Disconnect();intents.Disconnect();controls.reset();bar.reset();
        dispatcher.Shutdown();scheduler.Shutdown();device.Shutdown();
    }
    void Initialize(){
        REQUIRE(SUCCEEDED(apartment.Status()));REQUIRE(device.Initialize());
        REQUIRE(scheduler.Initialize(loop));REQUIRE(dispatcher.Initialize());
        topbar::TopBarContext context{device,scheduler,dispatcher};
        context.allowAppBar=false;context.groupQuickControls=true;
        // No real providers, application host or device-intent handlers are created.
        bar=std::make_unique<topbar::TopBarWindow>(context);
        const auto area=platform::PrimaryMonitor().workArea;
        REQUIRE(bar->Create(RECT{area.left+20,area.top+20,area.left+1020,area.top+68}));
        bar->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark),graphics::MotionPolicy{true,1});
        bar->Show();
        controls=std::make_unique<controlcenter::ControlCenterWindow>(device,scheduler,dispatcher);
        REQUIRE(controls->Initialize());
        controls->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark),graphics::MotionPolicy{true,1});
        state.audio=observed;controls->SetState(state);
        REQUIRE(controls->Toggle(RECT{area.left+100,area.top+80,area.left+600,area.top+110}));
        // No RefreshRequested handler is connected, so periodic refresh cannot publish state.
        // Only observed events update this fixture's UIA range; no real device is involved.
        const DWORD owner=::GetCurrentThreadId();
        intents=controls->Requested.Connect([this](auto){++audioIntents;});
        bridge=bar->AudioObservedChanged.Connect([this,owner]{
            wrongThread|=::GetCurrentThreadId()!=owner;++events;
            if(controls&&controls->IsShown()){
                // Fake provider Current(): the exact observed data, never an optimistic target.
                state.audio=observed;controls->SetState(state);++publications;
            }
        });
    }
    void Observe(float volume){
        observed.volume=volume;
        topbar::TopBarSnapshots snapshots;snapshots.audio=observed;
        bar->SetSnapshots(std::move(snapshots));
    }
    double UiaVolume(){
        std::atomic<bool> finished=false;bool passed=false;double value=-1;
        const HWND window=controls->Handle();
        std::jthread client([&]{
            platform::ComApartment com(COINIT_MULTITHREADED);
            try{
                if(FAILED(com.Status()))throw 1;
                winrt::com_ptr<IUIAutomation> automation;
                winrt::check_hresult(::CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,
                    __uuidof(IUIAutomation),automation.put_void()));
                winrt::com_ptr<IUIAutomation2> bounded;
                if(SUCCEEDED(automation->QueryInterface(__uuidof(IUIAutomation2),bounded.put_void()))){
                    (void)bounded->put_ConnectionTimeout(500);(void)bounded->put_TransactionTimeout(500);
                }
                DWORD owner=0;(void)::GetWindowThreadProcessId(window,&owner);
                if(owner!=::GetCurrentProcessId())throw 1;
                winrt::com_ptr<IUIAutomationElement> root,item;
                winrt::check_hresult(automation->ElementFromHandle(window,root.put()));
                VARIANT id{};id.vt=VT_BSTR;id.bstrVal=::SysAllocString(L"item-301");
                if(!id.bstrVal)throw 1;
                winrt::com_ptr<IUIAutomationCondition> condition;
                const HRESULT status=automation->CreatePropertyCondition(UIA_AutomationIdPropertyId,id,condition.put());
                ::VariantClear(&id);winrt::check_hresult(status);
                winrt::check_hresult(root->FindFirst(TreeScope_Children,condition.get(),item.put()));
                if(!item)throw 1;
                winrt::com_ptr<IUIAutomationRangeValuePattern> range;
                winrt::check_hresult(item->GetCurrentPatternAs(UIA_RangeValuePatternId,
                    __uuidof(IUIAutomationRangeValuePattern),range.put_void()));
                winrt::check_hresult(range->get_CurrentValue(&value));passed=true;
            }catch(...){/* Synthetic own-window failure only; no external content is reported. */}
            finished.store(true);
        });
        const auto deadline=::GetTickCount64()+5000;
        while(!finished.load()&&::GetTickCount64()<deadline)Pump(loop,10);
        const bool inTime=finished.load();client.join();REQUIRE(inTime);REQUIRE(passed);
        return value;
    }
};
}
AETHER_TEST("app audio observation: external snapshots refresh real controls UIA without polling or writes") {
    AudioBridgeFixture fixture;fixture.Initialize();
    for(const auto [scalar,percent]:{std::pair{0.76f,76.0},std::pair{0.31f,31.0},std::pair{0.87f,87.0}}){
        fixture.Observe(scalar);CHECK_EQ(fixture.UiaVolume(),percent);
    }
    CHECK_EQ(fixture.publications,3);CHECK_EQ(fixture.audioIntents,0);CHECK(!fixture.wrongThread);
}
AETHER_TEST("app audio observation: unchanged audio is deduplicated and hidden controls do not republish") {
    AudioBridgeFixture fixture;fixture.Initialize();fixture.Observe(0.76f);
    const int events=fixture.events,publications=fixture.publications;
    fixture.Observe(0.76f);CHECK_EQ(fixture.events,events);CHECK_EQ(fixture.publications,publications);
    fixture.controls->Close();fixture.Observe(0.22f);
    CHECK_EQ(fixture.events,events+1);CHECK_EQ(fixture.publications,publications);
    CHECK_EQ(fixture.audioIntents,0);CHECK(!fixture.wrongThread);
}
AETHER_TEST("app audio observation: an outbound listener may destroy the observed Top Bar") {
    AudioBridgeFixture fixture;fixture.Initialize();
    auto destroyed=fixture.bar->AudioObservedChanged.Connect([&]{fixture.bar.reset();});
    fixture.Observe(0.31f);CHECK(!fixture.bar);CHECK_EQ(fixture.audioIntents,0);
}
