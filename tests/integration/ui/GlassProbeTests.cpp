// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Visual probe: renders real glass surfaces on the desktop, captures the
// composed screen pixels and saves them as PNG files for inspection
// (%TEMP%\AetherShellTests\probe-*.png). Assertions check that something was
// actually composed (non-uniform pixels inside the panel, transparent margins
// showing only an opaque private test pattern).
#include "AetherTest.h"

#include "graphics/FrameScheduler.h"
#include "graphics/GlassPainter.h"
#include "graphics/GraphicsDevice.h"
#include "graphics/TextCache.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "ui/SurfaceWindow.h"
#include "ui/TooltipWindow.h"

#include <wincodec.h>

#include <cmath>
#include <vector>

using namespace aether;

namespace {

class ProbeWindow final : public ui::SurfaceWindow {
public:
    ProbeWindow(graphics::GraphicsDevice& device, graphics::FrameScheduler& scheduler, graphics::ThemeMode mode,
                bool blur)
        : SurfaceWindow(device, scheduler), tokens_(graphics::MakeDesignTokens(mode)), blur_(blur) {
        tokens_.transparencyEnabled = mode != graphics::ThemeMode::HighContrast;
        text_.Initialize(device.DWriteFactory(), tokens_.fontFamily, tokens_.displayFontFamily);
    }
    ~ProbeWindow() override { Destroy(); }

    int frames = 0;

protected:
    void OnRender(const ui::RenderContext& context) override {
        ++frames;
        const D2D1_RECT_F panel{24.0f, 24.0f, context.widthDip - 24.0f, context.heightDip - 24.0f};
        SetBackdropPanel(panel, tokens_.dockCornerRadius, blur_);
        graphics::GlassStyle style;
        style.cornerRadius = tokens_.dockCornerRadius;
        style.backdropBlur = BackdropBlurActive();
        graphics::PaintGlassPanel(context.d2d, panel, tokens_, style);

        // A row of placeholder tiles standing in for icons, and a label.
        const wchar_t initials[] = L"AESHL";
        for (int i = 0; i < 5; ++i) {
            const float x = panel.left + 16.0f + static_cast<float>(i) * 60.0f;
            graphics::PaintFallbackIcon(context.d2d, Device().DWriteFactory(),
                                        D2D1::RectF(x, panel.top + 12.0f, x + 48.0f, panel.top + 60.0f), initials[i],
                                        tokens_);
        }
        winrt::com_ptr<ID2D1SolidColorBrush> brush;
        context.d2d->CreateSolidColorBrush(tokens_.textPrimary.ToD2D(), brush.put());
        graphics::TextStyle style2;
        style2.size = tokens_.fontSizeBody;
        text_.Draw(context.d2d, L"Aether Shell glass probe", D2D1::RectF(panel.left + 330.0f, panel.top, panel.right - 12.0f, panel.bottom), style2, brush.get());
    }

private:
    graphics::DesignTokens tokens_;
    graphics::TextCache text_;
    bool blur_;
};

// Opaque, colourful test-pattern window placed behind the probe so captures contain only test content
// (never the user's other windows) and the blur has something recognizable to blur.
LRESULT CALLBACK BackdropProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = ::BeginPaint(hwnd, &ps);
        RECT client{};
        ::GetClientRect(hwnd, &client);
        const int stripe = 24;
        for (int x = 0; x < client.right; x += stripe) {
            const int i = x / stripe;
            const bool alternate = ::GetWindowLongPtrW(hwnd, GWLP_USERDATA) != 0;
            const COLORREF color = alternate ? RGB(195 - (i * 31) % 90, 120 + (i * 17) % 100, 35 + (i * 11) % 80)
                : RGB(40 + (i * 53) % 200, 60 + (i * 97) % 180, 90 + (i * 31) % 160);
            HBRUSH brush = ::CreateSolidBrush(color);
            RECT band{x, 0, x + stripe, client.bottom};
            ::FillRect(dc, &band, brush);
            ::DeleteObject(brush);
        }
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, RGB(255, 255, 255));
        RECT text = client;
        ::DrawTextW(dc, L"AETHER SHELL TEST PATTERN  AETHER SHELL TEST PATTERN", -1, &text,
                    DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

HWND CreateTestBackdrop(const RECT& rect) {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = &BackdropProc;
    wc.hInstance = ::GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AetherShell.Tests.Backdrop";
    if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return nullptr;
    HWND hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"",
                                  WS_POPUP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                                  nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return nullptr;
    ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    ::UpdateWindow(hwnd);
    return hwnd;
}

// A foreign overlay anywhere in the guard band could contribute blurred pixels even outside the
// saved crop. Inspect only PID, visibility and geometry; never read a foreign title or content.
bool PrivateBackdropVisible(HWND backdrop, const RECT& guardedArea, DWORD trustedProcess = ::GetCurrentProcessId()) {
    DWORD process = 0; ::GetWindowThreadProcessId(backdrop, &process);
    RECT bounds{};
    if (process != ::GetCurrentProcessId() || !::IsWindowVisible(backdrop) || !::GetWindowRect(backdrop, &bounds) ||
        bounds.left > guardedArea.left || bounds.top > guardedArea.top ||
        bounds.right < guardedArea.right || bounds.bottom < guardedArea.bottom) return false;
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (window == backdrop) return true;
        if (!::IsWindowVisible(window)) continue;
        ::GetWindowThreadProcessId(window, &process);
        if (process == trustedProcess) continue;
        RECT overlap{};
        if (::GetWindowRect(window, &bounds) && ::IntersectRect(&overlap, &bounds, &guardedArea)) return false;
    }
    return false;
}

void RunLoopFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;
    ::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    const auto id = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    loop.Run();
    loop.RemoveWaitHandle(id);
    ::CloseHandle(timer);
}

struct Capture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;
    [[nodiscard]] const std::uint8_t* Pixel(int x, int y) const { return &bgra[(static_cast<size_t>(y) * width + x) * 4]; }
};

Capture CapturePrivate(HWND backdrop, const RECT& guardedArea, const RECT& rect) {
    REQUIRE(PrivateBackdropVisible(backdrop, guardedArea));
    REQUIRE(rect.left >= guardedArea.left && rect.top >= guardedArea.top && rect.right <= guardedArea.right && rect.bottom <= guardedArea.bottom);
    Capture capture;
    capture.width = rect.right - rect.left;
    capture.height = rect.bottom - rect.top;
    HDC screen = ::GetDC(nullptr);
    HDC memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = capture.width;
    info.bmiHeader.biHeight = -capture.height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const bool allocated = screen && memory && bitmap && bits;
    bool copied = false;
    HGDIOBJ old = nullptr;
    if (allocated) {
        old = ::SelectObject(memory, bitmap);
        copied = ::BitBlt(memory, 0, 0, capture.width, capture.height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT) != FALSE;
    }
    if (copied) capture.bgra.assign(static_cast<std::uint8_t*>(bits),
                        static_cast<std::uint8_t*>(bits) + static_cast<size_t>(capture.width) * capture.height * 4);
    if (old) ::SelectObject(memory, old);
    if (bitmap) ::DeleteObject(bitmap);
    if (memory) ::DeleteDC(memory);
    if (screen) ::ReleaseDC(nullptr, screen);
    REQUIRE(allocated); REQUIRE(copied);
    REQUIRE(PrivateBackdropVisible(backdrop, guardedArea));
    return capture;
}

void SavePng(const Capture& capture, const std::filesystem::path& path) {
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
    // Screen captures carry no meaningful alpha; store them opaque as 32bppBGRA (natively supported by
    // the PNG encoder, so SetPixelFormat must not substitute another layout).
    std::vector<std::uint8_t> opaque = capture.bgra;
    for (size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 0xFF;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format));
    REQUIRE(format == GUID_WICPixelFormat32bppBGRA);
    winrt::check_hresult(frame->WritePixels(capture.height, capture.width * 4, static_cast<UINT>(opaque.size()),
                                            opaque.data()));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
}

double LuminanceVariance(const Capture& capture, const RECT& area) {
    double sum = 0.0;
    double sumSq = 0.0;
    int n = 0;
    for (int y = area.top; y < area.bottom; ++y) {
        for (int x = area.left; x < area.right; ++x) {
            const auto* p = capture.Pixel(x, y);
            const double l = 0.0722 * p[0] + 0.7152 * p[1] + 0.2126 * p[2];
            sum += l;
            sumSq += l * l;
            ++n;
        }
    }
    const double mean = sum / n;
    return sumSq / n - mean * mean;
}

