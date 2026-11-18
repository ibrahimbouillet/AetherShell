// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/WindowCorners.h"
#include "graphics/CompositionSurface.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/Handles.h"
#include "platform/windows/WindowUtilities.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <string>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

#pragma comment(lib, "uxtheme.lib")

using namespace aether;
using namespace aether::app;
namespace {

// D72: the guard writes nothing to the windows it rounds; it shows the desktop in their corners with windows of its
// own. Every test gives it a private window to stand for the desktop, so the real one is never shown or captured.
// D56: rounding belongs to a maximized window between the bars; one that fills the monitor, as every maximize
// does on a desktop with both bars auto-hidden, is left alone. A fixture takes the place it would have between
// bars, on this one window only: the real work area is never touched.
void BetweenBars(HWND window) {
    MONITORINFO monitor{sizeof(monitor)};
    RECT bounds{}, frame{};
    if (!::GetMonitorInfoW(::MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) || !::GetWindowRect(window, &bounds) ||
        FAILED(::DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)))) return;
    if (frame.left > monitor.rcMonitor.left || frame.top > monitor.rcMonitor.top ||
        frame.right < monitor.rcMonitor.right || frame.bottom < monitor.rcMonitor.bottom) return;
    (void)::SetWindowPos(window, nullptr, bounds.left + 18, bounds.top + 60, bounds.right - bounds.left - 36, bounds.bottom - bounds.top - 120, SWP_NOZORDER | SWP_NOACTIVATE);
}

bool PumpUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = std::chrono::seconds(5)) {
    const auto end = std::chrono::steady_clock::now() + timeout;
    do {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message); ::DispatchMessageW(&message);
        }
        if (predicate()) return true;
        ::Sleep(5);
    } while (std::chrono::steady_clock::now() < end);
    return predicate();
}
LRESULT CALLBACK CustomFrameProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    // Chromium-style client rendering can retain standard caption/thick-frame styles while
    // painting its own nonclient area. A region-membership assertion alone misses DWM residue.
    if (message == WM_NCCALCSIZE && wParam) return 0;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(window, &paint);
        RECT bounds{}; ::GetClientRect(window, &bounds);
        HBRUSH brush = ::CreateSolidBrush(RGB(28, 36, 44));
        if (brush) { ::FillRect(dc, &bounds, brush); ::DeleteObject(brush); }
        ::EndPaint(window, &paint); return 0;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}
struct OwnWindow {
    HWND window = nullptr;
    bool mainWindowBackdrop = false;
    explicit OwnWindow(bool customFrame = false, bool micaBackdrop = false, bool layered = false) {
        const auto foreground = ::GetForegroundWindow();
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = customFrame ? L"AetherShell.Test.WindowCornerCustomFrame" : L"AetherShell.Test.WindowCornerGuard";
        type.lpfnWndProc = customFrame ? CustomFrameProc : ::DefWindowProcW;
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        REQUIRE(platform::RegisterWindowClassOnce(type).ok());
        // Own synthetic window only. NOACTIVATE keeps the owner's foreground unchanged, and is
        // permitted solely by the explicit own-process fixture restriction in this guard.
        // `layered`: like Chromium/Electron windows with transparency (D45 eligibility).
        const DWORD extended = WS_EX_NOACTIVATE | (micaBackdrop ? WS_EX_NOREDIRECTIONBITMAP : 0) | (layered ? WS_EX_LAYERED : 0);
        window = ::CreateWindowExW(extended, type.lpszClassName, L"Aether corner guard fixture",
            WS_OVERLAPPEDWINDOW, 160, 120, 640, 480, nullptr, nullptr, type.hInstance, nullptr);
        REQUIRE(window != nullptr);
        if (micaBackdrop) {
            const DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
            DWM_SYSTEMBACKDROP_TYPE observed = DWMSBT_AUTO;
            mainWindowBackdrop = SUCCEEDED(::DwmSetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop))) &&
                SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_SYSTEMBACKDROP_TYPE, &observed, sizeof(observed))) &&
                observed == backdrop;
        }
        // Exercise the real maximize operation rather than a style bit. NOACTIVATE is retained
        // throughout; the guard must observe real WINDOWPLACEMENT/IsZoomed state without moving
        // or activating any existing owner application.
        ::ShowWindow(window, SW_SHOWMAXIMIZED);
        BetweenBars(window);
        (void)::UpdateWindow(window);
        REQUIRE(::IsZoomed(window));
        REQUIRE(::GetForegroundWindow() == foreground);
        WINDOWPLACEMENT placement{sizeof(placement)};
        REQUIRE(::GetWindowPlacement(window, &placement));
        REQUIRE(placement.showCmd == SW_SHOWMAXIMIZED);
    }
    ~OwnWindow() { if (window) ::DestroyWindow(window); }
};
struct OwnGuard {
    platform::UniqueHandle parent;
    platform::UniqueHandle ready{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    platform::UniqueHandle stop{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::atomic<bool> finished{false};
    std::atomic<int> exitCode{-1};
    std::thread thread;
    // `desktop`: the private window that stands for the desktop. A test never shows the real one.
    OwnGuard(std::vector<HWND> windows, HANDLE parentProcess, WindowCornerGuardStatistics* statistics, HWND desktop) {
        if (parentProcess) {
            HANDLE copied = nullptr;
            REQUIRE(::DuplicateHandle(::GetCurrentProcess(), parentProcess, ::GetCurrentProcess(), &copied,
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0));
            parent.Reset(copied);
        } else parent.Reset(::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ::GetCurrentProcessId()));
        REQUIRE(parent && ready && stop);
        const WindowCornerGuardOptions restriction{::GetCurrentProcessId(), std::move(windows), statistics, desktop};
        thread = std::thread([this, restriction] {
            const auto result = RunWindowCornerGuard(parent.Get(), ready.Get(), stop.Get(), restriction);
            exitCode = result ? *result : 1; finished = true;
        });
    }
    bool Ready() { return PumpUntil([this] { return finished.load() || ::WaitForSingleObject(ready.Get(), 0) == WAIT_OBJECT_0; }) && !finished.load(); }
    bool Finish() {
        (void)::SetEvent(stop.Get());
        const bool exited = PumpUntil([this] { return finished.load(); });
        if (exited && thread.joinable()) thread.join();
        return exited;
    }
    ~OwnGuard() {
        (void)Finish();
        // Every fixture target is owned by this pumping thread; never leave a background guard
        // referring to fixture storage after an assertion unwinds.
        while (thread.joinable()) {
            if (PumpUntil([this] { return finished.load(); })) { thread.join(); break; }
        }
    }
};
struct LifetimeParent {
    platform::UniqueHandle process;
    platform::UniqueHandle exit;
    LifetimeParent() {
        std::array<wchar_t, 32768> module{};
        const auto length = ::GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
        REQUIRE(length && length < module.size());
        const auto fixture = std::filesystem::path(std::wstring_view(module.data(), length)).parent_path() /
            L"aether_window_corner_parent_fixture.exe";
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        platform::UniqueHandle ready(::CreateEventW(&security, TRUE, FALSE, nullptr));
        exit.Reset(::CreateEventW(&security, TRUE, FALSE, nullptr));
        REQUIRE(ready && exit);
        SIZE_T size = 0; (void)::InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<std::byte> memory(size);
        auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(memory.data());
        REQUIRE(::InitializeProcThreadAttributeList(attributes, 1, 0, &size));
        struct Cleanup { LPPROC_THREAD_ATTRIBUTE_LIST value; ~Cleanup() { ::DeleteProcThreadAttributeList(value); } } cleanup{attributes};
        HANDLE handles[]{ready.Get(), exit.Get()};
        REQUIRE(::UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
            handles, sizeof(handles), nullptr, nullptr));
        auto command = L"\"" + fixture.native() + L"\" " +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(ready.Get())) + L" " +
            std::to_wstring(reinterpret_cast<std::uintptr_t>(exit.Get()));
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
        PROCESS_INFORMATION child{};
        REQUIRE(::CreateProcessW(fixture.c_str(), command.data(), nullptr, nullptr, TRUE,
            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, fixture.parent_path().c_str(), &startup.StartupInfo, &child));
        platform::UniqueHandle thread(child.hThread); process.Reset(child.hProcess);
        const bool becameReady = ::WaitForSingleObject(ready.Get(), 5000) == WAIT_OBJECT_0;
        // A constructor assertion bypasses this class's destructor, so signal the owned child
        // before reporting startup failure. It must not retain its inherited wait indefinitely.
        if (!becameReady) (void)::SetEvent(exit.Get());
        REQUIRE(becameReady);
    }
    void ExitNormally() { (void)::SetEvent(exit.Get()); }
    ~LifetimeParent() { ExitNormally(); if (process) (void)::WaitForSingleObject(process.Get(), 5000); }
};
LRESULT CALLBACK PatternProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(window, &paint);
        RECT bounds{}; ::GetClientRect(window, &bounds);
        const bool alternate = ::GetWindowLongPtrW(window, GWLP_USERDATA) != 0;
        constexpr int stripe = 23;
        for (int x = 0; x < bounds.right; x += stripe) {
            const bool odd = (x / stripe) % 2 != 0;
            const COLORREF color = alternate ? (odd ? RGB(35, 185, 230) : RGB(225, 65, 155)) :
                (odd ? RGB(245, 175, 35) : RGB(55, 215, 110));
            HBRUSH brush = ::CreateSolidBrush(color);
            RECT band{x, 0, x + stripe, bounds.bottom};
            if (brush) { ::FillRect(dc, &band, brush); ::DeleteObject(brush); }
        }
        ::EndPaint(window, &paint); return 0;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}
