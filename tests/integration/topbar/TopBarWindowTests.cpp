// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "topbar/TopBarWindow.h"
#include "topbar/TopBarMenu.h"
#include "core/Strings.h"

#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Win32UiDispatcher.h"
#include "status/StatusProviders.h"
#include "status/WinRtSupport.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>
#include <UIAutomation.h>
#include <shellapi.h>

using namespace aether;
using namespace aether::topbar;

namespace {

LRESULT CALLBACK BackdropProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(hwnd, &paint);
        RECT client{}; ::GetClientRect(hwnd, &client);
        for (int x = 0; x < client.right; x += 16) {
            const auto color = RGB(24 + (x / 16) % 8 * 3, 35 + (x / 16) % 9 * 3, 62 + (x / 16) % 7 * 5);
            HBRUSH brush = ::CreateSolidBrush(color);
            RECT band{x, 0, x + 16, client.bottom}; ::FillRect(dc, &band, brush); ::DeleteObject(brush);
        }
        ::EndPaint(hwnd, &paint); return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}
struct Backdrop {
    HWND hwnd = nullptr;
    ~Backdrop() { if (hwnd) ::DestroyWindow(hwnd); }
    bool Create(const RECT& bounds) {
        WNDCLASSW cls{}; cls.lpfnWndProc = BackdropProc; cls.hInstance = ::GetModuleHandleW(nullptr);
        cls.lpszClassName = L"Aether.TopBar.TestBackdrop";
        if (!::RegisterClassW(&cls) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            cls.lpszClassName, L"", WS_POPUP, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, nullptr);
        if (!hwnd) return false;
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE); ::UpdateWindow(hwnd); return true;
    }
};
void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    REQUIRE(timer != nullptr);
    LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != FALSE);
    const auto id = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    loop.Run(); loop.RemoveWaitHandle(id); ::CloseHandle(timer);
}
struct Capture {
    int width, height;
    std::vector<std::uint8_t> bytes;
};
bool PrivateCrop(HWND backdrop, const RECT& crop) {
    DWORD owner = 0; ::GetWindowThreadProcessId(backdrop, &owner);
    RECT backdropBounds{};
    if (owner != ::GetCurrentProcessId() || !::IsWindowVisible(backdrop) || !::GetWindowRect(backdrop, &backdropBounds) ||
        backdropBounds.left > crop.left || backdropBounds.top > crop.top ||
        backdropBounds.right < crop.right || backdropBounds.bottom < crop.bottom) return false;
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (window == backdrop) return true;
        if (!::IsWindowVisible(window)) continue;
        DWORD process = 0; ::GetWindowThreadProcessId(window, &process);
        if (process == ::GetCurrentProcessId()) continue;
        RECT bounds{}, overlap{};
        if (::GetWindowRect(window, &bounds) && ::IntersectRect(&overlap, &bounds, &crop)) return false;
    }
    return false;
}
Capture CaptureOwnBackdrop(HWND backdrop, const RECT& guardedArea, const RECT& bounds) {
    REQUIRE(PrivateCrop(backdrop, guardedArea));
    REQUIRE(bounds.left >= guardedArea.left && bounds.top >= guardedArea.top &&
            bounds.right <= guardedArea.right && bounds.bottom <= guardedArea.bottom);
    Capture capture{bounds.right - bounds.left, bounds.bottom - bounds.top, {}};
    HDC screen = ::GetDC(nullptr), memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = capture.width; info.bmiHeader.biHeight = -capture.height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    REQUIRE(bitmap != nullptr); REQUIRE(bits != nullptr);
    HGDIOBJ previous = ::SelectObject(memory, bitmap);
    const bool copied = ::BitBlt(memory, 0, 0, capture.width, capture.height, screen,
        bounds.left, bounds.top, SRCCOPY | CAPTUREBLT) != FALSE;
    if (copied) capture.bytes.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) +
        static_cast<std::size_t>(capture.width) * capture.height * 4);
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen);
    REQUIRE(copied); REQUIRE(PrivateCrop(backdrop, guardedArea)); return capture;
}
void Save(const Capture& capture, const std::filesystem::path& file) {
    auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICStream> stream; winrt::check_hresult(factory->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder;
    winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame;
    winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr)); winrt::check_hresult(frame->Initialize(nullptr));
    winrt::check_hresult(frame->SetSize(capture.width, capture.height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format)); REQUIRE(format == GUID_WICPixelFormat32bppBGRA);
    auto bytes = capture.bytes; for (std::size_t index = 3; index < bytes.size(); index += 4) bytes[index] = 255;
    winrt::check_hresult(frame->WritePixels(capture.height, capture.width * 4, static_cast<UINT>(bytes.size()), bytes.data()));
    winrt::check_hresult(frame->Commit()); winrt::check_hresult(encoder->Commit());
}
TopBarSnapshots Synthetic() {
    TopBarSnapshots snapshots;
    snapshots.clock.state = status::ProviderState::Available;
    snapshots.clock.year = 2026; snapshots.clock.month = 10; snapshots.clock.day = 18;
    snapshots.clock.dayOfWeek = 0; snapshots.clock.hour = 17; snapshots.clock.minute = 42;
    snapshots.clock.timeZoneName = L"Synthetic test zone";
    snapshots.battery.state = status::ProviderState::Available; snapshots.battery.batteryPresent = true;
    snapshots.battery.percent = 78; snapshots.battery.source = status::PowerSource::Battery;
    snapshots.keyboard.state = status::ProviderState::Available; snapshots.keyboard.shortLabel = L"EN";
    snapshots.keyboard.languageName = L"English"; snapshots.keyboard.layoutName = L"Synthetic layout";
    snapshots.keyboard.fromForegroundWindow = true;
    snapshots.network.state = status::ProviderState::Available; snapshots.network.kind = status::ConnectionKind::WiFi;
    snapshots.network.connectivity = status::InternetConnectivity::Internet; snapshots.network.signalBars = std::uint8_t{4};
    snapshots.audio = {status::ProviderState::Available, true, 0.6f, false, L"Synthetic audio", {}};
    snapshots.microphone = {status::ProviderState::Available, true, true, 0.5f, L"Synthetic microphone", status::Support::Unsupported, {}};
    snapshots.weather.snapshot.state = status::WeatherState::Available;
    snapshots.weather.snapshot.temperature = 22.0; snapshots.weather.snapshot.conditionText = L"Clear sky";
    snapshots.weather.snapshot.attribution = L"Weather data by Open-Meteo.com";
    snapshots.weather.glyph = weather::WeatherGlyph::Clear; snapshots.weather.isDay = true;
    return snapshots;
}
}

