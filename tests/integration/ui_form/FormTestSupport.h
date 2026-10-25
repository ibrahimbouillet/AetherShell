// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Shared helpers of the ui_form integration tests. Tests create only their own windows; pixel
// checks read only screen areas covered by those windows over a private, opaque test-pattern
// backdrop (the approach of tests/integration/ui/GlassProbeTests.cpp): the backdrop is verified to
// be the only thing below our windows in the guarded area, before and after every capture.
#pragma once

#include "AetherTest.h"
#include "core/Strings.h"

#include "graphics/Theme.h"
#include "platform/windows/MonitorUtilities.h"
#include "ui/form/FormSurface.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <winrt/base.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace aether::test::form {

// --- private opaque backdrop --------------------------------------------------------------------

inline LRESULT CALLBACK BackdropProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = ::BeginPaint(hwnd, &ps);
        RECT client{};
        ::GetClientRect(hwnd, &client);
        const int stripe = 24;
        const bool alternate = ::GetWindowLongPtrW(hwnd, GWLP_USERDATA) != 0;
        for (int x = 0; x < client.right; x += stripe) {
            const int i = x / stripe;
            const COLORREF color = alternate ? RGB(195 - (i * 31) % 90, 120 + (i * 17) % 100, 35 + (i * 11) % 80)
                                             : RGB(40 + (i * 53) % 200, 60 + (i * 97) % 180, 90 + (i * 31) % 160);
            HBRUSH brush = ::CreateSolidBrush(color);
            RECT band{x, 0, x + stripe, client.bottom};
            ::FillRect(dc, &band, brush);
            ::DeleteObject(brush);
        }
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

inline HWND CreateTestBackdrop(const RECT& rect) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &BackdropProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AetherShell.Tests.FormBackdrop";
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    HWND hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, rect.left,
                                  rect.top, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return nullptr;
    ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    ::UpdateWindow(hwnd);
    return hwnd;
}

inline void SetBackdropPhase(HWND backdrop, bool alternate) {
    ::SetWindowLongPtrW(backdrop, GWLP_USERDATA, alternate ? 1 : 0);
    ::InvalidateRect(backdrop, nullptr, FALSE);
    ::UpdateWindow(backdrop);
}

// Local diagnosis of an inconclusive capture: the overlapping process's image name and geometry
// (never a title or content), once per process per test run.
inline void NoteOverlay(HWND window, DWORD process, const RECT& bounds) {
    static std::vector<DWORD> noted;
    if (std::find(noted.begin(), noted.end(), process) != noted.end()) return;
    noted.push_back(process);
    std::wstring image = L"?";
    if (HANDLE handle = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process)) {
        wchar_t path[MAX_PATH]{};
        DWORD size = MAX_PATH;
        if (::QueryFullProcessImageNameW(handle, 0, path, &size)) image = std::filesystem::path(path).filename().wstring();
        ::CloseHandle(handle);
    }
    BOOL cloaked = FALSE;
    ::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    TEST_NOTE("overlay: " + aether::text::Narrow(image) + " rect " + std::to_string(bounds.left) + "," + std::to_string(bounds.top) + "," +
              std::to_string(bounds.right) + "," + std::to_string(bounds.bottom) + (cloaked ? " cloaked" : "") +
              ((::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST) ? " topmost" : ""));
}

// A foreign overlay anywhere in the guard band could contribute pixels. Inspect only PID,
// visibility and geometry; never read a foreign title or content.
inline bool PrivateBackdropVisible(HWND backdrop, const RECT& guardedArea) {
    DWORD process = 0;
    ::GetWindowThreadProcessId(backdrop, &process);
    RECT bounds{};
    if (process != ::GetCurrentProcessId() || !::IsWindowVisible(backdrop) || !::GetWindowRect(backdrop, &bounds) ||
        bounds.left > guardedArea.left || bounds.top > guardedArea.top || bounds.right < guardedArea.right ||
        bounds.bottom < guardedArea.bottom) {
        return false;
    }
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (window == backdrop) return true;
        if (!::IsWindowVisible(window)) continue;
        ::GetWindowThreadProcessId(window, &process);
        if (process == ::GetCurrentProcessId()) continue;
        RECT overlap{};
        if (::GetWindowRect(window, &bounds) && ::IntersectRect(&overlap, &bounds, &guardedArea)) {
            NoteOverlay(window, process, bounds);
            return false;
        }
    }
    return false;
}