struct OwnPattern {
    HWND window = nullptr;
    RECT bounds{};
    OwnPattern() {
        MONITORINFO monitor{sizeof(monitor)};
        REQUIRE(::GetMonitorInfoW(::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY), &monitor));
        bounds = monitor.rcMonitor;
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = L"AetherShell.Test.WindowCornerOpaquePattern";
        type.lpfnWndProc = PatternProc;
        REQUIRE(platform::RegisterWindowClassOnce(type).ok());
        window = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            type.lpszClassName, L"", WS_POPUP, bounds.left, bounds.top,
            bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, type.hInstance, nullptr);
        REQUIRE(window != nullptr);
        ::ShowWindow(window, SW_SHOWNOACTIVATE); Repaint(false);
    }
    void Repaint(bool alternate) {
        (void)::SetWindowLongPtrW(window, GWLP_USERDATA, alternate ? 1 : 0);
        REQUIRE(::RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW));
    }
    ~OwnPattern() { if (window) ::DestroyWindow(window); }
};
void SettleComposition() {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    REQUIRE(PumpUntil([&] { return std::chrono::steady_clock::now() >= end; }));
    REQUIRE(SUCCEEDED(::DwmFlush()));
}
// No pixels are read until an opaque own-process pattern fully covers the crop and every
// visible window above it is the explicitly allowed own fixture. Check again after the copy;
// an unexpected overlay fails closed. No capture is saved and no foreign title/content is read.
bool PrivatePatternCovers(HWND backdrop, HWND target, const RECT& crop) {
    DWORD process = 0; ::GetWindowThreadProcessId(backdrop, &process);
    RECT bounds{};
    if (process != ::GetCurrentProcessId() || !::IsWindowVisible(backdrop) ||
        !::GetWindowRect(backdrop, &bounds) || bounds.left > crop.left || bounds.top > crop.top ||
        bounds.right < crop.right || bounds.bottom < crop.bottom) return false;
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (window == backdrop) return true;
        if (!::IsWindowVisible(window)) continue;
        ::GetWindowThreadProcessId(window, &process);
        if (window == target && process == ::GetCurrentProcessId()) continue;
        // The guard's own corner pieces and rim for that target (D72) run in this test process too.
        if (target && process == ::GetCurrentProcessId() && platform::WindowClassName(window).starts_with(L"AetherShell.WindowCornerGuard.")) continue;
        RECT overlap{};
        if (!::GetWindowRect(window, &bounds) || ::IntersectRect(&overlap, &bounds, &crop)) return false;
    }
    return false;
}
std::vector<std::uint8_t> CapturePrivatePatch(HWND backdrop, HWND target, const RECT& crop) {
    REQUIRE(PrivatePatternCovers(backdrop, target, crop));
    const int width = crop.right - crop.left, height = crop.bottom - crop.top;
    HDC screen = ::GetDC(nullptr);
    HDC memory = screen ? ::CreateCompatibleDC(screen) : nullptr;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = screen ? ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0) : nullptr;
    const bool allocated = screen && memory && bitmap && bits;
    HGDIOBJ previous = allocated ? ::SelectObject(memory, bitmap) : nullptr;
    const bool copied = allocated && ::BitBlt(memory, 0, 0, width, height, screen,
        crop.left, crop.top, SRCCOPY | CAPTUREBLT) != FALSE;
    std::vector<std::uint8_t> pixels;
    if (copied) {
        const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
        pixels.assign(static_cast<const std::uint8_t*>(bits), static_cast<const std::uint8_t*>(bits) + size);
        // Desktop RGB is meaningful; the screen-DC alpha byte is unspecified.
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    }
    if (previous) ::SelectObject(memory, previous);
    if (bitmap) ::DeleteObject(bitmap);
    if (memory) ::DeleteDC(memory);
    if (screen) ::ReleaseDC(nullptr, screen);
    REQUIRE(allocated); REQUIRE(copied);
    REQUIRE(PrivatePatternCovers(backdrop, target, crop));
    return pixels;
}
// Local inspection image of an own-window capture (never committed).
void SaveInspection(const std::vector<std::uint8_t>& pixels, int width, int height, const std::wstring& name) {
    try {
        const auto path = std::filesystem::temp_directory_path() / L"AetherShellTests" / name;
        std::error_code ec; std::filesystem::create_directories(path.parent_path(), ec);
        auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
        winrt::com_ptr<IWICStream> stream; winrt::check_hresult(factory->CreateStream(stream.put()));
        winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
        winrt::com_ptr<IWICBitmapEncoder> encoder; winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
        winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
        winrt::com_ptr<IWICBitmapFrameEncode> frame; winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
        winrt::check_hresult(frame->Initialize(nullptr)); winrt::check_hresult(frame->SetSize(width, height));
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA; winrt::check_hresult(frame->SetPixelFormat(&format));
        winrt::check_hresult(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), const_cast<BYTE*>(pixels.data())));
        winrt::check_hresult(frame->Commit()); winrt::check_hresult(encoder->Commit());
    } catch (...) { test::Note("inspection image unavailable"); }
}

