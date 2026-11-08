// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MonitorUtilities.h"
#include "search/ui/SearchWindow.h"
#include "applications/ShellApplicationCatalog.h"
#include "applications/ShellItems.h"
#include "applications/ShellIconProvider.h"
#include "search/FileSearch.h"
#include "core/Strings.h"
#include "CaptureFixture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace aether;
using namespace std::chrono_literals;
namespace {
class Launcher final : public apps::IApplicationLauncher {
public:
    int launches = 0;
    bool fail = false;
    void LaunchAsync(const apps::ApplicationInfo&, std::function<void(Result<void>)> completion) override {
        ++launches;
        if (fail) completion(MakeError(ErrorCode::Platform, "synthetic launch failure")); else completion(Ok());
    }
    Result<void> Activate(const apps::RunningApplication&) override { return Ok(); }
};
class Icons final : public apps::IIconProvider {
public:
    void RequestIcon(apps::IconRequest request, Callback callback) override { callback(request.key, nullptr); }
    void CancelAll() noexcept override {}
};
void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr); REQUIRE(timer);
    LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE));
    const auto token = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    loop.Run(); loop.RemoveWaitHandle(token); ::CloseHandle(timer);
}
LRESULT CALLBACK BackdropProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; const auto dc = ::BeginPaint(hwnd, &paint); RECT rect{}; ::GetClientRect(hwnd, &rect);
        for (int x = 0; x < rect.right; x += 20) {
            const auto brush = ::CreateSolidBrush(RGB(26 + (x / 20) % 6 * 4, 36 + (x / 20) % 5 * 4, 64 + (x / 20) % 7 * 4));
            RECT stripe{x, 0, x + 20, rect.bottom}; ::FillRect(dc, &stripe, brush); ::DeleteObject(brush);
        }
        ::EndPaint(hwnd, &paint); return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}
struct Backdrop {
    HWND hwnd = nullptr;
    ~Backdrop() { if (hwnd) ::DestroyWindow(hwnd); }
    bool Create(RECT bounds) {
        WNDCLASSW cls{}; cls.lpfnWndProc = BackdropProc; cls.hInstance = ::GetModuleHandleW(nullptr); cls.lpszClassName = L"Aether.Search.TestBackdrop";
        if (!::RegisterClassW(&cls) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, cls.lpszClassName, L"", WS_POPUP,
            bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, nullptr);
        if (!hwnd) return false;
        ::ShowWindow(hwnd, SW_SHOW); ::UpdateWindow(hwnd); return true;
    }
};
struct Fixture {
    platform::ComApartment apartment;
    ManualDispatcher dispatcher;
    search::SearchService service{dispatcher};
    graphics::GraphicsDevice device;
    platform::MessageLoop loop;
    graphics::FrameScheduler scheduler;
    Launcher launcher;
    Icons icons;
    std::unique_ptr<search::ui::SearchWindow> window;
    Fixture() {
        REQUIRE(SUCCEEDED(apartment.Status())); std::vector<apps::ApplicationInfo> apps;
        for (int i = 0; i < 30; ++i) {
            apps::ApplicationInfo app; app.id = L"test:" + std::to_wstring(i);
            app.displayName = L"Test Application " + std::to_wstring(i); apps.push_back(std::move(app));
        }
        REQUIRE(service.Rebuild(std::move(apps)).ok()); const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (service.Current().state == search::IndexState::Building && std::chrono::steady_clock::now() < deadline) {
            dispatcher.RunPending(); std::this_thread::sleep_for(1ms);
        }
        REQUIRE_EQ(service.Current().state, search::IndexState::Ready);
        REQUIRE(device.Initialize().ok()); REQUIRE(scheduler.Initialize(loop).ok());
        window = std::make_unique<search::ui::SearchWindow>(device, scheduler, service, launcher, icons);
        REQUIRE(window->Initialize().ok());
    }
    ~Fixture() { window.reset(); scheduler.Shutdown(); }
    void Query(const wchar_t* query) { ::SetWindowTextW(window->EditHandle(), query); RunFor(loop, 110); }
};
}

