// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"

#include "core/FileSystem.h"
#include "dock/DockWindow.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "settings/SettingsService.h"
#include "ui/accessibility/AccessibleSurface.h"

#include <wincodec.h>
#include <dwmapi.h>
#include <shellapi.h>

#include <chrono>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace aether;
using namespace std::chrono_literals;

namespace {

class FakeLauncher final : public apps::IApplicationLauncher {
public:
    int launches = 0;
    int activations = 0;
    void LaunchAsync(const apps::ApplicationInfo&, std::function<void(Result<void>)> completion) override {
        ++launches; if (completion) completion(Ok());
    }
    Result<void> Activate(const apps::RunningApplication&) override { ++activations; return Ok(); }
};
class FakeIcons final : public apps::IIconProvider {
public:
    std::vector<std::wstring> keys;
    void RequestIcon(apps::IconRequest request, Callback callback) override {
        keys.push_back(request.key);
        if (request.app.displayName == L"Synthetic B") { callback(request.key, {}); return; }
        auto image = std::make_shared<apps::IconImage>();
        image->width = image->height = request.pixelSize;
        image->pixels.resize(static_cast<std::size_t>(image->width) * image->height * 4);
        for (std::uint32_t y = 0; y < image->height; ++y) for (std::uint32_t x = 0; x < image->width; ++x) {
            const float fx = (static_cast<float>(x) + 0.5f) / static_cast<float>(image->width);
            const float fy = (static_cast<float>(y) + 0.5f) / static_cast<float>(image->height);
            const float dx = std::max(0.0f, std::abs(fx - 0.5f) - 0.28f);
            const float dy = std::max(0.0f, std::abs(fy - 0.5f) - 0.28f);
            const bool inside = dx * dx + dy * dy < 0.22f * 0.22f;
            const auto at = (static_cast<std::size_t>(y) * image->width + x) * 4;
            if (inside) {
                image->pixels[at] = 210; image->pixels[at + 1] = 115; image->pixels[at + 2] = 55;
                image->pixels[at + 3] = 255;
                if (fx > 0.31f && fx < 0.69f && fy > 0.31f && fy < 0.69f) {
                    image->pixels[at] = 245; image->pixels[at + 1] = 240; image->pixels[at + 2] = 235;
                }
            }
        }
        callback(request.key, image);
    }
    void CancelAll() noexcept override {}
};

LRESULT CALLBACK BackdropProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message != WM_PAINT) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(hwnd, &paint); RECT client{}; ::GetClientRect(hwnd, &client);
    if (::GetWindowLongPtrW(hwnd, GWLP_USERDATA) == 1) {
        ::FillRect(dc, &client, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
        ::EndPaint(hwnd, &paint); return 0;
    }
    for (LONG x = 0; x < client.right; x += 32) {
        const int index = x / 32;
        HBRUSH brush = ::CreateSolidBrush(RGB(28 + index % 5 * 13, 42 + index % 4 * 14, 72 + index % 6 * 12));
        RECT stripe{x, 0, x + 32, client.bottom}; ::FillRect(dc, &stripe, brush); ::DeleteObject(brush);
    }
    ::EndPaint(hwnd, &paint); return 0;
}
class Backdrop {
public:
    explicit Backdrop(const RECT& bounds) {
        WNDCLASSW type{}; type.hInstance = ::GetModuleHandleW(nullptr); type.lpfnWndProc = BackdropProc;
        type.lpszClassName = L"AetherShell.Tests.DockBackdrop"; ::RegisterClassW(&type);
        hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName,
            L"Synthetic Dock backdrop", WS_POPUP, bounds.left, bounds.top, bounds.right - bounds.left,
            bounds.bottom - bounds.top, nullptr, nullptr, type.hInstance, nullptr);
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE); ::UpdateWindow(hwnd);
    }
    ~Backdrop() { if (hwnd) ::DestroyWindow(hwnd); }
    HWND hwnd = nullptr;
};

void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr); REQUIRE(timer != nullptr);
    LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE));
    const auto id = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    loop.Run(); loop.RemoveWaitHandle(id); ::CloseHandle(timer);
}

