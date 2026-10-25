// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Own-window integration checks for T42/T43: hover emphasis, genuine DWM previews of this test's own
// windows, icon-to-card pointer continuity, selected-window normal close, refusal, stale identities,
// keyboard/UIA access, reduced motion/high contrast and idle frames. Every action routes to fakes or
// to windows created here; no owner application is touched, activated or closed. Captures contain
// only this process's windows above a private test-pattern backdrop.
#include "AetherTest.h"

#include "core/FileSystem.h"
#include "dock/DockMotion.h"
#include "dock/DockPreviewWindow.h"
#include "dock/DockWindow.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "settings/SettingsService.h"

#include <dwmapi.h>
#include <UIAutomation.h>
#include <wincodec.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <thread>

using namespace aether;
using namespace std::chrono_literals;

namespace {

constexpr const wchar_t* kIdA = L"exe:preview-a";
constexpr const wchar_t* kIdB = L"exe:preview-b";

class RecordingLauncher final : public apps::IApplicationLauncher {
public:
    std::vector<apps::RunningApplication> activated;
    int launches = 0;
    void LaunchAsync(const apps::ApplicationInfo&, std::function<void(Result<void>)> completion) override {
        ++launches; if (completion) completion(Ok());
    }
    Result<void> Activate(const apps::RunningApplication& app) override { activated.push_back(app); return Ok(); }
};
class NoIcons final : public apps::IIconProvider {
public:
    void RequestIcon(apps::IconRequest request, Callback callback) override { callback(request.key, {}); }
    void CancelAll() noexcept override {}
};

LRESULT CALLBACK PatternProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message != WM_PAINT) return ::DefWindowProcW(hwnd, message, wParam, lParam);
    PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(hwnd, &paint); RECT client{}; ::GetClientRect(hwnd, &client);
    for (LONG x = 0; x < client.right; x += 32) {
        const int index = x / 32;
        HBRUSH brush = ::CreateSolidBrush(RGB(28 + index % 5 * 13, 42 + index % 4 * 14, 72 + index % 6 * 12));
        RECT stripe{x, 0, x + 32, client.bottom}; ::FillRect(dc, &stripe, brush); ::DeleteObject(brush);
    }
    ::EndPaint(hwnd, &paint); return 0;
}

// A synthetic application window: solid color, optional refusal of WM_CLOSE (an app that cancels).
struct SourceWindow {
    HWND hwnd = nullptr;
    COLORREF color = RGB(0, 0, 0);
    bool refuse = false;
    int closeRequests = 0;
};
LRESULT CALLBACK SourceProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE)
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    auto* self = reinterpret_cast<SourceWindow*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && message == WM_CLOSE) {
        ++self->closeRequests;
        if (!self->refuse) ::DestroyWindow(hwnd);
        return 0;
    }
    if (self && message == WM_NCDESTROY) self->hwnd = nullptr;
    if (self && message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(hwnd, &paint); RECT client{}; ::GetClientRect(hwnd, &client);
        HBRUSH brush = ::CreateSolidBrush(self->color); ::FillRect(dc, &client, brush); ::DeleteObject(brush);
        ::EndPaint(hwnd, &paint); return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}
void CreateSource(SourceWindow& source, const RECT& bounds, const wchar_t* title) {
    static const bool registered = [] {
        WNDCLASSW type{}; type.hInstance = ::GetModuleHandleW(nullptr); type.lpfnWndProc = SourceProc;
        type.lpszClassName = L"AetherShell.Tests.PreviewSource"; type.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        return ::RegisterClassW(&type) != 0;
    }();
    REQUIRE(registered);
    // A captioned, resizable tool window: a normal close path, but no taskbar/Alt+Tab entry.
    source.hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"AetherShell.Tests.PreviewSource", title,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, bounds.left, bounds.top,
        bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, ::GetModuleHandleW(nullptr), &source);
    REQUIRE(source.hwnd != nullptr);
    ::ShowWindow(source.hwnd, SW_SHOWNOACTIVATE); ::UpdateWindow(source.hwnd);
}

void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr); REQUIRE(timer != nullptr);
    LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE));
    const auto id = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    loop.Run(); loop.RemoveWaitHandle(id); ::CloseHandle(timer);
}

