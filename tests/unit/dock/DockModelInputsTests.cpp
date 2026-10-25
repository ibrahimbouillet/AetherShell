// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The DockModelInputs entry point and the fixed Start / Task View kinds.
#include "AetherTest.h"

#include "dock/DockModel.h"
#include "applications/KnownApplications.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::dock;

namespace {

settings::PinnedApp Pin(std::wstring id) {
    settings::PinnedApp pin; pin.id = std::move(id); pin.displayName = L"Synthetic pinned app"; return pin;
}

apps::RunningApplication Running(std::wstring id, bool foreground = false) {
    apps::RunningApplication running; running.id = std::move(id); running.displayName = L"Synthetic running app";
    running.foreground = foreground; running.windows.push_back({reinterpret_cast<HWND>(1), L"Synthetic window", false});
    return running;
}

}  // namespace

AETHER_TEST("dock model inputs: the forwarding overload and the inputs overload build the same model") {
    settings::DockSettings config; config.pinned = {Pin(L"exe:a"), Pin(L"exe:b")};
    const std::vector running{Running(L"exe:b", true), Running(L"exe:c"), Running(L"exe:d")};
    auto legacy = BuildDockItems(config, running); REQUIRE(legacy.ok());
    auto inputs = BuildDockItems(DockModelInputs{config, running, {}}); REQUIRE(inputs.ok());
    REQUIRE_EQ(inputs->size(), legacy->size());
    for (std::size_t index = 0; index < legacy->size(); ++index) {
        CHECK_EQ((*inputs)[index].kind, (*legacy)[index].kind);
        CHECK_EQ((*inputs)[index].id, (*legacy)[index].id);
        CHECK_EQ((*inputs)[index].running, (*legacy)[index].running);
        CHECK_EQ((*inputs)[index].foreground, (*legacy)[index].foreground);
        CHECK_EQ((*inputs)[index].windowCount, (*legacy)[index].windowCount);
    }
    // Session positions apply after the fixed entries without changing the persisted pins.
    const std::vector<std::wstring> order{L"exe:d", L"exe:a", L"exe:c", L"exe:b"};
    auto ordered = BuildDockItems(DockModelInputs{.settings = config, .running = running, .sessionOrder = order});
    REQUIRE(ordered.ok());
    REQUIRE_EQ(ordered->size(), 7u);
    CHECK_EQ((*ordered)[3].id, std::wstring(L"exe:d"));
    CHECK_EQ((*ordered)[4].id, std::wstring(L"exe:a"));
    CHECK_EQ((*ordered)[5].id, std::wstring(L"exe:c"));
    CHECK_EQ((*ordered)[6].id, std::wstring(L"exe:b"));
}

AETHER_TEST("dock model inputs: configurable fixed entries precede applications and never move") {
    CHECK(IsReorderable(DockItemKind::Pinned));
    CHECK(IsReorderable(DockItemKind::Running));
    for (const auto kind : {DockItemKind::Search, DockItemKind::Overflow, DockItemKind::Start, DockItemKind::TaskView}) {
        CHECK_FALSE(IsReorderable(kind));
        CHECK_FALSE(IsApplicationItem(kind));
    }
    CHECK(IsApplicationItem(DockItemKind::Pinned));
    CHECK(IsApplicationItem(DockItemKind::Running));
    // The new kinds are appended, so existing values keep their numbers.
    CHECK_EQ(static_cast<int>(DockItemKind::Overflow), 3);
    CHECK_EQ(static_cast<int>(DockItemKind::Start), 4);
    CHECK_EQ(static_cast<int>(DockItemKind::TaskView), 5);
    settings::DockSettings config;
    CHECK(config.showStartButton);
    CHECK(config.showTaskViewButton);
    config.pinned = {Pin(L"exe:a")};
    auto model = BuildDockItems(DockModelInputs{config, std::vector{Running(L"exe:b")}, {}});
    REQUIRE(model.ok());
    REQUIRE_EQ(model->size(),5u);
    CHECK_EQ((*model)[0].kind,DockItemKind::Start);CHECK_EQ((*model)[1].kind,DockItemKind::Search);
    CHECK_EQ((*model)[2].kind,DockItemKind::TaskView);
    config.showStartButton=false;config.showTaskViewButton=false;
    auto compact=BuildDockItems(config,{});REQUIRE(compact.ok());CHECK_EQ(compact->size(),2u);
    CHECK_EQ((*compact)[0].kind,DockItemKind::Search);
}