AETHER_TEST("topbar integration: dark and light own-window captures, unchanged work area and idle frames") {
    platform::ComApartment com;
    REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    RECT originalWork{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &originalWork, 0) != FALSE);
    const auto monitor = platform::PrimaryMonitor();
    const float scale = static_cast<float>(monitor.dpi) / 96.0f;
    const int width = static_cast<int>(std::min<LONG>(1100, monitor.workArea.right - monitor.workArea.left - 80));
    const int height = static_cast<int>(std::lround(32 * scale));
    // 140 px keeps the 128 px guard band below the screen's top edge: Explorer can keep topmost 1x1
    // helper windows at the corner, and the guard rightly refuses any foreign overlap (2026-10-18).
    const RECT captureBounds{monitor.workArea.left + 40, monitor.workArea.top + 140,
        monitor.workArea.left + 40 + width, monitor.workArea.top + 140 + height + 48};
    RECT privateBounds = captureBounds;
    ::InflateRect(&privateBounds, 128, 128); // Guard band keeps blur samples over test content too.
    Backdrop backdrop; REQUIRE(backdrop.Create(privateBounds));
    const RECT barBounds{captureBounds.left + 10, captureBounds.top + 20, captureBounds.right - 10, captureBounds.top + 20 + height};
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false;
    TopBarWindow window(context); REQUIRE(window.Create(barBounds).ok());
    settings::TopBarSettings settings; settings.reserveSpace = false; settings.clockFormat = settings::ClockFormat::Hours24;
    window.SetSettings(settings, true); window.SetSnapshots(Synthetic());
    const auto output = std::filesystem::temp_directory_path() / "AetherShellTests";
    std::filesystem::create_directories(output);
    for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::Light}) {
        auto tokens = graphics::MakeDesignTokens(mode); tokens.transparencyEnabled = true;
        window.SetTheme(tokens, graphics::MotionPolicy{true, 1});
        window.Show(false); RunFor(loop, 350);
        // Probe only this test HWND. The former shadow margin must be outside its native region.
        const auto nativeBounds = window.BoundsPx();
        const int panelLeft = barBounds.left - nativeBounds.left;
        const int panelTop = barBounds.top - nativeBounds.top;
        REQUIRE(panelLeft > 0); REQUIRE(panelTop > 0);
        HRGN nativeRegion = ::CreateRectRgn(0, 0, 0, 0); REQUIRE(nativeRegion != nullptr);
        CHECK(::GetWindowRgn(window.Handle(), nativeRegion) != ERROR);
        CHECK_FALSE(::PtInRegion(nativeRegion, panelLeft - 1, panelTop + 1));
        CHECK_FALSE(::PtInRegion(nativeRegion, panelLeft + 1, panelTop - 1));
        CHECK(::PtInRegion(nativeRegion, panelLeft + 1, panelTop + 1));
        ::DeleteObject(nativeRegion);
        // Reassert that the private backdrop covers every captured pixel before capture.
        REQUIRE(::IsWindowVisible(backdrop.hwnd));
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RunFor(loop, 100);
        const auto captured = CaptureOwnBackdrop(backdrop.hwnd, privateBounds, captureBounds);
        REQUIRE_FALSE(captured.bytes.empty());
        const auto pixel = [&](int x, int y, int channel) {
            return captured.bytes[(static_cast<std::size_t>(y) * captured.width + x) * 4 + channel];
        };
        int materialDifference = 0;
        // Average many blank panel pixels; a single stripe can match the tint by coincidence.
        for (int x = 80; x < 280; ++x) for (int channel = 0; channel < 3; ++channel)
            materialDifference += std::abs(static_cast<int>(pixel(x, 20 + height / 2, channel)) -
                                           static_cast<int>(pixel(x, 3, channel)));
        CHECK(materialDifference > 2000); // Mean RGB difference >10, excluding alpha/text.
        CHECK(window.RenderCount() > 0);
        Save(captured, output / (mode == graphics::ThemeMode::Dark ? L"topbar-dark.png" : L"topbar-light.png"));
        // Showing, raising and capturing the window can leave native paint work queued. Require
        // bounded quiescence before measuring idle; a perpetual frame loop cannot satisfy it.
        int quietIntervals = 0;
        for (int attempt = 0; attempt < 12 && quietIntervals < 2; ++attempt) {
            const auto before = window.RenderCount(); RunFor(loop, 120);
            quietIntervals = window.RenderCount() == before ? quietIntervals + 1 : 0;
        }
        REQUIRE_EQ(quietIntervals, 2);
        const auto frames = window.RenderCount(); RunFor(loop, 120);
        CHECK_EQ(window.RenderCount(), frames);
    }
    // Render distinct kinds rather than using a shared cellular-bars mark for every connection.
    auto snapshots = Synthetic();
    for (const auto kind : {status::ConnectionKind::WiFi, status::ConnectionKind::Ethernet, status::ConnectionKind::Cellular}) {
        snapshots.network.kind = kind;
        snapshots.network.signalBars = kind == status::ConnectionKind::WiFi ? std::nullopt : std::optional<std::uint8_t>(std::uint8_t{4});
        window.SetSnapshots(snapshots); RunFor(loop, 180);
        const auto network = std::find_if(window.Items().begin(), window.Items().end(), [](const auto& entry) {
            return entry.item.kind == ItemKind::Network;
        });
        REQUIRE(network != window.Items().end()); CHECK(network->item.connection == kind);
        if (kind == status::ConnectionKind::WiFi) {
            CHECK_FALSE(network->item.level.has_value());
            CHECK(network->item.accessibleName.find(L"Signal strength unavailable") != std::wstring::npos);
        }
        const auto captured = CaptureOwnBackdrop(backdrop.hwnd, privateBounds, captureBounds);
        Save(captured, output / (kind == status::ConnectionKind::WiFi ? L"topbar-wifi-unknown.png" :
            kind == status::ConnectionKind::Ethernet ? L"topbar-ethernet.png" : L"topbar-cellular.png"));
    }
    REQUIRE_FALSE(window.Items().empty()); const auto logoId = window.Items().front().item.id;
    const auto beforeUpdate = window.RenderCount();
    REQUIRE(window.SetUpdateAvailable(true).ok());
    CHECK_EQ(window.Items().front().item.id, logoId);
    CHECK(window.Items().front().item.accessibleName.find(L"Update available") != std::wstring::npos);
    CHECK(window.Items().front().item.tooltip.find(L"Update available") != std::wstring::npos);
    // Changed state queues a frame, and UIA/native painting can queue another afterward. Observe
    // that work finish before attributing frames to the duplicate update. A perpetual idle loop
    // fails this bounded quiescence requirement; extending a fixed delay would hide that defect.
    int quietIntervals = 0;
    for (int attempt = 0; attempt < 12 && quietIntervals < 2; ++attempt) {
        const auto frames = window.RenderCount(); RunFor(loop, 120);
        quietIntervals = window.RenderCount() > beforeUpdate && window.RenderCount() == frames ? quietIntervals + 1 : 0;
    }
    REQUIRE_EQ(quietIntervals, 2);
    const auto updateFrames = window.RenderCount();
    REQUIRE(window.SetUpdateAvailable(true).ok()); RunFor(loop, 120); CHECK_EQ(window.RenderCount(), updateFrames);
    REQUIRE(window.SetUpdateAvailable(false).ok());
    CHECK(window.Items().front().item.accessibleName.find(L"Update available") == std::wstring::npos);
    CHECK(window.Items().front().item.tooltip.find(L"Update available") == std::wstring::npos);
    RECT after{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &after, 0) != FALSE);
    CHECK(::EqualRect(&originalWork, &after) != FALSE);
    window.Destroy(); dispatcher.Shutdown(); scheduler.Shutdown();
}

