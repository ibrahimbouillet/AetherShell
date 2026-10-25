// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/WindowCorners.h"
#include "graphics/CompositionSurface.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/Handles.h"
#include "platform/windows/WindowUtilities.h"

#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::app;
namespace {
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
    explicit OwnWindow(bool customFrame = false, bool micaBackdrop = false) {
        const auto foreground = ::GetForegroundWindow();
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = customFrame ? L"AetherShell.Test.WindowCornerCustomFrame" : L"AetherShell.Test.WindowCornerGuard";
        type.lpfnWndProc = customFrame ? CustomFrameProc : ::DefWindowProcW;
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        REQUIRE(platform::RegisterWindowClassOnce(type).ok());
        // Own synthetic window only. NOACTIVATE keeps the owner's foreground unchanged, and is
        // permitted solely by the explicit own-process fixture restriction in this guard.
        const DWORD extended = WS_EX_NOACTIVATE | (micaBackdrop ? WS_EX_NOREDIRECTIONBITMAP : 0);
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
    explicit OwnGuard(HWND window, HANDLE parentProcess = nullptr) {
        if (parentProcess) {
            HANDLE copied = nullptr;
            REQUIRE(::DuplicateHandle(::GetCurrentProcess(), parentProcess, ::GetCurrentProcess(), &copied,
                SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0));
            parent.Reset(copied);
        } else parent.Reset(::OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ::GetCurrentProcessId()));
        REQUIRE(parent && ready && stop);
        const WindowCornerGuardOptions restriction{::GetCurrentProcessId(), {window}};
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
bool HasRegion(HWND window) {
    platform::UniqueRegion region(::CreateRectRgn(0, 0, 0, 0));
    return ::GetWindowRgn(window, region.Get()) != ERROR;
}

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
std::vector<std::uint8_t> CutoutPixels(const std::vector<std::uint8_t>& pixels, const RECT& patch,
    const RECT& native, HRGN region) {
    std::vector<std::uint8_t> result;
    std::size_t pixel = 0;
    for (LONG y = patch.top; y < patch.bottom; ++y) for (LONG x = patch.left; x < patch.right; ++x) {
        if (!::PtInRegion(region, x - native.left, y - native.top))
            result.insert(result.end(), pixels.begin() + pixel, pixels.begin() + pixel + 4);
        pixel += 4;
    }
    REQUIRE(!result.empty());
    return result;
}
void VerifyComposedCornerCutouts(bool customFrame, bool micaBackdrop = false) {
    const auto foreground = ::GetForegroundWindow();
    platform::ComApartment apartment;
    REQUIRE(apartment.IsUsable());
    graphics::GraphicsDevice device;
    if (micaBackdrop) REQUIRE(device.Initialize().ok());
    OwnPattern backdrop;
    OwnWindow target(customFrame, micaBackdrop);
    if (micaBackdrop && !target.mainWindowBackdrop) {
        test::Note("SKIPPED mainwindow-backdrop fixture: this Windows build does not support the documented attribute");
        return;
    }
    graphics::CompositionSurface content;
    if (micaBackdrop) {
        // NOREDIRECTIONBITMAP deliberately has no GDI backing bitmap. Keep a genuine opaque
        // composition body, as browser/editor windows do, so removing only DWM's full-bounds
        // material must not make the application contents disappear as well as its corners.
        REQUIRE(content.Attach(device, target.window).ok());
        RECT client{}; REQUIRE(::GetClientRect(target.window, &client));
        REQUIRE(content.Resize(client.right - client.left, client.bottom - client.top).ok());
        REQUIRE(content.Draw(::GetDpiForWindow(target.window), [](ID2D1DeviceContext* context) {
            context->Clear(D2D1::ColorF(28.0f / 255.0f, 36.0f / 255.0f, 44.0f / 255.0f, 1.0f));
        }).ok());
    }
    REQUIRE(::SetWindowPos(target.window, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    // Complete this fixture's initial native frame/topmost composition before leasing it.
    // DefWindowProc may still install its own ordinary rectangular frame region during show.
    SettleComposition();
    OwnGuard guard(target.window); REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] {
        platform::UniqueRegion actual(::CreateRectRgn(0, 0, 0, 0));
        return ::GetPropW(target.window, L"AetherShell.WindowCornerGuard.Lease") &&
            ::GetWindowRgn(target.window, actual.Get()) == COMPLEXREGION;
    }));
    if (micaBackdrop) {
        DWM_SYSTEMBACKDROP_TYPE material = DWMSBT_AUTO;
        REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(target.window, DWMWA_SYSTEMBACKDROP_TYPE, &material, sizeof(material))));
        CHECK_EQ(material, DWMSBT_NONE);
    }
    RECT native{}; REQUIRE(::GetWindowRect(target.window, &native));
    platform::UniqueRegion region(::CreateRectRgn(0, 0, 0, 0));
    REQUIRE(::GetWindowRgn(target.window, region.Get()) != ERROR);
    RECT clip{}; REQUIRE(::GetRgnBox(region.Get(), &clip) != ERROR);
    ::OffsetRect(&clip, native.left, native.top);
    const int cornerSize = std::max(3, ::MulDiv(12, static_cast<int>(::GetDpiForWindow(target.window)), 96));
    const std::array<RECT, 4> corners{{
        {clip.left, clip.top, clip.left + cornerSize, clip.top + cornerSize},
        {clip.right - cornerSize, clip.top, clip.right, clip.top + cornerSize},
        {clip.left, clip.bottom - cornerSize, clip.left + cornerSize, clip.bottom},
        {clip.right - cornerSize, clip.bottom - cornerSize, clip.right, clip.bottom}
    }};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        int excluded = 0;
        for (LONG y = corners[i].top; y < corners[i].bottom; ++y)
            for (LONG x = corners[i].left; x < corners[i].right; ++x)
                if (!::PtInRegion(region.Get(), x - native.left, y - native.top)) ++excluded;
        if (!excluded) test::Note("Own fixture corner " + std::to_string(i) + " has no rounded cutout pixels");
        REQUIRE(excluded > 0);
    }
    const RECT interior{clip.left + 40, clip.top + 80, clip.left + 43, clip.top + 83};
    RECT uncovered = backdrop.bounds; ++uncovered.right;
    CHECK_FALSE(PrivatePatternCovers(backdrop.window, target.window, uncovered));
    CHECK_FALSE(PrivatePatternCovers(backdrop.window, nullptr, interior));
    // Capture both genuine backdrop phases while only the opaque private pattern is visible.
    // Then keep the target visible throughout both actual comparisons: a painted corner mask
    // cannot pass when the background changes under it.
    ::ShowWindow(target.window, SW_HIDE);
    // Windows may retain its own ordinary frame region while hidden. Actual invisibility,
    // not the absence of any HRGN, is what makes these backdrop references safe and meaningful.
    REQUIRE(!::IsWindowVisible(target.window));
    std::array<std::array<std::vector<std::uint8_t>, 4>, 2> expected;
    std::array<std::vector<std::uint8_t>, 2> expectedInterior;
    for (int phase = 0; phase < 2; ++phase) {
        backdrop.Repaint(phase != 0); SettleComposition();
        for (std::size_t i = 0; i < corners.size(); ++i)
            expected[phase][i] = CutoutPixels(CapturePrivatePatch(backdrop.window, nullptr, corners[i]),
                corners[i], native, region.Get());
        expectedInterior[phase] = CapturePrivatePatch(backdrop.window, nullptr, interior);
    }
    for (std::size_t i = 0; i < corners.size(); ++i) REQUIRE(expected[0][i] != expected[1][i]);
    backdrop.Repaint(false);
    ::ShowWindow(target.window, SW_SHOWMAXIMIZED);
    REQUIRE(::SetWindowPos(target.window, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE));
    REQUIRE(PumpUntil([&] {
        platform::UniqueRegion actual(::CreateRectRgn(0, 0, 0, 0));
        return ::GetPropW(target.window, L"AetherShell.WindowCornerGuard.Lease") &&
            ::GetWindowRgn(target.window, actual.Get()) != ERROR && ::EqualRgn(region.Get(), actual.Get());
    }));
    for (int phase = 0; phase < 2; ++phase) {
        backdrop.Repaint(phase != 0); SettleComposition();
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const auto actual = CutoutPixels(CapturePrivatePatch(backdrop.window, target.window, corners[i]),
                corners[i], native, region.Get());
            if (actual != expected[phase][i])
                test::Note("Composed corner " + std::to_string(i) + ", backdrop phase " + std::to_string(phase) +
                    " differs from the genuine opaque private backdrop");
            CHECK(actual == expected[phase][i]);
        }
        CHECK(CapturePrivatePatch(backdrop.window, target.window, interior) != expectedInterior[phase]);
        CHECK(::IsZoomed(target.window));
        CHECK(::GetForegroundWindow() == foreground);
    }
    REQUIRE(guard.Finish()); CHECK_EQ(guard.exitCode.load(), 0);
    if (micaBackdrop) {
        DWM_SYSTEMBACKDROP_TYPE material = DWMSBT_AUTO;
        REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(target.window, DWMWA_SYSTEMBACKDROP_TYPE, &material, sizeof(material))));
        CHECK_EQ(material, DWMSBT_MAINWINDOW);
    }
}
}