// Refuse capture when a foreign window overlaps the guarded crop above the backdrop.
bool PrivateCrop(HWND backdrop, const RECT& crop) {
    DWORD process = 0; ::GetWindowThreadProcessId(backdrop, &process);
    RECT bounds{};
    if (process != ::GetCurrentProcessId() || !::IsWindowVisible(backdrop) || !::GetWindowRect(backdrop, &bounds) ||
        bounds.left > crop.left || bounds.top > crop.top || bounds.right < crop.right || bounds.bottom < crop.bottom) return false;
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (window == backdrop) return true;
        if (!::IsWindowVisible(window)) continue;
        DWORD owner = 0; ::GetWindowThreadProcessId(window, &owner);
        if (owner == ::GetCurrentProcessId()) continue;
        RECT other{}, overlap{};
        if (::GetWindowRect(window, &other) && ::IntersectRect(&overlap, &other, &crop)) return false;
    }
    return false;
}
// Fraction of pixels in an own-window screen rectangle within `tolerance` of `color`.
double ColorShare(HWND backdrop, const RECT& crop, COLORREF color, int tolerance = 18) {
    RECT guarded = crop; ::InflateRect(&guarded, 64, 64);
    REQUIRE(SUCCEEDED(::DwmFlush()));
    REQUIRE(PrivateCrop(backdrop, guarded));
    const int width = crop.right - crop.left, height = crop.bottom - crop.top;
    REQUIRE(width > 0); REQUIRE(height > 0);
    HDC screen = ::GetDC(nullptr); HDC memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = ::SelectObject(memory, bitmap);
    const bool copied = ::BitBlt(memory, 0, 0, width, height, screen, crop.left, crop.top, SRCCOPY | CAPTUREBLT) != FALSE;
    std::size_t matching = 0;
    if (copied) {
        const auto* pixel = static_cast<const std::uint8_t*>(bits);
        for (int at = 0; at < width * height; ++at, pixel += 4) {
            if (std::abs(pixel[2] - GetRValue(color)) <= tolerance && std::abs(pixel[1] - GetGValue(color)) <= tolerance &&
                std::abs(pixel[0] - GetBValue(color)) <= tolerance) ++matching;
        }
    }
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen);
    REQUIRE(copied); REQUIRE(PrivateCrop(backdrop, guarded));
    return static_cast<double>(matching) / (static_cast<double>(width) * height);
}

LPARAM ClientPoint(HWND hwnd, const RECT& screen) {
    POINT point{(screen.left + screen.right) / 2, (screen.top + screen.bottom) / 2};
    ::ScreenToClient(hwnd, &point);
    return MAKELPARAM(static_cast<WORD>(point.x), static_cast<WORD>(point.y));
}
LPARAM IconPoint(const dock::DockWindow& window, const D2D1_RECT_F& rect) {
    return MAKELPARAM(static_cast<WORD>(std::lround(window.ToPx((rect.left + rect.right) / 2))),
        static_cast<WORD>(std::lround(window.ToPx((rect.top + rect.bottom) / 2))));
}

struct Fixture {
    platform::ComApartment com{COINIT_APARTMENTTHREADED};
    platform::MessageLoop loop;
    graphics::GraphicsDevice device;
    graphics::FrameScheduler scheduler;
    ManualDispatcher dispatcher;
    std::unique_ptr<settings::SettingsService> settings;
    RecordingLauncher launcher;
    NoIcons icons;
    HWND backdrop = nullptr;
    RECT area{};
    RECT workBefore{};
    std::unique_ptr<dock::DockWindow> window;
    std::size_t filter = 0;
    bool filtering = false;