struct BackdropGuard {
    HWND hwnd = nullptr;
    RECT area{};
    ~BackdropGuard() {
        if (hwnd) ::DestroyWindow(hwnd);
    }
};

// --- message pumping --------------------------------------------------------------------------------

struct PumpStats {
    // WM_TIMER messages for the test forms and their descendants. Timers of Windows' own helper
    // windows (the text-input-framework IME window) and windowless thread timers are not the form's
    // animation and are only noted.
    int timers = 0;
    // Timers the native progress bar started for its own (replaced) painting before the surface
    // owned it; the surface kills each one when it first fires, so they never recur.
    int nativeTimers = 0;
};

inline bool FormOwnsTimer(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t name[64]{};
    ::GetClassNameW(::GetAncestor(hwnd, GA_ROOT), name, 64);
    return std::wstring_view(name).starts_with(L"AetherShell.");
}

// Pumps this thread's messages for `milliseconds` (or until `until` returns true).
inline PumpStats Pump(DWORD milliseconds, const std::function<bool()>& until = {}) {
    PumpStats stats;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    for (;;) {
        MSG message;
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_TIMER && FormOwnsTimer(message.hwnd)) {
                wchar_t name[64]{};
                ::GetClassNameW(message.hwnd, name, 64);
                if (std::wstring_view(name) == PROGRESS_CLASSW) ++stats.nativeTimers;
                else ++stats.timers;
            }
            if (message.message == WM_QUIT) {
                ::PostQuitMessage(static_cast<int>(message.wParam));
                return stats;
            }
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        if (until && until()) return stats;
        const auto now = std::chrono::steady_clock::now();
        if (now >= end) return stats;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(end - now).count();
        ::MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(std::min<long long>(left, 20)), QS_ALLINPUT);
    }
}

// --- screen capture of our own windows ------------------------------------------------------------

struct Capture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;
    [[nodiscard]] bool Valid() const noexcept { return width > 0 && height > 0 && !bgra.empty(); }
    [[nodiscard]] const std::uint8_t* Pixel(int x, int y) const {
        x = std::clamp(x, 0, width - 1);
        y = std::clamp(y, 0, height - 1);
        return &bgra[(static_cast<std::size_t>(y) * width + x) * 4];
    }
};

// Captures `rect` (screen pixels) only when the private backdrop is the sole content below our
// windows in `guardedArea`, before and after the copy. Returns an invalid capture otherwise.
inline Capture CapturePrivate(HWND backdrop, const RECT& guardedArea, const RECT& rect) {
    Capture capture;
    if (!PrivateBackdropVisible(backdrop, guardedArea)) return capture;
    if (rect.left < guardedArea.left || rect.top < guardedArea.top || rect.right > guardedArea.right ||
        rect.bottom > guardedArea.bottom) {
        return capture;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    HDC screen = ::GetDC(nullptr);
    HDC memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    bool copied = false;
    if (screen && memory && bitmap && bits) {
        HGDIOBJ old = ::SelectObject(memory, bitmap);
        copied = ::BitBlt(memory, 0, 0, width, height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT) != FALSE;
        if (copied) {
            capture.width = width;
            capture.height = height;
            capture.bgra.assign(static_cast<std::uint8_t*>(bits),
                                static_cast<std::uint8_t*>(bits) + static_cast<std::size_t>(width) * height * 4);
        }
        ::SelectObject(memory, old);
    }
    if (bitmap) ::DeleteObject(bitmap);
    if (memory) ::DeleteDC(memory);
    if (screen) ::ReleaseDC(nullptr, screen);
    if (!PrivateBackdropVisible(backdrop, guardedArea)) return Capture{};
    return capture;
}

// Retries a guarded capture a few times: another engineer's test windows may pass by.
inline Capture CaptureWithRetry(HWND backdrop, const RECT& guardedArea, const RECT& rect, int attempts = 6) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto capture = CapturePrivate(backdrop, guardedArea, rect);
        if (capture.Valid()) return capture;
        TEST_NOTE("private backdrop not exclusively visible; retrying capture");
        Pump(500);
    }
    return Capture{};
}

inline std::filesystem::path InspectionPath(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / "AetherShellTests" / (name + ".png");
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return path;
}