// The guard's own two windows in this test process, when they are shown.
struct Overlays { HWND pieces = nullptr, rim = nullptr; };
Overlays OwnOverlays() {
    const auto shown = [](const wchar_t* type) {
        HWND found = nullptr;
        for (HWND window = ::FindWindowExW(nullptr, nullptr, type, nullptr); window; window = ::FindWindowExW(nullptr, window, type, nullptr)) {
            DWORD process = 0; ::GetWindowThreadProcessId(window, &process);
            if (process == ::GetCurrentProcessId() && ::IsWindowVisible(window)) found = window;
        }
        return found;
    };
    return Overlays{shown(L"AetherShell.WindowCornerGuard.Corners"), shown(L"AetherShell.WindowCornerGuard.Rim")};
}
bool Square() { const auto overlays = OwnOverlays(); return !overlays.pieces && !overlays.rim; }
// Rounded: both of the guard's windows sit directly above `target`, with nothing between them and it.
bool Rounded(HWND target) {
    const auto overlays = OwnOverlays();
    if (!overlays.pieces || !overlays.rim) return false;
    const HWND first = ::GetWindow(target, GW_HWNDPREV), second = first ? ::GetWindow(first, GW_HWNDPREV) : nullptr;
    return (first == overlays.pieces && second == overlays.rim) || (first == overlays.rim && second == overlays.pieces);
}
bool Above(HWND upper, HWND lower) {
    for (HWND window = ::GetWindow(upper, GW_HWNDNEXT); window; window = ::GetWindow(window, GW_HWNDNEXT)) if (window == lower) return true;
    return false;
}
void Wait(std::chrono::milliseconds time) {
    const auto end = std::chrono::steady_clock::now() + time;
    (void)PumpUntil([&] { return std::chrono::steady_clock::now() >= end; }, time + std::chrono::seconds(1));
}

// Everything the guard could have changed on a window, had it written to it: D72 says it writes nothing.
struct Untouched {
    HWND window = nullptr;
    int regionKind = 0;
    std::vector<std::byte> region;
    LONG_PTR style = 0, extended = 0;
    WINDOWPLACEMENT placement{sizeof(WINDOWPLACEMENT)};
    RECT bounds{};
    DWORD backdrop = 0;
    HRESULT backdropRead = E_FAIL;
    int properties = 0;
    explicit Untouched(HWND value) : window(value) { Read(*this); }
    static void Read(Untouched& into) {
        platform::UniqueRegion shape(::CreateRectRgn(0, 0, 0, 0));
        into.regionKind = ::GetWindowRgn(into.window, shape.Get());
        into.region.clear();
        if (into.regionKind != ERROR) {
            const DWORD size = ::GetRegionData(shape.Get(), 0, nullptr);
            into.region.resize(size);
            if (size) (void)::GetRegionData(shape.Get(), size, reinterpret_cast<RGNDATA*>(into.region.data()));
        }
        into.style = ::GetWindowLongPtrW(into.window, GWL_STYLE);
        into.extended = ::GetWindowLongPtrW(into.window, GWL_EXSTYLE);
        (void)::GetWindowPlacement(into.window, &into.placement);
        (void)::GetWindowRect(into.window, &into.bounds);
        into.backdropRead = ::DwmGetWindowAttribute(into.window, DWMWA_SYSTEMBACKDROP_TYPE, &into.backdrop, sizeof(into.backdrop));
        into.properties = 0;
        (void)::EnumPropsExW(into.window, [](HWND, LPWSTR, HANDLE, ULONG_PTR count) -> BOOL { ++*reinterpret_cast<int*>(count); return TRUE; },
                             reinterpret_cast<ULONG_PTR>(&into.properties));
    }
    [[nodiscard]] bool Still() const {
        Untouched now(window);
        const bool same = now.regionKind == regionKind && now.region == region && now.style == style && now.extended == extended &&
            now.placement.showCmd == placement.showCmd && ::EqualRect(&now.placement.rcNormalPosition, &placement.rcNormalPosition) &&
            ::EqualRect(&now.bounds, &bounds) && now.backdropRead == backdropRead && now.backdrop == backdrop && now.properties == properties &&
            !::GetPropW(window, L"AetherShell.WindowCornerGuard.Lease") && !::GetPropW(window, L"AetherShell.WindowCornerLeaseOwner");
        if (!same) test::Note("the target changed: region " + std::to_string(regionKind) + "->" + std::to_string(now.regionKind) + ", properties " +
                              std::to_string(properties) + "->" + std::to_string(now.properties) + ", backdrop " + std::to_string(backdrop) + "->" + std::to_string(now.backdrop));
        return same;
    }
};
void Note(const WindowCornerGuardStatistics& statistics) {
    TEST_NOTE("guard counters: events=" + std::to_string(statistics.events.load()) + " sweeps=" + std::to_string(statistics.sweeps.load()) +
              " placements=" + std::to_string(statistics.placements.load()) + " abandonedClips=" + std::to_string(statistics.abandonedClips.load()) +
              " longestSweepMicros=" + std::to_string(statistics.longestSweepMicros.load()));
}