AETHER_TEST("topbar integration: pointer keyboard and MTA UIA emit only injected interaction requests") {
    platform::ComApartment com; REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    RECT before{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &before, 0) != FALSE);
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false;
    TopBarWindow window(context); REQUIRE(window.Create(RECT{80, 180, 1180, 228}).ok());
    settings::TopBarSettings config; config.reserveSpace = false;
    window.SetSettings(config, true); window.SetSnapshots(Synthetic());
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1});
    int keyboard = 0, network = 0, weather = 0;
    const DWORD uiThread = ::GetCurrentThreadId(); bool wrongThread = false;
    auto keyConnection = window.KeyboardRequested.Connect([&] { ++keyboard; wrongThread |= ::GetCurrentThreadId() != uiThread; });
    auto netConnection = window.NetworkRequested.Connect([&] { ++network; wrongThread |= ::GetCurrentThreadId() != uiThread; });
    int hiddenIcons = 0;
    RECT hiddenAnchor{};
    auto hiddenConnection = window.HiddenIconsRequested.Connect([&](RECT anchor) {
        ++hiddenIcons; hiddenAnchor = anchor; wrongThread |= ::GetCurrentThreadId() != uiThread;
    });
    auto weatherConnection = window.WeatherRequested.Connect([&] { ++weather; wrongThread |= ::GetCurrentThreadId() != uiThread; });
    window.Show(false); RunFor(loop, 100);
    const auto click = [&](ItemKind kind) {
        const auto found = std::find_if(window.Items().begin(), window.Items().end(), [kind](const auto& item) { return item.item.kind == kind; });
        REQUIRE(found != window.Items().end()); REQUIRE(found->item.actionable);
        const auto x = static_cast<int>(std::lround(window.ToPx((found->rect.left + found->rect.right) / 2)));
        const auto y = static_cast<int>(std::lround(window.ToPx((found->rect.top + found->rect.bottom) / 2)));
        ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    };
    click(ItemKind::Keyboard); click(ItemKind::Network); click(ItemKind::Weather);
    CHECK_EQ(keyboard, 1); CHECK_EQ(network, 1); CHECK_EQ(weather, 1);
    ::SendMessageW(window.Handle(), WM_KEYDOWN, VK_HOME, 0);
    // Weather, the hidden-icons chevron (D45), the input language and the network, in reading order.
    for (int index = 0; index < 4; ++index) {
        ::SendMessageW(window.Handle(), WM_KEYDOWN, VK_RIGHT, 0);
        ::SendMessageW(window.Handle(), WM_KEYDOWN, VK_RETURN, 0);
    }
    CHECK_EQ(keyboard, 2); CHECK_EQ(network, 2); CHECK_EQ(weather, 2); CHECK_EQ(hiddenIcons, 1);
    // The focus ring follows keyboard navigation only: focus alone or a click never shows it.
    ::SetFocus(window.Handle()); RunFor(loop, 20);
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(0, 0));  // a click on no item
    CHECK_FALSE(window.FocusCuesVisible());
    ::SendMessageW(window.Handle(), WM_KEYDOWN, VK_LEFT, 0);
    CHECK(window.FocusCuesVisible());
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, MAKELPARAM(0, 0));
    CHECK_FALSE(window.FocusCuesVisible());
    // The chevron shows Windows' overflow state: up while open, down once it closed.
    CHECK_FALSE(window.HiddenIconsOpen());
    window.SetHiddenIconsOpen(true); CHECK(window.HiddenIconsOpen());
    window.SetHiddenIconsOpen(false); CHECK_FALSE(window.HiddenIconsOpen());
    std::atomic<bool> finished{false}; bool passed = false;
    const HWND hwnd = window.Handle();
    std::thread client([&] {
        platform::ComApartment apartment(COINIT_MULTITHREADED);
        const auto check = [](HRESULT hr) { if (FAILED(hr)) throw std::runtime_error("own Top Bar UIA request failed"); };
        try {
            check(apartment.Status());
            winrt::com_ptr<IUIAutomation> automation;
            check(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()));
            winrt::com_ptr<IUIAutomationElement> root; check(automation->ElementFromHandle(hwnd, root.put()));
            for (const auto kind : {ItemKind::Keyboard, ItemKind::Network, ItemKind::Weather, ItemKind::NotificationArea}) {
                VARIANT id{}; id.vt = VT_BSTR; id.bstrVal = ::SysAllocString((L"item-" + std::to_wstring(static_cast<int>(kind))).c_str());
                winrt::com_ptr<IUIAutomationCondition> condition;
                const auto created = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put());
                ::VariantClear(&id); check(created);
                winrt::com_ptr<IUIAutomationElement> element; check(root->FindFirst(TreeScope_Children, condition.get(), element.put()));
                if (!element) throw std::runtime_error("own Top Bar action missing");
                CONTROLTYPEID type{}; check(element->get_CurrentControlType(&type));
                BOOL focusable = FALSE; check(element->get_CurrentIsKeyboardFocusable(&focusable));
                if (type != UIA_ButtonControlTypeId || !focusable) throw std::runtime_error("own Top Bar action is not accessible");
                winrt::com_ptr<IUIAutomationInvokePattern> invoke;
                check(element->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), invoke.put_void()));
                check(invoke->Invoke());
            }
            passed = true;
        } catch (...) { /* Report a fixed failure without exposing external UIA details. */ }
        finished.store(true);
    });
    while (!finished.load()) RunFor(loop, 20);
    client.join(); RunFor(loop, 100);
    CHECK(passed); CHECK_FALSE(wrongThread);
    CHECK_EQ(keyboard, 3); CHECK_EQ(network, 3); CHECK_EQ(weather, 3); CHECK_EQ(hiddenIcons, 2);
    window.Hide(); click(ItemKind::Keyboard); click(ItemKind::Network); click(ItemKind::Weather);
    CHECK_EQ(keyboard, 3); CHECK_EQ(network, 3); CHECK_EQ(weather, 3);
    RECT after{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &after, 0) != FALSE);
    CHECK(::EqualRect(&before, &after) != FALSE);
    window.Destroy(); dispatcher.Shutdown(); scheduler.Shutdown(); device.Shutdown();
}

