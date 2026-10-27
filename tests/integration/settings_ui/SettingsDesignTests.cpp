// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The redesigned Settings window (FR-037 / T47): page layout at several sizes and DPIs, people-facing
// numbers with exact stored values, slider/field synchronisation, native accessible dropdowns,
// theme changes that keep drafts/focus/scroll, the reset confirmation seam, in-place update
// states and own-window renders over a private two-phase backdrop. Settings use temporary files and
// injected callbacks only; nothing touches startup, the taskbar or the real settings.
#include "AetherTest.h"

#include "../ui_form/FormTestSupport.h"

#include "core/FileSystem.h"
#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "settings/ui/SettingsWindow.h"

#include <commctrl.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <span>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace std::chrono_literals;
using namespace aether::test::form;

namespace {

template <class Predicate>
bool PumpUntil(ManualDispatcher& dispatcher, Predicate predicate, std::chrono::milliseconds timeout = 5s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        dispatcher.RunPending();
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(1ms);
    }
    return true;
}

void PumpFor(ManualDispatcher& dispatcher, std::chrono::milliseconds duration) {
    const auto end = std::chrono::steady_clock::now() + duration;
    (void)PumpUntil(dispatcher, [&] { return std::chrono::steady_clock::now() >= end; }, duration + 1s);
}

std::wstring TextOf(HWND control) {
    const int length = ::GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    ::GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

struct Fixture {
    platform::ComApartment apartment;
    ManualDispatcher dispatcher;
    std::filesystem::path file;
    settings::SettingsService service;
    explicit Fixture(const char* name)
        : file(test::MakeTempDirectory(name) / L"settings.json"), service(file, dispatcher) {
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_WIN95_CLASSES | ICC_HOTKEY_CLASS};
        ::InitCommonControlsEx(&common);
        REQUIRE(service.Load({}).ok());
        REQUIRE(PumpUntil(dispatcher, [&] { return service.Loaded(); }));
    }
};

// Visible descendants that are actual controls (no containers), in the window's client coordinates.
struct Placed { HWND hwnd; RECT rect; RECT bounds; std::wstring cls; };
std::vector<Placed> VisibleControls(HWND window) {
    struct Collect { HWND window; std::vector<Placed> items; } collect{window, {}};
    ::EnumChildWindows(window, [](HWND child, LPARAM parameter) -> BOOL {
        auto& c = *reinterpret_cast<Collect*>(parameter);
        if (!::IsWindowVisible(child)) return TRUE;
        wchar_t name[64]{};
        ::GetClassNameW(child, name, 64);
        const std::wstring cls(name);
        if (cls.rfind(L"AetherShell.", 0) == 0) return TRUE;  // scroll view containers and indicator
        RECT rect{};
        ::GetWindowRect(child, &rect);
        RECT bounds{};
        ::GetClientRect(c.window, &bounds);
        ::MapWindowPoints(c.window, nullptr, reinterpret_cast<POINT*>(&bounds), 2);
        // Clip vertically to skip off-screen rows. Preserve the original horizontal bounds so
        // an overflowing control cannot pass merely because the viewport hides its right side.
        for (HWND parent = ::GetParent(child); parent && parent != c.window; parent = ::GetParent(parent)) {
            wchar_t parentName[64]{};
            ::GetClassNameW(parent, parentName, 64);
            if (std::wstring_view(parentName) != L"AetherShell.FormScroll.Viewport") continue;
            RECT viewport{};
            ::GetWindowRect(parent, &viewport);
            if (rect.bottom <= viewport.top || rect.top >= viewport.bottom) return TRUE;
            rect.top = std::max(rect.top, viewport.top);
            rect.bottom = std::min(rect.bottom, viewport.bottom);
            bounds.left = viewport.left;
            bounds.right = viewport.right;
        }
        ::MapWindowPoints(nullptr, c.window, reinterpret_cast<POINT*>(&rect), 2);
        ::MapWindowPoints(nullptr, c.window, reinterpret_cast<POINT*>(&bounds), 2);
        c.items.push_back(Placed{child, rect, bounds, cls});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&collect));
    return collect.items;
}

// Every visible control lies within the client width and none overlaps another. Controls scrolled
// out of the viewport are clipped by it, so only horizontal bounds are absolute.
void CheckLayout(HWND window, const char* what) {
    const auto controls = VisibleControls(window);
    CHECK(controls.size() >= 4);
    for (const auto& item : controls) {
        const bool inside = item.rect.left >= item.bounds.left - 1 && item.rect.right <= item.bounds.right + 1 && item.rect.right > item.rect.left;
        if (!inside) TEST_NOTE(std::string(what) + ": control " + std::to_string(::GetDlgCtrlID(item.hwnd)) + " outside the client width");
        CHECK(inside);
    }
    for (std::size_t i = 0; i < controls.size(); ++i) {
        for (std::size_t j = i + 1; j < controls.size(); ++j) {
            RECT overlap{};
            const RECT a = controls[i].rect;
            const RECT b = controls[j].rect;
            // Focus margins of neighbouring buttons may touch; a real overlap is a few pixels deep.
            if (::IntersectRect(&overlap, &a, &b) && overlap.right - overlap.left > 4 && overlap.bottom - overlap.top > 4) {
                TEST_NOTE(std::string(what) + ": controls " + std::to_string(::GetDlgCtrlID(controls[i].hwnd)) + " and " +
                          std::to_string(::GetDlgCtrlID(controls[j].hwnd)) + " overlap");
                CHECK(false);
            }
        }
    }
}

