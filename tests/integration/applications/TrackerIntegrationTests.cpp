// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Runs the window tracker against the real desktop with a pumping message loop. The test creates one
// top-level window of its own, shown without activation (it never takes focus), and checks that the
// tracker excludes it. No other window is touched.
#include "AetherTest.h"

#include "applications/ApplicationIdentity.h"
#include "applications/WindowEligibility.h"
#include "applications/WindowTracker.h"
#include "core/Strings.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/Win32UiDispatcher.h"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <thread>

using namespace aether;
using namespace aether::apps;

namespace {

constexpr wchar_t kProbeClass[] = L"AetherShell.TrackerTestProbe";
constexpr wchar_t kProbeTitle[] = L"Aether tracker probe";

// Pumps this thread's messages until `done` holds or the timeout expires.
bool PumpUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        MSG message;
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (done()) return true;
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) return false;
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        ::MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(std::min<long long>(remaining, 50)), QS_ALLINPUT,
                                      MWMO_INPUTAVAILABLE);
    }
}

// A visible, titled, unowned top-level window with WS_EX_APPWINDOW: eligible in every respect except
// that it belongs to this process. WS_EX_NOACTIVATE + SW_SHOWNOACTIVATE keep it from taking focus.
class ProbeWindow {
public:
    ProbeWindow() {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = ::DefWindowProcW;
        windowClass.hInstance = ::GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kProbeClass;
        ::RegisterClassExW(&windowClass);
        hwnd_ = ::CreateWindowExW(WS_EX_APPWINDOW | WS_EX_NOACTIVATE, kProbeClass, kProbeTitle, WS_OVERLAPPEDWINDOW, 40,
                                  40, 320, 200, nullptr, nullptr, windowClass.hInstance, nullptr);
        if (hwnd_) ::ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
    }
    ~ProbeWindow() {
        if (hwnd_) ::DestroyWindow(hwnd_);
        ::UnregisterClassW(kProbeClass, ::GetModuleHandleW(nullptr));
    }
    ProbeWindow(const ProbeWindow&) = delete;
    ProbeWindow& operator=(const ProbeWindow&) = delete;
    [[nodiscard]] HWND Get() const noexcept { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};

std::string SchemeName(IdScheme scheme) {
    switch (scheme) {
        case IdScheme::Aumid: return "aumid";
        case IdScheme::Executable: return "exe";
        case IdScheme::Shell: return "shell";
        case IdScheme::Window: return "window";
        case IdScheme::Invalid: break;
    }
    return "invalid";
}

}  // namespace

