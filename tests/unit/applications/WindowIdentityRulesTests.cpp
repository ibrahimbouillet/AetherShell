// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "applications/WindowIdentityRules.h"

using namespace aether::apps;

namespace {

constexpr DWORD kFrameHostPid = 100;  // ApplicationFrameHost.exe
constexpr DWORD kAppPid = 200;
constexpr const wchar_t* kFrameHostExe = L"C:\\Windows\\System32\\ApplicationFrameHost.exe";
constexpr const wchar_t* kCalculatorAumid = L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
constexpr const wchar_t* kCalculatorExe =
    L"C:\\Program Files\\WindowsApps\\Microsoft.WindowsCalculator_11.0_x64__8wekyb3d8bbwe\\CalculatorApp.exe";

WindowIdentityFacts DesktopWindow() {
    WindowIdentityFacts facts;
    facts.className = L"Chrome_WidgetWin_1";
    facts.windowProcessId = kAppPid;
    facts.processOpened = true;
    facts.processExecutable = L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
    return facts;
}

// A Calculator frame: attached (CoreWindow present) or detached (minimized / suspended).
WindowIdentityFacts CalculatorFrame(bool attached) {
    WindowIdentityFacts facts;
    facts.className = kApplicationFrameWindowClass;
    facts.windowProcessId = kFrameHostPid;
    facts.processOpened = true;
    if (attached) {
        facts.hostedProcessId = kAppPid;
        facts.processAumid = kCalculatorAumid;
        facts.processExecutable = kCalculatorExe;
    } else {
        facts.processExecutable = kFrameHostExe;  // the identity process is the frame host itself
    }
    return facts;
}

}  // namespace

AETHER_TEST("window identity: an explicit window AUMID wins over the process") {
    auto facts = DesktopWindow();
    facts.windowAumid = L"Chrome._crx_agimnkijcamfeangaknmldooml";  // a PWA window
    const auto identity = ResolveWindowIdentity(facts);
    CHECK_EQ(identity.id, std::wstring(L"aumid:chrome._crx_agimnkijcamfeangaknmldooml"));
    CHECK_EQ(identity.appUserModelId, facts.windowAumid);
    CHECK_EQ(identity.executablePath, facts.processExecutable);
    CHECK_EQ(identity.processId, kAppPid);
}

AETHER_TEST("window identity: synthesized window AUMIDs are ignored") {
    auto facts = DesktopWindow();
    facts.windowAumid = L"{6D809377-6AF0-444B-8957-A3773F02200E}\\Google\\Chrome\\Application\\chrome.exe";
    const auto byExe = ResolveWindowIdentity(facts);
    CHECK_EQ(byExe.id, std::wstring(L"exe:c:\\program files\\google\\chrome\\application\\chrome.exe"));
    CHECK(byExe.appUserModelId.empty());

    facts.processAumid = kCalculatorAumid;  // packaged process: its AUMID comes before the executable
    const auto packaged = ResolveWindowIdentity(facts);
    CHECK_EQ(packaged.id, std::wstring(L"aumid:microsoft.windowscalculator_8wekyb3d8bbwe!app"));
    CHECK_EQ(packaged.appUserModelId, std::wstring(kCalculatorAumid));
}

AETHER_TEST("window identity: inaccessible processes are grouped by window class") {
    WindowIdentityFacts facts;
    facts.className = L"ProtectedAppWindow";
    facts.windowProcessId = 4321;
    facts.processOpened = false;
    facts.processExecutable = L"C:\\ignored.exe";  // never used when the process was not opened
    const auto identity = ResolveWindowIdentity(facts);
    CHECK_EQ(identity.id, std::wstring(L"window:protectedappwindow"));
    CHECK(identity.appUserModelId.empty());
    CHECK(identity.executablePath.empty());
    CHECK_EQ(identity.processId, DWORD{4321});

    // An image path that is not a valid executable path falls back the same way.
    facts.processOpened = true;
    facts.processExecutable = L"\\\\?\\GLOBALROOT\\Device\\HarddiskVolume3\\x.exe";
    CHECK_EQ(ResolveWindowIdentity(facts).id, std::wstring(L"window:protectedappwindow"));

    facts.className.clear();  // nothing usable at all: the tracker skips such windows
    CHECK(ResolveWindowIdentity(facts).id.empty());
}