    Fixture() {
        REQUIRE(device.Initialize().ok());
        REQUIRE(scheduler.Initialize(loop).ok());
        REQUIRE(::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workBefore, 0));
        settings = std::make_unique<settings::SettingsService>(test::MakeTempDirectory("dock-preview") / L"settings.json", dispatcher);
        REQUIRE(settings->Load({}).ok());
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!settings->Loaded() && std::chrono::steady_clock::now() < deadline) { dispatcher.RunPending(); std::this_thread::sleep_for(1ms); }
        REQUIRE(settings->Loaded());
        REQUIRE(settings->Update([](settings::Settings& value) {
            value.dock.showStartButton = false; value.dock.showTaskViewButton = false;
            for (const auto* id : {kIdA, kIdB}) {
                settings::PinnedApp pin; pin.id = id; pin.displayName = id == kIdA ? L"Synthetic A" : L"Synthetic B";
                pin.parsingName = id == kIdA ? L"Synthetic.Preview.A" : L"Synthetic.Preview.B";
                value.dock.pinned.push_back(std::move(pin));
            }
        }).ok());
        const auto monitor = platform::PrimaryMonitor();
        const LONG width = std::min<LONG>(1000, monitor.workArea.right - monitor.workArea.left - 100);
        const LONG height = std::min<LONG>(720, monitor.workArea.bottom - monitor.workArea.top - 260);
        const LONG x = (monitor.workArea.left + monitor.workArea.right - width) / 2;
        const LONG inset = 144 + static_cast<LONG>(std::ceil((dock::kDockShadowMargin + dock::kDockEntranceOffset) * monitor.dpi / 96.0f));
        area = {x, monitor.workArea.bottom - inset - height, x + width, monitor.workArea.bottom - inset};
        WNDCLASSW type{}; type.hInstance = ::GetModuleHandleW(nullptr); type.lpfnWndProc = PatternProc;
        type.lpszClassName = L"AetherShell.Tests.PreviewBackdrop"; ::RegisterClassW(&type);
        RECT guarded = area; ::InflateRect(&guarded, 96, 96);
        backdrop = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, type.lpszClassName, L"Synthetic preview backdrop",
            WS_POPUP, guarded.left, guarded.top, guarded.right - guarded.left, guarded.bottom - guarded.top, nullptr, nullptr, type.hInstance, nullptr);
        REQUIRE(backdrop != nullptr);
        ::ShowWindow(backdrop, SW_SHOWNOACTIVATE); ::UpdateWindow(backdrop);
        window = std::make_unique<dock::DockWindow>(dock::DockWindowContext{device, scheduler, dispatcher, launcher, icons, *settings});
        dock::DockWindowOptions options; options.workAreaPx = area; options.allowWorkAreaReservation = false; options.owner = backdrop;
        options.hoverTiming = dock::DockHoverTiming{0.25, 0.08, 0.3, 0.6};
        REQUIRE(window->Create(options).ok());
        REQUIRE(window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), {}, settings->Current().dock).ok());
        // Synthetic input is sent directly; discard queued real-cursor moves/leaves for these two
        // own windows so the owner's stationary cursor cannot change the fixture.
        filter = loop.AddMessageFilter([dockHwnd = window->Handle(), card = Card().Handle()](MSG& message) {
            return (message.hwnd == dockHwnd || message.hwnd == card) &&
                   (message.message == WM_MOUSEMOVE || message.message == WM_MOUSELEAVE);
        });
        filtering = true;
    }
    ~Fixture() {
        if (window) window->Destroy();
        if (filtering) loop.RemoveMessageFilter(filter);
        if (backdrop) ::DestroyWindow(backdrop);
        RECT workAfter{};
        if (::SystemParametersInfoW(SPI_GETWORKAREA, 0, &workAfter, 0)) CHECK(::EqualRect(&workBefore, &workAfter));
    }
    const dock::DockPreviewWindow& Card() const { return *window->PreviewWindow(); }
    dock::DockLayout Geometry() const { auto value = window->Geometry(); REQUIRE(value.ok()); return *value; }
    void Hover(std::size_t index) {
        ::SendMessageW(window->Handle(), WM_MOUSEMOVE, 0, IconPoint(*window, Geometry().items[index].icon));
    }
    void LeaveDock() { ::SendMessageW(window->Handle(), WM_MOUSELEAVE, 0, 0); }
    void Click(std::size_t index) {
        const auto point = IconPoint(*window, Geometry().items[index].icon);
        ::SendMessageW(window->Handle(), WM_LBUTTONDOWN, 0, point);
        ::SendMessageW(window->Handle(), WM_LBUTTONUP, 0, point);
    }
    void CardMove(const RECT& screen) { ::SendMessageW(Card().Handle(), WM_MOUSEMOVE, 0, ClientPoint(Card().Handle(), screen)); }
    void CardClick(const RECT& screen) {
        const auto point = ClientPoint(Card().Handle(), screen);
        ::SendMessageW(Card().Handle(), WM_MOUSEMOVE, 0, point);
        ::SendMessageW(Card().Handle(), WM_LBUTTONDOWN, 0, point);
        ::SendMessageW(Card().Handle(), WM_LBUTTONUP, 0, point);
    }
    void CardLeave() { ::SendMessageW(Card().Handle(), WM_MOUSELEAVE, 0, 0); }
    void Run(DWORD milliseconds) { RunFor(loop, milliseconds); }
};

apps::RunningApplication App(const wchar_t* id, bool foreground, std::vector<HWND> windows) {
    apps::RunningApplication app; app.id = id; app.displayName = id == kIdA ? L"Synthetic A" : L"Synthetic B";
    app.foreground = foreground;
    for (std::size_t index = 0; index < windows.size(); ++index)
        app.windows.push_back({windows[index], L"Synthetic window " + std::to_wstring(index + 1), false});
    return app;
}
const auto kFakeWindow = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(1));  // never a live identity

}  // namespace

