// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Additive taskbar pin sync policy with synthetic identities only.
#include "AetherTest.h"

#include "applications/ApplicationIdentity.h"
#include "applications/KnownApplications.h"
#include "applications/TaskbarPinSync.h"

#include <string>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

ApplicationInfo App(std::wstring name, std::wstring exe, std::wstring aumid = {}, std::wstring arguments = {}) {
    ApplicationInfo app;
    app.displayName = std::move(name);
    app.executablePath = std::move(exe);
    app.appUserModelId = std::move(aumid);
    app.arguments = std::move(arguments);
    if (!app.arguments.empty()) app.shortcutPath = L"C:\\Fixture\\Pins\\" + app.displayName + L".lnk";
    auto id = MakeApplicationId(app);
    REQUIRE(id.ok());
    app.id = std::move(id).value();
    return app;
}

PinIdentity Pin(const ApplicationInfo& app) {
    return PinIdentity{app.id, app.appUserModelId, app.executablePath, app.arguments};
}

ApplicationInfo Explorer() {
    ApplicationInfo explorer;
    explorer.displayName = L"File Explorer";
    explorer.appUserModelId = std::wstring(kFileExplorerAumid);
    explorer.parsingName = explorer.appUserModelId;
    explorer.executablePath = L"C:\\AetherFixtureWindows\\explorer.exe";
    explorer.id = FileExplorerId();
    return explorer;
}

std::vector<std::wstring> Ids(const std::vector<ApplicationInfo>& apps) {
    std::vector<std::wstring> ids;
    for (const auto& app : apps) ids.push_back(app.id);
    return ids;
}

// Applies a plan the way the app does: Explorer at the front, appended pins after the existing ones.
void Apply(PinSyncInput& input, const PinSyncPlan& plan) {
    if (plan.explorerAtFront) input.existing.insert(input.existing.begin(), Pin(*plan.explorerAtFront));
    for (const auto& app : plan.append) input.existing.push_back(Pin(app));
    if (plan.markExplorerApplied) input.explorerDefaultApplied = true;
    for (const auto& reinstated : plan.reinstatedIds) std::erase(input.dismissedIds, reinstated);
}

ApplicationInfo AlphaApp() { return App(L"Alpha", L"C:\\Fixture\\Alpha\\alpha.exe"); }
ApplicationInfo BetaApp() { return App(L"Beta", L"C:\\Fixture\\Beta\\beta.exe", L"Aether.Fixture.Beta"); }
ApplicationInfo GammaApp() { return App(L"Gamma", L"C:\\Fixture\\Gamma\\gamma.exe"); }

}  // namespace

AETHER_TEST("pin sync: an empty Dock gets Windows order with File Explorer at the front") {
    PinSyncInput input;
    input.windowsPins = {GammaApp(), AlphaApp(), BetaApp()};
    input.fileExplorer = Explorer();
    const auto plan = PlanTaskbarPinSync(input);
    REQUIRE(plan.ok());
    REQUIRE(plan.value().explorerAtFront.has_value());
    CHECK_EQ(plan.value().explorerAtFront->id, FileExplorerId());
    CHECK(Ids(plan.value().append) == Ids({GammaApp(), AlphaApp(), BetaApp()}));
    CHECK_EQ(plan.value().importedCount, std::size_t{3});
    CHECK(plan.value().markExplorerApplied);
    CHECK(plan.value().reinstatedIds.empty());
}

AETHER_TEST("pin sync: existing pins keep their order and new Windows pins are appended") {
    PinSyncInput input;
    input.existing = {Pin(BetaApp())};
    input.windowsPins = {AlphaApp(), BetaApp(), GammaApp(), AlphaApp()};
    input.explorerDefaultApplied = true;
    input.fileExplorer = Explorer();
    const auto plan = PlanTaskbarPinSync(input);
    REQUIRE(plan.ok());
    CHECK_FALSE(plan.value().explorerAtFront.has_value());
    CHECK_FALSE(plan.value().markExplorerApplied);  // already applied earlier
    CHECK(Ids(plan.value().append) == Ids({AlphaApp(), GammaApp()}));
    CHECK_EQ(plan.value().skippedExisting, std::size_t{2});  // Beta in the Dock, Alpha repeated
}