AETHER_TEST("dock presentation: unmatched explicit profiles activate without unsafe relaunch or pin routes") {
    settings::DockSettings config;config.showStartButton=false;config.showTaskViewButton=false;
    auto running=Running(L"aumid:Synthetic.Profile");running.appUserModelId=L"Synthetic.Profile";
    running.executablePath=LR"(C:\AetherFixture\browser.exe)";
    running.relaunchIconResource=LR"(C:\AetherFixture\profile.ico,0)";
    const std::vector group{running};
    auto first=BuildDockItems(DockModelInputs{config,group,{}});REQUIRE(first.ok());
    REQUIRE_EQ(first->size(),2u);const auto before=apps::IconSourceFingerprint((*first)[1].application);
    CHECK((*first)[1].runningApplication.has_value());CHECK_FALSE(apps::HasLaunchRoute((*first)[1].application));
    CHECK_FALSE(PinFromApplication((*first)[1].application).ok());
    apps::ApplicationInfo profile;profile.id=running.id;profile.appUserModelId=running.appUserModelId;
    profile.shortcutPath=LR"(C:\AetherFixture\trusted-profile.lnk)";profile.arguments=L"--profile=Synthetic";
    apps::ApplicationIndex index({profile});
    auto resolved=BuildDockItems(DockModelInputs{config,group,{},&index});REQUIRE(resolved.ok());
    CHECK(apps::HasLaunchRoute((*resolved)[1].application));
    CHECK_EQ((*resolved)[1].application.arguments,profile.arguments);
    CHECK(apps::IconSourceFingerprint((*resolved)[1].application)!=before);
    CHECK(PinFromApplication((*resolved)[1].application).ok());
}

AETHER_TEST("dock overflow: extreme width retains Search and keeps fixed entries reachable") {
    settings::DockSettings config;
    auto model=BuildDockItems(config,{});REQUIRE(model.ok());
    auto fitted=FitDockItems(*model,config,56);REQUIRE(fitted.ok());
    REQUIRE_EQ(fitted->items.size(),1u);CHECK_EQ(fitted->items[0].kind,DockItemKind::Search);
    CHECK_EQ(fitted->overflow.size(),2u);
}

AETHER_TEST("dock presentation: a generic executable pin cannot absorb distinct profile identities") {
    settings::DockSettings config;config.showStartButton=false;config.showTaskViewButton=false;
    auto pin=Pin(L"exe:synthetic-browser");pin.executablePath=LR"(C:\AetherFixture\browser.exe)";config.pinned={pin};
    auto a=Running(L"aumid:Synthetic.Profile.A");a.appUserModelId=L"Synthetic.Profile.A";a.executablePath=pin.executablePath;
    auto b=Running(L"aumid:Synthetic.Profile.B");b.appUserModelId=L"Synthetic.Profile.B";b.executablePath=pin.executablePath;
    auto model=BuildDockItems(config,{a,b});REQUIRE(model.ok());REQUIRE_EQ(model->size(),4u);
    CHECK_FALSE((*model)[1].running);CHECK_EQ((*model)[2].id,a.id);CHECK_EQ((*model)[3].id,b.id);
}

AETHER_TEST("dock presentation: a legacy Explorer executable pin matches the canonical running group") {
    settings::DockSettings config;config.showStartButton=false;config.showTaskViewButton=false;
    auto pin=Pin(L"exe:synthetic-legacy-explorer");pin.executablePath=LR"(C:\Windows\explorer.exe)";config.pinned={pin};
    auto running=Running(apps::FileExplorerId());running.appUserModelId=apps::kFileExplorerAumid;
    running.executablePath=LR"(c:\windows\explorer.exe)";
    auto model=BuildDockItems(config,{running});REQUIRE(model.ok());REQUIRE_EQ(model->size(),2u);
    CHECK((*model)[1].running);
}