// Reject capture if a foreign window is above the opaque backdrop anywhere in the blur guard band.
// The backdrop itself must belong to this process and cover that whole band, not only the crop.
bool PrivateCrop(HWND backdrop, const RECT& crop) {
    DWORD backdropProcess=0;::GetWindowThreadProcessId(backdrop,&backdropProcess);
    RECT backdropBounds{};
    if(backdropProcess!=::GetCurrentProcessId() || !::IsWindowVisible(backdrop) || !::GetWindowRect(backdrop,&backdropBounds) ||
        backdropBounds.left>crop.left || backdropBounds.top>crop.top || backdropBounds.right<crop.right || backdropBounds.bottom<crop.bottom)return false;
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

struct Capture {
    int width = 0, height = 0;
    std::vector<std::uint8_t> pixels;
};
Capture CaptureOwn(HWND backdrop, HWND dockWindow, const RECT& crop) {
    RECT guarded=crop;::InflateRect(&guarded,128,128);
    // Reassert only our own fixture windows without activation. Cover the full guard band and
    // repaint its newly exposed pattern before composition settles; guards still run on both sides.
    REQUIRE(::SetWindowPos(backdrop,HWND_TOPMOST,guarded.left,guarded.top,guarded.right-guarded.left,guarded.bottom-guarded.top,SWP_NOACTIVATE));
    ::UpdateWindow(backdrop);
    REQUIRE(::SetWindowPos(dockWindow,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE));
    REQUIRE(SUCCEEDED(::DwmFlush()));
    RECT uncovered=guarded;++uncovered.right;
    CHECK_FALSE(PrivateCrop(backdrop,uncovered));
    REQUIRE(PrivateCrop(backdrop, guarded));
    Capture image; image.width = crop.right - crop.left; image.height = crop.bottom - crop.top;
    HDC screen = ::GetDC(nullptr); HDC memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = image.width; info.bmiHeader.biHeight = -image.height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = ::SelectObject(memory, bitmap);
    const bool copied = ::BitBlt(memory, 0, 0, image.width, image.height, screen, crop.left, crop.top, SRCCOPY | CAPTUREBLT) != FALSE;
    if (copied) image.pixels.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) +
        static_cast<std::size_t>(image.width) * image.height * 4);
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen);
    REQUIRE(copied); REQUIRE(PrivateCrop(backdrop, guarded));
    return image;
}
void SavePng(const Capture& capture, const std::filesystem::path& path) {
    auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICStream> stream; winrt::check_hresult(factory->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder; winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame; winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    winrt::check_hresult(frame->Initialize(nullptr)); winrt::check_hresult(frame->SetSize(capture.width, capture.height));
    auto pixels = capture.pixels; for (std::size_t index = 3; index < pixels.size(); index += 4) pixels[index] = 255;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA; winrt::check_hresult(frame->SetPixelFormat(&format));
    REQUIRE(format == GUID_WICPixelFormat32bppBGRA);
    winrt::check_hresult(frame->WritePixels(capture.height, capture.width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    winrt::check_hresult(frame->Commit()); winrt::check_hresult(encoder->Commit());
}
std::size_t BluePixels(const Capture& image) {
    std::size_t count = 0;
    for (std::size_t at = 0; at < image.pixels.size(); at += 4) {
        if (image.pixels[at] > 195 && image.pixels[at] < 225 && image.pixels[at + 1] > 100 &&
            image.pixels[at + 1] < 130 && image.pixels[at + 2] > 40 && image.pixels[at + 2] < 70) ++count;
    }
    return count;
}
LPARAM Point(const dock::DockWindow& window, const D2D1_RECT_F& rect) {
    return MAKELPARAM(static_cast<WORD>(std::lround(window.ToPx((rect.left + rect.right) / 2))),
        static_cast<WORD>(std::lround(window.ToPx((rect.top + rect.bottom) / 2))));
}

void Probe(graphics::ThemeMode theme, const std::string& prefix) {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    platform::MessageLoop loop; graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok());
    ManualDispatcher dispatcher;
    const auto file = test::MakeTempDirectory("dock-settings") / L"settings.json";
    bool loaded = false, saved = false;
    settings::SettingsService settings(file, dispatcher);
    REQUIRE(settings.Load([&](Result<settings::LoadReport> result) { loaded = result.ok(); }).ok());
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!loaded && std::chrono::steady_clock::now() < deadline) { dispatcher.RunPending(); std::this_thread::sleep_for(1ms); }
    REQUIRE(loaded);
    REQUIRE(settings.Update([](settings::Settings& value) {
        settings::PinnedApp a; a.id = L"exe:synthetic-a"; a.displayName = L"Synthetic A";
        settings::PinnedApp b; b.id = L"exe:synthetic-b"; b.displayName = L"Synthetic B";
        a.parsingName=L"Synthetic.Application.A";b.parsingName=L"Synthetic.Application.B";
        value.dock.pinned = {a, b}; value.dock.reserveSpace = true;
        value.dock.showStartButton=false;value.dock.showTaskViewButton=false;
    }, [&](Result<void> result) { saved = result.ok(); }).ok());
    while (!saved && std::chrono::steady_clock::now() < deadline) { dispatcher.RunPending(); std::this_thread::sleep_for(1ms); }
    REQUIRE(saved);
    FakeLauncher launcher; FakeIcons icons;
    const auto monitor = platform::PrimaryMonitor();
    const LONG width = std::min<LONG>(1000, monitor.workArea.right - monitor.workArea.left - 100);
    const LONG x = (monitor.workArea.left + monitor.workArea.right - width) / 2;
    // Keep the capture guard and the complete shadow/entrance HWND below-margin above the real
    // taskbar at the monitor's DPI. The capture guards themselves are unchanged.
    const LONG bottomInset=144+static_cast<LONG>(std::ceil((dock::kDockShadowMargin+dock::kDockEntranceOffset)*monitor.dpi/96.0f));
    const RECT area{x, monitor.workArea.bottom - bottomInset - 260, x + width, monitor.workArea.bottom - bottomInset};
    Backdrop backdrop(area); REQUIRE(backdrop.hwnd != nullptr);
    dock::DockWindow window({device, scheduler, dispatcher, launcher, icons, settings});
    dock::DockWindowOptions options; options.workAreaPx = area; options.allowWorkAreaReservation = false; options.owner = backdrop.hwnd;
    options.replacementTaskbarMode = true;
    REQUIRE(window.Create(options).ok());
    const auto replacementBounds = window.BoundsPx();
    REQUIRE(window.SetTaskbarReplacement(false).ok());
    REQUIRE(window.SetTaskbarReplacement(true).ok());
    const auto restoredBounds = window.BoundsPx();
    CHECK(::EqualRect(&replacementBounds, &restoredBounds));
    const auto inputFilter=loop.AddMessageFilter([hwnd=window.Handle()](MSG& message) {
        // Synthetic input is sent directly. Discard only queued real-cursor moves/leaves for this
        // own test window so the stationary physical cursor cannot change the resting fixture.
        return message.hwnd==hwnd && (message.message==WM_MOUSEMOVE || message.message==WM_MOUSELEAVE);
    });
    const auto tokens = graphics::MakeDesignTokens(theme);
    REQUIRE(window.SetTheme(tokens, graphics::MotionPolicy{}, settings.Current().dock).ok());
    apps::RunningApplication running; running.id = L"exe:synthetic-c"; running.displayName = L"Synthetic C";
    running.foreground = true; running.windows.push_back({reinterpret_cast<HWND>(1), L"Synthetic window", false});
    REQUIRE(window.SetRunningApplications({running}).ok());
    window.Show(false); ::SendMessageW(window.Handle(), WM_MOUSELEAVE, 0, 0); RunFor(loop, 300);
    auto restGeometry = window.Geometry(); REQUIRE(restGeometry.ok()); REQUIRE_EQ(restGeometry->items.size(), 4u);
    // Native exclusion is required for cross-process click-through; HTTRANSPARENT is insufficient.
    const int nativePanelLeft = static_cast<int>(std::floor(window.ToPx(restGeometry->panel.left)));
    const int nativePanelY = static_cast<int>(std::floor(window.ToPx((restGeometry->panel.top + restGeometry->panel.bottom) / 2)));
    REQUIRE(nativePanelLeft > 0); // This point was inside the former shadow-expanded native region.
    HRGN restRegion = ::CreateRectRgn(0, 0, 0, 0); REQUIRE(restRegion != nullptr);
    CHECK(::GetWindowRgn(window.Handle(), restRegion) != ERROR);
    CHECK_FALSE(::PtInRegion(restRegion, nativePanelLeft - 1, nativePanelY));
    CHECK(::PtInRegion(restRegion, nativePanelLeft + 1, nativePanelY));
    ::DeleteObject(restRegion);
    const double panelBottom = window.BoundsPx().top + window.ToPx(restGeometry->panel.bottom);
    CHECK(std::abs(panelBottom - (area.bottom - window.ToPx(static_cast<float>(settings.Current().dock.bottomMargin)))) <= 1.0);
    const auto rest = CaptureOwn(backdrop.hwnd, window.Handle(), window.BoundsPx());
    const auto output = std::filesystem::temp_directory_path() / L"AetherShellTests";
    std::filesystem::create_directories(output);
    SavePng(rest, output / ("dock-" + prefix + "-rest.png"));
    CHECK(BluePixels(rest) > 300);
    ::SendMessageW(window.Handle(), WM_MOUSEMOVE, 0, Point(window, restGeometry->items[1].icon));
    // TrackMouseEvent consults the real cursor, which tests must never move. Cancel only this test
    // window's tracking and remove a leave message already queued for that stationary real cursor.
    TRACKMOUSEEVENT cancel{sizeof(cancel), TME_CANCEL | TME_LEAVE, window.Handle(), 0};
    ::TrackMouseEvent(&cancel);
    MSG leave{};
    while (::PeekMessageW(&leave, window.Handle(), WM_MOUSELEAVE, WM_MOUSELEAVE, PM_REMOVE)) {}
    RunFor(loop, 350);
    auto hoverGeometry = window.Geometry(); REQUIRE(hoverGeometry.ok());
    const auto hover = CaptureOwn(backdrop.hwnd, window.Handle(), window.BoundsPx());
    SavePng(hover, output / ("dock-" + prefix + "-hover.png"));
    const auto span = [](const D2D1_RECT_F& icon) { return icon.right - icon.left; };
    CHECK(span(hoverGeometry->items[1].icon) > span(restGeometry->items[1].icon) * 1.25f);
    CHECK(BluePixels(hover) > BluePixels(rest));

    // The exact native input region includes each raised icon's rectangular slot. Exterior panel
    // shadows must not leak into transparent bitmap corners and become square grey patches there.
    // Sample only our synthetic icon's known-transparent upper-left pixels, above the glass panel,
    // over a solid private white backdrop; genuine magnification and native hit regions stay intact.
    ::SetWindowLongPtrW(backdrop.hwnd, GWLP_USERDATA, 1);
    ::InvalidateRect(backdrop.hwnd, nullptr, TRUE); ::UpdateWindow(backdrop.hwnd);
    RunFor(loop, 50);
    const auto whiteHover = CaptureOwn(backdrop.hwnd, window.Handle(), window.BoundsPx());
    const auto& raised = hoverGeometry->items[1].icon;
    const int clearLeft = static_cast<int>(std::ceil(window.ToPx(raised.left))) + 2;
    const int clearTop = static_cast<int>(std::ceil(window.ToPx(raised.top))) + 2;
    REQUIRE(clearTop + 2 < static_cast<int>(std::floor(window.ToPx(hoverGeometry->panel.top))) - 2);
    int worstClearChannel = 255;
    for (int sampleY = clearTop; sampleY < clearTop + 3; ++sampleY)
        for (int sampleX = clearLeft; sampleX < clearLeft + 3; ++sampleX) {
        REQUIRE(sampleX >= 0 && sampleX < whiteHover.width && sampleY >= 0 && sampleY < whiteHover.height);
        const auto at = (static_cast<std::size_t>(sampleY) * whiteHover.width + sampleX) * 4;
        for (std::size_t channel = 0; channel < 3; ++channel)
            worstClearChannel = std::min(worstClearChannel, static_cast<int>(whiteHover.pixels[at + channel]));
    }
    TEST_NOTE("raised-icon transparent corner minimum white channel: " + std::to_string(worstClearChannel));
    CHECK(worstClearChannel >= 250);
    ::SetWindowLongPtrW(backdrop.hwnd, GWLP_USERDATA, 0);
    ::InvalidateRect(backdrop.hwnd, nullptr, TRUE); ::UpdateWindow(backdrop.hwnd);

    // Motion disabled makes deterministic clicks use exactly the drawn geometry. All actions route
    // to fakes: this test never launches or activates another application.
    REQUIRE(window.SetTheme(tokens, graphics::MotionPolicy{true}, settings.Current().dock).ok());
    ::SendMessageW(window.Handle(), WM_MOUSELEAVE, 0, 0); RunFor(loop, 100);
    int searches = 0; auto connection = window.SearchRequested.Connect([&] { ++searches; });
    const auto current = window.Geometry(); REQUIRE(current.ok());
    for (std::size_t index = 0; index < 4; ++index) {
        if (index == 2) continue;
        const auto point = Point(window, current->items[index].icon);
        ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, 0, point);
        ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, point);
    }
    CHECK_EQ(searches, 1); CHECK_EQ(launcher.launches, 1); CHECK_EQ(launcher.activations, 1);
    ::SendMessageW(window.Handle(), WM_MBUTTONUP, 0, Point(window, current->items[1].icon)); CHECK_EQ(launcher.launches, 2);
    ::SendMessageW(window.Handle(), WM_KEYDOWN, VK_HOME, 0); ::SendMessageW(window.Handle(), WM_KEYDOWN, VK_RETURN, 0);
    CHECK_EQ(searches, 2);
    REQUIRE(window.SetSuppressed(true).ok()); CHECK_FALSE(window.IsShown());
    REQUIRE(window.SetSuppressed(false).ok()); CHECK(window.IsShown());
    ::SendMessageW(window.Handle(), WM_MOUSELEAVE, 0, 0); RunFor(loop, 500);
    // Showing a layered native window can queue a final WM_PAINT. Establish an idle interval
    // before checking that an unchanged running-app notification does not request another frame.
    auto previous = window.RenderedFrames(); bool idle = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        RunFor(loop, 150);
        if (window.RenderedFrames() == previous) { idle = true; break; }
        previous = window.RenderedFrames();
    }
    REQUIRE(idle);
    const auto settled = window.RenderedFrames();
    REQUIRE(window.SetRunningApplications({running}).ok());
    RunFor(loop, 200); CHECK_EQ(window.RenderedFrames(), settled);
    REQUIRE(window.SetTheme(tokens, graphics::MotionPolicy{}, settings.Current().dock).ok());
    RunFor(loop, 150);
    const auto beforeDrag = window.Geometry(); REQUIRE(beforeDrag.ok());
    ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, 0, Point(window, beforeDrag->items[1].icon));
    ::SendMessageW(window.Handle(), WM_MOUSEMOVE, MK_LBUTTON, Point(window, beforeDrag->items[2].icon));
    ::TrackMouseEvent(&cancel);
    while (::PeekMessageW(&leave, window.Handle(), WM_MOUSELEAVE, WM_MOUSELEAVE, PM_REMOVE)) {}
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, Point(window, beforeDrag->items[2].icon));
    REQUIRE_EQ(settings.Current().dock.pinned.size(), 2u);
    CHECK_EQ(settings.Current().dock.pinned[0].displayName, L"Synthetic B");
    // The captured-drag integration below checks live neighbour displacement and settling. This
    // synthetic message sequence checks the committed order and eventual slot geometry.
    ::SendMessageW(window.Handle(), WM_MOUSELEAVE, 0, 0); RunFor(loop, 1200);
    const auto afterDrag = window.Geometry(); REQUIRE(afterDrag.ok());
    CHECK(afterDrag->items[1].icon.left < afterDrag->items[2].icon.left);
    // Running icons move in the session, without silently becoming pins. The release is consumed.
    REQUIRE(window.SetTheme(tokens,graphics::MotionPolicy{true},settings.Current().dock).ok());
    const auto beforeRunningDrag=window.Geometry();REQUIRE(beforeRunningDrag.ok());
    const int launchesBeforeDrag=launcher.launches,activationsBeforeDrag=launcher.activations;
    ::SendMessageW(window.Handle(),WM_LBUTTONDOWN,0,Point(window,beforeRunningDrag->items[3].icon));
    ::SendMessageW(window.Handle(),WM_MOUSEMOVE,MK_LBUTTON,Point(window,beforeRunningDrag->items[1].icon));
    ::TrackMouseEvent(&cancel);
    while(::PeekMessageW(&leave,window.Handle(),WM_MOUSELEAVE,WM_MOUSELEAVE,PM_REMOVE)){}
    ::SendMessageW(window.Handle(),WM_LBUTTONUP,0,Point(window,beforeRunningDrag->items[1].icon));
    REQUIRE_EQ(settings.Current().dock.pinned.size(),2u);
    CHECK_EQ(settings.Current().dock.pinned[0].displayName,L"Synthetic B");
    CHECK_EQ(settings.Current().dock.pinned[1].displayName,L"Synthetic A");
    CHECK_EQ(launcher.launches,launchesBeforeDrag);CHECK_EQ(launcher.activations,activationsBeforeDrag);
    const auto sessionGeometry=window.Geometry();REQUIRE(sessionGeometry.ok());
    ::SendMessageW(window.Handle(),WM_LBUTTONDOWN,0,Point(window,sessionGeometry->items[1].icon));
    ::SendMessageW(window.Handle(),WM_LBUTTONUP,0,Point(window,sessionGeometry->items[1].icon));
    CHECK_EQ(launcher.activations,activationsBeforeDrag+1); // the moved group now owns the drawn slot
    CHECK_EQ(launcher.launches,launchesBeforeDrag);
    const auto saveDeadline=std::chrono::steady_clock::now()+5s;
    while(settings.Dirty() && std::chrono::steady_clock::now()<saveDeadline){dispatcher.RunPending();std::this_thread::sleep_for(1ms);}
    CHECK_FALSE(settings.Dirty());
    const auto after = platform::PrimaryMonitor().workArea;
    CHECK(::EqualRect(&after, &monitor.workArea));
    std::atomic<bool> accessibilityFinished{false};
    bool accessibilityPassed = false;
    int accessibilityStage = 0;
    HRESULT accessibilityFocus = E_PENDING;
    CONTROLTYPEID accessibilityRootType{};
    HRESULT accessibilityRootTypeResult = E_PENDING;
    HRESULT accessibilityRootHandleResult = E_PENDING;
    bool accessibilityRootHandleMatches = false;
    const auto hwnd = window.Handle();
    const int beforeInvoke = searches;
    std::thread client([&] {
        platform::ComApartment mta(COINIT_MULTITHREADED);
        const auto check = [](HRESULT result) { if (FAILED(result)) throw std::runtime_error("own Dock accessibility operation failed"); };
        try {
            accessibilityStage = 1;
            winrt::com_ptr<IUIAutomation> automation;
            check(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()));
            winrt::com_ptr<IUIAutomationElement> root;
            accessibilityStage = 2;
            check(automation->ElementFromHandle(hwnd, root.put()));
            accessibilityRootTypeResult = root->get_CurrentControlType(&accessibilityRootType);
            check(accessibilityRootTypeResult);
            accessibilityStage = 3;
            if (accessibilityRootType != UIA_ToolBarControlTypeId) {
                UIA_HWND observedHandle{};
                accessibilityRootHandleResult = root->get_CurrentNativeWindowHandle(&observedHandle);
                accessibilityRootHandleMatches = SUCCEEDED(accessibilityRootHandleResult) && observedHandle == hwnd;
                throw std::runtime_error("Dock root type mismatch");
            }
            winrt::com_ptr<IUIAutomationCondition> all; check(automation->CreateTrueCondition(all.put()));
            accessibilityStage = 4;
            winrt::com_ptr<IUIAutomationElementArray> children; check(root->FindAll(TreeScope_Children, all.get(), children.put()));
            int count{}; check(children->get_Length(&count));
            if (count != 4) throw std::runtime_error("Dock navigation count mismatch");
            winrt::com_ptr<IUIAutomationElement> search; check(children->GetElement(0, search.put()));
            accessibilityStage = 5;
            BSTR name{}; check(search->get_CurrentName(&name)); const std::wstring label(name ? name : L""); ::SysFreeString(name);
            if (label != L"Search applications") throw std::runtime_error("Dock accessible label mismatch");
            winrt::com_ptr<IUIAutomationInvokePattern> invoke;
            accessibilityStage = 6;
            check(search->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), invoke.put_void()));
            check(invoke->Invoke()); accessibilityStage = 7; accessibilityFocus = search->SetFocus();
            // A background test cannot grant foreground permission. A refused request must be an
            // explicit denial and must restore the no-activate style rather than claim focus.
            if (accessibilityFocus == E_ACCESSDENIED) {
                if (!(::GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_NOACTIVATE)) throw std::runtime_error("Dock focus denial changed activation policy");
            } else check(accessibilityFocus);
            DWORD_PTR closed{};
            accessibilityStage = 8;
            if (!::SendMessageTimeoutW(hwnd, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG, 3000, &closed)) throw std::runtime_error("Dock close failed");
            BSTR disconnected{}; const auto result = root->get_CurrentName(&disconnected); ::SysFreeString(disconnected);
            accessibilityStage = 9;
            if (result != UIA_E_ELEMENTNOTAVAILABLE) throw std::runtime_error("Dock provider remained available after close");
            accessibilityPassed = true;
        } catch (...) { /* A fixed assertion below reports failure without private UIA text. */ }
        accessibilityFinished.store(true);
    });
    while (!accessibilityFinished.load()) RunFor(loop, 20);
    client.join(); CHECK(accessibilityPassed); CHECK_EQ(searches, beforeInvoke + 1);
    if (!accessibilityPassed) TEST_NOTE("Dock UIA failed stage " + std::to_string(accessibilityStage));
    if (!accessibilityPassed && accessibilityStage == 3) {
        TEST_NOTE("Dock UIA root type " + std::to_string(accessibilityRootType) +
            "; type HRESULT " + std::to_string(accessibilityRootTypeResult) +
            "; native handle HRESULT " + std::to_string(accessibilityRootHandleResult) +
            "; native handle matches requested HWND " + std::to_string(accessibilityRootHandleMatches));
    }
    if (accessibilityFocus==E_ACCESSDENIED) TEST_NOTE("background UIA focus was explicitly denied by Windows; no-activate restored");
    TEST_NOTE("saved own-window Dock " + prefix + " rest/hover PNGs; synthetic actions and idle checked");
    window.Destroy();
    loop.RemoveMessageFilter(inputFilter);
}

}  // namespace