void CheckScrollableLayout(settings::ui::SettingsWindow& window, const char* what) {
    ::SendMessageW(window.Handle(), WM_VSCROLL, SB_TOP, 0);
    for (;;) {
        CheckLayout(window.Handle(), what);
        const auto before = window.ScrollMetrics();
        if (before.position >= before.range) break;
        ::SendMessageW(window.Handle(), WM_VSCROLL, SB_PAGEDOWN, 0);
        if (window.ScrollMetrics().position <= before.position) { CHECK(false); break; }
    }
}

constexpr settings::ui::SettingsPage kPages[] = {
    settings::ui::SettingsPage::Dock, settings::ui::SettingsPage::TopBar, settings::ui::SettingsPage::Weather,
    settings::ui::SettingsPage::Updates, settings::ui::SettingsPage::General, settings::ui::SettingsPage::About,
    settings::ui::SettingsPage::Desktop, settings::ui::SettingsPage::Appearance};
constexpr const char* kPageNames[] = {"dock", "topbar", "weather", "updates", "general", "about", "desktop", "appearance"};

}  // namespace

AETHER_TEST("settings design: every page lays out without overlap at default, small and high-DPI sizes") {
    Fixture f("settings-design-layout");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    for (const auto page : kPages) {
        REQUIRE(window.Show(page).ok());
        PumpFor(f.dispatcher, 60ms);
        CheckScrollableLayout(window, kPageNames[static_cast<int>(page)]);
    }
    // A small window: navigation becomes a rail, rows stack, content scrolls, the footer stays.
    const int compactDpi = static_cast<int>(::GetDpiForWindow(window.Handle()));
    ::SetWindowPos(window.Handle(), nullptr, 0, 0, ::MulDiv(420, compactDpi, 96), ::MulDiv(360, compactDpi, 96),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (const auto page : kPages) {
        REQUIRE(window.Show(page).ok());
        PumpFor(f.dispatcher, 40ms);
        CheckScrollableLayout(window, kPageNames[static_cast<int>(page)]);
        RECT apply{}, client{};
        ::GetWindowRect(window.Control(101), &apply);
        ::MapWindowPoints(nullptr, window.Handle(), reinterpret_cast<POINT*>(&apply), 2);
        ::GetClientRect(window.Handle(), &client);
        CHECK(apply.bottom <= client.bottom);
        CHECK(apply.right <= client.right);
    }
    // Synthetic per-monitor DPI changes (144 and 192): the same layout scaled, drafts kept.
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    ::SetWindowTextW(window.Control(200), L"77");
    for (const UINT dpi : {144u, 192u}) {
        RECT bounds{};
        ::GetWindowRect(window.Handle(), &bounds);
        bounds.right = bounds.left + ::MulDiv(900, static_cast<int>(dpi), 96);
        bounds.bottom = bounds.top + ::MulDiv(640, static_cast<int>(dpi), 96);
        ::SendMessageW(window.Handle(), WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&bounds));
        PumpFor(f.dispatcher, 40ms);
        CheckLayout(window.Handle(), dpi == 144 ? "dock at 144 DPI" : "dock at 192 DPI");
        CHECK_EQ(TextOf(window.Control(200)), std::wstring(L"77"));
    }
    window.Close();
}

AETHER_TEST("settings design: numbers show for people, store exactly and follow their sliders") {
    Fixture f("settings-design-numbers");
    REQUIRE(f.service.Update([](settings::Settings& s) { s.dock.opacity = 0.8; s.dock.magnification = 1.45; }).ok());
    REQUIRE(PumpUntil(f.dispatcher, [&] { return !f.service.Dirty(); }));
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    // Binding order: 200 icon size, 201 bottom, 202 side, 203 spacing, 204 opacity, 205 magnification.
    CHECK_EQ(TextOf(window.Control(204)), std::wstring(L"80"));
    CHECK_EQ(TextOf(window.Control(205)), std::wstring(L"1.45"));
    CHECK(TextOf(window.Control(204)).find(L'.') == std::wstring::npos);  // never "0.800000"
    ::SetWindowTextW(window.Control(204), L"65");
    ::SendMessageW(window.Handle(), WM_TIMER, 19, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return std::fabs(f.service.Current().dock.opacity - 0.65) < 1e-9 && !f.service.Dirty(); }));
    // The opacity slider (400 + binding index) follows the field...
    HWND slider = window.Control(404);
    REQUIRE(slider != nullptr);
    CHECK_EQ(::SendMessageW(slider, TBM_GETPOS, 0, 0), LRESULT{35});  // 65 % on 30..100
    // ...and moving it rewrites the field and schedules the same live apply.
    ::SendMessageW(slider, TBM_SETPOS, TRUE, 50);
    ::SendMessageW(::GetParent(slider), WM_HSCROLL, MAKEWPARAM(TB_THUMBPOSITION, 50), reinterpret_cast<LPARAM>(slider));
    CHECK_EQ(TextOf(window.Control(204)), std::wstring(L"80"));
    ::SendMessageW(window.Handle(), WM_TIMER, 19, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return std::fabs(f.service.Current().dock.opacity - 0.8) < 1e-9 && !f.service.Dirty(); }));
    // An incomplete number is rejected with an explanation instead of being guessed.
    ::SetWindowTextW(window.Control(205), L"abc");
    ::SendMessageW(window.Handle(), WM_TIMER, 19, 0);
    PumpFor(f.dispatcher, 50ms);
    CHECK(std::fabs(f.service.Current().dock.magnification - 1.45) < 1e-9);
    CHECK(TextOf(window.Control(103)).find(L"number") != std::wstring::npos);
    window.Close();
}