AETHER_TEST("dock preview integration: hover grows the background marker and shows genuine own-window previews") {
    Fixture f;
    const RECT sourceArea{f.area.left + 40, f.area.top + 20, f.area.left + 680, f.area.top + 420};
    SourceWindow first{nullptr, RGB(226, 48, 168)}, second{nullptr, RGB(40, 196, 92)};
    CreateSource(first, sourceArea, L"Synthetic window 1");
    CreateSource(second, {sourceArea.left + 100, sourceArea.top, sourceArea.left + 420, sourceArea.top + 460}, L"Synthetic window 2");
    // Sources stay behind the private topmost backdrop: only their DWM thumbnails can appear.
    REQUIRE(f.window->SetRunningApplications({App(kIdA, true, {kFakeWindow}), App(kIdB, false, {first.hwnd, second.hwnd})}).ok());
    f.window->Show(false); f.LeaveDock(); f.Run(1000);  // a first observation grows from a dot
    REQUIRE_EQ(f.Geometry().items.size(), 3u);  // Search, A, B
    CHECK_EQ(f.window->MarkerWidth(kIdA), dock::kDockMarkerPillWidth);
    CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerDotWidth);
    const HWND foregroundBefore = ::GetForegroundWindow();

    f.Hover(2);
    CHECK_EQ(f.window->EmphasizedApplication(), std::wstring(kIdB));
    f.Run(110);
    const float growing = f.window->MarkerWidth(kIdB);
    CHECK(growing > dock::kDockMarkerDotWidth); CHECK(growing < dock::kDockMarkerPillWidth);  // visibly in motion
    CHECK_EQ(f.window->MarkerWidth(kIdA), dock::kDockMarkerPillWidth);  // the real foreground stays wide
    CHECK_FALSE(f.Card().IsPresenting());  // dwell not finished yet
    f.Run(450);
    REQUIRE(f.Card().IsPresenting());
    REQUIRE_EQ(f.Card().TileCount(), 2u);
    CHECK_EQ(f.Card().TileWindow(0), first.hwnd);
    CHECK_EQ(f.Card().TileWindow(1), second.hwnd);
    REQUIRE_EQ(f.Card().LiveThumbnailCount(), 2u);
    f.Run(500);
    CHECK_EQ(f.window->MarkerWidth(kIdB), f.window->MarkerWidth(kIdA));  // exactly the same width
    CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerPillWidth);
    CHECK(f.launcher.activated.empty());  // hover never activates
    CHECK_EQ(::GetForegroundWindow(), foregroundBefore);

    // Aspect ratio of the visible frame, no stretching.
    for (std::size_t index = 0; index < 2; ++index) {
        RECT frame{}; REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(f.Card().TileWindow(index), DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))));
        const auto thumbnail = f.Card().ThumbnailPx(index); REQUIRE(thumbnail.has_value());
        const double expected = static_cast<double>(frame.right - frame.left) / (frame.bottom - frame.top);
        const double actual = static_cast<double>(thumbnail->right - thumbnail->left) / (thumbnail->bottom - thumbnail->top);
        CHECK_NEAR(actual, expected, 0.03);
    }
    // Real association: each tile shows its own source's color through DWM.
    CHECK(ColorShare(f.backdrop, *f.Card().ThumbnailPx(0), first.color) > 0.5);
    CHECK(ColorShare(f.backdrop, *f.Card().ThumbnailPx(1), second.color) > 0.5);
    CHECK(ColorShare(f.backdrop, *f.Card().ThumbnailPx(0), second.color) < 0.05);

    // Input bounds: the HWND is exactly the card, above the icon row, with every tile inside it.
    RECT card{}; REQUIRE(::GetWindowRect(f.Card().Handle(), &card));
    const auto icon = f.Geometry().items[2].icon;
    CHECK(card.bottom <= f.window->BoundsPx().top + static_cast<LONG>(std::floor(f.window->ToPx(icon.top))));
    HRGN region = ::CreateRectRgn(0, 0, 0, 0); REQUIRE(region != nullptr);
    CHECK_EQ(::GetWindowRgn(f.Card().Handle(), region), ERROR);  // no extra region beyond its bounds
    ::DeleteObject(region);
    for (std::size_t index = 0; index < 2; ++index) {
        RECT inside{}; const auto close = *f.Card().CloseButtonPx(index);
        CHECK(::IntersectRect(&inside, &card, &close)); CHECK(::EqualRect(&inside, &close));
    }
    CHECK(card.right - card.left < 2 * (f.window->BoundsPx().right - f.window->BoundsPx().left));

    // Idle: once the motion settles neither the Dock nor the card requests frames.
    auto dockFrames = f.window->RenderedFrames(); auto cardFrames = f.Card().RenderedFrames();
    bool idle = false;
    for (int attempt = 0; attempt < 8 && !idle; ++attempt) {
        f.Run(150);
        idle = f.window->RenderedFrames() == dockFrames && f.Card().RenderedFrames() == cardFrames;
        dockFrames = f.window->RenderedFrames(); cardFrames = f.Card().RenderedFrames();
    }
    CHECK(idle);

    // Icon -> card crossing keeps both alive; the close button hover too.
    f.LeaveDock();
    f.CardMove(*f.Card().TileBodyPx(0));
    f.Run(450);  // longer than the crossing grace
    CHECK(f.Card().IsPresenting());
    CHECK_EQ(f.window->EmphasizedApplication(), std::wstring(kIdB));
    f.CardMove(*f.Card().CloseButtonPx(1));
    f.Run(100);
    CHECK(f.Card().IsPresenting());
    CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerPillWidth);

    // Leaving both without activating dismisses the card, releases thumbnails and contracts B.
    f.CardLeave();
    f.Run(120);
    CHECK(f.Card().IsPresenting());  // within the grace
    f.Run(500);
    CHECK_FALSE(f.Card().IsPresenting());
    CHECK_EQ(f.Card().LiveThumbnailCount(), 0u);
    CHECK(f.window->EmphasizedApplication().empty());
    f.Run(900);
    CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerDotWidth);
    CHECK_EQ(f.window->MarkerWidth(kIdA), dock::kDockMarkerPillWidth);
    CHECK(f.launcher.activated.empty());
    CHECK(::IsWindow(first.hwnd)); CHECK(::IsWindow(second.hwnd));
    CHECK_EQ(first.closeRequests, 0); CHECK_EQ(second.closeRequests, 0);

    // Rapid observed foreground alternation stays continuous within the dot/pill range.
    float previousA = f.window->MarkerWidth(kIdA), previousB = f.window->MarkerWidth(kIdB);
    for (int pass = 0; pass < 10; ++pass) {
        const bool aFront = pass % 2 != 0;
        REQUIRE(f.window->SetRunningApplications({App(kIdA, aFront, {kFakeWindow}), App(kIdB, !aFront, {first.hwnd, second.hwnd})}).ok());
        f.Run(45);
        const float a = f.window->MarkerWidth(kIdA), b = f.window->MarkerWidth(kIdB);
        CHECK(a >= dock::kDockMarkerDotWidth - 0.01f); CHECK(a <= dock::kDockMarkerPillWidth + 0.01f);
        CHECK(b >= dock::kDockMarkerDotWidth - 0.01f); CHECK(b <= dock::kDockMarkerPillWidth + 0.01f);
        CHECK(std::abs(a - previousA) < 3.0f); CHECK(std::abs(b - previousB) < 3.0f);  // never snaps
        previousA = a; previousB = b;
    }
    ::DestroyWindow(first.hwnd); ::DestroyWindow(second.hwnd);
}