inline bool SavePng(const Capture& capture, const std::filesystem::path& path) {
    try {
        auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
        winrt::com_ptr<IWICStream> stream;
        winrt::check_hresult(factory->CreateStream(stream.put()));
        winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
        winrt::com_ptr<IWICBitmapEncoder> encoder;
        winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
        winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
        winrt::com_ptr<IWICBitmapFrameEncode> frame;
        winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
        winrt::check_hresult(frame->Initialize(nullptr));
        winrt::check_hresult(frame->SetSize(capture.width, capture.height));
        std::vector<std::uint8_t> opaque = capture.bgra;
        for (std::size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 0xFF;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        winrt::check_hresult(frame->SetPixelFormat(&format));
        if (format != GUID_WICPixelFormat32bppBGRA) return false;
        winrt::check_hresult(frame->WritePixels(capture.height, capture.width * 4, static_cast<UINT>(opaque.size()), opaque.data()));
        winrt::check_hresult(frame->Commit());
        winrt::check_hresult(encoder->Commit());
        return true;
    } catch (const winrt::hresult_error& error) {
        TEST_NOTE("PNG encoding failed: HRESULT " + std::to_string(static_cast<std::int32_t>(error.code().value)));
        return false;
    } catch (const std::exception&) {
        TEST_NOTE("PNG encoding failed: standard exception");
        return false;
    } catch (...) {
        TEST_NOTE("PNG encoding failed: unknown exception");
        return false;
    }
}

// Mean absolute RGB difference between two captures over `area` (capture pixels).
inline double MeanDifference(const Capture& a, const Capture& b, const RECT& area) {
    double sum = 0.0;
    int samples = 0;
    for (int y = area.top; y < area.bottom; ++y) {
        for (int x = area.left; x < area.right; ++x) {
            const auto* p = a.Pixel(x, y);
            const auto* q = b.Pixel(x, y);
            for (int c = 0; c < 3; ++c) sum += std::abs(static_cast<int>(p[c]) - static_cast<int>(q[c]));
            samples += 3;
        }
    }
    return samples ? sum / samples : 0.0;
}

inline int MaxDifference(const Capture& a, const Capture& b, const RECT& area) {
    int worst = 0;
    for (int y = area.top; y < area.bottom; ++y) {
        for (int x = area.left; x < area.right; ++x) {
            const auto* p = a.Pixel(x, y);
            const auto* q = b.Pixel(x, y);
            for (int c = 0; c < 3; ++c) worst = std::max(worst, std::abs(static_cast<int>(p[c]) - static_cast<int>(q[c])));
        }
    }
    return worst;
}

inline bool PixelNear(const std::uint8_t* pixel, const graphics::Color& color, int tolerance) {
    const auto channel = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    return std::abs(pixel[2] - channel(color.r)) <= tolerance && std::abs(pixel[1] - channel(color.g)) <= tolerance &&
           std::abs(pixel[0] - channel(color.b)) <= tolerance;
}

inline std::string DescribePixel(const std::uint8_t* pixel) {
    return "rgb(" + std::to_string(pixel[2]) + "," + std::to_string(pixel[1]) + "," + std::to_string(pixel[0]) + ")";
}

// The safe band for test windows on the primary monitor: the middle of the work area, clear of the
// owner's Top Bar (top 90 px) and Dock (bottom 170 px).
inline RECT SafeArea() {
    const auto monitor = platform::PrimaryMonitor();
    RECT area = monitor.workArea;
    const int scale = static_cast<int>(monitor.dpi ? monitor.dpi : 96);
    area.top = std::max<LONG>(area.top, monitor.bounds.top + ::MulDiv(60, scale, 96));
    area.bottom = std::min<LONG>(area.bottom, monitor.bounds.bottom - ::MulDiv(120, scale, 96));
    // Visible top-most windows of other processes along the top or bottom edge (the owner's Top Bar
    // and Dock with its magnification headroom) bound the band too. Geometry only.
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (!(::GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)) break;
        DWORD process = 0;
        ::GetWindowThreadProcessId(window, &process);
        BOOL cloaked = FALSE;
        ::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
        RECT bounds{}, overlap{};
        if (process == ::GetCurrentProcessId() || !::IsWindowVisible(window) || cloaked || !::GetWindowRect(window, &bounds) ||
            !::IntersectRect(&overlap, &bounds, &area)) continue;
        if (bounds.right - bounds.left < (area.right - area.left) / 3) continue;  // not an edge bar
        const LONG gap = ::MulDiv(8, scale, 96);
        if ((bounds.top + bounds.bottom) / 2 < (monitor.bounds.top + monitor.bounds.bottom) / 2) area.top = std::max(area.top, bounds.bottom + gap);
        else area.bottom = std::min(area.bottom, bounds.top - gap);
    }
    return area;
}