void RunProbe(graphics::ThemeMode mode, bool blur, const char* name) {
    platform::MessageLoop loop;
    graphics::GraphicsDevice device;
    REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler;
    REQUIRE(scheduler.Initialize(loop).ok());

    const auto monitor = platform::PrimaryMonitor();
    const float scale = static_cast<float>(monitor.dpi) / 96.0f;
    const int width = static_cast<int>(std::lround(620 * scale));
    const int height = static_cast<int>(std::lround(120 * scale));
    RECT bounds{};
    bounds.left = (monitor.workArea.left + monitor.workArea.right - width) / 2;
    bounds.top = monitor.workArea.bottom - height - static_cast<int>(40 * scale);
    bounds.right = bounds.left + width;
    bounds.bottom = bounds.top + height;

    RECT captureArea = bounds;
    captureArea.top -= static_cast<int>(60 * scale);
    RECT guardedArea = captureArea;
    ::InflateRect(&guardedArea, 128, 128);
    struct BackdropCleanup { HWND hwnd; ~BackdropCleanup() { if (hwnd) ::DestroyWindow(hwnd); } } backdrop{CreateTestBackdrop(guardedArea)};
    REQUIRE(backdrop.hwnd != nullptr);
    RECT uncoveredArea = guardedArea; ++uncoveredArea.right;
    CHECK_FALSE(PrivateBackdropVisible(backdrop.hwnd, uncoveredArea));

    ProbeWindow window(device, scheduler, mode, blur);
    ui::SurfaceWindowOptions options;
    options.className = L"AetherShell.Tests.GlassProbe";
    options.title = L"Glass probe";
    REQUIRE(window.Create(options, bounds).ok());
    window.Show(false);

    ui::TooltipWindow tooltip(device, scheduler);
    REQUIRE(tooltip.Initialize().ok());
    tooltip.SetTheme(graphics::MakeDesignTokens(mode), graphics::MotionPolicy{});
    RECT anchor{bounds.left + static_cast<int>(40 * scale), bounds.top + static_cast<int>(36 * scale),
                bounds.left + static_cast<int>(88 * scale), bounds.top + static_cast<int>(84 * scale)};
    tooltip.ShowFor(L"Visual Studio Code", anchor, ui::TooltipPlacement::Above);

    RunLoopFor(loop, 1200);
    CHECK(window.frames >= 1);

    ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (tooltip.Handle()) ::SetWindowPos(tooltip.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RunLoopFor(loop, 100);
    CHECK_FALSE(PrivateBackdropVisible(backdrop.hwnd, guardedArea, 0)); // synthetic untrusted overlay classification
    const Capture capture = CapturePrivate(backdrop.hwnd, guardedArea, captureArea);
    const auto path = std::filesystem::temp_directory_path() / "AetherShellTests" / (std::string("probe-") + name + ".png");
    std::filesystem::create_directories(path.parent_path());
    SavePng(capture, path);
    TEST_NOTE(std::string("saved own-window probe-") + name + ".png blurActive=" + (window.BackdropBlurActive() ? "yes" : "no") +
              " frames=" + std::to_string(window.frames));

    // The panel interior (icons + text) must not be a flat color.
    const int offsetY = static_cast<int>(60 * scale);
    const RECT inside{static_cast<LONG>(40 * scale), offsetY + static_cast<LONG>(36 * scale),
                      static_cast<LONG>(400 * scale), offsetY + static_cast<LONG>(84 * scale)};
    CHECK(LuminanceVariance(capture, inside) > 20.0);

    // Change only our opaque backdrop, leaving the glass content untouched. A flat opaque panel
    // could pass a text/icon variance check; it cannot pass this composited-background dependency.
    ::SetWindowLongPtrW(backdrop.hwnd, GWLP_USERDATA, 1);
    REQUIRE(::InvalidateRect(backdrop.hwnd, nullptr, FALSE) != FALSE);
    ::UpdateWindow(backdrop.hwnd); RunLoopFor(loop, 300);
    const auto changed = CapturePrivate(backdrop.hwnd, guardedArea, captureArea);
    double difference = 0; int samples = 0;
    const int sampleY = offsetY + static_cast<int>(89 * scale);
    for (int x = static_cast<int>(45 * scale); x < static_cast<int>(570 * scale); ++x) {
        const auto* before = capture.Pixel(x, sampleY);
        const auto* after = changed.Pixel(x, sampleY);
        for (int channel = 0; channel < 3; ++channel) {
            difference += std::abs(static_cast<int>(before[channel]) - static_cast<int>(after[channel])); ++samples;
        }
    }
    difference /= samples;
    TEST_NOTE(std::string("own backdrop response mean RGB delta=") + std::to_string(difference));
    if (mode == graphics::ThemeMode::HighContrast) CHECK(difference < 2.0);
    else CHECK(difference > 8.0);

    tooltip.Destroy();
    window.Destroy();
    scheduler.Shutdown();
    device.Shutdown();
}

}  // namespace

AETHER_TEST("ui probe: dark glass with backdrop blur renders") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    RunProbe(graphics::ThemeMode::Dark, true, "dark-blur");
}

AETHER_TEST("ui probe: light glass without blur renders") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    RunProbe(graphics::ThemeMode::Light, false, "light-noblur");
}

AETHER_TEST("ui probe: high contrast stays opaque when the private backdrop changes") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    RunProbe(graphics::ThemeMode::HighContrast, false, "high-contrast");
}