AETHER_TEST("dock preview integration: activation continues the expanded marker and X closes only the selected window") {
    Fixture f;
    const RECT sourceArea{f.area.left + 40, f.area.top + 20, f.area.left + 600, f.area.top + 380};
    SourceWindow first{nullptr, RGB(226, 48, 168)}, second{nullptr, RGB(40, 196, 92)}, third{nullptr, RGB(60, 120, 230)};
    CreateSource(first, sourceArea, L"Synthetic window 1");
    CreateSource(second, sourceArea, L"Synthetic window 2");
    REQUIRE(f.window->SetRunningApplications({App(kIdA, true, {kFakeWindow}), App(kIdB, false, {first.hwnd, second.hwnd})}).ok());
    f.window->Show(false); f.LeaveDock(); f.Run(1000);  // a first observation grows from a dot

    // Hover fully expands B; clicking its icon activates through the launcher and closes the card.
    f.Hover(2); f.Run(1100);
    REQUIRE(f.Card().IsPresenting());
    REQUIRE_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerPillWidth);
    f.Click(2);
    REQUIRE_EQ(f.launcher.activated.size(), 1u);
    CHECK_EQ(f.launcher.activated[0].id, std::wstring(kIdB));
    CHECK_EQ(f.launcher.activated[0].windows.size(), 2u);
    f.Run(250);
    CHECK_FALSE(f.Card().IsPresenting());
    // Observed transfer: A contracts while B keeps its full width instead of restarting.
    REQUIRE(f.window->SetRunningApplications({App(kIdA, false, {kFakeWindow}), App(kIdB, true, {first.hwnd, second.hwnd})}).ok());
    float lastA = f.window->MarkerWidth(kIdA);
    for (int sample = 0; sample < 6; ++sample) {
        f.Run(30);
        CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerPillWidth);
        CHECK(f.window->MarkerWidth(kIdA) <= lastA); lastA = f.window->MarkerWidth(kIdA);
    }
    CHECK(lastA < dock::kDockMarkerPillWidth);
    f.LeaveDock(); f.Run(900);
    CHECK_EQ(f.window->MarkerWidth(kIdA), dock::kDockMarkerDotWidth);
    CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerPillWidth);  // still the real foreground

    // A tile click activates exactly that window, through the same launcher path.
    f.Hover(2); f.Run(700);
    REQUIRE(f.Card().IsPresenting()); REQUIRE_EQ(f.Card().TileCount(), 2u);
    f.LeaveDock(); f.CardClick(*f.Card().TileBodyPx(1));
    REQUIRE_EQ(f.launcher.activated.size(), 2u);
    REQUIRE_EQ(f.launcher.activated[1].windows.size(), 1u);
    CHECK_EQ(f.launcher.activated[1].windows[0].handle, second.hwnd);
    f.Run(300);
    CHECK_FALSE(f.Card().IsPresenting());

    // X requests a normal close of the selected window only.
    f.Hover(2); f.Run(700);
    REQUIRE(f.Card().IsPresenting());
    f.LeaveDock(); f.CardClick(*f.Card().CloseButtonPx(0));
    CHECK(f.Card().ClosePending(0));
    f.Run(150);
    CHECK_EQ(first.closeRequests, 1);
    CHECK(first.hwnd == nullptr);  // the synthetic app accepted and destroyed only this window
    CHECK(::IsWindow(second.hwnd)); CHECK_EQ(second.closeRequests, 0);
    CHECK_EQ(f.launcher.activated.size(), 2u);  // closing never activated anything
    REQUIRE(f.window->SetRunningApplications({App(kIdA, false, {kFakeWindow}), App(kIdB, true, {second.hwnd})}).ok());
    REQUIRE(f.Card().IsPresenting());
    REQUIRE_EQ(f.Card().TileCount(), 1u);
    CHECK_EQ(f.Card().TileWindow(0), second.hwnd);

    // A refused/cancelled close is not success: the window and its tile remain, X recovers.
    second.refuse = true;
    f.CardClick(*f.Card().CloseButtonPx(0));
    f.Run(200);
    CHECK_EQ(second.closeRequests, 1);
    CHECK(::IsWindow(second.hwnd));
    CHECK(f.Card().ClosePending(0));
    f.CardClick(*f.Card().CloseButtonPx(0));  // no duplicate request while pending
    f.Run(100);
    CHECK_EQ(second.closeRequests, 1);
    f.Run(2700);
    CHECK_FALSE(f.Card().ClosePending(0));
    CHECK(f.Card().IsPresenting()); CHECK_EQ(f.Card().TileCount(), 1u);

    // Stale identity: a destroyed window still listed by a slow tracker is never acted upon.
    CreateSource(third, sourceArea, L"Synthetic window 3");
    REQUIRE(f.window->SetRunningApplications({App(kIdA, false, {kFakeWindow}), App(kIdB, true, {second.hwnd, third.hwnd})}).ok());
    REQUIRE_EQ(f.Card().TileCount(), 2u);
    const auto staleClose = *f.Card().CloseButtonPx(1);
    ::DestroyWindow(third.hwnd);
    f.CardClick(staleClose);
    f.Run(100);
    CHECK_EQ(third.closeRequests, 0);
    CHECK_EQ(second.closeRequests, 1);
    CHECK(::IsWindow(second.hwnd));
    CHECK_EQ(f.Card().TileCount(), 1u);  // the card dropped the dead tile
    CHECK_EQ(f.launcher.activated.size(), 2u);

    // Keyboard: Up opens the focused running item's card, arrows reach X, Escape closes it.
    f.CardLeave(); f.Run(600);
    REQUIRE_FALSE(f.Card().IsPresenting());
    ::SendMessageW(f.window->Handle(), WM_KEYDOWN, VK_END, 0);  // the Dock's keyboard focus on B
    ::SendMessageW(f.window->Handle(), WM_KEYDOWN, VK_UP, 0);
    REQUIRE(f.Card().IsPresenting());
    CHECK(f.Card().KeyboardActive());
    CHECK_EQ(f.window->EmphasizedApplication(), std::wstring(kIdB));
    f.Run(300);
    ::SendMessageW(f.window->Handle(), WM_KEYDOWN, VK_RETURN, 0);  // tile body: activate that window
    REQUIRE_EQ(f.launcher.activated.size(), 3u);
    CHECK_EQ(f.launcher.activated[2].windows.size(), 1u);
    f.Run(300);
    CHECK_FALSE(f.Card().IsPresenting());
    ::SendMessageW(f.window->Handle(), WM_KEYDOWN, VK_UP, 0);
    REQUIRE(f.Card().KeyboardActive());

    // UI Automation: one list with an invokable preview and a named close button per window.
    std::atomic<bool> finished{false}; bool passed = false; int stage = 0;
    const HWND cardHwnd = f.Card().Handle();
    std::thread client([&] {
        platform::ComApartment mta(COINIT_MULTITHREADED);
        const auto check = [](HRESULT result) { if (FAILED(result)) throw std::runtime_error("own preview UIA failed"); };
        try {
            winrt::com_ptr<IUIAutomation> automation;
            check(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()));
            stage = 1; winrt::com_ptr<IUIAutomationElement> root; check(automation->ElementFromHandle(cardHwnd, root.put()));
            CONTROLTYPEID type{}; check(root->get_CurrentControlType(&type));
            stage = 2; if (type != UIA_ListControlTypeId) throw std::runtime_error("type");
            winrt::com_ptr<IUIAutomationCondition> all; check(automation->CreateTrueCondition(all.put()));
            winrt::com_ptr<IUIAutomationElementArray> children; check(root->FindAll(TreeScope_Children, all.get(), children.put()));
            int count{}; check(children->get_Length(&count));
            stage = 3; if (count != 2) throw std::runtime_error("count");
            winrt::com_ptr<IUIAutomationElement> body, close; check(children->GetElement(0, body.put())); check(children->GetElement(1, close.put()));
            BSTR bodyName{}, closeName{}; check(body->get_CurrentName(&bodyName)); check(close->get_CurrentName(&closeName));
            const std::wstring a(bodyName ? bodyName : L""), b(closeName ? closeName : L"");
            ::SysFreeString(bodyName); ::SysFreeString(closeName);
            stage = 4; if (a != L"Synthetic window 1" || b != L"Close Synthetic window 1") throw std::runtime_error("names");
            winrt::com_ptr<IUIAutomationInvokePattern> invoke;
            check(close->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), invoke.put_void()));
            stage = 5; passed = invoke != nullptr;
        } catch (...) {}
        finished.store(true);
    });
    while (!finished.load()) f.Run(20);
    client.join();
    CHECK(passed);
    if (!passed) TEST_NOTE("preview UIA failed at stage " + std::to_string(stage));
    ::SendMessageW(f.window->Handle(), WM_KEYDOWN, VK_ESCAPE, 0);
    f.Run(300);
    CHECK_FALSE(f.Card().IsPresenting());
    CHECK_FALSE(f.Card().KeyboardActive());
    CHECK_EQ(second.closeRequests, 1);  // UIA inspection did not invoke anything
    second.refuse = false;
    if (second.hwnd) ::DestroyWindow(second.hwnd);
}