AETHER_TEST("pin sync: dismissed pins stay out unless the user imports manually") {
    PinSyncInput input;
    input.windowsPins = {AlphaApp(), BetaApp()};
    input.dismissedIds = {L"AUMID:AETHER.FIXTURE.BETA"};
    const auto automatic = PlanTaskbarPinSync(input);
    REQUIRE(automatic.ok());
    CHECK(Ids(automatic.value().append) == Ids({AlphaApp()}));
    CHECK_EQ(automatic.value().skippedDismissed, std::size_t{1});
    CHECK(automatic.value().reinstatedIds.empty());

    input.ignoreDismissed = true;
    const auto manual = PlanTaskbarPinSync(input);
    REQUIRE(manual.ok());
    CHECK(Ids(manual.value().append) == Ids({AlphaApp(), BetaApp()}));
    REQUIRE_EQ(manual.value().reinstatedIds.size(), std::size_t{1});
    CHECK_EQ(manual.value().reinstatedIds[0], std::wstring(L"AUMID:AETHER.FIXTURE.BETA"));
    CHECK_EQ(manual.value().skippedDismissed, std::size_t{0});

    // A pin dismissed under its executable id stays dismissed when it comes back with an AUMID.
    PinSyncInput reidentified;
    reidentified.windowsPins = {App(L"Alpha Again", L"C:\\Fixture\\Alpha\\alpha.exe", L"Aether.Fixture.Alpha")};
    reidentified.dismissedIds = {AlphaApp().id};
    const auto again = PlanTaskbarPinSync(reidentified);
    REQUIRE(again.ok());
    CHECK(again.value().append.empty());
    CHECK_EQ(again.value().skippedDismissed, std::size_t{1});
}

AETHER_TEST("pin sync: one application under an executable id and an AUMID id is not duplicated") {
    const auto exePin = App(L"Editor", L"C:\\Fixture\\Editor\\editor.exe");
    const auto aumidImport = App(L"Editor", L"C:\\Fixture\\Editor\\editor.exe", L"Aether.Fixture.Editor");
    CHECK_NE(exePin.id, aumidImport.id);
    CHECK(SameLaunchIdentity(Pin(exePin), aumidImport));

    PinSyncInput input;
    input.existing = {Pin(exePin)};
    input.windowsPins = {aumidImport};
    const auto plan = PlanTaskbarPinSync(input);
    REQUIRE(plan.ok());
    CHECK(plan.value().append.empty());
    CHECK_EQ(plan.value().skippedExisting, std::size_t{1});

    // Ids compare ignoring case.
    PinIdentity shouting{L"EXE:C:\\FIXTURE\\GAMMA\\GAMMA.EXE", L"", L"", L""};
    CHECK(SameLaunchIdentity(shouting, GammaApp()));
}

AETHER_TEST("pin sync: profiles of one executable stay distinct") {
    const auto first = App(L"Profile One", L"C:\\Fixture\\Browser\\browser.exe", L"Aether.Fixture.Browser.Profile1");
    const auto second = App(L"Profile Two", L"C:\\Fixture\\Browser\\browser.exe", L"Aether.Fixture.Browser.Profile2");
    CHECK_FALSE(SameLaunchIdentity(Pin(first), second));
    const auto argumentsOne = App(L"Args One", L"C:\\Fixture\\Browser\\browser.exe", {}, L"--profile=one");
    const auto argumentsTwo = App(L"Args Two", L"C:\\Fixture\\Browser\\browser.exe", {}, L"--profile=two");
    CHECK_FALSE(SameLaunchIdentity(Pin(argumentsOne), argumentsTwo));
    const auto bare = App(L"Bare", L"C:\\Fixture\\Browser\\browser.exe");
    CHECK_FALSE(SameLaunchIdentity(Pin(bare), argumentsOne));

    PinSyncInput input;
    input.existing = {Pin(first)};
    input.windowsPins = {second, argumentsOne, argumentsTwo};
    const auto plan = PlanTaskbarPinSync(input);
    REQUIRE(plan.ok());
    CHECK_EQ(plan.value().append.size(), std::size_t{3});
}

