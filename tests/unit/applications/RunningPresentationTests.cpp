// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
// Presentation of unpinned running groups. Synthetic identities and paths only (no real browser,
// profile or window is involved).
#include "AetherTest.h"

#include "applications/ApplicationIdentity.h"
#include "applications/RunningPresentation.h"

#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <string>
#include <vector>

using namespace aether;
using namespace aether::apps;

namespace {

constexpr wchar_t kBrowserExe[] = L"C:\\Fixture\\Browser\\browser.exe";
constexpr wchar_t kBrowserAumid[] = L"Aether.Fixture.Browser";
constexpr wchar_t kProfileOneAumid[] = L"Aether.Fixture.Browser.UserData.Profile1";
constexpr wchar_t kProfileTwoAumid[] = L"Aether.Fixture.Browser.UserData.Profile2";

ApplicationInfo WithId(ApplicationInfo app) {
    auto id = MakeApplicationId(app);
    REQUIRE(id.ok());
    app.id = std::move(id).value();
    return app;
}

std::vector<ApplicationInfo> Catalog() {
    ApplicationInfo browser;
    browser.displayName = L"Fixture Browser";
    browser.appUserModelId = kBrowserAumid;
    browser.parsingName = kBrowserAumid;
    browser.executablePath = kBrowserExe;
    browser.shortcutPath = L"C:\\Fixture\\Start\\Fixture Browser.lnk";

    ApplicationInfo profileTwo;
    profileTwo.displayName = L"Fixture Profile Two";
    profileTwo.appUserModelId = kProfileTwoAumid;
    profileTwo.executablePath = kBrowserExe;
    profileTwo.arguments = L"--profile-directory=\"Profile 2\"";
    profileTwo.shortcutPath = L"C:\\Fixture\\Start\\Fixture Profile Two.lnk";

    ApplicationInfo editor;
    editor.displayName = L"Fixture Editor";
    editor.executablePath = L"C:\\Fixture\\Editor\\editor.exe";
    editor.parsingName = L"{00000000-0000-0000-0000-000000000000}\\Fixture\\editor.exe";
    editor.shortcutPath = L"C:\\Fixture\\Start\\Fixture Editor.lnk";

    ApplicationInfo packaged;
    packaged.displayName = L"Fixture Packaged";
    packaged.kind = ApplicationKind::Packaged;
    packaged.appUserModelId = L"Aether.Fixture_123456789abcd!App";
    packaged.parsingName = packaged.appUserModelId;

    return {WithId(browser), WithId(profileTwo), WithId(editor), WithId(packaged)};
}

RunningApplication Running(std::wstring aumid, std::wstring exe, std::wstring name = L"Fixture Window Group") {
    RunningApplication running;
    running.appUserModelId = std::move(aumid);
    running.executablePath = std::move(exe);
    running.displayName = std::move(name);
    running.id = !running.appUserModelId.empty() ? MakeIdForAumid(running.appUserModelId).value()
                                                 : MakeIdForExecutable(running.executablePath).value();
    running.windows.push_back(RunningWindow{reinterpret_cast<HWND>(std::uintptr_t{0x10}), L"window", false});
    return running;
}

}  // namespace

AETHER_TEST("running presentation: an exact AUMID catalog match supplies launch and icon metadata") {
    const ApplicationIndex index(Catalog());
    auto running = Running(kProfileTwoAumid, kBrowserExe, L"Fixture Profile Name");
    running.relaunchIconResource = L"C:\\Fixture\\Profile2\\Avatar.ico,0";
    const auto app = ResolveRunningPresentation(running, index);
    CHECK_EQ(app.id, running.id);  // running identity is preserved
    CHECK_EQ(app.displayName, std::wstring(L"Fixture Profile Name"));
    CHECK_EQ(app.shortcutPath, std::wstring(L"C:\\Fixture\\Start\\Fixture Profile Two.lnk"));
    CHECK_EQ(app.arguments, std::wstring(L"--profile-directory=\"Profile 2\""));
    CHECK_EQ(app.executablePath, std::wstring(kBrowserExe));
    CHECK_EQ(app.appUserModelId, std::wstring(kProfileTwoAumid));
    CHECK_EQ(app.kind, ApplicationKind::Desktop);
    CHECK(app.iconResource.empty());  // the matched shortcut provides the icon
    CHECK(HasLaunchRoute(app));

    // Case does not matter for the AUMID.
    const auto upper = ResolveRunningPresentation(Running(L"AETHER.FIXTURE.BROWSER", kBrowserExe), index);
    CHECK_EQ(upper.parsingName, std::wstring(kBrowserAumid));
}