AETHER_TEST("window corners native: guard preserves genuine maximize and restores the exact prior region on stop") {
    OwnWindow window;
    WINDOWPLACEMENT before{sizeof(before)}; REQUIRE(::GetWindowPlacement(window.window, &before));
    platform::UniqueRegion original(::CreateRectRgn(0, 0, 0, 0));
    const bool hadOriginal = ::GetWindowRgn(window.window, original.Get()) != ERROR;
    OwnGuard guard(window.window); REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return HasRegion(window.window); }));
    CHECK(::IsZoomed(window.window));
    WINDOWPLACEMENT after{sizeof(after)}; REQUIRE(::GetWindowPlacement(window.window, &after));
    CHECK_EQ(after.showCmd, before.showCmd); CHECK(::EqualRect(&after.rcNormalPosition, &before.rcNormalPosition));
    RECT native{}, frame{}; REQUIRE(::GetWindowRect(window.window, &native));
    REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(window.window, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))));
    MONITORINFO monitor{sizeof(monitor)}; REQUIRE(::GetMonitorInfoW(::MonitorFromWindow(window.window, MONITOR_DEFAULTTONEAREST), &monitor));
    const int left = std::max(frame.left, monitor.rcWork.left) - native.left;
    const int top = std::max(frame.top, monitor.rcWork.top) - native.top;
    platform::UniqueRegion clipped(::CreateRectRgn(0, 0, 0, 0));
    REQUIRE(::GetWindowRgn(window.window, clipped.Get()) != ERROR);
    CHECK_FALSE(::PtInRegion(clipped.Get(), left, top));
    CHECK(::PtInRegion(clipped.Get(), left + 24, top + 24));
    REQUIRE(guard.Finish()); CHECK_EQ(guard.exitCode.load(), 0);
    platform::UniqueRegion restored(::CreateRectRgn(0, 0, 0, 0));
    const bool hasRestored = ::GetWindowRgn(window.window, restored.Get()) != ERROR;
    CHECK_EQ(hasRestored, hadOriginal);
    if (hadOriginal) CHECK(::EqualRgn(original.Get(), restored.Get()));
    CHECK(::IsZoomed(window.window));
}