AETHER_TEST("dock presentation: a unique desktop AUMID pin absorbs its executable-only window") {
    settings::DockSettings config; config.showStartButton = false; config.showTaskViewButton = false;
    auto pin = Pin(L"aumid:Synthetic.Editor"); pin.appUserModelId = L"Synthetic.Editor";
    pin.executablePath = LR"(C:\AetherFixture\Editor.exe)";
    pin.shortcutPath = LR"(C:\AetherFixture\Editor.lnk)"; config.pinned = {pin};
    auto running = Running(L"exe:synthetic-editor", true);
    running.executablePath = L"c:/aetherfixture/editor.exe";
    auto model = BuildDockItems(config, {running}); REQUIRE(model.ok()); REQUIRE_EQ(model->size(), 2u);
    const auto& item = (*model)[1];
    CHECK_EQ(item.kind, DockItemKind::Pinned); CHECK_EQ(item.id, pin.id);
    CHECK(item.running); CHECK(item.foreground); CHECK_EQ(item.windowCount, 1u);
    REQUIRE(item.runningApplication.has_value());
    CHECK_EQ(item.runningApplication->windows[0].handle, running.windows[0].handle);
    CHECK_EQ(item.application.shortcutPath, pin.shortcutPath);
    running.id = pin.id; running.appUserModelId = pin.appUserModelId;
    auto identified = BuildDockItems(config, {running}); REQUIRE(identified.ok());
    REQUIRE_EQ(identified->size(), 2u); CHECK_EQ((*identified)[1].id, item.id);
}

AETHER_TEST("dock presentation: missing AUMID does not guess conflicting installs or profiles") {
    settings::DockSettings config; config.showStartButton = false; config.showTaskViewButton = false;
    auto pin = Pin(L"aumid:Synthetic.Editor"); pin.appUserModelId = L"Synthetic.Editor";
    pin.executablePath = LR"(C:\AetherFixture\Editor.exe)";
    auto running = Running(L"exe:synthetic-editor"); running.executablePath = pin.executablePath;
    for (int variant = 0; variant < 5; ++variant) {
        auto selected = pin; auto window = running;
        if (variant == 0) window.appUserModelId = L"Synthetic.Other";
        if (variant == 1) window.executablePath = LR"(C:\AetherOtherFixture\Editor.exe)";
        if (variant == 2) selected.arguments = L"--profile=Synthetic";
        if (variant == 3) selected.appUserModelId = L"Synthetic.Editor_123456789abcd!App";
        if (variant == 4) window.executablePath = L"Editor.exe";
        config.pinned = {selected};
        auto model = BuildDockItems(config, {window}); REQUIRE(model.ok()); REQUIRE_EQ(model->size(), 3u);
        CHECK_FALSE((*model)[1].running); CHECK((*model)[2].running);
    }
    config.pinned = {pin};
    apps::ApplicationInfo profile; profile.id = L"aumid:Synthetic.Profile";
    profile.appUserModelId = L"Synthetic.Profile"; profile.executablePath = pin.executablePath;
    apps::ApplicationIndex catalog({profile}); const std::vector windows{running};
    auto ambiguous = BuildDockItems(DockModelInputs{config, windows, {}, &catalog});
    REQUIRE(ambiguous.ok()); REQUIRE_EQ(ambiguous->size(), 3u); CHECK_FALSE((*ambiguous)[1].running);
}

AETHER_TEST("dock presentation: an unidentified window never runs on two candidate pins") {
    settings::DockSettings config; config.showStartButton = false; config.showTaskViewButton = false;
    auto pin = Pin(L"aumid:Synthetic.Editor"); pin.appUserModelId = L"Synthetic.Editor";
    pin.executablePath = LR"(C:\AetherFixture\Editor.exe)";
    auto other = pin; other.id = L"aumid:Synthetic.Other"; other.appUserModelId = L"Synthetic.Other";
    config.pinned = {pin, other};
    auto running = Running(L"exe:synthetic-editor"); running.executablePath = pin.executablePath;
    auto ambiguous = BuildDockItems(config, {running}); REQUIRE(ambiguous.ok()); REQUIRE_EQ(ambiguous->size(), 4u);
    CHECK_FALSE((*ambiguous)[1].running); CHECK_FALSE((*ambiguous)[2].running);
    other.id = running.id; other.appUserModelId.clear(); config.pinned = {pin, other};
    auto exact = BuildDockItems(config, {running}); REQUIRE(exact.ok()); REQUIRE_EQ(exact->size(), 3u);
    CHECK_FALSE((*exact)[1].running); CHECK((*exact)[2].running);
}

AETHER_TEST("dock session: external pin order and new pins preserve running application slots") {
    const std::vector<std::wstring> session{L"exe:running-a",L"exe:pin-a",L"exe:running-b",L"exe:pin-b"};
    const std::vector pins{Pin(L"exe:pin-b"),Pin(L"exe:pin-a"),Pin(L"exe:pin-new")};
    auto aligned=AlignSessionPins(session,pins);REQUIRE(aligned.ok());REQUIRE_EQ(aligned->size(),5u);
    CHECK_EQ((*aligned)[0],L"exe:running-a");CHECK_EQ((*aligned)[1],L"exe:pin-b");
    CHECK_EQ((*aligned)[2],L"exe:running-b");CHECK_EQ((*aligned)[3],L"exe:pin-a");CHECK_EQ((*aligned)[4],L"exe:pin-new");
}