AETHER_TEST("dock preview integration: destruction during dwell, reduced motion and high contrast stay truthful") {
    Fixture f;
    const RECT sourceArea{f.area.left + 40, f.area.top + 20, f.area.left + 600, f.area.top + 380};
    SourceWindow source{nullptr, RGB(226, 48, 168)};
    CreateSource(source, sourceArea, L"Synthetic window 1");
    REQUIRE(f.window->SetRunningApplications({App(kIdA, true, {kFakeWindow}), App(kIdB, false, {source.hwnd})}).ok());
    f.window->Show(false); f.LeaveDock(); f.Run(1000);  // a first observation grows from a dot

    // The window disappears during the dwell: no card, no thumbnail, no stale action target.
    f.Hover(2); f.Run(100);
    ::DestroyWindow(source.hwnd);
    REQUIRE(f.window->SetRunningApplications({App(kIdA, true, {kFakeWindow}), App(kIdB, false, {kFakeWindow})}).ok());
    f.Run(800);
    CHECK_FALSE(f.Card().IsPresenting());
    CHECK_EQ(f.Card().LiveThumbnailCount(), 0u);
    CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerPillWidth);  // still hovered: emphasis only
    f.LeaveDock(); f.Run(900);
    CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerDotWidth);

    // Reduced motion and high contrast: immediate marker and card changes, still usable.
    CreateSource(source, sourceArea, L"Synthetic window 1");
    REQUIRE(f.window->SetRunningApplications({App(kIdA, true, {kFakeWindow}), App(kIdB, false, {source.hwnd})}).ok());
    for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::HighContrast}) {
        REQUIRE(f.window->SetTheme(graphics::MakeDesignTokens(mode), graphics::MotionPolicy{mode == graphics::ThemeMode::Dark},
            f.settings->Current().dock).ok());
        f.LeaveDock(); f.Run(400);
        f.Hover(2);
        CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerPillWidth);  // snapped, not animated
        f.Run(330);
        REQUIRE(f.Card().IsPresenting());
        CHECK_EQ(f.Card().LiveThumbnailCount(), 1u);
        f.LeaveDock(); f.Run(360);
        CHECK_FALSE(f.Card().IsPresenting());  // dismissed without a fade
        CHECK_EQ(f.window->MarkerWidth(kIdB), dock::kDockMarkerDotWidth);
    }
    // A minimized window shows either DWM's retained content at its own aspect or an explicit
    // "Minimized" placeholder; never its stretched minimized caption. Never activated to get pixels.
    REQUIRE(f.window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), {}, f.settings->Current().dock).ok());
    RECT restoredFrame{};
    REQUIRE(SUCCEEDED(::DwmGetWindowAttribute(source.hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &restoredFrame, sizeof(restoredFrame))));
    const HWND foregroundBefore = ::GetForegroundWindow();
    ::ShowWindow(source.hwnd, SW_SHOWMINNOACTIVE);
    REQUIRE(::IsIconic(source.hwnd));
    f.LeaveDock(); f.Run(400);
    f.Hover(2); f.Run(700);
    REQUIRE(f.Card().IsPresenting());
    REQUIRE_EQ(f.Card().TileCount(), 1u);
    if (f.Card().TileUnavailable(0)) {
        TEST_NOTE("minimized source: explicit placeholder (DWM offered only the minimized caption)");
    } else {
        const auto thumbnail = *f.Card().ThumbnailPx(0);
        const double restored = static_cast<double>(restoredFrame.right - restoredFrame.left) / (restoredFrame.bottom - restoredFrame.top);
        const double shown = static_cast<double>(thumbnail.right - thumbnail.left) / (thumbnail.bottom - thumbnail.top);
        CHECK(std::abs(shown - restored) < 0.08);
        TEST_NOTE("minimized source: DWM retained content at its restored aspect");
    }
    CHECK(::IsIconic(source.hwnd));
    CHECK_EQ(::GetForegroundWindow(), foregroundBefore);
    f.LeaveDock(); f.Run(500);
    ::ShowWindow(source.hwnd, SW_SHOWNOACTIVATE);

    // Suppression releases the card and its thumbnail at once.
    f.Hover(2); f.Run(700);
    REQUIRE(f.Card().IsPresenting());
    REQUIRE(f.window->SetSuppressed(true).ok());
    CHECK_FALSE(f.Card().IsPresenting());
    CHECK_EQ(f.Card().LiveThumbnailCount(), 0u);
    CHECK(f.window->EmphasizedApplication().empty());
    REQUIRE(f.window->SetSuppressed(false).ok());
    CHECK_EQ(source.closeRequests, 0);
    ::DestroyWindow(source.hwnd);
}