AETHER_TEST("window corners native: application replacement wins and is preserved after guard exit") {
    OwnWindow window;
    OwnGuard guard(window.window); REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return HasRegion(window.window); }));
    platform::UniqueRegion replacement(::CreateRectRgn(20, 30, 400, 300));
    platform::UniqueRegion expected(::CreateRectRgn(20, 30, 400, 300));
    REQUIRE(::SetWindowRgn(window.window, replacement.Get(), FALSE)); (void)replacement.Release();
    REQUIRE(guard.Finish()); CHECK_EQ(guard.exitCode.load(), 0);
    platform::UniqueRegion actual(::CreateRectRgn(0, 0, 0, 0));
    REQUIRE(::GetWindowRgn(window.window, actual.Get()) != ERROR);
    CHECK(::EqualRgn(actual.Get(), expected.Get()));
}

AETHER_TEST("window corners native: real fixture parent death restores without a stop request") {
    LifetimeParent parent;
    OwnWindow window;
    platform::UniqueRegion original(::CreateRectRgn(0, 0, 0, 0));
    const bool hadOriginal = ::GetWindowRgn(window.window, original.Get()) != ERROR;
    OwnGuard guard(window.window, parent.process.Get()); REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return HasRegion(window.window); }));
    parent.ExitNormally();
    REQUIRE(PumpUntil([&] { return guard.finished.load(); }));
    CHECK_EQ(guard.exitCode.load(), 0);
    CHECK_EQ(::WaitForSingleObject(guard.stop.Get(), 0), static_cast<DWORD>(WAIT_TIMEOUT)); // recovery came from process death
    platform::UniqueRegion restored(::CreateRectRgn(0, 0, 0, 0));
    const bool hasRestored = ::GetWindowRgn(window.window, restored.Get()) != ERROR;
    CHECK_EQ(hasRestored, hadOriginal);
    if (hadOriginal) CHECK(::EqualRgn(original.Get(), restored.Get()));
    CHECK(::IsZoomed(window.window));
}