AETHER_TEST("tracker: lists the desktop's application windows and excludes our own") {
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    REQUIRE(apartment.IsUsable());
    platform::Win32UiDispatcher dispatcher;
    REQUIRE(dispatcher.Initialize().ok());

    ProbeWindow probe;
    REQUIRE(probe.Get() != nullptr);
    REQUIRE(::IsWindowVisible(probe.Get()));

    // The probe is excluded only because it is ours.
    WindowFacts facts = CollectWindowFacts(probe.Get());
    CHECK(facts.ownProcess);
    CHECK_EQ(ClassifyWindow(facts), WindowExclusion::OwnProcess);
    facts.ownProcess = false;
    facts.title = kProbeTitle;
    facts.className = kProbeClass;
    CHECK_EQ(ClassifyWindow(facts), WindowExclusion::None);

    WindowTracker tracker(dispatcher);
    int notifications = 0;
    std::vector<RunningApplication> latest;
    REQUIRE(tracker.Start([&](const std::vector<RunningApplication>& apps) {
                CHECK(dispatcher.IsUiThread());
                ++notifications;
                latest = apps;
            }).ok());
    CHECK_EQ(notifications, 0);  // the first notification is asynchronous
    CHECK_EQ(tracker.Start([](const std::vector<RunningApplication>&) {}).error().code, ErrorCode::InvalidState);

    REQUIRE(PumpUntil([&] { return notifications > 0; }, std::chrono::seconds(5)));
    // Give display-name resolution (STA worker) a moment, then take the settled state.
    PumpUntil([] { return false; }, std::chrono::milliseconds(1500));

    // Notes carry counts and identity schemes only: titles, names, ids and relaunch properties of the
    // desktop's real windows can name people, documents or profiles.
    std::size_t windowCount = 0;
    std::map<std::string, std::size_t> schemes;
    std::size_t withRelaunchIcon = 0;
    std::size_t withRelaunchName = 0;
    std::size_t minimized = 0;
    for (const auto& app : latest) {
        CHECK_FALSE(app.id.empty());
        CHECK(GetIdScheme(app.id) != IdScheme::Invalid);
        CHECK_FALSE(app.windows.empty());
        CHECK_NE(app.processId, ::GetCurrentProcessId());
        ++schemes[SchemeName(GetIdScheme(app.id))];
        if (!app.relaunchIconResource.empty()) ++withRelaunchIcon;
        if (!app.relaunchDisplayName.empty()) {
            ++withRelaunchName;
            // Distinct group names come from it. CHECK, not CHECK_EQ: a failure must not print the name.
            if (GetIdScheme(app.id) != IdScheme::Window) CHECK(app.displayName == app.relaunchDisplayName);
        }
        for (const auto& window : app.windows) {
            ++windowCount;
            CHECK(window.handle != probe.Get());
            if (window.minimized) ++minimized;
        }
    }
    std::string schemeSummary;
    for (const auto& [scheme, count] : schemes) schemeSummary += " " + scheme + "=" + std::to_string(count);
    TEST_NOTE(std::to_string(latest.size()) + " applications, " + std::to_string(windowCount) + " windows (" +
              std::to_string(minimized) + " minimized), " + std::to_string(notifications) + " notifications; id schemes:" +
              schemeSummary + "; relaunch icon " + std::to_string(withRelaunchIcon) + ", relaunch name " +
              std::to_string(withRelaunchName));
    CHECK_FALSE(latest.empty());  // at least the console or terminal running the tests

    // Snapshot() matches what the listener saw last.
    const auto snapshot = tracker.Snapshot();
    CHECK_EQ(snapshot.size(), latest.size());

    // Every listed window is currently eligible according to the pure predicate.
    for (const auto& app : snapshot) {
        for (const auto& window : app.windows) {
            if (::IsWindow(window.handle)) CHECK(IsEligibleAppWindow(CollectWindowFacts(window.handle)));
        }
    }

    tracker.Stop();
    tracker.Stop();  // idempotent
    CHECK(tracker.Snapshot().empty());  // a stopped tracker reports nothing rather than stale windows
    const int afterStop = notifications;
    PumpUntil([] { return false; }, std::chrono::milliseconds(300));
    CHECK_EQ(notifications, afterStop);

    // Restartable after Stop(); the window list is available again as soon as Start() returns.
    int restarted = 0;
    std::size_t restartedCount = 0;
    REQUIRE(tracker.Start([&](const std::vector<RunningApplication>& apps) {
                ++restarted;
                restartedCount = apps.size();
            }).ok());
    CHECK_EQ(tracker.Snapshot().empty(), latest.empty());
    CHECK(PumpUntil([&] { return restarted > 0; }, std::chrono::seconds(5)));
    CHECK_EQ(restartedCount, tracker.Snapshot().size());
    tracker.Stop();
    dispatcher.Shutdown();
}

AETHER_TEST("tracker: Start must run on the UI thread") {
    const platform::ComApartment apartment(COINIT_APARTMENTTHREADED);
    platform::Win32UiDispatcher dispatcher;
    REQUIRE(dispatcher.Initialize().ok());
    WindowTracker tracker(dispatcher);
    std::optional<ErrorCode> code;
    std::thread other([&] {
        auto result = tracker.Start([](const std::vector<RunningApplication>&) {});
        code = result.ok() ? std::optional<ErrorCode>() : std::optional<ErrorCode>(result.error().code);
    });
    other.join();
    REQUIRE(code.has_value());
    CHECK_EQ(*code, ErrorCode::InvalidState);
    dispatcher.Shutdown();
}