// D68: the owner could not find Device Manager. The real catalog of this PC and the search index, as the shell uses them.
AETHER_TEST("search: the real catalog finds Windows' own tools by name, Device Manager first") {
    platform::ComApartment com;
    REQUIRE(SUCCEEDED(com.Status()));
    auto discovered = apps::ShellApplicationCatalog().DiscoverApplications(CancellationToken{});
    REQUIRE(discovered.ok());
    auto index = search::SearchIndex::Build(discovered.value());
    REQUIRE(index.ok());
    for (const auto* query : {L"device manager", L"Device Manager", L"device", L"devmgmt", L"gestionnaire de périphériques"}) {
        auto found = index.value()->Query(query, 8);
        REQUIRE(found.ok());
        std::string top;
        for (const auto& result : found.value()) top += text::Narrow(result.application.displayName) + " | ";
        TEST_NOTE(text::Narrow(query) + " -> " + top);
    }
    // Device Manager first, by its name in either language or the command people type.
    for (const auto* query : {L"device manager", L"devmgmt", L"gestionnaire de périphériques"}) {
        const auto first = index.value()->Query(query, 8);
        REQUIRE(first.ok());
        REQUIRE_FALSE(first.value().empty());
        CHECK(text::ToLower(first.value().front().application.parsingName).find(L"74246bfc") != std::wstring::npos);
    }
    // Windows' Settings pages, as Start finds them, and each one opens.
    const auto display = index.value()->Query(L"display settings", 4);
    REQUIRE(display.ok());
    REQUIRE_FALSE(display.value().empty());
    CHECK(display.value().front().application.parsingName == L"ms-settings:display");
    CHECK(apps::shell::CreateApplicationItem(display.value().front().application).ok());
    const auto wifi = index.value()->Query(L"wifi", 4);
    REQUIRE(wifi.ok());
    CHECK(std::any_of(wifi.value().begin(), wifi.value().end(), [](const search::SearchResult& r) { return r.application.parsingName == L"ms-settings:network-wifi"; }));
}

// D68: files and folders from Windows' own index, read-only. Windows Search can be off: then nothing, and no failure.
AETHER_TEST("search: files come from Windows' index by name, existing, as files that open as themselves") {
    platform::ComApartment com;
    REQUIRE(SUCCEEDED(com.Status()));
    for (const auto* query : {L"a", L"e", L"desktop"}) {
        auto files = search::FindFiles(query, 8, CancellationToken{});
        if (!files) { TEST_NOTE("Windows' index unavailable here: " + files.error().Describe()); return; }
        CHECK(files.value().size() <= 8u);
        std::size_t existing = 0;
        for (const auto& file : files.value()) {
            CHECK(file.kind == apps::ApplicationKind::File);
            CHECK(file.id.rfind(L"file:", 0) == 0);
            CHECK_FALSE(search::IsApplicationFile(file.parsingName));
            CHECK_FALSE(file.detail.empty());
            if (auto item = apps::shell::CreateApplicationItem(file); item.ok()) ++existing;
            else if (existing == 0) TEST_NOTE("not opened (" + std::to_string(file.parsingName.size()) + " chars, starts '" + text::Narrow(file.parsingName.substr(0, 3)) + "'): " + item.error().Describe());
        }
        TEST_NOTE(text::Narrow(query) + ": " + std::to_string(files.value().size()) + " found, " + std::to_string(existing) + " still there");
        CHECK_EQ(existing, files.value().size());   // only what still exists, never a deleted file the index remembers
    }
}