AETHER_TEST("settings design: compact pages keep complete rows and actions at enlarged text sizes") {
    Fixture f("settings-design-text-scale");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    const int compactDpi = static_cast<int>(::GetDpiForWindow(window.Handle()));
    ::SetWindowPos(window.Handle(), nullptr, 0, 0, ::MulDiv(420, compactDpi, 96), ::MulDiv(520, compactDpi, 96),
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (const float scale : {1.5f, 2.25f}) {
        auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
        tokens.textScale = scale;
        window.SetTheme(tokens, graphics::MotionPolicy{.reduced = true});
        for (const auto page : kPages) {
            REQUIRE(window.Show(page).ok());
            PumpFor(f.dispatcher, 20ms);
            CheckScrollableLayout(window, kPageNames[static_cast<int>(page)]);
            RECT apply{}, reset{}, client{};
            ::GetWindowRect(window.Control(101), &apply);
            ::GetWindowRect(window.Control(102), &reset);
            ::MapWindowPoints(nullptr, window.Handle(), reinterpret_cast<POINT*>(&apply), 2);
            ::MapWindowPoints(nullptr, window.Handle(), reinterpret_cast<POINT*>(&reset), 2);
            ::GetClientRect(window.Handle(), &client);
            CHECK(apply.top >= 0 && apply.bottom <= client.bottom);
            CHECK(reset.top >= 0 && reset.bottom <= client.bottom);
        }
    }
    window.Close();
}

AETHER_TEST("settings design: dropdowns keep the native list and selection contract") {
    Fixture f("settings-design-dropdown");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::Appearance).ok());
    HWND mode = window.Control(200);  // Color mode: Follow Windows / Light / Dark
    REQUIRE(mode != nullptr);
    ::SetFocus(mode);
    ::SendMessageW(mode, CB_SHOWDROPDOWN, TRUE, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return ::SendMessageW(mode, CB_GETDROPPEDSTATE, 0, 0) != 0; }));
    COMBOBOXINFO combo{sizeof(combo)};
    REQUIRE(::GetComboBoxInfo(mode, &combo));
    REQUIRE(combo.hwndList != nullptr);
    CHECK(::IsWindowVisible(combo.hwndList));
    CHECK_EQ(::SendMessageW(combo.hwndList, LB_GETCOUNT, 0, 0), LRESULT{3});
    CHECK_EQ(::SendMessageW(combo.hwndList, LB_GETCURSEL, 0, 0), LRESULT{0});
    ::SendMessageW(mode, CB_SHOWDROPDOWN, FALSE, 0);
    CHECK_FALSE(::IsWindowVisible(combo.hwndList));
    // Selection is still represented by the real combo and its normal CBN_SELCHANGE notification.
    ::SendMessageW(mode, CB_SETCURSEL, 2, 0);
    ::SendMessageW(::GetParent(mode), WM_COMMAND, MAKEWPARAM(200, CBN_SELCHANGE), reinterpret_cast<LPARAM>(mode));
    CHECK_EQ(::SendMessageW(mode, CB_GETCURSEL, 0, 0), LRESULT{2});
    ::SendMessageW(window.Handle(), WM_TIMER, 19, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return f.service.Current().general.themeMode == settings::ThemePreference::Dark && !f.service.Dirty(); }));
    // Dismissing the native list changes neither selection nor the current setting.
    ::SendMessageW(mode, CB_SHOWDROPDOWN, TRUE, 0);
    ::SendMessageW(mode, CB_SHOWDROPDOWN, FALSE, 0);
    CHECK_EQ(::SendMessageW(mode, CB_GETCURSEL, 0, 0), LRESULT{2});
    window.Close();
}

AETHER_TEST("settings design: theme changes keep drafts, staged consent, focus and scroll position") {
    Fixture f("settings-design-theme");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
    ::SetWindowPos(window.Handle(), nullptr, 0, 0, 560, 420, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    PumpFor(f.dispatcher, 40ms);
    ::SendMessageW(window.Control(200), BM_CLICK, 0, 0);  // staged startup consent: needs Apply
    CHECK(TextOf(window.Control(103)).find(L"Apply") != std::wstring::npos);
    // Focusing a control lower on the page scrolls it into view; then the position must survive.
    ::SetFocus(window.Control(205));
    PumpFor(f.dispatcher, 60ms);
    const HWND focus = ::GetFocus();
    const int position = window.ScrollMetrics().position;
    CHECK(position > 0);
    for (const auto mode : {graphics::ThemeMode::Light, graphics::ThemeMode::HighContrast, graphics::ThemeMode::Dark}) {
        auto tokens = graphics::MakeDesignTokens(mode);
        window.SetTheme(tokens, graphics::MotionPolicy{});
        PumpFor(f.dispatcher, 40ms);
        CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_CHECKED});
        CHECK_EQ(window.ScrollMetrics().position, position);
        CHECK(::GetFocus() == focus);
    }
    CHECK_FALSE(f.service.Current().general.startWithWindows);  // nothing was committed
    window.Close();
}