AETHER_TEST("running presentation: an unmatched profile never launches the shared executable") {
    const ApplicationIndex index(Catalog());
    auto running = Running(kProfileOneAumid, kBrowserExe, L"Fixture Profile One");
    running.relaunchIconResource = L"C:\\Fixture\\Profile1\\Avatar.ico,0";
    const auto app = ResolveRunningPresentation(running, index);
    CHECK_EQ(app.id, running.id);
    CHECK(app.executablePath.empty());  // rule (d): the bare executable would open another profile
    CHECK(app.parsingName.empty());
    CHECK(app.shortcutPath.empty());
    CHECK_EQ(app.appUserModelId, std::wstring(kProfileOneAumid));
    CHECK_EQ(app.iconResource, std::wstring(L"C:\\Fixture\\Profile1\\Avatar.ico,0"));
    CHECK_EQ(app.kind, ApplicationKind::Other);
    CHECK_FALSE(HasLaunchRoute(app));

    // Unsupported icon forms are not carried.
    running.relaunchIconResource = L"C:\\Fixture\\Browser\\browser.exe,0";
    CHECK(ResolveRunningPresentation(running, index).iconResource.empty());
}

AETHER_TEST("running presentation: without a match or a conflict the running executable stays launchable") {
    const ApplicationIndex index(Catalog());
    const auto explicitOnly = ResolveRunningPresentation(
        Running(L"Aether.Fixture.Standalone", L"C:\\Fixture\\Standalone\\standalone.exe"), index);
    CHECK_EQ(explicitOnly.executablePath, std::wstring(L"C:\\Fixture\\Standalone\\standalone.exe"));
    CHECK_EQ(explicitOnly.kind, ApplicationKind::Desktop);
    CHECK(HasLaunchRoute(explicitOnly));

    const auto empty = ResolveRunningPresentation(Running(kProfileOneAumid, kBrowserExe), ApplicationIndex{});
    CHECK_EQ(empty.executablePath, std::wstring(kBrowserExe));  // no catalog: nothing to conflict with
    CHECK(HasLaunchRoute(empty));
}

AETHER_TEST("running presentation: an executable without an AUMID copies only an unambiguous entry") {
    const ApplicationIndex index(Catalog());
    const auto editor = ResolveRunningPresentation(Running(L"", L"c:/fixture/EDITOR/editor.exe", L""), index);
    CHECK_EQ(editor.parsingName, std::wstring(L"{00000000-0000-0000-0000-000000000000}\\Fixture\\editor.exe"));
    CHECK_EQ(editor.shortcutPath, std::wstring(L"C:\\Fixture\\Start\\Fixture Editor.lnk"));
    CHECK_EQ(editor.displayName, std::wstring(L"Fixture Editor"));  // the catalog's when the running one is empty

    // The browser executable is started without arguments by one entry only, so it matches; with a
    // second argument-less entry it would be ambiguous and only running facts remain.
    auto catalog = Catalog();
    ApplicationInfo duplicate;
    duplicate.displayName = L"Fixture Duplicate";
    duplicate.executablePath = L"C:\\Fixture\\Editor\\editor.exe";
    duplicate.shortcutPath = L"C:\\Fixture\\Start\\Duplicate.lnk";
    duplicate.id = L"shell:fixture-duplicate";
    catalog.push_back(duplicate);
    const ApplicationIndex ambiguous(std::move(catalog));
    const auto unresolved = ResolveRunningPresentation(Running(L"", L"C:\\Fixture\\Editor\\editor.exe"), ambiguous);
    CHECK(unresolved.shortcutPath.empty());
    CHECK(unresolved.parsingName.empty());
    CHECK_EQ(unresolved.executablePath, std::wstring(L"C:\\Fixture\\Editor\\editor.exe"));
    CHECK(HasLaunchRoute(unresolved));
}

AETHER_TEST("running presentation: packaged and window-only identities are kept") {
    const ApplicationIndex index(Catalog());
    const auto packaged = ResolveRunningPresentation(Running(L"Aether.Other_123456789abcd!App", L""), ApplicationIndex{});
    CHECK_EQ(packaged.id, MakeIdForAumid(L"Aether.Other_123456789abcd!App").value());
    CHECK_EQ(packaged.kind, ApplicationKind::Packaged);
    CHECK_EQ(packaged.parsingName, std::wstring(L"Aether.Other_123456789abcd!App"));
    CHECK(HasLaunchRoute(packaged));

    const auto catalogued = ResolveRunningPresentation(Running(L"Aether.Fixture_123456789abcd!App", L""), index);
    CHECK_EQ(catalogued.kind, ApplicationKind::Packaged);
    CHECK_EQ(catalogued.displayName, std::wstring(L"Fixture Window Group"));

    RunningApplication windowOnly;
    windowOnly.id = L"window:aetherfixtureclass";
    windowOnly.displayName = L"Fixture Title";
    const auto window = ResolveRunningPresentation(windowOnly, index);
    CHECK_EQ(window.id, std::wstring(L"window:aetherfixtureclass"));
    CHECK_FALSE(HasLaunchRoute(window));
}

