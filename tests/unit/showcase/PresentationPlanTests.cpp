// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "showcase/PresentationPlan.h"
#include <limits>
#include <stdexcept>
using namespace aether;
using namespace aether::showcase;
namespace {
struct FakePresentation final : IPresentationActions {
    int prepares=0,restores=0;
    bool volume=false,failPrepare=false,failAction=false,throwAction=false;
    std::vector<PresentationAction> actions;
    Result<void> Prepare(bool changed) override {
        ++prepares;volume=changed;
        return failPrepare?Result<void>(MakeError(ErrorCode::Unavailable,"fake preparation failure")):Ok();
    }
    Result<void> Apply(const PresentationAction& action) override {
        if(throwAction)throw std::runtime_error("fake action");
        if(failAction)return MakeError(ErrorCode::Unavailable,"fake action failure");
        actions.push_back(action);return Ok();
    }
    Result<void> Restore() noexcept override {++restores;return Ok();}
};
}
AETHER_TEST("showcase presentation: named scenarios are bounded and only volume requires device changes") {
    for(const auto* name:{"desktop","quick-controls","settings","volume","gallery","gallery-volume","gallery-full","gallery-full-volume"}) {
        auto plan=MakePresentationPlan(name);REQUIRE(plan);CHECK(ValidatePresentationPlan(*plan));
        FakePresentation fake;PresentationSession run(fake);
        const bool volume=std::string_view(name)=="volume"||std::string_view(name)=="gallery-volume"||std::string_view(name)=="gallery-full-volume";
        CHECK(run.Begin(*plan,!volume).ok()!=volume);
        CHECK_EQ(fake.prepares,volume?0:1);
    }
    CHECK(!MakePresentationPlan("arbitrary-application"));
}
AETHER_TEST("showcase presentation: full gallery covers real panels and menus within thirty seconds") {
    auto plan=MakePresentationPlan("gallery-full");REQUIRE(plan);REQUIRE(ValidatePresentationPlan(*plan));
    CHECK_EQ(plan->durationSeconds,24.5);
    int pages=0,search=0,explorerMenu=0,topbarMenu=0,secondControls=0;
    for(const auto& action:plan->actions){
        if(action.kind==PresentationActionKind::SettingsPage)++pages;
        if(action.kind==PresentationActionKind::OpenSearchExplorer)++search;
        if(action.kind==PresentationActionKind::DockExplorerMenu)++explorerMenu;
        if(action.kind==PresentationActionKind::TopBarMenu)++topbarMenu;
        if(action.kind==PresentationActionKind::QuickControlsNextPage)++secondControls;
    }
    CHECK_EQ(pages,8);CHECK_EQ(search,1);CHECK_EQ(explorerMenu,1);CHECK_EQ(topbarMenu,1);CHECK_EQ(secondControls,1);
    CHECK(!ParsePresentationPlan("AETHER_PRESENTATION_1 5 2 1 1 7 1"));
    CHECK(!ParsePresentationPlan("AETHER_PRESENTATION_1 5 2 1 1 11 0"));
}
AETHER_TEST("showcase presentation: observed completion selects settled scenes and rejects missed actions") {
    auto plan=MakePresentationPlan("gallery-full");REQUIRE(plan);
    std::vector<double> started,completed;
    for(const auto& action:plan->actions){started.push_back(action.atSeconds);completed.push_back(action.atSeconds+0.05);}
    REQUIRE(ValidatePresentationObservations(*plan,started,completed,plan->actions.size()));
    CHECK(!ValidatePresentationObservations(*plan,started,completed,plan->actions.size()-1));
    const auto saved=completed[0];completed[0]=started[1];
    CHECK(!ValidatePresentationObservations(*plan,started,completed,plan->actions.size()));
    completed[0]=saved;completed.back()=std::numeric_limits<double>::quiet_NaN();
    CHECK(!ValidatePresentationObservations(*plan,started,completed,plan->actions.size()));
}
AETHER_TEST("showcase presentation: deterministic due actions never execute twice") {
    auto plan=MakePresentationPlan("volume");REQUIRE(plan);
    FakePresentation fake;
    {PresentationSession run(fake);REQUIRE(run.Begin(*plan,true));CHECK(fake.volume);
        REQUIRE(run.Tick(0));CHECK(fake.actions.empty());
        REQUIRE(run.Tick(0.5));CHECK_EQ(fake.actions.size(),1U);
        REQUIRE(run.Tick(0.5));CHECK_EQ(fake.actions.size(),1U);
        REQUIRE(run.Tick(4));CHECK_EQ(fake.actions.size(),plan->actions.size());
        CHECK(!run.Tick(3));CHECK(!run.Tick(std::numeric_limits<double>::infinity()));
        REQUIRE(run.Restore());REQUIRE(run.Restore());CHECK_EQ(fake.restores,1);
    }CHECK_EQ(fake.restores,1);
}
AETHER_TEST("showcase presentation: partial preparation action failure and exceptions restore exactly once") {
    auto plan=MakePresentationPlan("quick-controls");REQUIRE(plan);
    for(int mode=0;mode<3;++mode){
        FakePresentation fake;fake.failPrepare=mode==0;fake.failAction=mode==1;fake.throwAction=mode==2;
        {PresentationSession run(fake);
            if(mode==0)CHECK(!run.Begin(*plan,false));
            else {REQUIRE(run.Begin(*plan,false));CHECK(!run.Tick(1));}
            CHECK_EQ(fake.restores,1);
        }CHECK_EQ(fake.restores,1);
    }
}
AETHER_TEST("showcase presentation: numeric plans reject unknown actions private trailing data and excessive bounds") {
    auto plan=ParsePresentationPlan("AETHER_PRESENTATION_1\n6 2 2\n0.5 1 0\n4 5 0\n");
    REQUIRE(plan);CHECK_EQ(plan->actions.size(),2U);
    for(const auto* text:{"AETHER_PRESENTATION_1 31 2 0","AETHER_PRESENTATION_1 5 5 0",
        "AETHER_PRESENTATION_1 5 2 129","AETHER_PRESENTATION_1 5 2 1 1 99 0",
        "AETHER_PRESENTATION_1 5 2 1 1 3 31","AETHER_PRESENTATION_1 5 2 1 1 1 1",
        "AETHER_PRESENTATION_1 5 2 2 2 1 0 1 5 0","AETHER_PRESENTATION_1 5 2 0 trailing"})
        CHECK(!ParsePresentationPlan(text));
}
AETHER_TEST("showcase presentation: concurrent caller can defer failure cleanup until acquisition joins") {
    auto plan=MakePresentationPlan("quick-controls");REQUIRE(plan);
    FakePresentation fake;fake.failAction=true;
    {
        PresentationSession run(fake,false);REQUIRE(run.Begin(*plan,false));
        CHECK(!run.Tick(1));CHECK_EQ(fake.restores,0);
        // The owner of the acquisition worker joins it before calling Restore or leaving scope.
        REQUIRE(run.Restore());CHECK_EQ(fake.restores,1);
    }
    CHECK_EQ(fake.restores,1);
}