AETHER_TEST("topbar integration: fake menus route actions and reject stale taskbar commands") {
    class FakeMenu final : public ui::IMenuPresenter {
    public:
        BarCommand next = BarCommand::Settings;
        HWND previousForeground = nullptr;
        std::function<void()> duringShow;
        std::vector<ui::MenuEntry> last;
        Result<ui::MenuSelection> Show(std::span<const ui::MenuEntry> entries, const ui::MenuRequest&) override {
            last.assign(entries.begin(), entries.end());
            if (duringShow) duringShow();
            return ui::MenuSelection{static_cast<std::uint32_t>(next), previousForeground};
        }
    } menu;
    class FakeFocus final : public ui::IMenuWindowOps {
    public:
        HWND foreground = nullptr;
        HWND target = reinterpret_cast<HWND>(std::uintptr_t{0x20});
        bool accept = true;
        HWND Foreground() noexcept override { return foreground; }
        bool SetForeground(HWND window) noexcept override { if (accept) foreground = window; return accept; }
        bool IsValidWindow(HWND window) noexcept override { return window == target; }
        LONG_PTR ExtendedStyle(HWND) noexcept override { return 0; }
        void SetExtendedStyle(HWND, LONG_PTR) noexcept override {}
        UINT Track(HMENU, UINT, POINT, HWND, const TPMPARAMS*) noexcept override { return 0; }
        void PostNull(HWND) noexcept override {}
    } focus;
    platform::ComApartment com; REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    RECT before{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &before, 0) != FALSE);
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false; context.menu = &menu;
    context.menuWindowOps = &focus;
    TopBarWindow window(context); REQUIRE(window.Create(RECT{80, 180, 1180, 228}).ok());
    settings::TopBarSettings config; config.reserveSpace = false;
    window.SetSettings(config, true); window.SetSnapshots(Synthetic());
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1});
    int reveal = 0, resume = 0, sound = 0, privacy = 0, settings = 0, clock = 0, power = 0, voice = 0;
    bool voiceHadTargetFocus = true;
    auto revealConnection = window.RevealWindowsTaskbarRequested.Connect([&] { ++reveal; });
    auto resumeConnection = window.ResumeTaskbarReplacementRequested.Connect([&] { ++resume; });
    auto soundConnection = window.SoundSettingsRequested.Connect([&] { ++sound; });
    auto privacyConnection = window.MicrophoneSettingsRequested.Connect([&] { ++privacy; });
    auto settingsConnection = window.SettingsRequested.Connect([&] { ++settings; });
    auto clockConnection = window.ClockRequested.Connect([&] { ++clock; });
    int calendar = 0, hiddenIcons = 0;
    auto calendarConnection = window.CalendarRequested.Connect([&] { ++calendar; });
    RECT hiddenAnchor{};
    auto hiddenIconsConnection = window.HiddenIconsRequested.Connect([&](RECT anchor) { ++hiddenIcons; hiddenAnchor = anchor; });
    auto powerConnection = window.PowerSettingsRequested.Connect([&] { ++power; });
    auto voiceConnection = window.VoiceTypingRequested.Connect([&] { ++voice; voiceHadTargetFocus &= focus.foreground == focus.target; });
    window.Show(false); RunFor(loop, 100);
    const auto click = [&](ItemKind kind, bool right = false) {
        const auto found = std::find_if(window.Items().begin(), window.Items().end(), [kind](const auto& p) { return p.item.kind == kind; });
        REQUIRE(found != window.Items().end());
        const auto x = static_cast<int>(std::lround(window.ToPx((found->rect.left + found->rect.right) * 0.5f)));
        const auto y = static_cast<int>(std::lround(window.ToPx((found->rect.top + found->rect.bottom) * 0.5f)));
        ::SendMessageW(window.Handle(), right ? WM_RBUTTONUP : WM_LBUTTONUP, 0, MAKELPARAM(x, y));
    };
    click(ItemKind::Clock); click(ItemKind::Battery);
    CHECK_EQ(clock, 1); CHECK_EQ(power, 1);
    menu.next = BarCommand::Calendar; click(ItemKind::Clock, true); CHECK_EQ(calendar, 1); CHECK_EQ(clock, 1);
    menu.next = BarCommand::SoundSettings; click(ItemKind::Volume, true); CHECK_EQ(sound, 1);
    menu.next = BarCommand::MicrophoneSettings; click(ItemKind::Microphone, true); CHECK_EQ(privacy, 1);
    menu.next = BarCommand::VoiceTyping; menu.previousForeground = focus.target;
    focus.foreground = window.Handle();
    click(ItemKind::Microphone, true); CHECK_EQ(voice, 1); CHECK(voiceHadTargetFocus);
    focus.foreground = window.Handle(); focus.accept = false;
    click(ItemKind::Microphone, true); CHECK_EQ(voice, 1); // failed focus restoration suppresses Win+H request
    focus.accept = true; focus.foreground = reinterpret_cast<HWND>(std::uintptr_t{0x30});
    click(ItemKind::Microphone, true); CHECK_EQ(voice, 1); // a third foreground window is never overridden
    menu.previousForeground = nullptr;
    click(ItemKind::Microphone, true); CHECK_EQ(voice, 1);
    REQUIRE(window.SetTaskbarReplacementState(true, true, false).ok());
    // D45: the hidden-icons chevron asks for Windows' overflow; it never reveals or resumes the taskbar.
    click(ItemKind::NotificationArea); CHECK_EQ(hiddenIcons, 1); CHECK_EQ(reveal, 0); CHECK_EQ(resume, 0);
    {
        // The request carries the chevron's own screen bounds: the overflow is placed below it.
        const auto chevron = std::find_if(window.Items().begin(), window.Items().end(),
            [](const auto& item) { return item.item.kind == ItemKind::NotificationArea; });
        REQUIRE(chevron != window.Items().end());
        const RECT bar = window.BoundsPx();
        CHECK_EQ(hiddenAnchor.left, bar.left + static_cast<LONG>(std::lround(window.ToPx(chevron->rect.left))));
        CHECK_EQ(hiddenAnchor.bottom, bar.top + static_cast<LONG>(std::lround(window.ToPx(chevron->rect.bottom))));
        CHECK(hiddenAnchor.right > hiddenAnchor.left);
        hiddenAnchor = RECT{};
    }
    menu.next = BarCommand::HiddenIcons; click(ItemKind::NotificationArea, true); CHECK_EQ(hiddenIcons, 2);
    CHECK(hiddenAnchor.right > hiddenAnchor.left);  // the menu command anchors the same way
    window.SetFullscreenHidden(true); click(ItemKind::NotificationArea); window.SetFullscreenHidden(false);
    CHECK_EQ(hiddenIcons, 2);  // a suppressed bar dispatches nothing
    menu.next = BarCommand::RevealWindowsTaskbar;
    click(ItemKind::Logo, true); CHECK_EQ(reveal, 1); // recovery remains in the Top Bar menu
    REQUIRE(window.SetTaskbarReplacementState(true, false, true).ok());
    menu.next = BarCommand::ResumeReplacement;
    click(ItemKind::Logo, true); CHECK_EQ(resume, 1);
    REQUIRE(window.SetTaskbarReplacementState(true, true, false).ok());
    menu.next = BarCommand::RevealWindowsTaskbar;
    menu.duringShow = [&] { REQUIRE(window.SetTaskbarReplacementState(false, false, false).ok()); };
    click(ItemKind::Logo, true); CHECK_EQ(reveal, 1);  // selected command became unavailable during the menu
    menu.duringShow = {}; menu.next = BarCommand::Settings;
    ::SendMessageW(window.Handle(), WM_KEYDOWN, VK_HOME, 0);
    ::SendMessageW(window.Handle(), WM_KEYDOWN, VK_APPS, 0);
    CHECK_EQ(settings, 1); CHECK(ui::ValidateMenu(menu.last).ok());
    // A provider/settings refresh must not resurrect a hovered tooltip after fullscreen suppression.
    const auto hover = window.Items().front().rect;
    ::SendMessageW(window.Handle(), WM_MOUSEMOVE, 0, MAKELPARAM(
        static_cast<int>(window.ToPx((hover.left + hover.right) * 0.5f)),
        static_cast<int>(window.ToPx((hover.top + hover.bottom) * 0.5f))));
    window.SetFullscreenHidden(true);
    auto changedSnapshot = Synthetic(); ++changedSnapshot.clock.minute;
    window.SetSnapshots(std::move(changedSnapshot));
    unsigned visibleTooltips = 0;
    ::EnumThreadWindows(::GetCurrentThreadId(), [](HWND hwnd, LPARAM contextValue) -> BOOL {
        wchar_t name[64]{};
        const int length = ::GetClassNameW(hwnd, name, 64);
        if (::IsWindowVisible(hwnd) && length > 0 && std::wstring_view(name, static_cast<std::size_t>(length)) == L"AetherShell.Tooltip")
            ++*reinterpret_cast<unsigned*>(contextValue);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&visibleTooltips));
    CHECK_EQ(visibleTooltips, 0u); // only this test thread's own windows are inspected; no capture
    window.SetFullscreenHidden(false); window.SetSnapshots(Synthetic());
    RunFor(loop, 100);
    const auto frames = window.RenderCount();
    REQUIRE(window.SetTaskbarReplacementState(false, false, false).ok());
    window.SetSnapshots(Synthetic()); RunFor(loop, 80);
    CHECK_EQ(window.RenderCount(), frames);  // repeated status/snapshots request no frames
    RECT after{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &after, 0) != FALSE);
    CHECK(::EqualRect(&before, &after) != FALSE);
    window.Destroy(); dispatcher.Shutdown(); scheduler.Shutdown(); device.Shutdown();
}

