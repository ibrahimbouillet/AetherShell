// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "showcase/GdiDesktopCapture.h"

#include <Windows.h>

#include <cstdint>

using namespace aether;
using namespace aether::showcase;

namespace {

constexpr wchar_t kCaptureWindowClass[] = L"AetherShell.Tests.ShowcaseCapture";
constexpr COLORREF kPatternColor = RGB(47, 111, 203);

LRESULT CALLBACK CapturePatternProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = ::BeginPaint(hwnd, &paint);
        RECT client{};
        (void)::GetClientRect(hwnd, &client);
        HBRUSH brush = ::CreateSolidBrush(kPatternColor);
        if (brush) {
            (void)::FillRect(dc, &client, brush);
            (void)::DeleteObject(brush);
        }
        ::EndPaint(hwnd, &paint);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

class CapturePatternWindow final {
public:
    CapturePatternWindow() {
        WNDCLASSEXW type{sizeof(type)};
        type.lpfnWndProc = &CapturePatternProc;
        type.hInstance = ::GetModuleHandleW(nullptr);
        type.lpszClassName = kCaptureWindowClass;
        if (!::RegisterClassExW(&type) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        hwnd_ = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                  kCaptureWindowClass, L"", WS_POPUP, 300, 300, 64, 64,
                                  nullptr, nullptr, type.hInstance, nullptr);
        if (!hwnd_) return;
        (void)::SetWindowPos(hwnd_, HWND_TOPMOST, 300, 300, 64, 64,
                             SWP_NOACTIVATE | SWP_SHOWWINDOW);
        (void)::RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        ::Sleep(75);
    }

    ~CapturePatternWindow() { if (hwnd_) (void)::DestroyWindow(hwnd_); }
    [[nodiscard]] HWND Handle() const noexcept { return hwnd_; }

private:
    HWND hwnd_ = nullptr;
};

}  // namespace

AETHER_TEST("showcase GDI capture: captures only the requested private test window as opaque RGBA") {
    CapturePatternWindow window;
    REQUIRE(window.Handle() != nullptr);
    RECT bounds{};
    REQUIRE(::GetWindowRect(window.Handle(), &bounds) != FALSE);

    auto capture = GdiDesktopCapture::Capture(bounds);
    REQUIRE(capture.ok());
    const auto frame = capture->Frame();
    CHECK_EQ(frame.width, 64U);
    CHECK_EQ(frame.height, 64U);
    CHECK_EQ(frame.rowStrideBytes, 64U * 4U);
    REQUIRE(frame.pixels.size() == 64U * 64U * 4U);
    CHECK_EQ(frame.pixels[0], static_cast<std::uint8_t>(47));
    CHECK_EQ(frame.pixels[1], static_cast<std::uint8_t>(111));
    CHECK_EQ(frame.pixels[2], static_cast<std::uint8_t>(203));
    CHECK_EQ(frame.pixels[3], static_cast<std::uint8_t>(255));
}

AETHER_TEST("showcase GDI capture: invalid bounds fail without taking a desktop snapshot") {
    const RECT invalid{40, 40, 40, 80};
    const auto capture = GdiDesktopCapture::Capture(invalid);
    CHECK_FALSE(capture.ok());
    CHECK_EQ(capture.error().code, ErrorCode::InvalidArgument);
}