AETHER_TEST("running presentation: launch routes") {
    ApplicationInfo app;
    CHECK_FALSE(HasLaunchRoute(app));
    app.executablePath = L"C:\\Fixture\\tool.exe";
    CHECK(HasLaunchRoute(app));
    app.arguments = L"--fixture";
    CHECK_FALSE(HasLaunchRoute(app));  // arguments are never passed to a bare executable
    app.shortcutPath = L"C:\\Fixture\\Tool.lnk";
    CHECK(HasLaunchRoute(app));
    ApplicationInfo explicitAumid;
    explicitAumid.appUserModelId = L"Aether.Fixture.Explicit";
    CHECK_FALSE(HasLaunchRoute(explicitAumid));  // an explicit AUMID alone has no launch route
    explicitAumid.parsingName = L"Aether.Fixture.Explicit";
    CHECK(HasLaunchRoute(explicitAumid));
    ApplicationInfo badShortcut;
    badShortcut.shortcutPath = L"\\\\server\\share\\Tool.lnk";
    CHECK_FALSE(HasLaunchRoute(badShortcut));
}

AETHER_TEST("running presentation: icon source fingerprints follow what icon extraction reads") {
    ApplicationInfo app;
    app.id = L"aumid:aether.fixture.browser.userdata.profile1";
    app.appUserModelId = kProfileOneAumid;
    app.displayName = L"First Name";
    const auto base = IconSourceFingerprint(app);
    CHECK_EQ(base.size(), std::size_t{16});
    CHECK(std::all_of(base.begin(), base.end(), [](wchar_t c) { return std::iswxdigit(c) && !std::iswupper(c); }));
    CHECK_EQ(IconSourceFingerprint(app), base);  // stable

    auto renamed = app;
    renamed.displayName = L"Second Name";
    renamed.id = L"aumid:other";
    CHECK_EQ(IconSourceFingerprint(renamed), base);  // names and ids do not change the icon

    auto cased = app;
    cased.appUserModelId = L"AETHER.FIXTURE.BROWSER.USERDATA.PROFILE1";
    CHECK_EQ(IconSourceFingerprint(cased), base);

    auto avatar = app;
    avatar.iconResource = L"C:\\Fixture\\Profile1\\Avatar.ico,0";
    const auto withAvatar = IconSourceFingerprint(avatar);
    CHECK_NE(withAvatar, base);
    avatar.iconResource = L"C:\\Fixture\\Profile1\\Other.ico,0";
    CHECK_NE(IconSourceFingerprint(avatar), withAvatar);

    auto shortcut = app;
    shortcut.shortcutPath = L"C:\\Fixture\\Start\\Profile.lnk";
    CHECK_NE(IconSourceFingerprint(shortcut), base);
    auto kind = app;
    kind.kind = ApplicationKind::Packaged;
    CHECK_NE(IconSourceFingerprint(kind), base);
    // Field boundaries matter.
    ApplicationInfo a;
    a.parsingName = L"ab";
    a.appUserModelId = L"c";
    ApplicationInfo b;
    b.parsingName = L"a";
    b.appUserModelId = L"bc";
    CHECK_NE(IconSourceFingerprint(a), IconSourceFingerprint(b));
}

AETHER_TEST("running presentation: the application index ignores case and spelling") {
    const ApplicationIndex index(Catalog());
    CHECK_EQ(index.size(), std::size_t{4});
    REQUIRE(index.FindByAumid(L"  aether.fixture.BROWSER ") != nullptr);
    CHECK_EQ(index.FindByAumid(L"aether.fixture.browser")->displayName, std::wstring(L"Fixture Browser"));
    CHECK(index.FindByAumid(L"Aether.Fixture.Missing") == nullptr);
    CHECK(index.FindByAumid(L"C:\\Fixture\\path-like") == nullptr);
    REQUIRE(index.FindByExecutable(L"c:/fixture/browser/BROWSER.EXE") != nullptr);
    CHECK(index.HasExecutableWithOtherAumid(kBrowserExe, kProfileOneAumid));
    CHECK(index.HasExecutableWithOtherAumid(kBrowserExe, kBrowserAumid));  // the profile entry differs
    CHECK_FALSE(index.HasExecutableWithOtherAumid(L"C:\\Fixture\\Editor\\editor.exe", L"Aether.Fixture.Any"));
    CHECK(ApplicationIndex{}.empty());
}
