// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "FakeTouch.h"
#include "LiquidScenes.h"
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
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
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

AETHER_TEST("topbar integration: Liquid's glass keeps text readable and its edge seen over eight scenes, and its blur layer can be shaped (D66, D67)") {
    namespace lq = aether::test::liquid;
    platform::ComApartment com;
    REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    if (!device.Capabilities().hostBackdrop) { TEST_NOTE("no host backdrop here: Liquid falls back to Grain, nothing to measure"); return; }
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    lq::Photo() = lq::LoadPhoto();
    if (lq::Photo().width <= 0) TEST_NOTE("the photograph was not found near the build: that scene is a plain grey");
    const auto monitor = platform::PrimaryMonitor();
    const float scale = static_cast<float>(monitor.dpi) / 96.0f;
    const int width = static_cast<int>(std::min<LONG>(1100, monitor.workArea.right - monitor.workArea.left - 80));
    const int height = static_cast<int>(std::lround(32 * scale));
    const RECT captureBounds{monitor.workArea.left + 40, monitor.workArea.top + 140,
        monitor.workArea.left + 40 + width, monitor.workArea.top + 140 + height + 48};
    RECT privateBounds = captureBounds;
    ::InflateRect(&privateBounds, 128, 128);   // the compositor's blur reads around the bar: the scene there too
    lq::SceneBackdrop backdrop; REQUIRE(backdrop.Create(privateBounds));
    const RECT barBounds{captureBounds.left + 10, captureBounds.top + 20, captureBounds.right - 10, captureBounds.top + 20 + height};
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false;
    TopBarWindow window(context); REQUIRE(window.Create(barBounds).ok());
    settings::TopBarSettings settings; settings.reserveSpace = false; settings.clockFormat = settings::ClockFormat::Hours24;
    window.SetSettings(settings, true); window.SetSnapshots(Synthetic());
    const auto output = std::filesystem::temp_directory_path() / "AetherShellTests";
    std::filesystem::create_directories(output);
    const int panelTop = barBounds.top - captureBounds.top, panelBottom = panelTop + height;
    const int panelLeft = barBounds.left - captureBounds.left, panelRight = barBounds.right - captureBounds.left;
    const int inset = static_cast<int>(std::lround(5 * scale));
    // One look at the bar: shown over the scene, captured when no foreign window passes; nothing when one does.
    const auto look = [&](const graphics::DesignTokens& tokens, const std::string& file) -> std::optional<lq::Image> {
        window.SetTheme(tokens, graphics::MotionPolicy{true, 1});
        window.Show(false);
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RunFor(loop, 450);   // the compositor's backdrop catches up with the scene
        bool privateNow = false;
        for (int attempt = 0; attempt < 6 && !privateNow; ++attempt) {
            privateNow = PrivateCrop(backdrop.hwnd, privateBounds);
            if (!privateNow) RunFor(loop, 250);
        }
        if (!privateNow) { TEST_NOTE("inconclusive, a foreign window passed: " + file); return std::nullopt; }
        const auto captured = CaptureOwnBackdrop(backdrop.hwnd, privateBounds, captureBounds);
        Save(captured, output / file);
        return lq::Image{captured.width, captured.height, captured.bytes};
    };
    // The text's contrast with the glass beside it, and the edge against the scene outside.
    const auto measure = [&](const lq::Image& image, const graphics::DesignTokens& tokens, double& worst, double& edge) {
        const auto at = [&](int x, int y) { return image.bgra.data() + (static_cast<std::size_t>(y) * image.width + x) * 4; };
        const int windowLeft = window.BoundsPx().left - captureBounds.left;
        const auto glyphsAt = [&](int x, int y) {
            for (const auto& entry : window.Items()) {
                const bool tall = entry.item.kind == ItemKind::Keyboard || entry.item.kind == ItemKind::Logo;
                const bool within = x >= windowLeft + static_cast<int>(entry.rect.left * scale) - 4 && x <= windowLeft + static_cast<int>(entry.rect.right * scale) + 4;
                if (within && (tall || y == (panelTop + panelBottom) / 2)) return true;
            }
            return false;
        };
        const double text = lq::LuminanceOf(tokens.textPrimary.r, tokens.textPrimary.g, tokens.textPrimary.b);
        worst = 100.0;
        for (const int y : {(panelTop + panelBottom) / 2, panelTop + inset, panelBottom - 1 - inset})
            for (int x = panelLeft + static_cast<int>(24 * scale); x < panelRight - static_cast<int>(24 * scale); x += 2)
                if (!glyphsAt(x, y)) worst = std::min(worst, lq::Contrast(lq::Luminance(at(x, y)), text));
        edge = 0.0;
        int samples = 0;
        for (int x = panelLeft + static_cast<int>(40 * scale); x < panelRight - static_cast<int>(40 * scale); x += 4, ++samples)
            edge += std::max(std::abs(lq::Luminance(at(x, panelTop + 1)) - lq::Luminance(at(x, panelTop - 3))),
                             std::abs(lq::Luminance(at(x, panelBottom - 2)) - lq::Luminance(at(x, panelBottom + 2))));
        edge /= std::max(samples, 1);
    };
    std::vector<std::vector<lq::Image>> rows;
    for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::Light}) {
        const std::string modeName = mode == graphics::ThemeMode::Dark ? "dark" : "light";
        for (int scene = 0; scene < static_cast<int>(lq::kSceneNames.size()); ++scene) {
            backdrop.Show(scene);
            std::vector<lq::Image> row;
            for (const bool liquid : {true, false}) {
                auto tokens = graphics::MakeDesignTokens(mode); tokens.transparencyEnabled = true; tokens.liquid = liquid;
                const auto image = look(tokens, std::string("topbar-") + (liquid ? "liquid-" : "grain-") + modeName + "-" + lq::kSceneNames[static_cast<std::size_t>(scene)] + ".png");
                if (!image) continue;
                row.push_back(*image);
                if (!liquid) continue;
                CHECK(window.BackdropGlassActive());
                double worst = 0.0, edge = 0.0;
                measure(*image, tokens, worst, edge);
                char line[160];
                std::snprintf(line, sizeof(line), "%s %s: text contrast at least %.2f:1, edge %.3f", modeName.c_str(), lq::kSceneNames[static_cast<std::size_t>(scene)], worst, edge);
                TEST_NOTE(line);
                CHECK(worst >= 4.5);                 // WCAG 2.2 AA for the bar's text, on every scene
                if (scene <= 1) CHECK(edge >= 0.04);  // over plain white and plain black the bar is still seen
            }
            if (!row.empty()) rows.push_back(std::move(row));
        }
    }
    if (!rows.empty()) lq::SavePng(lq::Sheet(rows), output / L"topbar-liquid-sheet.png");
    // D67: the blur layer the person shapes. A colour keeps the text's band (it is mixed in before it); a lower cover
    // lets the sharp page through, which is the person's choice, so it is only shown and measured.
    struct Variant { const char* name; float opacity; graphics::Color tint; float strength; };
    const Variant variants[] = {{"blur-60", 0.6f, {}, 0.0f}, {"white-40", 1.0f, graphics::Color::FromHex(0xFFFFFF), 0.4f},
                                {"accent-40", 1.0f, graphics::Color::FromHex(0x5B6CFF), 0.4f}, {"teal-60", 1.0f, graphics::Color::FromHex(0x14B8A6), 0.6f}};
    std::vector<std::vector<lq::Image>> shaped;
    for (const int scene : {5, 7}) {
        backdrop.Show(scene);
        for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::Light}) {
            std::vector<lq::Image> row;
            for (const auto& variant : variants) {
                auto tokens = graphics::MakeDesignTokens(mode); tokens.transparencyEnabled = true; tokens.liquid = true;
                tokens.liquidGlass.blurOpacity = variant.opacity;
                tokens.liquidGlass.tintR = variant.tint.r; tokens.liquidGlass.tintG = variant.tint.g; tokens.liquidGlass.tintB = variant.tint.b;
                tokens.liquidGlass.tintStrength = variant.strength;
                const std::string name = std::string("topbar-shaped-") + variant.name + "-" + (mode == graphics::ThemeMode::Dark ? "dark-" : "light-") + lq::kSceneNames[static_cast<std::size_t>(scene)];
                const auto image = look(tokens, name + ".png");
                if (!image) continue;
                row.push_back(*image);
                double worst = 0.0, edge = 0.0;
                measure(*image, tokens, worst, edge);
                char line[160];
                std::snprintf(line, sizeof(line), "%s: text contrast at least %.2f:1", name.c_str(), worst);
                TEST_NOTE(line);
                if (variant.opacity >= 1.0f) CHECK(worst >= 4.5);   // whatever the colour, the text still reads
            }
            if (!row.empty()) shaped.push_back(std::move(row));
        }
    }
    if (!shaped.empty()) lq::SavePng(lq::Sheet(shaped), output / L"topbar-liquid-shaped-sheet.png");
    window.Destroy(); dispatcher.Shutdown(); scheduler.Shutdown();
}