AETHER_TEST("window identity: an attached frame is the hosted app's process") {
    const auto facts = CalculatorFrame(true);
    const auto identity = ResolveWindowIdentity(facts);
    CHECK_EQ(identity.id, std::wstring(L"aumid:microsoft.windowscalculator_8wekyb3d8bbwe!app"));
    CHECK_EQ(identity.processId, kAppPid);
    CHECK_EQ(identity.executablePath, std::wstring(kCalculatorExe));
    CHECK(ShouldRememberFrameIdentity(facts, identity));
}

AETHER_TEST("window identity: a detached frame keeps the identity seen while attached") {
    const auto attached = ResolveWindowIdentity(CalculatorFrame(true));

    // Without its own AUMID property.
    auto detached = CalculatorFrame(false);
    detached.rememberedFrameIdentity = attached;
    auto identity = ResolveWindowIdentity(detached);
    CHECK_EQ(identity.id, attached.id);
    CHECK_EQ(identity.processId, kAppPid);  // not ApplicationFrameHost's
    CHECK_EQ(identity.executablePath, std::wstring(kCalculatorExe));
    CHECK_FALSE(ShouldRememberFrameIdentity(detached, identity));

    // With the same AUMID on the frame window (the frame host sets it for taskbar grouping): the
    // process id and executable must not flip to ApplicationFrameHost's while minimized.
    detached.windowAumid = L"microsoft.windowscalculator_8wekyb3d8bbwe!App";
    identity = ResolveWindowIdentity(detached);
    CHECK_EQ(identity.processId, kAppPid);
    CHECK_EQ(identity.executablePath, std::wstring(kCalculatorExe));
    CHECK_FALSE(ShouldRememberFrameIdentity(detached, identity));
}

AETHER_TEST("window identity: a detached frame showing another app is re-identified") {
    auto detached = CalculatorFrame(false);
    detached.rememberedFrameIdentity = ResolveWindowIdentity(CalculatorFrame(true));
    detached.windowAumid = L"Microsoft.WindowsAlarms_8wekyb3d8bbwe!App";
    const auto identity = ResolveWindowIdentity(detached);
    CHECK_EQ(identity.id, std::wstring(L"aumid:microsoft.windowsalarms_8wekyb3d8bbwe!app"));
    CHECK_EQ(identity.processId, kFrameHostPid);
    CHECK_FALSE(ShouldRememberFrameIdentity(detached, identity));  // only attached observations replace it
}

AETHER_TEST("window identity: a frame never seen attached") {
    // With an AUMID on the frame: grouped correctly, remembered until an attached observation.
    auto withAumid = CalculatorFrame(false);
    withAumid.windowAumid = kCalculatorAumid;
    const auto first = ResolveWindowIdentity(withAumid);
    CHECK_EQ(first.id, std::wstring(L"aumid:microsoft.windowscalculator_8wekyb3d8bbwe!app"));
    CHECK_EQ(first.processId, kFrameHostPid);
    CHECK(ShouldRememberFrameIdentity(withAumid, first));

    // Without one: only the frame host is known (documented limitation).
    const auto bare = ResolveWindowIdentity(CalculatorFrame(false));
    CHECK_EQ(bare.id, std::wstring(L"exe:c:\\windows\\system32\\applicationframehost.exe"));
    CHECK_FALSE(ShouldRememberFrameIdentity(CalculatorFrame(false), bare));

    // A CoreWindow owned by the frame host itself does not count as attached.
    auto selfHosted = CalculatorFrame(false);
    selfHosted.hostedProcessId = kFrameHostPid;
    CHECK_EQ(ResolveWindowIdentity(selfHosted).processId, kFrameHostPid);
}

AETHER_TEST("window identity: frame memory applies only to frames") {
    auto facts = DesktopWindow();
    facts.windowAumid = L"Chrome";
    WindowIdentity stale;
    stale.id = L"aumid:something.else";
    facts.rememberedFrameIdentity = stale;  // ignored for a non-frame window
    const auto identity = ResolveWindowIdentity(facts);
    CHECK_EQ(identity.id, std::wstring(L"aumid:chrome"));
    CHECK_FALSE(ShouldRememberFrameIdentity(facts, identity));
    CHECK(IsApplicationFrameClass(L"ApplicationFrameWindow"));
    CHECK_FALSE(IsApplicationFrameClass(L"applicationframewindow"));  // class names are exact
}