AETHER_TEST("search UI: burst edits debounce, keys flush, failed launch remains visible and short results scroll") {
    Fixture fixture; auto& window = *fixture.window;
    const auto monitor = platform::PrimaryMonitor();
    window.SetPlacementArea(RECT{monitor.workArea.left + 40, monitor.workArea.top + 40, monitor.workArea.left + 840, monitor.workArea.top + 430});
    window.Open(); const auto before = window.QueryCount();
    ::SetWindowTextW(window.EditHandle(), L"T"); ::SetWindowTextW(window.EditHandle(), L"Test"); ::SetWindowTextW(window.EditHandle(), L"Application");
    CHECK_EQ(window.QueryCount(), before); RunFor(fixture.loop, 110); CHECK_EQ(window.QueryCount(), before + 1);
    REQUIRE(window.Results().size() >= 8u); CHECK_FALSE(window.Item(0).isOffscreen); CHECK(window.Item(7).isOffscreen);
    const auto lastId = window.Item(7).id; REQUIRE(window.SetFocus(lastId).ok());
    CHECK(window.Item(0).isOffscreen); CHECK_FALSE(window.Item(7).isOffscreen); CHECK(window.Item(7).hasKeyboardFocus);
    const auto visible = window.Item(7).boundsPx; const auto bounds = window.BoundsPx(); CHECK(visible.bottom < bounds.bottom);
    fixture.launcher.fail = true; ::SendMessageW(window.EditHandle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK(window.IsOpen()); CHECK_FALSE(window.ErrorText().empty()); CHECK_FALSE(window.Results().empty());
    ::SetWindowTextW(window.EditHandle(), L"Application 1"); const auto pending = window.QueryCount();
    ::SendMessageW(window.EditHandle(), WM_KEYDOWN, VK_DOWN, 0); CHECK_EQ(window.QueryCount(), pending + 1); CHECK(window.ErrorText().empty());
    CHECK(window.Item(1).hasKeyboardFocus);
    window.Close(); const auto closed = window.QueryCount(); RunFor(fixture.loop, 120); CHECK_EQ(window.QueryCount(), closed);
}

AETHER_TEST("search UI: repeated Open preserves its restore target and native input exposes its name and value from an MTA") {
    Fixture fixture; auto& window = *fixture.window;
    window.Open(); const auto previous = window.PreviousForegroundWindow(); fixture.Query(L"Application"); window.Open();
    CHECK_EQ(window.PreviousForegroundWindow(), previous);
    const HWND edit = window.EditHandle(); std::atomic<bool> done{false}; std::string failure;
    std::thread client([&] {
        const auto initialized = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            winrt::com_ptr<IUIAutomation> automation;
            winrt::check_hresult(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()));
            winrt::com_ptr<IUIAutomationElement> input; winrt::check_hresult(automation->ElementFromHandle(edit, input.put()));
            BSTR name = nullptr; winrt::check_hresult(input->get_CurrentName(&name)); const std::wstring text = name ? name : L""; ::SysFreeString(name);
            if (text != L"Search apps, settings and files") throw std::runtime_error("native edit name mismatch");
            BOOL focusable = FALSE; winrt::check_hresult(input->get_CurrentIsKeyboardFocusable(&focusable));
            if (!focusable) throw std::runtime_error("native edit is not focusable");
            winrt::com_ptr<IUIAutomationValuePattern> value;
            winrt::check_hresult(input->GetCurrentPatternAs(UIA_ValuePatternId, __uuidof(IUIAutomationValuePattern), value.put_void()));
            BSTR current = nullptr; winrt::check_hresult(value->get_CurrentValue(&current)); const std::wstring query = current ? current : L""; ::SysFreeString(current);
            if (query != L"Application") throw std::runtime_error("native edit value mismatch");
        } catch (...) { failure = "own native edit UIA assertion failed"; }
        if (SUCCEEDED(initialized)) ::CoUninitialize(); done = true;
    });
    while (!done.load()) RunFor(fixture.loop, 10);
    client.join(); CHECK(failure.empty());
    // Do not activate the saved target: it may belong to a real foreground application.
    window.Hide();
}

AETHER_TEST("search UI: dark and light own-window captures include query selection and inline launch error") {
    Fixture fixture; auto& window = *fixture.window; const auto monitor = platform::PrimaryMonitor();
    const int areaWidth = std::min(800, platform::Width(monitor.workArea) - 360);
    const int areaHeight = std::min(500, platform::Height(monitor.workArea) - 400);
    REQUIRE(areaWidth >= 320); REQUIRE(areaHeight >= 260);
    const int areaLeft = monitor.workArea.left + (platform::Width(monitor.workArea) - areaWidth) / 2;
    const int areaTop = monitor.workArea.top + (platform::Height(monitor.workArea) - areaHeight) / 2;
    const RECT area{areaLeft, areaTop, areaLeft + areaWidth, areaTop + areaHeight};
    window.SetPlacementArea(area);
    RECT privateBounds = window.BoundsPx(); ::InflateRect(&privateBounds, 138, 138);
    REQUIRE(privateBounds.left >= monitor.workArea.left); REQUIRE(privateBounds.top >= monitor.workArea.top);
    REQUIRE(privateBounds.right <= monitor.workArea.right); REQUIRE(privateBounds.bottom <= monitor.workArea.bottom);
    Backdrop backdrop; REQUIRE(backdrop.Create(privateBounds));
    window.SetPlacementArea(area); window.Open(); fixture.Query(L"Test Application");
    fixture.launcher.fail = true;
    ::SendMessageW(window.EditHandle(), WM_KEYDOWN, VK_RETURN, 0); REQUIRE_FALSE(window.ErrorText().empty());
    ::SendMessageW(window.EditHandle(), EM_SETSEL, 0, 4);
    const auto output = std::filesystem::temp_directory_path() / "AetherShellTests";
    std::filesystem::create_directories(output);
    RECT originalWork{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &originalWork, 0));
    for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::Light}) {
        window.SetTheme(graphics::MakeDesignTokens(mode), graphics::MotionPolicy{true, 1});
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RunFor(fixture.loop, 220);
        auto bounds = window.BoundsPx(); ::InflateRect(&bounds, 10, 10);
        const auto captured = CaptureOwnBackdrop(backdrop.hwnd, bounds);
        REQUIRE_FALSE(captured.bytes.empty());
        Save(captured, output / (mode == graphics::ThemeMode::Dark ? L"search-dark.png" : L"search-light.png"));
    }
    RECT after{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &after, 0));
    CHECK(::EqualRect(&originalWork, &after)); window.Close();
}

