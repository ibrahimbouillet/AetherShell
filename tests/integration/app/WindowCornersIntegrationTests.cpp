// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "app/WindowCorners.h"
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
struct OwnWindow {
    HWND window = nullptr;
    OwnWindow() {
        const auto foreground = ::GetForegroundWindow();
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = L"AetherShell.Test.WindowCornerGuard";
        type.lpfnWndProc = ::DefWindowProcW;
        type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        REQUIRE(platform::RegisterWindowClassOnce(type).ok());
        // Own synthetic window only. NOACTIVATE keeps the owner's foreground unchanged, and is
        // permitted solely by the explicit own-process fixture restriction in this guard.
        window = ::CreateWindowExW(WS_EX_NOACTIVATE, type.lpszClassName, L"Aether corner guard fixture",
            WS_OVERLAPPEDWINDOW, 160, 120, 640, 480, nullptr, nullptr, type.hInstance, nullptr);
        REQUIRE(window != nullptr);
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