AETHER_TEST("window corners pixels: standard maximized frame has live transparent cutouts at all four corners") {
    VerifyComposedCornerCutouts(false);
}

AETHER_TEST("window corners pixels: custom nonclient frame has live transparent cutouts at all four corners") {
    VerifyComposedCornerCutouts(true);
}

AETHER_TEST("window corners pixels: custom nonclient mainwindow backdrop has live transparent cutouts at all four corners") {
    VerifyComposedCornerCutouts(true, true);
}

AETHER_TEST("window corners native: an application backdrop replacement survives guard restoration") {
    OwnWindow target(true, true);
    if (!target.mainWindowBackdrop) {
        test::Note("SKIPPED mainwindow-backdrop fixture: this Windows build does not support the documented attribute");
        return;
    }
    OwnGuard guard(target.window); REQUIRE(guard.Ready());
    REQUIRE(PumpUntil([&] { return HasRegion(target.window); }));
    DWM_SYSTEMBACKDROP_TYPE material = DWMSBT_AUTO;
    REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(target.window, DWMWA_SYSTEMBACKDROP_TYPE, &material, sizeof(material))));
    CHECK_EQ(material, DWMSBT_NONE);
    const DWM_SYSTEMBACKDROP_TYPE replacement = DWMSBT_TABBEDWINDOW;
    REQUIRE(SUCCEEDED(::DwmSetWindowAttribute(target.window, DWMWA_SYSTEMBACKDROP_TYPE, &replacement, sizeof(replacement))));
    // DWM can itself install a normal region while changing material. Give this own application
    // an explicit replacement region as well, then prove neither of its changes is overwritten.
    platform::UniqueRegion applicationRegion(::CreateRectRgn(24, 24, 640, 480));
    platform::UniqueRegion expected(::CreateRectRgn(24, 24, 640, 480));
    REQUIRE(::SetWindowRgn(target.window, applicationRegion.Get(), TRUE)); (void)applicationRegion.Release();
    REQUIRE(guard.Finish()); CHECK_EQ(guard.exitCode.load(), 0);
    REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(target.window, DWMWA_SYSTEMBACKDROP_TYPE, &material, sizeof(material))));
    CHECK_EQ(material, replacement);
    platform::UniqueRegion restored(::CreateRectRgn(0, 0, 0, 0));
    REQUIRE(::GetWindowRgn(target.window, restored.Get()) != ERROR);
    CHECK(::EqualRgn(expected.Get(), restored.Get()));
    CHECK(::IsZoomed(target.window));
}