// --- a form window owned by the test ------------------------------------------------------------

struct Notification {
    UINT message = 0;
    int id = 0;
    int code = 0;
    HWND from = nullptr;
};

class FormHost {
public:
    ui::form::FormSurface surface;
    HWND hwnd = nullptr;
    std::vector<Notification> notifications;
    std::function<std::optional<LRESULT>(UINT, WPARAM, LPARAM)> extra;

    FormHost() = default;
    FormHost(const FormHost&) = delete;
    FormHost& operator=(const FormHost&) = delete;
    ~FormHost() { Destroy(); }

    // Creates a top-most (above the private backdrop), non-activated window whose client area is
    // `clientDip` DIPs, centred in the safe area.
    bool Create(float clientWidthDip, float clientHeightDip, const wchar_t* title = L"Aether form test") {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &FormHost::Proc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"AetherShell.Tests.FormHost";
        if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        const auto monitor = platform::PrimaryMonitor();
        const float scale = static_cast<float>(monitor.dpi) / 96.0f;
        RECT frame{0, 0, static_cast<LONG>(std::lround(clientWidthDip * scale)), static_cast<LONG>(std::lround(clientHeightDip * scale))};
        ::AdjustWindowRectExForDpi(&frame, kStyle, FALSE, kExStyle, monitor.dpi);
        const RECT safe = SafeArea();
        const int w = frame.right - frame.left;
        const int h = frame.bottom - frame.top;
        const int x = (safe.left + safe.right - w) / 2;
        const int y = (safe.top + safe.bottom - h) / 2;
        hwnd = ::CreateWindowExW(kExStyle, wc.lpszClassName, title, kStyle, x, y, w, h, nullptr, nullptr, wc.hInstance, this);
        return hwnd != nullptr;
    }

    void Show() {
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        ::SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::UpdateWindow(hwnd);
    }

    void Destroy() {
        if (hwnd && ::IsWindow(hwnd)) ::DestroyWindow(hwnd);
        hwnd = nullptr;
    }

    [[nodiscard]] RECT ClientOnScreen() const {
        RECT client{};
        ::GetClientRect(hwnd, &client);
        ::MapWindowPoints(hwnd, nullptr, reinterpret_cast<POINT*>(&client), 2);
        return client;
    }

    [[nodiscard]] RECT WindowBounds() const {
        RECT bounds{};
        ::GetWindowRect(hwnd, &bounds);
        return bounds;
    }

    [[nodiscard]] int Px(float dip) const { return static_cast<int>(std::lround(surface.ToPx(dip))); }
    [[nodiscard]] RECT PxRect(float l, float t, float r, float b) const { return RECT{Px(l), Px(t), Px(r), Px(b)}; }

    HWND Add(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) {
        return ::CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent,
                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
    }

    [[nodiscard]] int Count(UINT message, int id, int code) const {
        return static_cast<int>(std::count_if(notifications.begin(), notifications.end(), [&](const Notification& n) {
            return n.message == message && n.id == id && n.code == code;
        }));
    }

    static constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    static constexpr DWORD kExStyle = WS_EX_TOPMOST | WS_EX_CONTROLPARENT;

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        auto* self = reinterpret_cast<FormHost*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) {
            if (auto handled = self->surface.HandleMessage(message, wParam, lParam)) return *handled;
            if (message == WM_COMMAND) {
                self->notifications.push_back(Notification{message, LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam)});
            } else if (message == WM_NOTIFY) {
                const auto* header = reinterpret_cast<const NMHDR*>(lParam);
                self->notifications.push_back(Notification{message, static_cast<int>(header->idFrom), static_cast<int>(header->code),
                                                           header->hwndFrom});
            } else if (message == WM_HSCROLL && lParam) {
                self->notifications.push_back(Notification{message, ::GetDlgCtrlID(reinterpret_cast<HWND>(lParam)), LOWORD(wParam),
                                                           reinterpret_cast<HWND>(lParam)});
            }
            if (self->extra) {
                if (auto handled = self->extra(message, wParam, lParam)) return *handled;
            }
            if (message == WM_NCDESTROY) {
                ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
                self->hwnd = nullptr;
            }
        }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
};

}  // namespace aether::test::form