AETHER_TEST("topbar integration: written on the desktop the bar is its items alone, read over eight scenes, and sits on glass over a window (D71)") {
    namespace lq = aether::test::liquid;
    platform::ComApartment com;
    REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    lq::Photo() = lq::LoadPhoto();
    if (lq::Photo().width <= 0) TEST_NOTE("the photograph was not found near the build: that scene is a plain grey");
    const auto monitor = platform::PrimaryMonitor();
    const float scale = static_cast<float>(monitor.dpi) / 96.0f;
    const int width = static_cast<int>(std::min<LONG>(1100, monitor.workArea.right - monitor.workArea.left - 80));
    const int height = static_cast<int>(std::lround(32 * scale));
    const RECT captureBounds{monitor.workArea.left + 40, monitor.workArea.top + 140,
        monitor.workArea.left + 40 + width, monitor.workArea.top + 140 + height + 48};
    RECT privateBounds = captureBounds;
    ::InflateRect(&privateBounds, 128, 128);
    lq::SceneBackdrop backdrop; REQUIRE(backdrop.Create(privateBounds));
    const RECT barBounds{captureBounds.left + 10, captureBounds.top + 20, captureBounds.right - 10, captureBounds.top + 20 + height};
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false;
    TopBarWindow window(context); REQUIRE(window.Create(barBounds).ok());
    settings::TopBarSettings settings; settings.reserveSpace = false; settings.clockFormat = settings::ClockFormat::Hours24;
    settings.presentation = settings::TopBarPresentation::Text;
    auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark); tokens.transparencyEnabled = true;
    window.SetTheme(tokens, graphics::MotionPolicy{true, 1});
    window.SetSettings(settings, true); window.SetSnapshots(Synthetic());
    REQUIRE(window.TextPresentation());
    // Flush: the window is the strip itself, with nothing kept at its ends, above it or around it.
    const RECT flush = window.BoundsPx();
    CHECK(::EqualRect(&flush, &barBounds) != FALSE);
    const auto output = std::filesystem::temp_directory_path() / "AetherShellTests";
    std::filesystem::create_directories(output);
    const int barTop = barBounds.top - captureBounds.top, barLeft = barBounds.left - captureBounds.left;
    const int screenWidth = monitor.bounds.right - monitor.bounds.left;
    // One look at the scene, with or without the bar over it; nothing when a foreign window passes.
    const auto look = [&](bool withBar) -> std::optional<lq::Image> {
        if (withBar) window.Show(false); else window.Hide();
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (withBar) ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RunFor(loop, 260);
        bool privateNow = false;
        for (int attempt = 0; attempt < 6 && !privateNow; ++attempt) {
            privateNow = PrivateCrop(backdrop.hwnd, privateBounds);
            if (!privateNow) RunFor(loop, 250);
        }
        if (!privateNow) { TEST_NOTE("inconclusive, a foreign window passed"); return std::nullopt; }
        const auto captured = CaptureOwnBackdrop(backdrop.hwnd, privateBounds, captureBounds);
        return lq::Image{captured.width, captured.height, captured.bytes};
    };
    const auto at = [](const lq::Image& image, int x, int y) { return image.bgra.data() + (static_cast<std::size_t>(y) * image.width + x) * 4; };
    const auto grey = [&](const lq::Image& image, int x, int y) { const auto* p = at(image, x, y); return (p[0] + p[1] + p[2]) / 3.0; };
    // What the host measures: the strip under the bar, in columns across the whole screen.
    const auto profileOf = [&](const lq::Image& scene) {
        std::vector<std::uint8_t> strip(static_cast<std::size_t>(screenWidth) * height * 4);
        for (int y = 0; y < height; ++y) for (int x = 0; x < screenWidth; ++x) {
            const int sx = std::clamp(x + static_cast<int>(monitor.bounds.left - captureBounds.left), 0, scene.width - 1);
            std::copy_n(at(scene, sx, barTop + y), 4, strip.data() + (static_cast<std::size_t>(y) * screenWidth + x) * 4);
        }
        return MeasureBackdrop(strip, screenWidth, height, RECT{0, 0, screenWidth, height}, 64);
    };
    // The two sides of the bar, in capture pixels, and the ink each takes over `profile`, as the bar chooses it.
    struct Side { int left = 0, right = 0; TextInk ink; };
    const auto sidesOf = [&](const BackdropProfile& profile) {
        std::array<Side, 2> sides{};
        std::array<bool, 2> any{};
        for (const auto& entry : window.Items()) {
            const std::size_t side = entry.item.kind == ItemKind::Logo ||
                (entry.item.kind == ItemKind::Weather && settings.weatherPlacement == settings::WeatherPlacement::Left) ? 0u : 1u;
            const int left = barLeft + static_cast<int>(std::floor(entry.rect.left * scale)), right = barLeft + static_cast<int>(std::ceil(entry.rect.right * scale));
            sides[side].left = any[side] ? std::min(sides[side].left, left) : left;
            sides[side].right = any[side] ? std::max(sides[side].right, right) : right;
            any[side] = true;
        }
        for (auto& side : sides) {
            const auto across = [&](int capturePx) { return static_cast<float>(capturePx + captureBounds.left - monitor.bounds.left) / static_cast<float>(screenWidth); };
            side.ink = ChooseTextInk(&profile, across(side.left), across(side.right), false);
        }
        return sides;
    };
    // The same bar over plain white and plain black gives what it draws, pixel by pixel: how much of the ink covers a
    // pixel. A stroke's own pixels are then compared with the pixels right around it, as they appear over the scene.
    struct Reading { double worst = 0.0, low = 0.0, median = 0.0; std::size_t strokes = 0; };
    const auto read = [&](const lq::Image& overWhite, const lq::Image& overBlack, const lq::Image& overScene, const Side& side) {
        const auto cover = [&](int x, int y) {
            const double white = grey(overWhite, x, y), black = grey(overBlack, x, y);
            const double drawn = 1.0 - (white - black) / 255.0;   // ink and halo together
            return side.ink.dark ? drawn - black / 255.0 : black / 255.0;
        };
        std::vector<double> contrasts;
        for (int y = barTop + 2; y < barTop + height - 2; ++y) for (int x = std::max(2, side.left); x < std::min(overScene.width - 2, side.right); ++x) {
            if (cover(x, y) < 0.7) continue;
            double around = 0.0; int count = 0;
            for (int dy = -2; dy <= 2; ++dy) for (int dx = -2; dx <= 2; ++dx)
                if (cover(x + dx, y + dy) <= 0.12) { around += lq::Luminance(at(overScene, x + dx, y + dy)); ++count; }
            if (count) contrasts.push_back(lq::Contrast(lq::Luminance(at(overScene, x, y)), around / count));
        }
        Reading reading;
        reading.strokes = contrasts.size();
        if (contrasts.empty()) return reading;
        std::sort(contrasts.begin(), contrasts.end());
        reading.worst = contrasts.front();
        reading.low = contrasts[contrasts.size() / 20];
        reading.median = contrasts[contrasts.size() / 2];
        return reading;
    };
    std::vector<std::vector<lq::Image>> rows;
    for (int scene = 0; scene < static_cast<int>(lq::kSceneNames.size()); ++scene) {
        const std::string name = lq::kSceneNames[static_cast<std::size_t>(scene)];
        backdrop.Show(scene);
        const auto bare = look(false);
        if (!bare) continue;
        const auto profile = profileOf(*bare);
        REQUIRE_FALSE(profile.columns.empty());
        window.SetBackdropProfile(profile);
        const auto over = look(true);
        backdrop.Show(0); const auto overWhite = look(true);
        backdrop.Show(1); const auto overBlack = look(true);
        if (!over || !overWhite || !overBlack) continue;
        lq::SavePng(*over, output / ("topbar-written-" + name + ".png"));
        rows.push_back({*bare, *over});
        const auto sides = sidesOf(profile);
        // No bar: between its two sides the scene is exactly what it is without the bar.
        int touched = 0;
        for (int y = barTop; y < barTop + height; ++y) for (int x = sides[0].right + 24; x < sides[1].left - 24; ++x)
            for (int channel = 0; channel < 3; ++channel) touched = std::max(touched, std::abs(static_cast<int>(at(*over, x, y)[channel]) - static_cast<int>(at(*bare, x, y)[channel])));
        CHECK(sides[1].left - sides[0].right > 100);
        CHECK(touched <= 2);
        for (std::size_t index = 0; index < sides.size(); ++index) {
            const auto reading = read(*overWhite, *overBlack, *over, sides[index]);
            char line[200];
            std::snprintf(line, sizeof(line), "%s, %s side: %s ink, halo %.2f; strokes against what surrounds them: worst %.2f:1, low %.2f:1, median %.2f:1 (%zu pixels)",
                          name.c_str(), index == 0 ? "left" : "right", sides[index].ink.dark ? "dark" : "light", sides[index].ink.halo,
                          reading.worst, reading.low, reading.median, reading.strokes);
            TEST_NOTE(line);
            CHECK(reading.strokes > 40);
            // What a desktop shows: plain colours, a split, every hue, a photograph. A page of an application
            // (scenes 4 to 6) is not what the written bar lies over: there it sits on glass, checked below.
            if (scene >= 4 && scene <= 6) continue;
            CHECK(reading.low >= 4.5);      // nineteen stroke pixels in twenty stand at 4.5:1 (WCAG) or more from their surroundings
            CHECK(reading.median >= 7.0);
        }
    }
    if (!rows.empty()) lq::SavePng(lq::Sheet(rows), output / L"topbar-written-sheet.png");
    // Each side takes the ink of its own part of the picture. Over the hues the left side lies on yellow and takes
    // dark ink, while the right side, which crosses blue, takes light ink. Over the split, the left side lies on
    // black with a light halo, and the right side crosses black and white at once: light ink under a strong halo.
    {
        backdrop.Show(3);
        if (const auto bare = look(false)) {
            const auto sides = sidesOf(profileOf(*bare));
            CHECK(sides[0].ink.dark);
            CHECK_FALSE(sides[1].ink.dark);
        }
        backdrop.Show(2);
        if (const auto bare = look(false)) {
            const auto sides = sidesOf(profileOf(*bare));
            CHECK_FALSE(sides[0].ink.dark);
            CHECK(sides[0].ink.halo <= 0.35f);
            CHECK_FALSE(sides[1].ink.dark);
            CHECK(sides[1].ink.halo >= 0.8f);
        }
    }
    // Over an application window (Auto-Hide brought the bar back there) the items sit on the style's glass, square,
    // from edge to edge; back on the desktop the glass is gone.
    std::vector<std::vector<lq::Image>> plates;
    for (const int scene : {4, 5}) {
        backdrop.Show(scene);
        const auto bare = look(false);
        if (!bare) continue;
        window.SetBackdropProfile(profileOf(*bare));
        const auto sides = sidesOf(profileOf(*bare));
        const auto material = [&](const lq::Image& image) {
            long difference = 0, samples = 0;
            for (int x = sides[0].right + 24; x < sides[1].left - 24; x += 2, ++samples) for (int channel = 0; channel < 3; ++channel)
                difference += std::abs(static_cast<int>(at(image, x, barTop + height / 2)[channel]) - static_cast<int>(at(*bare, x, barTop + height / 2)[channel]));
            return samples ? static_cast<double>(difference) / static_cast<double>(samples) : 0.0;
        };
        std::vector<lq::Image> row{*bare};
        for (const bool liquid : {false, true}) {
            auto styled = graphics::MakeDesignTokens(graphics::ThemeMode::Dark); styled.transparencyEnabled = true; styled.liquid = liquid;
            window.SetTheme(styled, graphics::MotionPolicy{true, 1});
            window.SetOverWindows(true);
            RunFor(loop, 300);   // the compositor's backdrop catches up
            const auto plated = look(true);
            if (!plated) continue;
            row.push_back(*plated);
            lq::SavePng(*plated, output / (std::string("topbar-written-plate-") + (liquid ? "liquid-" : "grain-") + lq::kSceneNames[static_cast<std::size_t>(scene)] + ".png"));
            char line[160];
            std::snprintf(line, sizeof(line), "%s, %s glass under the items: the strip differs from the page by %.1f per pixel", lq::kSceneNames[static_cast<std::size_t>(scene)], liquid ? "Liquid" : "Grain", material(*plated));
            TEST_NOTE(line);
            CHECK(material(*plated) > 10.0);
            // The glass reaches the window's edges and corners: no rounded corner left open.
            int corner = 0;
            for (int channel = 0; channel < 3; ++channel) corner += std::abs(static_cast<int>(at(*plated, barLeft + 1, barTop + 1)[channel]) - static_cast<int>(at(*bare, barLeft + 1, barTop + 1)[channel]));
            CHECK(corner > 6);
            window.SetOverWindows(false);
            const auto back = look(true);
            if (back) CHECK(material(*back) <= 2.0);
        }
        plates.push_back(std::move(row));
    }
    if (!plates.empty()) lq::SavePng(lq::Sheet(plates), output / L"topbar-written-plate-sheet.png");
    // At rest nothing is drawn again.
    window.SetTheme(tokens, graphics::MotionPolicy{true, 1});
    window.Show(false);
    int quietIntervals = 0;
    for (int attempt = 0; attempt < 12 && quietIntervals < 2; ++attempt) {
        const auto before = window.RenderCount(); RunFor(loop, 120);
        quietIntervals = window.RenderCount() == before ? quietIntervals + 1 : 0;
    }
    CHECK_EQ(quietIntervals, 2);
    // High contrast keeps the bar: its text stays on the system's own colours.
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast), graphics::MotionPolicy{true, 1});
    CHECK_FALSE(window.TextPresentation());
    const RECT bar = window.BoundsPx();
    CHECK(::EqualRect(&bar, &barBounds) == FALSE);
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
        ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
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
        ::SendMessageW(window.Handle(), right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(x, y));
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