// The corners and the rim as they are composed on screen, over a private window that stands for the desktop
// and changes its picture: the corner pieces must show that picture as it is at that moment, pixel for pixel,
// the rim one colour of its own, and the application must be exactly as it was.
void VerifyRoundedOverTheLiveDesktop(bool customFrame, bool micaBackdrop = false, bool layered = false) {
    const auto foreground = ::GetForegroundWindow();
    platform::ComApartment apartment;
    REQUIRE(apartment.IsUsable());
    graphics::GraphicsDevice device;
    if (micaBackdrop) REQUIRE(device.Initialize().ok());
    OwnPattern desktop;
    OwnWindow target(customFrame, micaBackdrop, layered);
    if (micaBackdrop && !target.mainWindowBackdrop) {
        test::Note("SKIPPED mainwindow-backdrop fixture: this Windows build does not support the documented attribute");
        return;
    }
    graphics::CompositionSurface content;
    if (micaBackdrop) {
        // NOREDIRECTIONBITMAP has no GDI backing bitmap: a genuine opaque composition body, as browser and
        // editor windows have, with Windows' own material behind it.
        REQUIRE(content.Attach(device, target.window).ok());
        RECT client{}; REQUIRE(::GetClientRect(target.window, &client));
        REQUIRE(content.Resize(client.right - client.left, client.bottom - client.top).ok());
        REQUIRE(content.Draw(::GetDpiForWindow(target.window), [](ID2D1DeviceContext* context) {
            context->Clear(D2D1::ColorF(28.0f / 255.0f, 36.0f / 255.0f, 44.0f / 255.0f, 1.0f));
        }).ok());
    }
    REQUIRE(::SetWindowPos(target.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    SettleComposition();
    const Untouched before(target.window);
    WindowCornerGuardStatistics statistics;
    OwnGuard guard(std::vector<HWND>{target.window}, nullptr, &statistics, desktop.window);
    REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return Rounded(target.window); }));
    const auto overlays = OwnOverlays();
    for (const HWND overlay : {overlays.pieces, overlays.rim}) {
        const auto extended = ::GetWindowLongPtrW(overlay, GWL_EXSTYLE);
        CHECK((extended & WS_EX_TRANSPARENT) != 0);   // clicks go through to the application
        CHECK((extended & WS_EX_NOACTIVATE) != 0);
        CHECK((extended & WS_EX_TOOLWINDOW) != 0);    // no taskbar entry, not in Alt+Tab
        CHECK(::GetWindow(overlay, GW_OWNER) == nullptr);
    }
    // The outline is the application's visible frame inside the work area.
    RECT outline{}, piecesBounds{}, frame{}, expected{};
    REQUIRE(::GetWindowRect(overlays.rim, &outline)); REQUIRE(::GetWindowRect(overlays.pieces, &piecesBounds));
    CHECK(::EqualRect(&outline, &piecesBounds) != FALSE);
    REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(target.window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))));
    MONITORINFO monitor{sizeof(monitor)};
    REQUIRE(::GetMonitorInfoW(::MonitorFromWindow(target.window, MONITOR_DEFAULTTONEAREST), &monitor));
    REQUIRE(::IntersectRect(&expected, &frame, &monitor.rcWork));
    CHECK(::EqualRect(&outline, &expected) != FALSE);
    // This fixture is always-on-top, to be above the private desktop: so are the pieces over it.
    CHECK((::GetWindowLongPtrW(overlays.pieces, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    CHECK((::GetWindowLongPtrW(overlays.rim, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0);
    platform::UniqueRegion pieces(::CreateRectRgn(0, 0, 0, 0)), ring(::CreateRectRgn(0, 0, 0, 0));
    REQUIRE(::GetWindowRgn(overlays.pieces, pieces.Get()) != ERROR);
    REQUIRE(::GetWindowRgn(overlays.rim, ring.Get()) != ERROR);
    const int radius = std::max(3, ::MulDiv(12, static_cast<int>(::GetDpiForWindow(target.window)), 96));
    const std::array<RECT, 4> corners{{
        {outline.left, outline.top, outline.left + radius, outline.top + radius},
        {outline.right - radius, outline.top, outline.right, outline.top + radius},
        {outline.left, outline.bottom - radius, outline.left + radius, outline.bottom},
        {outline.right - radius, outline.bottom - radius, outline.right, outline.bottom}}};
    const LONG midX = (outline.left + outline.right) / 2, midY = (outline.top + outline.bottom) / 2;
    const std::array<RECT, 4> edges{{{midX, outline.top, midX + 1, outline.top + 1}, {midX, outline.bottom - 1, midX + 1, outline.bottom},
                                     {outline.left, midY, outline.left + 1, midY + 1}, {outline.right - 1, midY, outline.right, midY + 1}}};
    for (const auto& edge : edges) CHECK(::PtInRegion(ring.Get(), edge.left - outline.left, edge.top - outline.top));
    const RECT interior{outline.left + 40, outline.top + 80, outline.left + 43, outline.top + 83};
    // The desktop alone, in both of its phases. With the application hidden the guard shows nothing.
    ::ShowWindow(target.window, SW_HIDE);
    REQUIRE(!::IsWindowVisible(target.window));
    REQUIRE(PumpUntil([] { return Square(); }));
    std::array<std::array<std::vector<std::uint8_t>, 4>, 2> bare;
    std::array<std::vector<std::uint8_t>, 2> bareInterior;
    for (int phase = 0; phase < 2; ++phase) {
        desktop.Repaint(phase != 0); SettleComposition();
        for (std::size_t i = 0; i < corners.size(); ++i) bare[phase][i] = CapturePrivatePatch(desktop.window, nullptr, corners[i]);
        bareInterior[phase] = CapturePrivatePatch(desktop.window, nullptr, interior);
    }
    for (std::size_t i = 0; i < corners.size(); ++i) REQUIRE(bare[0][i] != bare[1][i]);
    desktop.Repaint(false);
    ::ShowWindow(target.window, SW_SHOWMAXIMIZED);
    BetweenBars(target.window);
    REQUIRE(::SetWindowPos(target.window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    REQUIRE(PumpUntil([&] { return Rounded(target.window); }));
    std::array<std::vector<std::uint8_t>, 2> rimColor;
    for (int phase = 0; phase < 2; ++phase) {
        desktop.Repaint(phase != 0); SettleComposition();
        std::vector<std::uint8_t> rimPixels;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const auto actual = CapturePrivatePatch(desktop.window, target.window, corners[i]);
            int pieceCount = 0, pieceWrong = 0, ringCount = 0, both = 0;
            std::size_t pixel = 0;
            for (LONG y = corners[i].top; y < corners[i].bottom; ++y) for (LONG x = corners[i].left; x < corners[i].right; ++x, pixel += 4) {
                const bool piece = ::PtInRegion(pieces.Get(), x - outline.left, y - outline.top) != FALSE;
                const bool band = ::PtInRegion(ring.Get(), x - outline.left, y - outline.top) != FALSE;
                if (piece && band) ++both;
                if (piece) { ++pieceCount; if (!std::equal(actual.begin() + pixel, actual.begin() + pixel + 3, bare[phase][i].begin() + pixel)) ++pieceWrong; }
                else if (band) { ++ringCount; rimPixels.insert(rimPixels.end(), actual.begin() + pixel, actual.begin() + pixel + 3); }
            }
            REQUIRE(pieceCount > 0); REQUIRE(ringCount > 0);
            CHECK_EQ(both, 0);
            if (pieceWrong) test::Note("corner " + std::to_string(i) + ", desktop phase " + std::to_string(phase) + ": " + std::to_string(pieceWrong) + " of " +
                                       std::to_string(pieceCount) + " corner pixels are not the desktop's");
            CHECK_EQ(pieceWrong, 0);   // the live desktop, pixel for pixel: a painted corner could not follow its change
        }
        for (const auto& edge : edges) {
            const auto pixel = CapturePrivatePatch(desktop.window, target.window, edge);
            rimPixels.insert(rimPixels.end(), pixel.begin(), pixel.begin() + 3);
        }
        // One colour all the way round: every corner arc and the middle of every side.
        REQUIRE(rimPixels.size() >= 3);
        int odd = 0;
        for (std::size_t offset = 0; offset + 3 <= rimPixels.size(); offset += 3)
            if (!std::equal(rimPixels.begin() + offset, rimPixels.begin() + offset + 3, rimPixels.begin())) ++odd;
        CHECK_EQ(odd, 0);
        rimColor[phase].assign(rimPixels.begin(), rimPixels.begin() + 3);
        CHECK(CapturePrivatePatch(desktop.window, target.window, interior) != bareInterior[phase]);   // the application itself is still there
        CHECK(::IsZoomed(target.window));
        CHECK(::GetForegroundWindow() == foreground);
    }
    CHECK(rimColor[0] == rimColor[1]);   // the rim's own colour, whatever lies behind
    {
        // Local inspection: the top edge with both corners and the caption area, phase 0.
        desktop.Repaint(false); SettleComposition();
        const RECT& screen = desktop.bounds;
        const RECT left{std::max(screen.left, outline.left - 24), std::max(screen.top, outline.top - 24), outline.left + 96, outline.top + 72};
        const RECT right{outline.right - 360, std::max(screen.top, outline.top - 24), std::min(screen.right, outline.right + 24), outline.top + 72};
        const auto suffix = std::wstring(customFrame ? L"custom" : L"standard") + (micaBackdrop ? L"-mica" : L"") + (layered ? L"-layered" : L"") + L".png";
        SaveInspection(CapturePrivatePatch(desktop.window, target.window, left), left.right - left.left, left.bottom - left.top, L"window-corners-left-" + suffix);
        SaveInspection(CapturePrivatePatch(desktop.window, target.window, right), right.right - right.left, right.bottom - right.top, L"window-corners-right-" + suffix);
    }
    CHECK(before.Still());   // nothing was written to the application: region, material, styles, placement, properties
    Note(statistics);
    REQUIRE(guard.Finish()); CHECK_EQ(guard.exitCode.load(), 0);
    CHECK(Square());         // the guard's windows leave with it
    CHECK(before.Still());
}
}

AETHER_TEST("window corners pixels: a standard maximized frame gets the live desktop in its four corners and a continuous rim, untouched") {
    VerifyRoundedOverTheLiveDesktop(false);
}
AETHER_TEST("window corners pixels: a client-drawn frame, as browsers have, gets the live desktop in its corners and a continuous rim, untouched") {
    VerifyRoundedOverTheLiveDesktop(true);
}
AETHER_TEST("window corners pixels: a composition window with Windows' own material keeps that material and is rounded all the same") {
    VerifyRoundedOverTheLiveDesktop(true, true);
}
AETHER_TEST("window corners pixels: a layered composition window is rounded all the same") {
    VerifyRoundedOverTheLiveDesktop(true, true, true);
}

AETHER_TEST("window corners native: the pieces follow the front maximized window, and whatever is in front of it covers them") {
    OwnPattern desktop;
    OwnWindow first, second;   // the second, made last, is in front
    const Untouched firstBefore(first.window), secondBefore(second.window);
    WindowCornerGuardStatistics statistics;
    OwnGuard guard(std::vector<HWND>{first.window, second.window}, nullptr, &statistics, desktop.window);
    REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return Rounded(second.window); }));
    CHECK_FALSE(Rounded(first.window));
    // Another maximized window comes to the front: the pieces go to it, and it is never restacked by the guard.
    const auto raised = std::chrono::steady_clock::now();
    REQUIRE(::SetWindowPos(first.window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    REQUIRE(PumpUntil([&] { return Rounded(first.window); }));
    TEST_NOTE("the pieces followed the new front window after " +
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - raised).count()) + " ms");
    CHECK(Above(first.window, second.window));
    // A window that is not maximized comes in front of it, over a corner: it is in front of the pieces too.
    RECT bounds{}; REQUIRE(::GetWindowRect(OwnOverlays().rim, &bounds));
    const HWND front = ::CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"Static", L"", WS_POPUP, bounds.left - 10, bounds.top - 10, 220, 140,
                                         nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    REQUIRE(front != nullptr);
    struct Close { HWND window; ~Close() { ::DestroyWindow(window); } } close{front};
    // Windows 11 26H2 was seen putting such a window directly above the maximized one, under the guard's two windows,
    // and leaving it there despite HWND_TOP; no reorder event follows. Being shown is what the guard hears then, well
    // within the 400 ms here, which the slow look (2 s) cannot answer.
    const auto shown = std::chrono::steady_clock::now();
    ::ShowWindow(front, SW_SHOWNOACTIVATE);
    REQUIRE(::SetWindowPos(front, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    const bool putUnder = ::GetWindow(first.window, GW_HWNDPREV) == front;   // as Windows placed it, unless the guard was quicker
    const bool coveredInTime = PumpUntil([&] {
        const auto now = OwnOverlays();
        return Rounded(first.window) && now.pieces && now.rim && Above(front, now.pieces) && Above(front, now.rim);
    }, std::chrono::milliseconds(400));
    TEST_NOTE(std::string(putUnder ? "Windows put the new window right above the maximized one, under the pieces; " : "") +
              "the window in front covered the pieces after " +
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - shown).count()) + " ms");
    CHECK(coveredInTime);
    Wait(std::chrono::milliseconds(100));
    CHECK(Rounded(first.window));
    const auto overlays = OwnOverlays();
    REQUIRE(overlays.pieces != nullptr); REQUIRE(overlays.rim != nullptr);
    CHECK(Above(front, overlays.pieces)); CHECK(Above(front, overlays.rim));
    // Ordinary windows over an ordinary window: an always-on-top piece would lie over everything in front of it.
    CHECK((::GetWindowLongPtrW(overlays.pieces, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
    CHECK((::GetWindowLongPtrW(overlays.rim, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0);
    // At rest nothing is restacked again: the guard does not chase its own changes.
    const auto placed = statistics.placements.load();
    Wait(std::chrono::milliseconds(700));
    CHECK_EQ(statistics.placements.load(), placed);
    // The front window is restored: no longer maximized, it gives the pieces to the one behind.
    ::ShowWindow(first.window, SW_SHOWNOACTIVATE);
    REQUIRE(PumpUntil([&] { return !::IsZoomed(first.window); }));
    REQUIRE(PumpUntil([&] { return Rounded(second.window); }));
    // Minimized: nothing maximized is left, and nothing is drawn.
    ::ShowWindow(second.window, SW_SHOWMINNOACTIVE);
    REQUIRE(PumpUntil([] { return Square(); }));
    ::ShowWindow(second.window, SW_SHOWMAXIMIZED);
    BetweenBars(second.window);
    REQUIRE(PumpUntil([&] { return Rounded(second.window); }));
    Note(statistics);
    REQUIRE(guard.Finish()); CHECK_EQ(guard.exitCode.load(), 0);
    CHECK(Square());
    CHECK(::IsZoomed(second.window));
    // Only what this test itself did to them changed: the guard added no region, property or style.
    CHECK(::GetPropW(first.window, L"AetherShell.WindowCornerGuard.Lease") == nullptr);
    CHECK_EQ(Untouched(first.window).regionKind, firstBefore.regionKind);
    CHECK_EQ(Untouched(second.window).regionKind, secondBefore.regionKind);
    CHECK_EQ(Untouched(second.window).properties, secondBefore.properties);
}

AETHER_TEST("window corners native: a window with a shape of its own keeps it, and a plain rectangle is no shape") {
    OwnPattern desktop;
    OwnWindow target;
    OwnGuard guard(std::vector<HWND>{target.window}, nullptr, nullptr, desktop.window);
    REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return Rounded(target.window); }));
    RECT bounds{}; REQUIRE(::GetWindowRect(target.window, &bounds));
    const int width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    // The application shapes itself (as Aether's own maximized Settings does): the guard draws nothing over it.
    HRGN shaped = ::CreateRoundRectRgn(0, 0, width, height, 80, 80);
    REQUIRE(shaped != nullptr);
    REQUIRE(::SetWindowRgn(target.window, shaped, TRUE) != 0);
    ::NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE, target.window, OBJID_WINDOW, CHILDID_SELF);
    CHECK(PumpUntil([] { return Square(); }, std::chrono::seconds(4)));
    // A plain rectangle around the frame, as a browser keeps on its maximized window, is no shape of its own.
    HRGN plain = ::CreateRectRgn(0, 0, width, height);
    REQUIRE(plain != nullptr);
    REQUIRE(::SetWindowRgn(target.window, plain, TRUE) != 0);
    ::NotifyWinEvent(EVENT_OBJECT_LOCATIONCHANGE, target.window, OBJID_WINDOW, CHILDID_SELF);
    CHECK(PumpUntil([&] { return Rounded(target.window); }, std::chrono::seconds(4)));
    platform::UniqueRegion kept(::CreateRectRgn(0, 0, 0, 0)), same(::CreateRectRgn(0, 0, width, height));
    REQUIRE(::GetWindowRgn(target.window, kept.Get()) != ERROR);
    CHECK(::EqualRgn(kept.Get(), same.Get()));   // and it is still exactly the application's
    REQUIRE(guard.Finish());
    REQUIRE(::GetWindowRgn(target.window, kept.Get()) != ERROR);
    CHECK(::EqualRgn(kept.Get(), same.Get()));
}

AETHER_TEST("window corners native: the guard leaves with its parent, and its windows with it") {
    LifetimeParent parent;
    OwnPattern desktop;
    OwnWindow window;
    const Untouched before(window.window);
    OwnGuard guard(std::vector<HWND>{window.window}, parent.process.Get(), nullptr, desktop.window);
    REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return Rounded(window.window); }));
    parent.ExitNormally();
    REQUIRE(PumpUntil([&] { return guard.finished.load(); }));
    CHECK_EQ(guard.exitCode.load(), 0);
    CHECK_EQ(::WaitForSingleObject(guard.stop.Get(), 0), static_cast<DWORD>(WAIT_TIMEOUT));   // it left because the process was gone
    CHECK(Square());
    CHECK(before.Still());
    CHECK(::IsZoomed(window.window));
}