AETHER_TEST("pin sync: the pin limit is enforced and reported") {
    PinSyncInput input;
    input.existing = {Pin(App(L"One", L"C:\\Fixture\\One\\one.exe")), Pin(App(L"Two", L"C:\\Fixture\\Two\\two.exe"))};
    input.windowsPins = {AlphaApp(), BetaApp(), GammaApp()};
    input.maxPins = 3;
    const auto plan = PlanTaskbarPinSync(input);
    REQUIRE(plan.ok());
    CHECK(Ids(plan.value().append) == Ids({AlphaApp()}));
    CHECK_EQ(plan.value().skippedLimit, std::size_t{2});

    // Explorer takes its slot first; at the limit it is not marked, so it is offered again later.
    input.fileExplorer = Explorer();
    const auto withExplorer = PlanTaskbarPinSync(input);
    REQUIRE(withExplorer.ok());
    CHECK(withExplorer.value().explorerAtFront.has_value());
    CHECK(withExplorer.value().append.empty());
    CHECK_EQ(withExplorer.value().skippedLimit, std::size_t{3});
    input.existing.push_back(Pin(GammaApp()));
    const auto full = PlanTaskbarPinSync(input);
    REQUIRE(full.ok());
    CHECK_FALSE(full.value().explorerAtFront.has_value());
    CHECK_FALSE(full.value().markExplorerApplied);

    input.maxPins = kMaxDockPins + 1;
    const auto invalid = PlanTaskbarPinSync(input);
    REQUIRE_FALSE(invalid.ok());
    CHECK_EQ(invalid.error().code, ErrorCode::InvalidArgument);

    PinSyncInput withInvalid;
    ApplicationInfo nameless;
    nameless.displayName = L"No identity";
    withInvalid.windowsPins = {nameless, AlphaApp()};
    const auto skipped = PlanTaskbarPinSync(withInvalid);
    REQUIRE(skipped.ok());
    CHECK_EQ(skipped.value().skippedInvalid, std::size_t{1});
    CHECK_EQ(skipped.value().append.size(), std::size_t{1});
}

AETHER_TEST("pin sync: a second run adds nothing") {
    PinSyncInput input;
    input.existing = {Pin(BetaApp())};
    input.windowsPins = {AlphaApp(), GammaApp(), BetaApp()};
    input.fileExplorer = Explorer();
    input.dismissedIds = {GammaApp().id};
    input.ignoreDismissed = true;
    const auto first = PlanTaskbarPinSync(input);
    REQUIRE(first.ok());
    CHECK_EQ(first.value().importedCount, std::size_t{2});
    Apply(input, first.value());
    CHECK(input.dismissedIds.empty());
    CHECK_EQ(input.existing.front().id, FileExplorerId());

    const auto second = PlanTaskbarPinSync(input);
    REQUIRE(second.ok());
    CHECK(second.value().append.empty());
    CHECK_FALSE(second.value().explorerAtFront.has_value());
    CHECK_FALSE(second.value().markExplorerApplied);
    CHECK_EQ(second.value().skippedExisting, std::size_t{3});
}

AETHER_TEST("pin sync: File Explorer is never re-added once applied or dismissed") {
    PinSyncInput applied;
    applied.fileExplorer = Explorer();
    applied.explorerDefaultApplied = true;
    const auto afterUnpin = PlanTaskbarPinSync(applied);
    REQUIRE(afterUnpin.ok());
    CHECK_FALSE(afterUnpin.value().explorerAtFront.has_value());

    PinSyncInput dismissed;
    dismissed.fileExplorer = Explorer();
    dismissed.dismissedIds = {FileExplorerId()};
    dismissed.ignoreDismissed = true;  // a manual import reinstates Windows pins, not the default
    const auto refused = PlanTaskbarPinSync(dismissed);
    REQUIRE(refused.ok());
    CHECK_FALSE(refused.value().explorerAtFront.has_value());
    CHECK(refused.value().markExplorerApplied);

    // Windows already pins File Explorer: it keeps its Windows position, with no second copy.
    PinSyncInput pinned;
    pinned.fileExplorer = Explorer();
    auto windowsExplorer = Explorer();
    windowsExplorer.displayName = L"Fixture Explorer Shortcut";
    pinned.windowsPins = {AlphaApp(), windowsExplorer};
    const auto inWindows = PlanTaskbarPinSync(pinned);
    REQUIRE(inWindows.ok());
    CHECK_FALSE(inWindows.value().explorerAtFront.has_value());
    CHECK(inWindows.value().markExplorerApplied);
    CHECK(Ids(inWindows.value().append) == Ids({AlphaApp(), windowsExplorer}));

    // An older executable-only Explorer pin in the Dock counts as File Explorer.
    PinSyncInput legacy;
    legacy.fileExplorer = Explorer();
    legacy.existing = {PinIdentity{L"exe:c:\\aetherfixturewindows\\explorer.exe", L"",
                                   L"C:\\AetherFixtureWindows\\explorer.exe", L""}};
    const auto legacyPlan = PlanTaskbarPinSync(legacy);
    REQUIRE(legacyPlan.ok());
    CHECK_FALSE(legacyPlan.value().explorerAtFront.has_value());
    CHECK(legacyPlan.value().markExplorerApplied);
}