AETHER_TEST("topbar integration: auto-hide slides the bar away and back without overriding full-screen suppression") {
    // D47: the host decides when; the bar executes, reports use and never touches the work area here.
    platform::ComApartment com; REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    RECT before{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &before, 0) != FALSE);
    bool fullscreen = false;
    unsigned changes = 0;
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false;
    context.fullscreenProbe = [&] { return fullscreen; };
    TopBarWindow window(context); REQUIRE(window.Create(RECT{80, 180, 1180, 228}).ok());
    window.SetSettings(settings::TopBarSettings{}, false); window.SetSnapshots(Synthetic());
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1});
    auto connection = window.FullscreenChanged.Connect([&](bool) { ++changes; });
    window.Show(false); RunFor(loop, 30);
    // In use while the pointer is over it; not after it left.
    CHECK_FALSE(window.Engaged());
    ::SendMessageW(window.Handle(), WM_MOUSEMOVE, 0, MAKELPARAM(40, 20));
    CHECK(window.Engaged());
    ::SendMessageW(window.Handle(), WM_MOUSELEAVE, 0, 0);
    CHECK_FALSE(window.Engaged());
    // Reduced motion: away and back at once.
    window.SetAutoHidden(true); RunFor(loop, 30);
    CHECK(window.AutoHidden()); CHECK_FALSE(window.IsShown()); CHECK(::IsWindowVisible(window.Handle()) == FALSE);
    window.SetAutoHidden(false); RunFor(loop, 30);
    CHECK(window.IsShown()); CHECK(::IsWindowVisible(window.Handle()) != FALSE);
    CHECK_EQ(changes, 0u);  // auto-hide is not full-screen suppression
    // Full-screen suppression wins both ways.
    window.SetAutoHidden(true);
    fullscreen = true; window.SetFullscreenHidden(true);
    window.SetAutoHidden(false); RunFor(loop, 30);
    CHECK_FALSE(window.IsShown());  // still suppressed by the full-screen application
    window.SetAutoHidden(true);
    fullscreen = false; window.SetFullscreenHidden(false); RunFor(loop, 30);
    CHECK_FALSE(window.IsShown());  // the full-screen application left, but the bar is auto-hidden
    window.SetAutoHidden(false); RunFor(loop, 30);
    CHECK(window.IsShown());
    // With motion, the bar slides away: still shown while it leaves, gone afterwards, and back.
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{});
    window.SetAutoHidden(true);
    CHECK(window.IsShown()); CHECK(window.IsHiding());
    RunFor(loop, 700);
    CHECK_FALSE(window.IsShown());
    window.SetAutoHidden(false);
    RunFor(loop, 700);
    CHECK(window.IsShown()); CHECK_FALSE(window.IsTransitioning());
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