// Before D72 the guard gave other applications' windows a rounded region and marked them. A guard that was killed
// left the clip behind, and the application stayed cut even once restored.
AETHER_TEST("window corners native: a clip left by an earlier version's dead guard is removed once; a living guard's is left to it") {
    OwnPattern desktop;
    OwnWindow abandoned, kept;
    const auto clip = [](HWND window, DWORD owner) {
        RECT bounds{}; REQUIRE(::GetWindowRect(window, &bounds));
        HRGN rounded = ::CreateRoundRectRgn(8, 8, bounds.right - bounds.left - 8, bounds.bottom - bounds.top - 8, 36, 36);
        REQUIRE(rounded != nullptr);
        REQUIRE(::SetWindowRgn(window, rounded, TRUE) != 0);
        REQUIRE(::SetPropW(window, L"AetherShell.WindowCornerGuard.Lease", reinterpret_cast<HANDLE>(std::uintptr_t{7})));
        REQUIRE(::SetPropW(window, L"AetherShell.WindowCornerLeaseOwner", reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(owner))));
    };
    const auto complex = [](HWND window) { platform::UniqueRegion region(::CreateRectRgn(0, 0, 0, 0)); return ::GetWindowRgn(window, region.Get()) == COMPLEXREGION; };
    DWORD gone = 0;
    {
        LifetimeParent earlier;   // stands for the earlier version's guard process
        gone = ::GetProcessId(earlier.process.Get());
        REQUIRE(gone != 0);
        clip(abandoned.window, gone);
        LifetimeParent living;
        clip(kept.window, ::GetProcessId(living.process.Get()));
        earlier.ExitNormally();
        REQUIRE(::WaitForSingleObject(earlier.process.Get(), 5000) == WAIT_OBJECT_0);
        WindowCornerGuardStatistics statistics;
        OwnGuard guard(std::vector<HWND>{abandoned.window, kept.window}, nullptr, &statistics, desktop.window);
        REQUIRE(guard.Ready());
        // The dead guard's clip and marks go; the window is whole again and, being in front or not, simply maximized.
        REQUIRE(PumpUntil([&] { return !complex(abandoned.window) && !::GetPropW(abandoned.window, L"AetherShell.WindowCornerGuard.Lease"); }));
        CHECK(::GetPropW(abandoned.window, L"AetherShell.WindowCornerLeaseOwner") == nullptr);
        CHECK_EQ(statistics.abandonedClips.load(), std::uint64_t{1});
        // The other window's guard still runs: its clip and its marks are its own business.
        Wait(std::chrono::milliseconds(300));
        CHECK(complex(kept.window));
        CHECK(::GetPropW(kept.window, L"AetherShell.WindowCornerGuard.Lease") != nullptr);
        REQUIRE(guard.Finish());
        CHECK(complex(kept.window));
    }
    // That guard is gone too now: the next start removes its clip as well.
    WindowCornerGuardStatistics statistics;
    OwnGuard guard(std::vector<HWND>{abandoned.window, kept.window}, nullptr, &statistics, desktop.window);
    REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return !complex(kept.window) && !::GetPropW(kept.window, L"AetherShell.WindowCornerGuard.Lease"); }));
    CHECK_EQ(statistics.abandonedClips.load(), std::uint64_t{1});
    REQUIRE(guard.Finish());
}