AETHER_TEST("settings design: navigation retains unfinished edits and page-local consent until explicit Apply") {
    Fixture f("settings-design-navigation-drafts");
    int startupCalls = 0;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.setStartup = [&](bool, std::function<void(Result<void>)> complete) { ++startupCalls; complete(Ok()); };
    settings::ui::SettingsWindow window(f.service, f.dispatcher, std::move(callbacks));
    const auto navigate = [&](settings::ui::SettingsPage page) {
        ::SendMessageW(window.Control(100), LB_SETCURSEL, static_cast<WPARAM>(page), 0);
        ::SendMessageW(window.Handle(), WM_COMMAND, MAKEWPARAM(100, LBN_SELCHANGE), reinterpret_cast<LPARAM>(window.Control(100)));
    };
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    HWND number = window.Control(205);
    ::SetWindowTextW(number, L"unfinished");
    ::SetFocus(number);
    ::SendMessageW(number, EM_SETSEL, 2, 5);
    PumpFor(f.dispatcher, 30ms);
    // A repeated --settings/open request must keep the actual editor and its in-progress input.
    REQUIRE(f.service.Update([](settings::Settings& value) { value.dock.iconSize = 64; }).ok());
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    CHECK(window.Control(205) == number);
    CHECK_EQ(TextOf(number), std::wstring(L"unfinished"));
    CHECK_EQ(TextOf(window.Control(200)), std::wstring(L"64"));  // refresh an externally changed, untouched field
    PumpFor(f.dispatcher, 30ms);
    const int dockScroll = window.ScrollMetrics().position;
    navigate(settings::ui::SettingsPage::General);
    ::SendMessageW(window.Control(200), BM_CLICK, 0, 0);  // staged startup: no explicit consent yet
    REQUIRE(f.service.Update([](settings::Settings& value) { value.general.searchHotkey.modifiers = 15; }).ok());
    REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
    CHECK_EQ(::SendMessageW(window.Control(205), CB_GETCURSEL, 0, 0), LRESULT{14});
    CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_CHECKED});  // keep the consent draft
    navigate(settings::ui::SettingsPage::Desktop);
    ::SendMessageW(window.Control(200), BM_CLICK, 0, 0);  // staged taskbar opt-out
    navigate(settings::ui::SettingsPage::Dock);
    PumpFor(f.dispatcher, 30ms);
    CHECK_EQ(TextOf(window.Control(205)), std::wstring(L"unfinished"));
    CHECK(::GetFocus() == window.Control(205));
    DWORD selectionStart = 0, selectionEnd = 0;
    ::SendMessageW(window.Control(205), EM_GETSEL, reinterpret_cast<WPARAM>(&selectionStart), reinterpret_cast<LPARAM>(&selectionEnd));
    CHECK_EQ(selectionStart, DWORD{2});
    CHECK_EQ(selectionEnd, DWORD{5});
    CHECK_EQ(window.ScrollMetrics().position, dockScroll);
    CHECK_FALSE(f.service.Current().general.startWithWindows);
    CHECK(f.service.Current().general.replaceWindowsTaskbar);
    CHECK_EQ(startupCalls, 0);
    navigate(settings::ui::SettingsPage::Desktop);
    CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_UNCHECKED});
    ::SendMessageW(window.Control(101), BM_CLICK, 0, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return !f.service.Current().general.replaceWindowsTaskbar && !f.service.Dirty(); }));
    CHECK_FALSE(f.service.Current().general.startWithWindows);  // Apply commits only the owning page
    CHECK_EQ(startupCalls, 0);
    navigate(settings::ui::SettingsPage::General);
    CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_CHECKED});
    window.Close();
    REQUIRE(window.Show(settings::ui::SettingsPage::General).ok());
    CHECK_EQ(::SendMessageW(window.Control(200), BM_GETCHECK, 0, 0), LRESULT{BST_UNCHECKED});
    CHECK_EQ(startupCalls, 0);  // closing discards the staged startup choice
    window.Close();
}

AETHER_TEST("settings design: reset asks through the confirmation seam and declining keeps everything") {
    Fixture f("settings-design-reset");
    REQUIRE(f.service.Update([](settings::Settings& s) { s.dock.iconSize = 64; s.general.replaceWindowsTaskbar = false; }).ok());
    REQUIRE(PumpUntil(f.dispatcher, [&] { return !f.service.Dirty(); }));
    int asked = 0;
    bool answer = false;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.confirm = [&](const settings::ui::SettingsConfirmation& question) {
        ++asked;
        CHECK(question.message.find(L"default") != std::wstring::npos);
        CHECK(question.detail.find(L"taskbar") != std::wstring::npos);
        return answer;
    };
    settings::ui::SettingsWindow window(f.service, f.dispatcher, std::move(callbacks));
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    ::SendMessageW(window.Control(102), BM_CLICK, 0, 0);
    CHECK_EQ(asked, 1);
    PumpFor(f.dispatcher, 50ms);
    CHECK_EQ(f.service.Current().dock.iconSize, 64);
    answer = true;
    ::SendMessageW(window.Control(102), BM_CLICK, 0, 0);
    REQUIRE(PumpUntil(f.dispatcher, [&] { return f.service.Current().dock.iconSize == 40 && !f.service.Dirty(); }));
    CHECK_EQ(asked, 2);
    CHECK_FALSE(f.service.Current().general.replaceWindowsTaskbar);  // D23: the taskbar choice is kept
    window.Close();
}