AETHER_TEST("topbar integration: a release acts only on the pressed item and touch acts only on a tap or hold (AS4-11, FR-040)") {
    class FakeMenu final : public ui::IMenuPresenter {
    public:
        int shown = 0;
        Result<ui::MenuSelection> Show(std::span<const ui::MenuEntry>, const ui::MenuRequest&) override {
            ++shown;
            return MakeError(ErrorCode::Cancelled, "synthetic menu dismissed");
        }
    } menu;
    platform::ComApartment com; REQUIRE(SUCCEEDED(com.Status()));
    platform::MessageLoop loop;
    graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    platform::Win32UiDispatcher dispatcher; REQUIRE(dispatcher.Initialize().ok());
    TopBarContext context{device, scheduler, dispatcher}; context.allowAppBar = false; context.menu = &menu;
    TopBarWindow window(context); REQUIRE(window.Create(RECT{80, 180, 1180, 228}).ok());
    settings::TopBarSettings config; config.reserveSpace = false;
    window.SetSettings(config, true); window.SetSnapshots(Synthetic());
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1});
    int keyboard = 0, network = 0;
    auto keyConnection = window.KeyboardRequested.Connect([&] { ++keyboard; });
    auto netConnection = window.NetworkRequested.Connect([&] { ++network; });
    window.Show(false); RunFor(loop, 100);
    const auto center = [&](ItemKind kind) {
        const auto found = std::find_if(window.Items().begin(), window.Items().end(), [kind](const auto& item) { return item.item.kind == kind; });
        REQUIRE(found != window.Items().end());
        return POINT{static_cast<LONG>(std::lround(window.ToPx((found->rect.left + found->rect.right) / 2))),
                     static_cast<LONG>(std::lround(window.ToPx((found->rect.top + found->rect.bottom) / 2)))};
    };
    const POINT a = center(ItemKind::Keyboard), b = center(ItemKind::Network);
    const auto mouse = [&](UINT message, POINT at) { ::SendMessageW(window.Handle(), message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0, MAKELPARAM(at.x, at.y)); };
    // Mouse: pressed A, released on B acts on neither; a release with no press acts on nothing.
    mouse(WM_LBUTTONDOWN, a); mouse(WM_LBUTTONUP, b);
    CHECK_EQ(keyboard, 0); CHECK_EQ(network, 0);
    mouse(WM_LBUTTONUP, b);
    CHECK_EQ(network, 0);
    // Pressed blank space, released on an item.
    mouse(WM_LBUTTONDOWN, POINT{0, 0}); mouse(WM_LBUTTONUP, b);
    CHECK_EQ(network, 0);
    // A press canceled by capture loss, then released on the same item.
    mouse(WM_LBUTTONDOWN, b);
    ::ReleaseCapture();
    mouse(WM_LBUTTONUP, b);
    CHECK_EQ(network, 0);
    // A right press on one item and release on another shows no menu.
    mouse(WM_RBUTTONDOWN, a); mouse(WM_RBUTTONUP, b);
    CHECK_EQ(menu.shown, 0);
    // One ordinary click acts once.
    mouse(WM_LBUTTONDOWN, b); mouse(WM_LBUTTONUP, b);
    CHECK_EQ(network, 1);

    test::FakeTouch touch;
    const auto screen = [&](POINT client) { REQUIRE(::ClientToScreen(window.Handle(), &client) != FALSE); return client; };
    const HWND hwnd = window.Handle();
    // A tap, and it never also arrives as promoted mouse input (the contact is consumed).
    CHECK_EQ(touch.Send(hwnd, WM_POINTERDOWN, 1, screen(b)), 0);
    CHECK(window.Engaged());
    touch.Send(hwnd, WM_POINTERUP, 1, screen(b));
    CHECK_EQ(network, 2);
    // A pan across items, a second finger and a canceled contact do nothing.
    touch.Send(hwnd, WM_POINTERDOWN, 1, screen(b));
    touch.Send(hwnd, WM_POINTERUPDATE, 1, screen(POINT{b.x + 60, b.y}));
    touch.Send(hwnd, WM_POINTERUP, 1, screen(b));
    touch.Send(hwnd, WM_POINTERDOWN, 1, screen(b));
    touch.Send(hwnd, WM_POINTERDOWN, 2, screen(a));
    touch.Send(hwnd, WM_POINTERUP, 2, screen(a));
    touch.Send(hwnd, WM_POINTERUP, 1, screen(b));
    touch.Send(hwnd, WM_POINTERDOWN, 1, screen(b));
    touch.Send(hwnd, WM_POINTERUP, 1, screen(b), /*canceled*/ true);
    touch.Send(hwnd, WM_POINTERDOWN, 1, screen(b));
    ::SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
    touch.Send(hwnd, WM_POINTERUP, 1, screen(b));
    CHECK_EQ(network, 2); CHECK_EQ(keyboard, 0); CHECK_EQ(menu.shown, 0);
    CHECK_FALSE(window.Engaged());
    // A hold opens that item's menu and its release activates nothing.
    touch.Send(hwnd, WM_POINTERDOWN, 1, screen(b));
    RunFor(loop, 650);
    CHECK_EQ(menu.shown, 1);
    touch.Send(hwnd, WM_POINTERUP, 1, screen(b));
    CHECK_EQ(network, 2);
    window.Destroy(); dispatcher.Shutdown(); scheduler.Shutdown();
}