// D68: the real Search, for a person to look at: this PC's catalog, its icons and Windows' index of its files.
AETHER_TEST("search UI: the real catalog, icons and files, captured as the owner will see them (D68)") {
    platform::ComApartment apartment;
    REQUIRE(SUCCEEDED(apartment.Status()));
    ManualDispatcher dispatcher;
    search::SearchService service{dispatcher};
    auto discovered = apps::ShellApplicationCatalog().DiscoverApplications(CancellationToken{});
    REQUIRE(discovered.ok());
    REQUIRE(service.Rebuild(std::move(discovered.value())).ok());
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (service.Current().state == search::IndexState::Building && std::chrono::steady_clock::now() < deadline) {
        dispatcher.RunPending(); std::this_thread::sleep_for(1ms);
    }
    REQUIRE_EQ(service.Current().state, search::IndexState::Ready);
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    platform::MessageLoop loop;
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    Launcher launcher;
    apps::ShellIconProvider icons(dispatcher);
    auto window = std::make_unique<search::ui::SearchWindow>(device, scheduler, service, launcher, icons);
    REQUIRE(window->Initialize().ok());
    const auto monitor = platform::PrimaryMonitor();
    const int areaWidth = std::min(800, platform::Width(monitor.workArea) - 360);
    const int areaHeight = std::min(560, platform::Height(monitor.workArea) - 300);
    const int areaLeft = monitor.workArea.left + (platform::Width(monitor.workArea) - areaWidth) / 2;
    const int areaTop = monitor.workArea.top + (platform::Height(monitor.workArea) - areaHeight) / 2;
    const RECT area{areaLeft, areaTop, areaLeft + areaWidth, areaTop + areaHeight};
    window->SetPlacementArea(area);
    window->Open();
    RECT privateBounds = window->BoundsPx(); ::InflateRect(&privateBounds, 150, 150);   // the panel and its guard band
    privateBounds.top = std::max(privateBounds.top, monitor.workArea.top);
    privateBounds.bottom = std::min(privateBounds.bottom, monitor.workArea.bottom);
    Backdrop backdrop; REQUIRE(backdrop.Create(privateBounds));
    const auto output = std::filesystem::temp_directory_path() / "AetherShellTests";
    std::filesystem::create_directories(output);
    const auto settle = [&](DWORD milliseconds) {
        for (DWORD waited = 0; waited < milliseconds; waited += 40) { dispatcher.RunPending(); RunFor(loop, 40); }
    };
    window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1});
    for (const auto* query : {L"device", L"display", L"pexels", L"aether"}) {
        window->Open();
        ::SetWindowTextW(window->EditHandle(), query);
        settle(900);   // the applications at once, then the files from Windows' index, then the icons
        const auto& results = window->Results();
        std::size_t files = 0;
        for (const auto& result : results) files += result.application.kind == apps::ApplicationKind::File ? 1u : 0u;
        TEST_NOTE(text::Narrow(query) + ": " + std::to_string(results.size()) + " results, " + std::to_string(files) + " files");
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(window->Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        settle(200);
        auto bounds = window->BoundsPx(); ::InflateRect(&bounds, 10, 10);
        if (!PrivateCrop(backdrop.hwnd, bounds)) { TEST_NOTE("inconclusive, a foreign window passed"); continue; }
        Save(CaptureOwnBackdrop(backdrop.hwnd, bounds), output / (L"search-real-" + std::wstring(query) + L".png"));
    }
    CHECK_EQ(launcher.launches, 0);   // nothing was opened
    window->Close();
    window.reset();
    icons.CancelAll();
    scheduler.Shutdown();
}

AETHER_TEST("search UI: finite exit hides, reopen cancels exit and reduced motion is immediate") {
    Fixture fixture;auto& window=*fixture.window;
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark),graphics::MotionPolicy{});
    window.Open();fixture.Query(L"Test");RunFor(fixture.loop,180);
    REQUIRE(window.IsOpen());REQUIRE(::IsWindowVisible(window.Handle())!=FALSE);
    window.Close();CHECK_FALSE(window.IsOpen());CHECK(::IsWindowVisible(window.Handle())!=FALSE);
    CHECK_FALSE(window.Invoke(window.Item(0).id).ok());CHECK_FALSE(window.Item(0).isEnabled);
    RunFor(fixture.loop,220);CHECK(::IsWindowVisible(window.Handle())==FALSE);
    window.Open();window.Close();window.Open();RunFor(fixture.loop,220);
    CHECK(window.IsOpen());CHECK(::IsWindowVisible(window.Handle())!=FALSE);CHECK(::IsWindowEnabled(window.EditHandle())!=FALSE);
    graphics::MotionPolicy reduced;reduced.reduced=true;window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark),reduced);
    window.Close();CHECK_FALSE(window.IsOpen());CHECK(::IsWindowVisible(window.Handle())==FALSE);
}