AETHER_TEST("settings design: update states refresh in place and keep focus and drafts") {
    Fixture f("settings-design-updates");
    int checks = 0;
    settings::ui::SettingsCallbacks callbacks;
    callbacks.checkForUpdates = [&] { ++checks; };
    settings::ui::SettingsWindow window(f.service, f.dispatcher, std::move(callbacks));
    REQUIRE(window.Show(settings::ui::SettingsPage::Updates).ok());
    ::SetWindowTextW(window.Control(201), L"36");
    ::SetFocus(window.Control(201));
    const HWND summary = window.Control(317);
    const HWND install = window.Control(302);
    updates::UpdateSnapshot update;
    update.state = updates::UpdateState::Checking;
    window.SetUpdateSnapshot(update);
    CHECK(::IsWindowVisible(window.Control(322)));  // indeterminate progress while checking
    CHECK_FALSE(::IsWindowEnabled(install));
    update.state = updates::UpdateState::UpdateAvailable;
    update.manifest = std::make_shared<updates::UpdateManifest>();
    update.downloadSize = 4u * 1024u * 1024u;
    window.SetUpdateSnapshot(update);
    CHECK(::IsWindowEnabled(install));
    CHECK(TextOf(window.Control(323)).find(L"available") != std::wstring::npos);
    CHECK(TextOf(summary).find(L"MB") != std::wstring::npos);
    update.state = updates::UpdateState::Downloading;
    update.progress = {256, 0};
    window.SetUpdateSnapshot(update);
    CHECK(TextOf(summary).find(L'%') == std::wstring::npos);  // unknown total: no invented percentage
    update.progress.total = 1024;
    window.SetUpdateSnapshot(update);
    CHECK(TextOf(summary).find(L"25%") != std::wstring::npos);
    CHECK_FALSE(::IsWindowEnabled(install));
    update.state = updates::UpdateState::ReadyToInstall;
    update.prepared = updates::PreparedUpdate{};
    window.SetUpdateSnapshot(update);
    CHECK(TextOf(summary).find(L"signed") != std::wstring::npos);
    CHECK(TextOf(install).find(L"Install") != std::wstring::npos);
    CHECK_FALSE(::IsWindowVisible(window.Control(322)));
    // Same controls throughout: the draft and focus were never rebuilt away.
    CHECK(window.Control(317) == summary);
    CHECK_EQ(TextOf(window.Control(201)), std::wstring(L"36"));
    CHECK(::GetFocus() == window.Control(201));
    ::SendMessageW(window.Control(301), BM_CLICK, 0, 0);
    CHECK_EQ(checks, 1);
    window.Close();
}

AETHER_TEST("settings design: pages render on the form material over a private backdrop") {
    Fixture f("settings-design-render");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    const RECT safe = SafeArea();
    const int width = std::min<int>(safe.right - safe.left - 40, ::MulDiv(820, static_cast<int>(::GetDpiForWindow(window.Handle())), 96));
    const int height = std::min<int>(safe.bottom - safe.top - 20, ::MulDiv(600, static_cast<int>(::GetDpiForWindow(window.Handle())), 96));
    const int left = (safe.left + safe.right - width) / 2;
    const int top = (safe.top + safe.bottom - height) / 2;
    ::SetWindowPos(window.Handle(), HWND_TOPMOST, left, top, width, height, SWP_NOACTIVATE);
    RECT guard{};
    ::GetWindowRect(window.Handle(), &guard);
    ::InflateRect(&guard, 24, 24);
    ::IntersectRect(&guard, &guard, &safe);
    RECT bounds{};
    ::GetWindowRect(window.Handle(), &bounds);
    ::UnionRect(&guard, &guard, &bounds);
    BackdropGuard backdrop;
    backdrop.hwnd = CreateTestBackdrop(guard);
    REQUIRE(backdrop.hwnd != nullptr);
    ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::Light}) {
        window.SetTheme(graphics::MakeDesignTokens(mode), graphics::MotionPolicy{});
        for (const auto page : kPages) {
            REQUIRE(window.Show(page).ok());
            ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            PumpFor(f.dispatcher, 600ms);
            RECT client{};
            ::GetClientRect(window.Handle(), &client);
            ::MapWindowPoints(window.Handle(), nullptr, reinterpret_cast<POINT*>(&client), 2);
            SetBackdropPhase(backdrop.hwnd, false);
            PumpFor(f.dispatcher, 250ms);
            const auto first = CaptureWithRetry(backdrop.hwnd, guard, client);
            if (!first.Valid()) {
                TEST_NOTE("render inconclusive: a foreign window overlapped the private backdrop");
                continue;
            }
            const auto path = InspectionPath(std::string("settings-") + (mode == graphics::ThemeMode::Dark ? "dark-" : "light-") +
                                             kPageNames[static_cast<int>(page)]);
            CHECK(SavePng(first, path));
            TEST_NOTE("saved " + path.string());
            if (page == settings::ui::SettingsPage::Dock) {
                // The value field of the icon size is an opaque native editor: identical over both phases.
                SetBackdropPhase(backdrop.hwnd, true);
                PumpFor(f.dispatcher, 250ms);
                const auto second = CaptureWithRetry(backdrop.hwnd, guard, client);
                if (second.Valid()) {
                    RECT field{};
                    ::GetWindowRect(window.Control(200), &field);
                    ::OffsetRect(&field, -client.left, -client.top);
                    ::InflateRect(&field, -2, -2);
                    CHECK_EQ(MaxDifference(first, second, field), 0);
                }
            }
        }
    }
    window.Close();
}