AETHER_TEST("dock integration: dark native Dock renders synthetic icons magnifies and routes input safely") {
    Probe(graphics::ThemeMode::Dark, "dark");
}
AETHER_TEST("dock integration: light native Dock renders synthetic icons and becomes idle") {
    Probe(graphics::ThemeMode::Light, "light");
}

AETHER_TEST("dock integration: fixed actions menus profile refresh and explicit unpin stay isolated") {
    class FakeMenu final : public ui::IMenuPresenter {
    public:
        std::uint32_t choice=0;
        std::vector<ui::MenuEntry> entries;
        ui::MenuRequest request;
        std::function<void()> during;
        Result<ui::MenuSelection> Show(std::span<const ui::MenuEntry> items,const ui::MenuRequest& value) override {
            entries.assign(items.begin(),items.end());request=value;
            if(during)during();
            return ui::MenuSelection{choice,nullptr};
        }
    } menu;
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    platform::MessageLoop loop;graphics::GraphicsDevice device;REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler;REQUIRE(scheduler.Initialize(loop).ok());ManualDispatcher dispatcher;
    settings::SettingsService settings(test::MakeTempDirectory("dock-actions")/L"settings.json",dispatcher);
    REQUIRE(settings.Load({}).ok());
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(!settings.Loaded() && std::chrono::steady_clock::now()<deadline){dispatcher.RunPending();std::this_thread::sleep_for(1ms);}
    REQUIRE(settings.Loaded());
    FakeLauncher launcher;FakeIcons icons;
    dock::DockWindow window({device,scheduler,dispatcher,launcher,icons,settings,&menu});
    const RECT area{0,0,1000,600};RECT workBefore{};REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA,0,&workBefore,0));
    dock::DockWindowOptions options;options.workAreaPx=area;options.allowWorkAreaReservation=false;
    REQUIRE(window.Create(options).ok());
    REQUIRE(window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark),graphics::MotionPolicy{true},settings.Current().dock).ok());
    window.Show(false);
    apps::RunningApplication running;running.id=L"aumid:Synthetic.Profile";running.appUserModelId=L"Synthetic.Profile";
    running.displayName=L"Synthetic profile";running.executablePath=LR"(C:\AetherFixture\browser.exe)";
    running.relaunchIconResource=LR"(C:\AetherFixture\avatar.ico,0)";
    running.windows.push_back({reinterpret_cast<HWND>(1),L"Synthetic window",false});
    REQUIRE(window.SetRunningApplications({running}).ok());
    int starts=0,searches=0,taskViews=0;
    auto start=window.StartRequested.Connect([&]{++starts;});
    auto search=window.SearchRequested.Connect([&]{++searches;});
    auto taskView=window.TaskViewRequested.Connect([&]{++taskViews;});
    auto geometry=window.Geometry();REQUIRE(geometry.ok());REQUIRE_EQ(geometry->items.size(),4u);
    const auto visibleTooltips=[] {
        int count=0;
        ::EnumThreadWindows(::GetCurrentThreadId(),[](HWND handle,LPARAM contextValue)->BOOL {
            wchar_t name[256]{};const int length=::GetClassNameW(handle,name,256);
            if(::IsWindowVisible(handle) && length>0 &&
                std::wstring_view(name,static_cast<std::size_t>(length))==L"AetherShell.Tooltip")
                ++*reinterpret_cast<int*>(contextValue);
            return TRUE;
        },reinterpret_cast<LPARAM>(&count));
        return count;
    };
    ::SendMessageW(window.Handle(),WM_MOUSEMOVE,0,Point(window,geometry->items[0].icon));
    ::SendMessageW(window.Handle(),WM_TIMER,1,0);CHECK_EQ(visibleTooltips(),1);
    ::SendMessageW(window.Handle(),WM_APP+113,ABN_FULLSCREENAPP,TRUE);
    CHECK(window.IsShown()); // a raw shell hint cannot replace the host's combined policy
    REQUIRE(window.SetSuppressed(true).ok());
    ::SendMessageW(window.Handle(),WM_APP+113,ABN_FULLSCREENAPP,FALSE);
    CHECK_FALSE(window.IsShown()); // leaving fullscreen cannot cancel a native-taskbar reveal
    ::SendMessageW(window.Handle(),WM_TIMER,1,0);CHECK_EQ(visibleTooltips(),0);
    REQUIRE(window.SetSuppressed(false).ok());
    ::EnableWindow(window.Handle(),FALSE);
    ::SendMessageW(window.Handle(),WM_MOUSEMOVE,0,Point(window,geometry->items[0].icon));
    ::SendMessageW(window.Handle(),WM_TIMER,1,0);CHECK_EQ(visibleTooltips(),0);
    ::EnableWindow(window.Handle(),TRUE);
    geometry=window.Geometry();REQUIRE(geometry.ok());
    HRGN region=::CreateRectRgn(0,0,0,0);REQUIRE(region!=nullptr);
    const int regionKind=::GetWindowRgn(window.Handle(),region);
    CHECK(regionKind!=ERROR);CHECK_FALSE(::PtInRegion(region,0,0));::DeleteObject(region);
    for(std::size_t i=0;i<3;++i){
        const auto point=Point(window,geometry->items[i].icon);
        ::SendMessageW(window.Handle(),WM_LBUTTONDOWN,0,point);::SendMessageW(window.Handle(),WM_LBUTTONUP,0,point);
    }
    CHECK_EQ(starts,1);CHECK_EQ(searches,1);CHECK_EQ(taskViews,1);CHECK_EQ(launcher.launches,0);
    window.Hide();
    REQUIRE(window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark),graphics::MotionPolicy{},settings.Current().dock).ok());
    window.Show(false);
    if(window.IsTransitioning()) {
        ::SendMessageW(window.Handle(),WM_LBUTTONDOWN,0,Point(window,geometry->items[0].icon));
        ::SendMessageW(window.Handle(),WM_LBUTTONUP,0,Point(window,geometry->items[0].icon));
        CHECK_EQ(starts,1);
    }
    REQUIRE(window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark),graphics::MotionPolicy{true},settings.Current().dock).ok());
    CHECK_FALSE(window.IsTransitioning());geometry=window.Geometry();REQUIRE(geometry.ok());
    ::SendMessageW(window.Handle(),WM_MBUTTONUP,0,Point(window,geometry->items[3].icon));CHECK_EQ(launcher.launches,0);
    ::SendMessageW(window.Handle(),WM_RBUTTONUP,0,Point(window,geometry->items[3].icon));
    REQUIRE(ui::FindMenuCommand(menu.entries,2));CHECK_FALSE(ui::FindMenuCommand(menu.entries,2)->enabled);
    REQUIRE(ui::FindMenuCommand(menu.entries,3));CHECK_FALSE(ui::FindMenuCommand(menu.entries,3)->enabled);
    CHECK_EQ(menu.request.placement,ui::MenuPlacement::AboveAnchor);CHECK_EQ(menu.request.restore,ui::ForegroundRestore::Always);
    const auto oldKey=icons.keys.back();const auto requestCount=icons.keys.size();
    apps::ApplicationInfo profile;profile.id=running.id;profile.appUserModelId=running.appUserModelId;
    profile.displayName=running.displayName;profile.shortcutPath=LR"(C:\AetherFixture\trusted-profile.lnk)";
    profile.arguments=L"--profile=Synthetic";
    REQUIRE(window.SetApplicationCatalog({profile}).ok());
    CHECK(icons.keys.size()>requestCount);CHECK(icons.keys.back()!=oldKey);
    geometry=window.Geometry();REQUIRE(geometry.ok());
    ::EnableWindow(window.Handle(),FALSE);
    ::SendMessageW(window.Handle(),WM_MBUTTONUP,0,Point(window,geometry->items[3].icon));CHECK_EQ(launcher.launches,0);
    ::EnableWindow(window.Handle(),TRUE);
    menu.choice=3;::SendMessageW(window.Handle(),WM_RBUTTONUP,0,Point(window,geometry->items[3].icon));
    REQUIRE_EQ(settings.Current().dock.pinned.size(),1u);CHECK_EQ(settings.Current().dock.pinned[0].arguments,profile.arguments);
    geometry=window.Geometry();REQUIRE(geometry.ok());
    ::SendMessageW(window.Handle(),WM_RBUTTONUP,0,Point(window,geometry->items[3].icon));
    CHECK(settings.Current().dock.pinned.empty());REQUIRE_EQ(settings.Current().dock.dismissedPins.size(),1u);
    CHECK_EQ(settings.Current().dock.dismissedPins[0],running.id);
    REQUIRE(window.SetRunningApplications({}).ok());REQUIRE(window.SetRunningApplications({running}).ok());
    CHECK(settings.Current().dock.pinned.empty());
    geometry=window.Geometry();REQUIRE(geometry.ok());
    menu.choice=1;menu.during=[&]{window.Hide();};
    const int beforeModal=launcher.activations;
    ::SendMessageW(window.Handle(),WM_RBUTTONUP,0,Point(window,geometry->items[3].icon));
    CHECK_EQ(launcher.activations,beforeModal);CHECK_EQ(launcher.launches,0);
    RECT workAfter{};REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA,0,&workAfter,0));CHECK(::EqualRect(&workBefore,&workAfter));
    window.Destroy();
}