namespace {

void SaveCapture(HWND backdrop, const RECT& crop, const std::filesystem::path& path) {
    RECT guarded = crop; ::InflateRect(&guarded, 32, 32);
    REQUIRE(PrivateCrop(backdrop, guarded));
    const int width = crop.right - crop.left, height = crop.bottom - crop.top;
    HDC screen = ::GetDC(nullptr); HDC memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ previous = ::SelectObject(memory, bitmap);
    const bool copied = ::BitBlt(memory, 0, 0, width, height, screen, crop.left, crop.top, SRCCOPY | CAPTUREBLT) != FALSE;
    std::vector<std::uint8_t> pixels;
    if (copied) pixels.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) + static_cast<std::size_t>(width) * height * 4);
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen);
    REQUIRE(copied); REQUIRE(PrivateCrop(backdrop, guarded));
    for (std::size_t index = 3; index < pixels.size(); index += 4) pixels[index] = 255;
    auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICStream> stream; winrt::check_hresult(factory->CreateStream(stream.put()));
    winrt::check_hresult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder; winrt::check_hresult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    winrt::check_hresult(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame; winrt::check_hresult(encoder->CreateNewFrame(frame.put(), nullptr));
    winrt::check_hresult(frame->Initialize(nullptr)); winrt::check_hresult(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA; winrt::check_hresult(frame->SetPixelFormat(&format));
    winrt::check_hresult(frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), pixels.data()));
    winrt::check_hresult(frame->Commit()); winrt::check_hresult(encoder->Commit());
}

}  // namespace