AETHER_TEST("topbar integration: AppBar notifications recheck current fullscreen policy without raw overrides") {
    platform::ComApartment com; REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    RECT before{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &before, 0) != FALSE);
    bool fullscreen = false;
    unsigned probes = 0, changes = 0;
    bool lastHidden = false;
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false;
    context.fullscreenProbe = [&] { ++probes; return fullscreen; }; // no foreground HWND is inspected
    TopBarWindow window(context); REQUIRE(window.Create(RECT{80, 180, 1180, 228}).ok());
    window.SetSettings(settings::TopBarSettings{}, false); window.SetSnapshots(Synthetic());
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1});
    auto connection = window.FullscreenChanged.Connect([&](bool hidden) { ++changes; lastHidden = hidden; });
    window.Show(false);
    constexpr UINT appBarMessage = WM_APP + 101;
    ::SendMessageW(window.Handle(), appBarMessage, ABN_FULLSCREENAPP, TRUE);
    CHECK_EQ(probes, 0u); // callback queues reevaluation rather than treating lParam as state
    RunFor(loop, 30);
    CHECK(window.IsShown()); CHECK(::IsWindowVisible(window.Handle()) != FALSE); CHECK_EQ(changes, 0u);
    fullscreen = true;
    ::SendMessageW(window.Handle(), appBarMessage, ABN_FULLSCREENAPP, FALSE);
    RunFor(loop, 30);
    CHECK_FALSE(window.IsShown()); CHECK(::IsWindowVisible(window.Handle()) == FALSE);
    CHECK_EQ(changes, 1u); CHECK(lastHidden); // stale close cannot reveal over another fullscreen
    ::SendMessageW(window.Handle(), appBarMessage, ABN_FULLSCREENAPP, TRUE);
    ::SendMessageW(window.Handle(), appBarMessage, ABN_FULLSCREENAPP, FALSE);
    RunFor(loop, 30); CHECK_EQ(changes, 1u);
    fullscreen = false;
    ::SendMessageW(window.Handle(), appBarMessage, ABN_FULLSCREENAPP, TRUE);
    RunFor(loop, 30);
    CHECK(window.IsShown()); CHECK(::IsWindowVisible(window.Handle()) != FALSE);
    CHECK_EQ(changes, 2u); CHECK_FALSE(lastHidden);
    CHECK(probes >= 5u);
    RECT after{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &after, 0) != FALSE);
    CHECK(::EqualRect(&before, &after) != FALSE);
    window.Destroy(); dispatcher.Shutdown(); scheduler.Shutdown(); device.Shutdown();
}

AETHER_TEST("topbar integration: real providers publish read-only items for three seconds") {
    platform::ComApartment com; REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    auto providers = status::CreateStatusProviders(); REQUIRE(providers.ok());
    status::audio::AudioProvider audio; status::audio::MicrophoneProvider microphone;
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false;
    context.clock = providers.value().clock.get(); context.battery = providers.value().battery.get();
    context.keyboard = providers.value().keyboard.get(); context.network = providers.value().network.get();
    context.audio = &audio; context.microphone = &microphone;
    TopBarWindow window(context); REQUIRE(window.Create(RECT{0, 0, 1000, 32}).ok());
    settings::TopBarSettings settings; settings.reserveSpace = false; window.SetSettings(settings, false);
    RunFor(loop, 3000);
    for (const auto& item : window.Items()) {
        if (!item.item.text.empty()) TEST_NOTE("item " + std::to_string(item.item.id) + ": " + text::Narrow(item.item.text));
    }
    CHECK_FALSE(window.Items().empty());
    window.Destroy(); providers.value().StopAll(); audio.Stop(); microphone.Stop();
    dispatcher.Shutdown(); scheduler.Shutdown(); device.Shutdown();
    CHECK(status::FinalizeStatusRuntime().ok());
}