AETHER_TEST("dock integration: captured drag follows continuously and neighbour drop and attention motions settle") {
    platform::ComApartment com(COINIT_APARTMENTTHREADED);
    platform::MessageLoop loop; graphics::GraphicsDevice device; REQUIRE(device.Initialize().ok());
    graphics::FrameScheduler scheduler; REQUIRE(scheduler.Initialize(loop).ok()); ManualDispatcher dispatcher;
    settings::SettingsService settings(test::MakeTempDirectory("dock-motion") / L"settings.json", dispatcher);
    REQUIRE(settings.Load({}).ok());
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!settings.Loaded() && std::chrono::steady_clock::now() < deadline) { dispatcher.RunPending(); std::this_thread::sleep_for(1ms); }
    REQUIRE(settings.Loaded());
    REQUIRE(settings.Update([](settings::Settings& value) {
        value.dock.showStartButton = false; value.dock.showTaskViewButton = false;
        value.dock.magnification = 1; // isolate horizontal gesture/lift continuity from hover scale
        for (const auto* name : {L"A", L"B", L"C"}) {
            settings::PinnedApp pin; pin.id = std::wstring(L"exe:motion-") + name;
            pin.displayName = std::wstring(L"Synthetic ") + name; pin.parsingName = std::wstring(L"Synthetic.Motion.") + name;
            value.dock.pinned.push_back(std::move(pin));
        }
    }).ok());
    FakeLauncher launcher; FakeIcons icons;
    RECT workBefore{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workBefore, 0));
    const RECT area{0, 0, 1000, 600}; Backdrop backdrop(area); REQUIRE(backdrop.hwnd);
    dock::DockWindow window({device, scheduler, dispatcher, launcher, icons, settings});
    dock::DockWindowOptions options; options.workAreaPx = area; options.allowWorkAreaReservation = false; options.owner = backdrop.hwnd;
    REQUIRE(window.Create(options).ok());
    REQUIRE(window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), {}, settings.Current().dock).ok());
    const auto filter = loop.AddMessageFilter([hwnd = window.Handle()](MSG& message) {
        return message.hwnd == hwnd && (message.message == WM_MOUSEMOVE || message.message == WM_MOUSELEAVE);
    });
    window.Show(false); RunFor(loop, 350);
    const auto geometry = [&]() { auto value = window.Geometry(); REQUIRE(value.ok()); return *value; };
    const auto center = [](const D2D1_RECT_F& rect) { return (rect.left + rect.right) / 2; };
    const auto at = [&](float x, float y) { return MAKELPARAM(static_cast<WORD>(std::lround(window.ToPx(x))), static_cast<WORD>(std::lround(window.ToPx(y)))); };
    const auto start = geometry(); REQUIRE_EQ(start.items.size(), 4u);
    const auto initialA = start.items[1].icon, initialB = start.items[2].icon, initialC = start.items[3].icon;
    const float y = (initialA.top + initialA.bottom) / 2;
    ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, 0, at(center(initialA), y));
    CHECK_EQ(::GetCapture(), window.Handle());
    auto pressed = geometry();
    CHECK(std::abs(pressed.items[1].icon.left - initialA.left) < 0.01f); // no instant press-scale jump
    RunFor(loop, 35); pressed = geometry();
    CHECK(pressed.items[1].icon.right - pressed.items[1].icon.left < initialA.right - initialA.left);
    CHECK(pressed.items[1].icon.right - pressed.items[1].icon.left > (initialA.right - initialA.left) * 0.90f);
    const float firstX = center(initialB) + 3;
    ::SendMessageW(window.Handle(), WM_MOUSEMOVE, MK_LBUTTON, at(firstX, y));
    auto crossed = geometry(); REQUIRE_EQ(settings.Current().dock.pinned[0].id, L"exe:motion-B");
    REQUIRE(crossed.raisedItem.has_value()); CHECK_EQ(*crossed.raisedItem, 2u);
    CHECK(std::abs(center(crossed.items[2].icon) - firstX) <= 1.0f);
    CHECK(std::abs(center(crossed.items[1].icon) - center(initialB)) < 0.05f); // neighbour does not teleport
    const float liftStart = crossed.items[2].icon.top;
    RunFor(loop, 35); const auto sliding = geometry();
    CHECK(center(sliding.items[1].icon) < center(initialB));
    CHECK(center(sliding.items[1].icon) > center(initialA));
    CHECK(sliding.items[2].icon.top < liftStart); // lift begins over time, not at pointer-down
    const float neighbourBefore = center(sliding.items[1].icon);
    const float secondX = center(initialC) + 14;
    ::SendMessageW(window.Handle(), WM_MOUSEMOVE, MK_LBUTTON, at(secondX, y));
    crossed = geometry(); REQUIRE_EQ(settings.Current().dock.pinned[2].id, L"exe:motion-A");
    CHECK(std::abs(center(crossed.items[3].icon) - secondX) <= 1.0f);
    CHECK(std::abs(center(crossed.items[1].icon) - neighbourBefore) < 0.05f);
    apps::RunningApplication running; running.id = L"exe:motion-A"; running.displayName = L"Synthetic A";
    running.windows.push_back({reinterpret_cast<HWND>(1), L"Synthetic title", false});
    REQUIRE(window.SetRunningApplications({running}).ok()); // rebuild while original icon remains captured
    CHECK_EQ(::GetCapture(), window.Handle());
    CHECK(std::abs(center(geometry().items[3].icon) - secondX) <= 1.0f);
    RunFor(loop, 30); const auto beforeRelease = geometry();
    CHECK(center(beforeRelease.items[1].icon) < neighbourBefore); // momentum survives the second rebase
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, at(secondX, y));
    const auto released = geometry();
    CHECK(std::abs(center(released.items[3].icon) - center(beforeRelease.items[3].icon)) < 0.05f);
    CHECK(std::abs(released.items[3].icon.top - beforeRelease.items[3].icon.top) < 0.05f);
    RunFor(loop, 35); const auto settling = geometry();
    CHECK(center(settling.items[3].icon) < center(released.items[3].icon));
    CHECK(center(settling.items[3].icon) > center(initialC));
    ::SendMessageW(window.Handle(), WM_MOUSELEAVE, 0, 0); RunFor(loop, 1300);
    const auto settled = geometry(); CHECK(std::abs(center(settled.items[3].icon) - center(initialC)) < 0.05f);
    CHECK_FALSE(settled.raisedItem.has_value()); CHECK_EQ(launcher.launches, 0); CHECK_EQ(launcher.activations, 0);
    // A stationary hold is also a drag gesture and consumes release without requiring a double click.
    ::SendMessageW(window.Handle(), WM_LBUTTONDOWN, 0, Point(window, settled.items[3].icon));
    RunFor(loop, 440); CHECK(geometry().raisedItem.has_value());
    ::SendMessageW(window.Handle(), WM_LBUTTONUP, 0, Point(window, settled.items[3].icon));
    CHECK_EQ(launcher.activations, 0); RunFor(loop, 1200);
    const auto attentionRest = geometry(); running.attentionRequested = true;
    REQUIRE(window.SetRunningApplications({running}).ok()); RunFor(loop, 180);
    CHECK(geometry().items[3].icon.top < attentionRest.items[3].icon.top - 0.5f);
    RunFor(loop, 2700);
    // The pulse's elapsed-time budget is over, but a final native region update can still queue
    // WM_PAINT. Establish an observed idle baseline before attributing any new frame to the
    // unchanged notification. Continuous rendering must fail this bounded settling precondition.
    const auto settleDeadline = std::chrono::steady_clock::now() + 1200ms;
    auto previousFrames = window.RenderedFrames();
    int idleIntervals = 0;
    while (idleIntervals < 2 && std::chrono::steady_clock::now() < settleDeadline) {
        RunFor(loop, 150);
        const auto currentFrames = window.RenderedFrames();
        idleIntervals = currentFrames == previousFrames ? idleIntervals + 1 : 0;
        previousFrames = currentFrames;
    }
    REQUIRE_EQ(idleIntervals, 2);
    const auto frames = window.RenderedFrames();
    REQUIRE(window.SetRunningApplications({running}).ok()); RunFor(loop, 180);
    CHECK_EQ(window.RenderedFrames(), frames); // unchanged true request does not start another pulse
    REQUIRE(window.SetSuppressed(true).ok()); running.attentionRequested = false;
    REQUIRE(window.SetRunningApplications({running}).ok()); running.attentionRequested = true;
    REQUIRE(window.SetRunningApplications({running}).ok()); CHECK_FALSE(window.IsShown());
    REQUIRE(window.SetSuppressed(false).ok());
    REQUIRE(window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true}, settings.Current().dock).ok());
    RunFor(loop, 200); const auto reduced = geometry(); RunFor(loop, 200);
    CHECK(std::abs(geometry().items[3].icon.top - reduced.items[3].icon.top) < 0.01f);
    RECT workAfter{}; REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workAfter, 0)); CHECK(::EqualRect(&workBefore, &workAfter));
    window.Destroy(); loop.RemoveMessageFilter(filter);
}