// Opt-in inspection aid, skipped unless AETHER_DOCK_PREVIEW_FRAMES names a local output folder. It
// records the real native Dock/card at normal speed with synthetic windows only (own windows over
// the private backdrop) so motion and material can be reviewed without the owner's desktop.
AETHER_TEST("dock preview frames: optional own-window normal-speed capture sequence") {
    wchar_t folder[MAX_PATH]{};
    if (!::GetEnvironmentVariableW(L"AETHER_DOCK_PREVIEW_FRAMES", folder, MAX_PATH)) {
        TEST_NOTE("skipped: set AETHER_DOCK_PREVIEW_FRAMES to a local folder to record own-window frames");
        return;
    }
    const std::filesystem::path output(folder);
    std::filesystem::create_directories(output);
    Fixture f;
    const RECT sourceArea{f.area.left + 40, f.area.top + 20, f.area.left + 680, f.area.top + 420};
    SourceWindow first{nullptr, RGB(226, 48, 168)}, second{nullptr, RGB(40, 196, 92)};
    CreateSource(first, sourceArea, L"Synthetic window 1");
    CreateSource(second, {sourceArea.left + 100, sourceArea.top, sourceArea.left + 420, sourceArea.top + 460}, L"Synthetic window 2");
    REQUIRE(f.window->SetRunningApplications({App(kIdA, true, {kFakeWindow}), App(kIdB, false, {first.hwnd, second.hwnd})}).ok());
    f.window->Show(false); f.LeaveDock(); f.Run(1000);
    // The Dock panel rests on the area's bottom margin; keep the crop inside the private backdrop.
    const RECT crop{f.area.left, std::max(f.area.top, f.area.bottom - static_cast<LONG>(f.window->ToPx(340))), f.area.right, f.area.bottom};
    std::FILE* log = nullptr;
    REQUIRE(_wfopen_s(&log, (output / L"frames.csv").c_str(), L"w") == 0);
    std::fputs("frame,ms,phase,markerA,markerB,card\n", log);
    int frame = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto record = [&](const char* phase, DWORD milliseconds) {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
        while (std::chrono::steady_clock::now() < until) {
            f.Run(16);
            wchar_t name[32]; swprintf_s(name, L"frame-%04d.png", frame);
            SaveCapture(f.backdrop, crop, output / name);
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
            std::fprintf(log, "%d,%lld,%s,%.3f,%.3f,%d\n", frame++, static_cast<long long>(ms), phase,
                f.window->MarkerWidth(kIdA), f.window->MarkerWidth(kIdB), f.Card().IsPresenting() ? 1 : 0);
        }
    };
    record("rest", 200);
    f.Hover(2); record("hover", 1300);
    f.LeaveDock(); f.CardMove(*f.Card().TileBodyPx(0)); record("card", 500);
    f.CardMove(*f.Card().CloseButtonPx(0)); record("close-hover", 400);
    f.CardLeave(); record("leave", 1300);
    f.Hover(2); record("hover-again", 900);
    f.Click(2);
    REQUIRE(f.window->SetRunningApplications({App(kIdA, false, {kFakeWindow}), App(kIdB, true, {first.hwnd, second.hwnd})}).ok());
    record("transfer", 1000);
    f.LeaveDock(); record("after", 600);
    std::fclose(log);
    TEST_NOTE("recorded " + std::to_string(frame) + " own-window frames");
    ::DestroyWindow(first.hwnd); ::DestroyWindow(second.hwnd);
}
