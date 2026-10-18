// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "topbar/TopBarWindow.h"
#include "core/Strings.h"

#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Win32UiDispatcher.h"
#include "status/StatusProviders.h"
#include "status/WinRtSupport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

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
Capture CaptureOwnBackdrop(HWND backdrop, const RECT& bounds) {
    REQUIRE(PrivateCrop(backdrop, bounds));
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
    REQUIRE(copied); REQUIRE(PrivateCrop(backdrop, bounds)); return capture;
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
    const RECT captureBounds{monitor.workArea.left + 40, monitor.workArea.top + 100,
        monitor.workArea.left + 40 + width, monitor.workArea.top + 100 + height + 48};
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
        window.SetTheme(graphics::MakeDesignTokens(mode), graphics::MotionPolicy{true, 1});
        window.Show(false); RunFor(loop, 350);
        // Reassert that the private backdrop covers every captured pixel before capture.
        REQUIRE(::IsWindowVisible(backdrop.hwnd));
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RunFor(loop, 100);
        const auto captured = CaptureOwnBackdrop(backdrop.hwnd, captureBounds);
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