AETHER_TEST("settings design: the integrated title strip keeps native caption, resize, menu and maximize behaviour") {
    Fixture f("settings-design-frame");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{});
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    const HWND hwnd = window.Handle();
    const int dpi = static_cast<int>(::GetDpiForWindow(hwnd));
    const RECT safe = SafeArea();
    const int width = std::min<int>(safe.right - safe.left - 40, ::MulDiv(820, dpi, 96));
    const int height = std::min<int>(safe.bottom - safe.top - 20, ::MulDiv(600, dpi, 96));
    ::SetWindowPos(hwnd, nullptr, (safe.left + safe.right - width) / 2, (safe.top + safe.bottom - height) / 2, width, height,
                   SWP_NOZORDER | SWP_NOACTIVATE);
    PumpFor(f.dispatcher, 200ms);
    const auto origin = [&] { POINT point{0, 0}; ::ClientToScreen(hwnd, &point); return point; };
    RECT frame{};
    ::GetWindowRect(hwnd, &frame);
    // The window stays a standard resizable, maximizable window with its system menu.
    const auto style = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_STYLE));
    CHECK((style & WS_THICKFRAME) != 0); CHECK((style & WS_MAXIMIZEBOX) != 0); CHECK((style & WS_SYSMENU) != 0);
    const HMENU menu = ::GetSystemMenu(hwnd, FALSE);
    REQUIRE(menu != nullptr);
    CHECK(::GetMenuState(menu, SC_MAXIMIZE, MF_BYCOMMAND) != static_cast<UINT>(-1));
    CHECK(::GetMenuState(menu, SC_CLOSE, MF_BYCOMMAND) != static_cast<UINT>(-1));
    if (origin().y != frame.top) {
        // Opaque material keeps the standard caption (colored by the window on Windows 11).
        TEST_NOTE("standard frame: the form material is opaque on this system");
        CHECK(origin().y > frame.top);
        window.Close();
        return;
    }
    // Integrated strip: the client starts at the window's top; side borders remain resize borders.
    CHECK(origin().x > frame.left);
    RECT buttons{};
    REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(hwnd, 5 /* DWMWA_CAPTION_BUTTON_BOUNDS */, &buttons, sizeof(buttons))));
    REQUIRE(buttons.right > buttons.left); REQUIRE(buttons.bottom > buttons.top);
    const auto hit = [&](LONG x, LONG y) { return ::SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(x, y)); };
    const LONG stripY = frame.top + (buttons.top + buttons.bottom) / 2;
    CHECK_EQ(hit((frame.left + frame.right) / 2, frame.top + 1), LRESULT{HTTOP});
    CHECK_EQ(hit(origin().x + 2, frame.top + 1), LRESULT{HTTOPLEFT});
    CHECK_EQ(hit(frame.left + buttons.left / 2, stripY), LRESULT{HTCAPTION});
    CHECK_EQ(hit(frame.left + buttons.right - (buttons.right - buttons.left) / 8, stripY), LRESULT{HTCLOSE});
    CHECK_EQ(hit(origin().x + ::MulDiv(22, dpi, 96), origin().y + buttons.bottom / 2), LRESULT{HTSYSMENU});
    CHECK_EQ(hit((frame.left + frame.right) / 2, frame.top + buttons.bottom + ::MulDiv(60, dpi, 96)), LRESULT{HTCLIENT});
    // The title text lets the strip drag the window through it.
    const HWND title = ::GetDlgItem(hwnd, 122);
    REQUIRE(title != nullptr); CHECK(::IsWindowVisible(title));
    RECT titleRect{};
    ::GetWindowRect(title, &titleRect);
    CHECK_EQ(::SendMessageW(title, WM_NCHITTEST, 0, MAKELPARAM((titleRect.left + titleRect.right) / 2, (titleRect.top + titleRect.bottom) / 2)),
             LRESULT{HTTRANSPARENT});
    CHECK(titleRect.bottom <= frame.top + buttons.bottom + 1);
    // Maximized: the client begins at the work area (not under the screen edge) and the top is caption.
    ::ShowWindow(hwnd, SW_MAXIMIZE);
    PumpFor(f.dispatcher, 250ms);
    MONITORINFO monitor{sizeof(monitor)};
    REQUIRE(::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE);
    CHECK(std::abs(origin().y - monitor.rcWork.top) <= 1);
    CHECK_EQ(hit((monitor.rcWork.left + monitor.rcWork.right) / 2, monitor.rcWork.top + 2), LRESULT{HTCAPTION});
    ::ShowWindow(hwnd, SW_RESTORE);
    PumpFor(f.dispatcher, 250ms);
    ::GetWindowRect(hwnd, &frame);
    CHECK_EQ(origin().y, frame.top);
    window.Close();
}

