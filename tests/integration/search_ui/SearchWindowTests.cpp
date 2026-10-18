// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "platform/windows/ComApartment.h"
#include "search/ui/SearchWindow.h"

#include <chrono>
#include <thread>

using namespace aether;
using namespace std::chrono_literals;
namespace {
class Launcher final:public apps::IApplicationLauncher {
public:
    int launches=0;
    void LaunchAsync(const apps::ApplicationInfo&,std::function<void(Result<void>)> completion)override{++launches;completion(Ok());}
    Result<void> Activate(const apps::RunningApplication&)override{return Ok();}
};
class Icons final:public apps::IIconProvider {
public:void RequestIcon(apps::IconRequest request,Callback callback)override{callback(request.key,nullptr);} void CancelAll()noexcept override{}
};
}

AETHER_TEST("search UI: typed names, keyboard selection, safe fake launch and stable UIA IDs") {
    platform::ComApartment apartment;
    ManualDispatcher dispatcher;
    search::SearchService service(dispatcher);
    std::vector<apps::ApplicationInfo> apps;
    for(int i=0;i<30;++i){apps::ApplicationInfo app;app.id=L"test:"+std::to_wstring(i);app.displayName=L"Test Application "+std::to_wstring(i);apps.push_back(std::move(app));}
    REQUIRE(service.Rebuild(std::move(apps)).ok());
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(service.Current().state==search::IndexState::Building && std::chrono::steady_clock::now()<deadline){dispatcher.RunPending();std::this_thread::sleep_for(1ms);}
    REQUIRE_EQ(service.Current().state,search::IndexState::Ready);
    graphics::GraphicsDevice device;REQUIRE(device.Initialize().ok());
    platform::MessageLoop loop;graphics::FrameScheduler scheduler;REQUIRE(scheduler.Initialize(loop).ok());
    Launcher launcher;Icons icons;
    search::ui::SearchWindow window(device,scheduler,service,launcher,icons);
    REQUIRE(window.Initialize().ok());window.Open();
    ::SetWindowTextW(window.EditHandle(),L"Application 2");
    ::SendMessageW(window.EditHandle(),WM_KEYDOWN,VK_HOME,0); // Flush the pending query before checking its results.
    REQUIRE_FALSE(window.Results().empty());
    const auto first=window.Item(0);CHECK(first.id>0);CHECK(first.supportsInvoke);
    const auto name=first.name;
    ::SetWindowTextW(window.EditHandle(),L"Test Application");
    ::SendMessageW(window.EditHandle(),WM_KEYDOWN,VK_HOME,0);
    bool found=false;
    for(std::size_t i=0;i<window.ItemCount();++i)if(window.Item(i).name==name){CHECK_EQ(window.Item(i).id,first.id);found=true;}
    CHECK(found);
    ::SendMessageW(window.EditHandle(),WM_KEYDOWN,VK_DOWN,0);
    ::SendMessageW(window.EditHandle(),WM_KEYDOWN,VK_RETURN,0);
    CHECK_EQ(launcher.launches,1);CHECK_FALSE(window.IsOpen());
    CHECK_FALSE(window.Invoke(window.Item(0).id).ok());
    ::SendMessageW(window.EditHandle(),WM_KEYDOWN,VK_RETURN,0);
    CHECK_EQ(launcher.launches,1);CHECK_FALSE(window.Item(0).isEnabled);
    window.Open();::SendMessageW(window.EditHandle(),WM_KEYDOWN,VK_ESCAPE,0);
    CHECK(window.IsOpen());::SendMessageW(window.EditHandle(),WM_KEYDOWN,VK_ESCAPE,0);CHECK_FALSE(window.IsOpen());
}