AETHER_TEST("dock presentation: a pinned profile accepts a late window avatar without changing its launch route") {
    settings::DockSettings config;config.showStartButton=false;config.showTaskViewButton=false;
    auto pin=Pin(L"aumid:Synthetic.Profile");pin.appUserModelId=L"Synthetic.Profile";
    pin.shortcutPath=LR"(C:\AetherFixture\profile.lnk)";pin.arguments=L"--profile=Synthetic";config.pinned={pin};
    auto running=Running(pin.id);running.appUserModelId=pin.appUserModelId;
    auto before=BuildDockItems(config,{running});REQUIRE(before.ok());const auto key=apps::IconSourceFingerprint((*before)[1].application);
    running.relaunchIconResource=LR"(C:\AetherFixture\avatar.ico,0)";
    auto after=BuildDockItems(config,{running});REQUIRE(after.ok());
    CHECK_EQ((*after)[1].application.iconResource,running.relaunchIconResource);
    CHECK_EQ((*after)[1].application.shortcutPath,pin.shortcutPath);CHECK_EQ((*after)[1].application.arguments,pin.arguments);
    CHECK(apps::IconSourceFingerprint((*after)[1].application)!=key);
}

AETHER_TEST("dock attention: pinned and duplicate running groups combine only real background requests") {
    settings::DockSettings config;config.showStartButton=false;config.showTaskViewButton=false;
    config.pinned={Pin(L"exe:pinned")};
    auto pinnedA=Running(L"exe:pinned"),pinnedB=pinnedA;pinnedB.attentionRequested=true;
    auto runningA=Running(L"exe:running"),runningB=runningA;runningB.attentionRequested=true;
    auto model=BuildDockItems(config,{pinnedA,pinnedB,runningA,runningB});REQUIRE(model.ok());
    REQUIRE_EQ(model->size(),3u);CHECK_FALSE((*model)[0].attentionRequested);
    for(const std::size_t i:{1u,2u}){
        CHECK((*model)[i].attentionRequested);REQUIRE((*model)[i].runningApplication.has_value());
        CHECK((*model)[i].runningApplication->attentionRequested);CHECK_EQ((*model)[i].windowCount,2u);
    }
    pinnedA.foreground=true;runningA.foreground=true;
    auto active=BuildDockItems(config,{pinnedA,pinnedB,runningA,runningB});REQUIRE(active.ok());
    for(const std::size_t i:{1u,2u}){
        CHECK_FALSE((*active)[i].attentionRequested);CHECK((*active)[i].foreground);
        CHECK_FALSE((*active)[i].runningApplication->attentionRequested);CHECK((*active)[i].runningApplication->foreground);
    }
    auto stopped=BuildDockItems(config,{});REQUIRE(stopped.ok());CHECK_FALSE((*stopped)[1].attentionRequested);
}

AETHER_TEST("dock attention: profile identity and overflow preserve independent attention") {
    settings::DockSettings config;config.showStartButton=false;config.showTaskViewButton=false;
    config.pinned={Pin(L"aumid:Synthetic.A")};config.pinned[0].appUserModelId=L"Synthetic.A";
    auto a=Running(L"aumid:Synthetic.A"),b=Running(L"aumid:Synthetic.B");
    a.appUserModelId=L"Synthetic.A";b.appUserModelId=L"Synthetic.B";b.attentionRequested=true;
    auto model=BuildDockItems(config,{a,b});REQUIRE(model.ok());REQUIRE_EQ(model->size(),3u);
    CHECK_FALSE((*model)[1].attentionRequested);CHECK((*model)[2].attentionRequested);
    auto fitted=FitDockItems(*model,config,56);REQUIRE(fitted.ok());
    const auto waiting=std::find_if(fitted->overflow.begin(),fitted->overflow.end(),[&](const auto& item){return item.id==b.id;});
    REQUIRE(waiting!=fitted->overflow.end());CHECK(waiting->attentionRequested);
    CHECK(fitted->items.front().attentionRequested); // Search exposes hidden applications at this width.
    auto more=FitDockItems(*model,config,160);REQUIRE(more.ok());
    REQUIRE_EQ(more->items.back().kind,DockItemKind::Overflow);CHECK(more->items.back().attentionRequested);
}