AETHER_TEST("settings design: a maximized window keeps rounded corners, a continuous rim and working caption buttons") {
    // D45: DWM neither rounds nor borders a maximized window. Settings then takes a rounded window
    // region over its visible client, paints the thin rim itself and draws the caption buttons DWM
    // no longer shows under a region. Checked over a private two-phase backdrop.
    Fixture f("settings-design-maximized");
    settings::ui::SettingsWindow window(f.service, f.dispatcher);
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{});
    REQUIRE(window.Show(settings::ui::SettingsPage::Dock).ok());
    const HWND hwnd = window.Handle();
    MONITORINFO monitor{sizeof(monitor)};
    REQUIRE(::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE);
    BackdropGuard backdrop;
    backdrop.hwnd = CreateTestBackdrop(monitor.rcMonitor);
    REQUIRE(backdrop.hwnd != nullptr);
    const auto shoot = [&](const RECT& crop, bool phase) {
        SetBackdropPhase(backdrop.hwnd, phase);
        PumpFor(f.dispatcher, 250ms);
        return CaptureWithRetry(backdrop.hwnd, crop, crop, 3);
    };
    // The bare backdrop at the work-area corners (the restored window sits in the middle): what a
    // transparent corner must show exactly. A blurred material pixel changes with the phase too.
    const int radius = ::MulDiv(8, static_cast<int>(::GetDpiForWindow(hwnd)), 96);
    const int side = 4 * radius;
    const RECT& work = monitor.rcWork;
    const std::array<RECT, 4> crops{{RECT{work.left, work.top, work.left + side, work.top + side},
                                     RECT{work.right - side, work.top, work.right, work.top + side},
                                     RECT{work.left, work.bottom - side, work.left + side, work.bottom},
                                     RECT{work.right - side, work.bottom - side, work.right, work.bottom}}};
    std::array<std::array<Capture, 2>, 4> bare;
    for (std::size_t i = 0; i < crops.size(); ++i)
        for (const bool phase : {false, true}) bare[i][phase ? 1 : 0] = shoot(crops[i], phase);
    ::ShowWindow(hwnd, SW_MAXIMIZE);
    ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    PumpFor(f.dispatcher, 600ms);
    REQUIRE(::IsZoomed(hwnd));
    POINT origin{0, 0};
    ::ClientToScreen(hwnd, &origin);
    RECT client{};
    ::GetClientRect(hwnd, &client);
    const RECT visible{origin.x, origin.y, origin.x + client.right, origin.y + client.bottom};
    REQUIRE(::EqualRect(&visible, &work));

    // The region: the visible client, its corners rounded; nothing past the work area.
    RECT bounds{};
    ::GetWindowRect(hwnd, &bounds);
    HRGN region = ::CreateRectRgn(0, 0, 0, 0);
    REQUIRE(region != nullptr);
    const bool shaped = ::GetWindowRgn(hwnd, region) != ERROR;
    CHECK(shaped);
    const auto inside = [&](LONG x, LONG y) { return shaped && ::PtInRegion(region, x - bounds.left, y - bounds.top) != FALSE; };
    const LONG midX = (visible.left + visible.right) / 2, midY = (visible.top + visible.bottom) / 2;
    CHECK_FALSE(inside(visible.left, visible.top));
    CHECK_FALSE(inside(visible.right - 1, visible.top));
    CHECK_FALSE(inside(visible.left, visible.bottom - 1));
    CHECK_FALSE(inside(visible.right - 1, visible.bottom - 1));
    CHECK(inside(visible.left + radius, visible.top));
    CHECK(inside(midX, visible.top));
    CHECK(inside(visible.left, midY));
    CHECK(inside(visible.right - 1, midY));
    CHECK(inside(midX, visible.bottom - 1));
    CHECK_FALSE(inside(visible.left - 1, midY));
    CHECK_FALSE(inside(midX, visible.top - 1));
    ::DeleteObject(region);

    // The drawn caption buttons: Windows' hit codes (Snap layouts open over HTMAXBUTTON), at the top
    // right of the visible client, reported to the system title bar accessibility.
    TITLEBARINFOEX info{};
    info.cbSize = sizeof(info);
    ::SendMessageW(hwnd, WM_GETTITLEBARINFOEX, 0, reinterpret_cast<LPARAM>(&info));
    const auto center = [](const RECT& r) { return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; };
    const auto hit = [&](POINT p) { return ::SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(p.x, p.y)); };
    const RECT minimize = info.rgrect[2], maximize = info.rgrect[3], close = info.rgrect[5];
    CHECK_EQ(hit(center(minimize)), LRESULT{HTMINBUTTON});
    CHECK_EQ(hit(center(maximize)), LRESULT{HTMAXBUTTON});
    CHECK_EQ(hit(center(close)), LRESULT{HTCLOSE});
    CHECK_EQ(close.right, visible.right);
    CHECK_EQ(close.top, visible.top);
    CHECK_EQ(maximize.right, close.left);
    CHECK_EQ(minimize.right, maximize.left);
    CHECK(close.bottom - close.top >= ::MulDiv(16, static_cast<int>(::GetDpiForWindow(hwnd)), 96));
    for (const int index : {2, 3, 5}) CHECK((info.rgstate[index] & STATE_SYSTEM_INVISIBLE) == 0);
    CHECK_EQ(hit(POINT{minimize.left - ::MulDiv(40, static_cast<int>(::GetDpiForWindow(hwnd)), 96), center(minimize).y}), LRESULT{HTCAPTION});
    RECT title{};
    ::GetWindowRect(::GetDlgItem(hwnd, 122), &title);
    CHECK(title.right <= minimize.left);

    // Pixels: transparent corners over both backdrop phases, an opaque rim of the windowRim color
    // along each edge, the close glyph, and the red hover of close.
    const auto rim = graphics::MakeDesignTokens(graphics::ThemeMode::Dark).form.windowRim;
    const auto isRim = [&](const std::uint8_t* p) {
        return std::abs(p[2] - static_cast<int>(rim.r * 255.0f + 0.5f)) <= 12 && std::abs(p[1] - static_cast<int>(rim.g * 255.0f + 0.5f)) <= 12 &&
               std::abs(p[0] - static_cast<int>(rim.b * 255.0f + 0.5f)) <= 12;
    };
    const auto same = [](const std::uint8_t* a, const std::uint8_t* b) { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2]; };
    struct Corner { const char* name; POINT tip; POINT alongX; POINT alongY; };
    const std::array<Corner, 4> corners{{
        {"top-left", POINT{0, 0}, POINT{radius + 2, 0}, POINT{0, radius + 2}},
        {"top-right", POINT{side - 1, 0}, POINT{side - radius - 3, 0}, POINT{side - 1, radius + 2}},
        {"bottom-left", POINT{0, side - 1}, POINT{radius + 2, side - 1}, POINT{0, side - radius - 3}},
        {"bottom-right", POINT{side - 1, side - 1}, POINT{side - radius - 3, side - 1}, POINT{side - 1, side - radius - 3}},
    }};
    int verified = 0;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const auto& corner = corners[i];
        const auto a = shoot(crops[i], false);
        const auto b = shoot(crops[i], true);
        if (!a.Valid() || !b.Valid() || !bare[i][0].Valid() || !bare[i][1].Valid()) {
            TEST_NOTE(std::string(corner.name) + " inconclusive: a foreign window overlapped the private backdrop");
            continue;
        }
        ++verified;
        CHECK(SavePng(a, InspectionPath(std::string("settings-maximized-") + corner.name)));
        // The tip is the live scene itself in both phases: transparent, not material.
        if (!same(a.Pixel(corner.tip.x, corner.tip.y), bare[i][0].Pixel(corner.tip.x, corner.tip.y)))
            TEST_NOTE(std::string(corner.name) + " tip " + DescribePixel(a.Pixel(corner.tip.x, corner.tip.y)) + " vs scene " +
                      DescribePixel(bare[i][0].Pixel(corner.tip.x, corner.tip.y)));
        CHECK(same(a.Pixel(corner.tip.x, corner.tip.y), bare[i][0].Pixel(corner.tip.x, corner.tip.y)));
        CHECK(same(b.Pixel(corner.tip.x, corner.tip.y), bare[i][1].Pixel(corner.tip.x, corner.tip.y)));
        CHECK_FALSE(same(a.Pixel(corner.tip.x, corner.tip.y), b.Pixel(corner.tip.x, corner.tip.y)));
        for (const POINT p : {corner.alongX, corner.alongY}) {
            const bool ok = same(a.Pixel(p.x, p.y), b.Pixel(p.x, p.y)) && isRim(a.Pixel(p.x, p.y));
            if (!ok) TEST_NOTE(std::string(corner.name) + " edge pixel " + DescribePixel(a.Pixel(p.x, p.y)) + " / " + DescribePixel(b.Pixel(p.x, p.y)));
            CHECK(ok);
        }
    }
    CHECK(verified > 0);
    const RECT strip{minimize.left - ::MulDiv(120, static_cast<int>(::GetDpiForWindow(hwnd)), 96), visible.top, visible.right, close.bottom + 2 * radius};
    if (const auto caption = shoot(strip, false); caption.Valid()) CHECK(SavePng(caption, InspectionPath("settings-maximized-caption")));
    const auto glyphs = shoot(close, false);
    if (glyphs.Valid()) {
        int bright = 0;  // the close glyph in the text color, in the middle of the button
        for (int y = glyphs.height / 4; y < glyphs.height * 3 / 4; ++y)
            for (int x = glyphs.width / 4; x < glyphs.width * 3 / 4; ++x)
                bright += glyphs.Pixel(x, y)[2] > 160 && glyphs.Pixel(x, y)[1] > 160 && glyphs.Pixel(x, y)[0] > 160 ? 1 : 0;
        CHECK(bright > 0);
        // The real pointer is elsewhere, so the system posts a leave at once: paint and capture
        // the hover state before any message is dispatched.
        ::SendMessageW(hwnd, WM_NCMOUSEMOVE, HTCLOSE, MAKELPARAM(center(close).x, center(close).y));
        ::UpdateWindow(hwnd);
        ::DwmFlush();
        ::DwmFlush();
        const auto hot = CapturePrivate(backdrop.hwnd, close, close);
        if (!hot.Valid()) TEST_NOTE("close hover inconclusive: a foreign window overlapped the private backdrop");
        if (hot.Valid()) {
            const auto* p = hot.Pixel(3, hot.height / 2);
            if (!(p[2] > 150 && p[1] < 90 && p[0] < 90)) TEST_NOTE("close hover " + DescribePixel(p));
            CHECK(p[2] > 150 && p[1] < 90 && p[0] < 90);
        }
        ::SendMessageW(hwnd, WM_NCMOUSELEAVE, 0, 0);
    } else {
        TEST_NOTE("caption pixels inconclusive: a foreign window overlapped the private backdrop");
    }

    // A press that leaves the button does nothing; a release over the pressed button restores.
    const POINT press = center(maximize);
    ::SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(press.x, press.y));
    POINT away{center(close).x, center(close).y + ::MulDiv(80, static_cast<int>(::GetDpiForWindow(hwnd)), 96)};
    ::ScreenToClient(hwnd, &away);
    ::SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(away.x, away.y));
    PumpFor(f.dispatcher, 150ms);
    CHECK(::IsZoomed(hwnd));
    ::SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTMAXBUTTON, MAKELPARAM(press.x, press.y));
    POINT release = press;
    ::ScreenToClient(hwnd, &release);
    ::SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(release.x, release.y));
    PumpFor(f.dispatcher, 250ms);
    CHECK_FALSE(::IsZoomed(hwnd));
    HRGN after = ::CreateRectRgn(0, 0, 0, 0);
    CHECK_EQ(::GetWindowRgn(hwnd, after), ERROR);  // the restored window is DWM's again
    ::DeleteObject(after);
    // Restored: DWM rounds the window and draws its border in the same neutral rim color.
    ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    PumpFor(f.dispatcher, 400ms);
    RECT frame{};
    REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))));
    const RECT restoredCorner{frame.left, frame.top, frame.left + side, frame.top + side};
    const auto ra = shoot(restoredCorner, false);
    const auto rb = shoot(restoredCorner, true);
    if (ra.Valid() && rb.Valid()) {
        CHECK(SavePng(ra, InspectionPath("settings-restored-top-left")));
        CHECK_FALSE(same(ra.Pixel(0, 0), rb.Pixel(0, 0)));  // rounded: the corner shows the backdrop
        const auto* edge = ra.Pixel(0, side - 1);
        if (!(same(edge, rb.Pixel(0, side - 1)) && isRim(edge))) TEST_NOTE("restored edge pixel " + DescribePixel(edge));
        CHECK(same(edge, rb.Pixel(0, side - 1)) && isRim(edge));
    } else {
        TEST_NOTE("restored frame inconclusive: a foreign window overlapped the private backdrop");
    }
    window.Close();
}