// --- AS4-01/AS4-03 fixtures --------------------------------------------------------------------------
// A disposable maximized target on its own thread: it repaints changing content continuously, and on request
// stops answering its messages, as a page's busy UI thread does. Own windows only; never activated.
namespace {
constexpr UINT kHang = WM_APP + 40;
constexpr UINT_PTR kRepaintTimer = 9;
struct BusyTarget {
    HWND window = nullptr;
    std::atomic<int> paints{0};
    std::atomic<bool> fillMonitor{false};   // its maximized place is the whole monitor, as with both bars hidden
    platform::UniqueHandle resume{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    platform::UniqueHandle created{::CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::thread thread;
    BusyTarget() {
        thread = std::thread([this] {
            WNDCLASSEXW type{sizeof(type)};
            type.hInstance = ::GetModuleHandleW(nullptr);
            type.lpszClassName = L"AetherShell.Test.WindowCornerBusyTarget";
            type.lpfnWndProc = &BusyTarget::Proc;
            (void)platform::RegisterWindowClassOnce(type);
            HWND handle = ::CreateWindowExW(WS_EX_NOACTIVATE, type.lpszClassName, L"Aether busy target", WS_OVERLAPPEDWINDOW,
                                            200, 160, 640, 480, nullptr, nullptr, type.hInstance, this);
            if (handle) {
                (void)::SetWindowTheme(handle, L"", L"");   // its visible frame is exactly its window
                ::ShowWindow(handle, SW_SHOWMAXIMIZED);
                BetweenBars(handle);
                ::SetTimer(handle, kRepaintTimer, 16, nullptr);
            }
            window = handle;
            (void)::SetEvent(created.Get());
            MSG message{};
            while (handle && ::GetMessageW(&message, nullptr, 0, 0) > 0) { ::TranslateMessage(&message); ::DispatchMessageW(&message); }
        });
        (void)::WaitForSingleObject(created.Get(), 5000);
        REQUIRE(window != nullptr);
        REQUIRE(::IsZoomed(window));
    }
    ~BusyTarget() {
        (void)::SetEvent(resume.Get());
        if (window) ::PostMessageW(window, WM_CLOSE, 0, 0);
        if (thread.joinable()) thread.join();
    }
    void Hang() { (void)::ResetEvent(resume.Get()); REQUIRE(::PostMessageW(window, kHang, 0, 0)); }
    void Resume() { (void)::SetEvent(resume.Get()); }
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        auto* self = reinterpret_cast<BusyTarget*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!self) return ::DefWindowProcW(hwnd, message, wParam, lParam);
        switch (message) {
            case WM_NCCALCSIZE: if (wParam) return 0; break;   // a client-drawn frame like a browser's
            case WM_GETMINMAXINFO: {
                if (!self->fillMonitor.load()) break;
                MONITORINFO monitor{sizeof(monitor)};
                if (!::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) break;
                auto* limits = reinterpret_cast<MINMAXINFO*>(lParam);
                // Windows places a maximized window from the work area's corner: stepping back by the work area's
                // inset puts this one at the monitor's own corner.
                limits->ptMaxPosition = {monitor.rcMonitor.left - monitor.rcWork.left, monitor.rcMonitor.top - monitor.rcWork.top};
                limits->ptMaxSize = {monitor.rcMonitor.right - monitor.rcMonitor.left, monitor.rcMonitor.bottom - monitor.rcMonitor.top};
                limits->ptMaxTrackSize = limits->ptMaxSize;
                return 0;
            }
            case kHang: (void)::WaitForSingleObject(self->resume.Get(), 30000); return 0;
            case WM_TIMER: ::InvalidateRect(hwnd, nullptr, FALSE); return 0;
            case WM_PAINT: {
                PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(hwnd, &paint);
                const int value = ++self->paints;
                if (HBRUSH brush = ::CreateSolidBrush(RGB(value * 7 % 256, value * 13 % 256, 60))) {
                    RECT client{}; ::GetClientRect(hwnd, &client); ::FillRect(dc, &client, brush); ::DeleteObject(brush);
                }
                ::EndPaint(hwnd, &paint);
                return 0;
            }
            case WM_DESTROY: ::PostQuitMessage(0); return 0;
            default: break;
        }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
};
}  // namespace

AETHER_TEST("window corners native: a maximize filling the monitor stays square and is rounded again between the bars (D56)") {
    OwnPattern desktop;
    BusyTarget target;   // a client-drawn frame: its visible frame is its whole window
    OwnGuard guard(std::vector<HWND>{target.window}, nullptr, nullptr, desktop.window);
    REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return Rounded(target.window); }));
    // What both bars auto-hidden gives a maximized window: its maximized place is the whole monitor. Simulated on
    // this one window, which says so itself when Windows asks where it maximizes; the real work area and other
    // windows are never touched. Windows keeps a window that is already maximized inside the work area, so the
    // window is restored and maximized again.
    MONITORINFO monitor{sizeof(monitor)}; REQUIRE(::GetMonitorInfoW(::MonitorFromWindow(target.window, MONITOR_DEFAULTTONEAREST), &monitor));
    const RECT& full = monitor.rcMonitor;
    const auto maximize = [&](bool wholeMonitor) {
        target.fillMonitor = wholeMonitor;
        ::ShowWindow(target.window, SW_SHOWNOACTIVATE);
        REQUIRE(PumpUntil([&] { return !::IsZoomed(target.window); }));
        ::ShowWindow(target.window, SW_SHOWMAXIMIZED);
        REQUIRE(PumpUntil([&] { return ::IsZoomed(target.window) != FALSE; }));
        if (!wholeMonitor) BetweenBars(target.window);
    };
    maximize(true);
    RECT now{};
    const bool covers = PumpUntil([&] {
        return SUCCEEDED(::DwmGetWindowAttribute(target.window, DWMWA_EXTENDED_FRAME_BOUNDS, &now, sizeof(now))) && now.left <= full.left &&
            now.top <= full.top && now.right >= full.right && now.bottom >= full.bottom;
    }, std::chrono::seconds(2));
    if (!covers) {
        // Not every Windows build lets a window choose a maximized place beyond the work area. The rule itself is
        // unit tested from exact facts; here there is then nothing to observe.
        TEST_NOTE("SKIPPED: this Windows build kept the fixture's maximized frame at " + std::to_string(now.left) + "," + std::to_string(now.top) + "," +
                  std::to_string(now.right) + "," + std::to_string(now.bottom) + " on a monitor " + std::to_string(full.right - full.left) + "x" + std::to_string(full.bottom - full.top));
        REQUIRE(guard.Finish());
        return;
    }
    CHECK(PumpUntil([] { return Square(); }, std::chrono::seconds(3)));
    // Back between the bars, it is rounded again.
    maximize(false);
    CHECK(PumpUntil([&] { return Rounded(target.window); }, std::chrono::seconds(3)));
    REQUIRE(guard.Finish());
    CHECK(Square());
}

// AS4-01, AS4-03: the guard used to write into the applications it rounded, and one that did not answer could hold
// it, or be left blank. It now asks a window nothing that the window itself must answer.
AETHER_TEST("window corners native: an application that stops answering is rounded like any other and holds nothing up") {
    OwnPattern desktop;
    BusyTarget hung, healthy;   // the healthy one, made last, is in front
    WindowCornerGuardStatistics statistics;
    OwnGuard guard(std::vector<HWND>{hung.window, healthy.window}, nullptr, &statistics, desktop.window);
    REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return Rounded(healthy.window); }));
    hung.Hang();
    REQUIRE(PumpUntil([&] { const int before = hung.paints.load(); ::Sleep(60); return hung.paints.load() == before; }));
    // The front window is minimized: the pieces go to the window behind, which does not answer. Nothing the guard
    // does waits for it.
    auto started = std::chrono::steady_clock::now();
    ::ShowWindow(healthy.window, SW_SHOWMINNOACTIVE);
    CHECK(PumpUntil([&] { return Rounded(hung.window); }, std::chrono::seconds(3)));
    TEST_NOTE("the pieces reached the application that does not answer after " +
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()) + " ms");
    // The healthy window comes back: the pieces are again above whichever of the two is in front.
    started = std::chrono::steady_clock::now();
    ::ShowWindow(healthy.window, SW_SHOWMAXIMIZED);
    BetweenBars(healthy.window);
    CHECK(PumpUntil([&] { return Rounded(Above(healthy.window, hung.window) ? healthy.window : hung.window) && ::IsZoomed(healthy.window); }, std::chrono::seconds(3)));
    TEST_NOTE("and followed the front window again after " +
              std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()) + " ms");
    CHECK(statistics.longestSweepMicros.load() < 200000);   // no look at the windows ever waited on the hung one
    Note(statistics);
    // Stopping does not wait for it either: there is nothing to give back.
    started = std::chrono::steady_clock::now();
    REQUIRE(guard.Finish());
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(2));
    CHECK(Square());
    hung.Resume();
    const int paints = hung.paints.load();
    CHECK(PumpUntil([&] { return hung.paints.load() > paints; }, std::chrono::seconds(3)));   // it was never changed: it simply carries on
}
