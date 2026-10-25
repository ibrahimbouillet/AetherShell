// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// GlassMenuPresenter on the real desktop with this test's own windows only: a hidden owner, the
// menu's level windows, a click-counting window and an opaque private backdrop for the pixel
// checks. The foreground rule runs through fake window operations, so the real foreground is never
// changed; input is posted to this thread's own windows, never injected. Windows sit in the middle of
// the primary work area, clear of the top 90 and bottom 170 pixels (Top Bar and Dock). Captures read
// only screen areas covered by this test's windows over the private backdrop and are saved under
// %TEMP%\AetherShellTests for inspection.
#include "AetherTest.h"

#include "graphics/FrameScheduler.h"
#include "graphics/GraphicsDevice.h"
#include "graphics/Theme.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/WindowUtilities.h"
#include "ui/GlassMenu.h"

#include <UIAutomation.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::ui;

namespace {

constexpr wchar_t kOwnerTitle[] = L"Glass menu test owner";

// The foreground rule's window operations, recorded instead of performed.
class FakeOps final : public IMenuWindowOps {
public:
    HWND owner = nullptr;
    HWND previous = reinterpret_cast<HWND>(0x200);  // stands for the window the user was using
    HWND foreground = reinterpret_cast<HWND>(0x200);
    LONG_PTR style = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    UINT trackCommand = 0;
    bool allowForeground = true;
    std::vector<std::string> log;

    HWND Foreground() noexcept override { return foreground; }
    bool SetForeground(HWND window) noexcept override {
        log.push_back(window == owner ? "foreground owner" : "foreground previous");
        if (allowForeground) foreground = window;
        return allowForeground;
    }
    bool IsValidWindow(HWND window) noexcept override { return window && (window == owner || window == previous); }
    LONG_PTR ExtendedStyle(HWND) noexcept override { return style; }
    void SetExtendedStyle(HWND, LONG_PTR value) noexcept override {
        log.push_back((value & WS_EX_NOACTIVATE) ? "style noactivate" : "style activatable");
        style = value;
    }
    UINT Track(HMENU, UINT, POINT, HWND, const TPMPARAMS*) noexcept override {
        log.push_back("track");  // native fallback: no menu is shown
        return trackCommand;
    }
    void PostNull(HWND) noexcept override { log.push_back("post-null"); }
};

struct OwnerWindow {
    HWND hwnd = nullptr;
    OwnerWindow() {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &::DefWindowProcW;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AetherShell.Tests.GlassMenuOwner";
        if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        // Never shown: the menu levels are owned popups of this window.
        hwnd = ::CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, kOwnerTitle, WS_POPUP, 0, 0, 10, 10,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    }
    ~OwnerWindow() {
        if (hwnd) ::DestroyWindow(hwnd);
    }
    OwnerWindow(const OwnerWindow&) = delete;
    OwnerWindow& operator=(const OwnerWindow&) = delete;
};

struct Harness {
    platform::ComApartment com{COINIT_APARTMENTTHREADED};
    platform::MessageLoop loop;
    graphics::GraphicsDevice device;
    graphics::FrameScheduler scheduler;
    OwnerWindow owner;
    FakeOps ops;
    Harness() {
        // UIA's HWND proxy infrastructure and WinRT factories share the UI thread's COM
        // apartment. Keep that apartment alive across consecutive test harnesses, as the
        // application's UI thread does, rather than disconnecting/recreating its identity.
        static thread_local platform::ComApartment suiteApartment{COINIT_APARTMENTTHREADED};
        REQUIRE(suiteApartment.IsUsable());
        REQUIRE(com.IsUsable());
        REQUIRE(device.Initialize().ok());
        REQUIRE(scheduler.Initialize(loop).ok());
        REQUIRE(owner.hwnd != nullptr);
        ops.owner = owner.hwnd;
    }
    ~Harness() {
        scheduler.Shutdown();
        device.Shutdown();
    }
    Harness(const Harness&) = delete;
    Harness& operator=(const Harness&) = delete;
};

// One-shot timers serviced by the loop's wait handles, so they also fire inside the menu's nested loop.
class LoopTimers {
public:
    explicit LoopTimers(platform::MessageLoop& loop) : loop_(loop) {}
    ~LoopTimers() {
        for (const auto& timer : timers_) {
            loop_.RemoveWaitHandle(timer.id);
            ::CloseHandle(timer.handle);
        }
    }
    LoopTimers(const LoopTimers&) = delete;
    LoopTimers& operator=(const LoopTimers&) = delete;

    void After(int milliseconds, std::function<void()> action) {
        HANDLE handle = ::CreateWaitableTimerW(nullptr, FALSE, nullptr);  // auto-reset: fires once
        REQUIRE(handle != nullptr);
        LARGE_INTEGER due{};
        due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;
        REQUIRE(::SetWaitableTimer(handle, &due, 0, nullptr, nullptr, FALSE));
        auto pending = std::make_shared<std::function<void()>>(std::move(action));
        const auto id = loop_.AddWaitHandle(handle, [pending] {
            auto run = std::move(*pending);
            *pending = nullptr;
            if (run) run();
        });
        timers_.push_back(Timer{id, handle});
    }

private:
    struct Timer {
        std::uint64_t id;
        HANDLE handle;
    };
    platform::MessageLoop& loop_;
    std::vector<Timer> timers_;
};

void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    REQUIRE(timer != nullptr);
    LARGE_INTEGER due{};
    due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10'000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE));
    const auto id = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    loop.Run();
    loop.RemoveWaitHandle(id);
    ::CloseHandle(timer);
}

template <class Predicate>
long long RunUntil(platform::MessageLoop& loop, Predicate done, int limitMs) {
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed = [&] {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    };
    while (!done() && elapsed() < limitMs) RunFor(loop, 10);
    return elapsed();
}

// Where the menus open: the middle of the primary work area, below the band the Top Bar uses and
// away from the real pointer (so a stray hover cannot change the selection).
struct Stage {
    RECT work{};
    float scale = 1.0f;
    RECT anchor{};
    [[nodiscard]] int Px(float dip) const { return static_cast<int>(std::lround(dip * scale)); }
    [[nodiscard]] LONG CenterX() const { return (anchor.left + anchor.right) / 2; }
};

Stage MakeStage() {
    Stage stage;
    const auto monitor = platform::PrimaryMonitor();
    stage.work = monitor.workArea;
    stage.scale = static_cast<float>(monitor.dpi) / 96.0f;
    const LONG width = platform::Width(stage.work);
    LONG centerX = stage.work.left + width / 2;
    const LONG top = stage.work.top + 90 + stage.Px(70);
    POINT cursor{};
    if (::GetCursorPos(&cursor)) {
        const RECT predicted{centerX - stage.Px(460), top - stage.Px(40), centerX + stage.Px(460), top + stage.Px(560)};
        if (::PtInRect(&predicted, cursor)) centerX = cursor.x < centerX ? stage.work.left + width * 3 / 4 : stage.work.left + width / 4;
    }
    stage.anchor = RECT{centerX - stage.Px(16), top, centerX + stage.Px(16), top + stage.Px(28)};
    return stage;
}

MenuRequest RequestFor(const Harness& harness, const Stage& stage) {
    MenuRequest request;
    request.owner = harness.owner.hwnd;
    request.anchor = stage.anchor;
    request.placement = MenuPlacement::BelowAnchor;
    request.restore = ForegroundRestore::WhenDismissed;
    return request;
}

void PostKey(HWND target, UINT key) {
    ::PostMessageW(target, WM_KEYDOWN, key, 1);
    ::PostMessageW(target, WM_KEYUP, key, static_cast<LPARAM>(0xC0000001u));
}

LPARAM ClientPoint(HWND window, POINT screen) {
    POINT client = screen;
    ::ScreenToClient(window, &client);
    return MAKELPARAM(static_cast<WORD>(static_cast<SHORT>(client.x)), static_cast<WORD>(static_cast<SHORT>(client.y)));
}

POINT Center(const RECT& rect) { return POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}; }

void PostClick(HWND capture, POINT screen) {
    const LPARAM point = ClientPoint(capture, screen);
    ::PostMessageW(capture, WM_LBUTTONDOWN, MK_LBUTTON, point);
    ::PostMessageW(capture, WM_LBUTTONUP, 0, point);
}

std::vector<MenuEntry> BasicMenu() {
    return {MenuEntry::Command(1, L"&Open\tEnter"), MenuEntry::Command(2, L"P&roperties", false), MenuEntry::Separator(),
            MenuEntry::Command(3, L"&Pin to Dock"), MenuEntry::Command(4, L"Dock &settings…")};
}

// A logo-like menu: icons, a separator, radio entries (one checked), a disabled row, a submenu and
// an accelerator.
std::vector<MenuEntry> LogoMenu() {
    std::vector<MenuEntry> entries;
    entries.push_back(MenuEntry::Command(1, L"&About Aether Shell"));
    entries.back().glyph = graphics::Glyph::About;
    entries.push_back(MenuEntry::Command(2, L"&Settings…\tCtrl+,"));
    entries.back().glyph = graphics::Glyph::Settings;
    entries.push_back(MenuEntry::Separator());
    entries.push_back(MenuEntry::Command(3, L"Follow s&ystem colors"));
    entries.back().radio = true;
    entries.push_back(MenuEntry::Command(4, L"&Dark appearance"));
    entries.back().radio = true;
    entries.back().checked = true;
    entries.push_back(MenuEntry::Command(5, L"Check for &updates", false));
    entries.back().glyph = graphics::Glyph::Updates;
    entries.push_back(MenuEntry::Separator());
    entries.push_back(MenuEntry::Submenu(L"Dock si&ze", {MenuEntry::Command(10, L"Small"), MenuEntry::Command(11, L"Large")}));
    entries.back().glyph = graphics::Glyph::Dock;
    entries.push_back(MenuEntry::Command(6, L"&Quit Aether Shell\tAlt+F4"));
    entries.back().glyph = graphics::Glyph::Power;
    return entries;
}

// Ends a menu that would otherwise wait forever after a failed expectation.
void Watchdog(LoopTimers& timers, GlassMenuPresenter& presenter, bool& timedOut, int milliseconds = 10'000) {
    timers.After(milliseconds, [&presenter, &timedOut] {
        if (!presenter.IsOpen()) return;
        timedOut = true;
        presenter.Cancel();
    });
}

bool SkipForHighContrast() {
    if (!platform::HighContrastEnabled()) return false;
    TEST_NOTE("high contrast is on: glass menus use the system-colored fallback by design");
    return true;
}

// --- Private backdrop and captures (the GlassProbeTests approach, with a white phase) ---

LRESULT CALLBACK BackdropProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC dc = ::BeginPaint(hwnd, &ps);
        RECT client{};
        ::GetClientRect(hwnd, &client);
        const LONG_PTR phase = ::GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (phase == 2) {
            ::FillRect(dc, &client, static_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));
        } else {
            const int stripe = 24;
            for (int x = 0; x < client.right; x += stripe) {
                const int i = x / stripe;
                const COLORREF color = phase == 1 ? RGB(195 - (i * 31) % 90, 120 + (i * 17) % 100, 35 + (i * 11) % 80)
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
        }
        ::EndPaint(hwnd, &ps);
        return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}

struct Backdrop {
    HWND hwnd = nullptr;
    explicit Backdrop(const RECT& rect) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &BackdropProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = L"AetherShell.Tests.GlassMenuBackdrop";
        if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP,
                                 rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr,
                                 wc.hInstance, nullptr);
        if (!hwnd) return;
        ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
        ::UpdateWindow(hwnd);
    }
    ~Backdrop() {
        if (hwnd) ::DestroyWindow(hwnd);
    }
    Backdrop(const Backdrop&) = delete;
    Backdrop& operator=(const Backdrop&) = delete;
    void SetPhase(LONG_PTR phase) const {
        ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, phase);
        ::InvalidateRect(hwnd, nullptr, FALSE);
        ::UpdateWindow(hwnd);
    }
};

// A foreign overlay anywhere in the guard band could contribute pixels even outside the saved crop.
// Inspect only PID, visibility and geometry; never read a foreign title or content.
bool PrivateBackdropVisible(HWND backdrop, const RECT& guardedArea) {
    DWORD process = 0;
    ::GetWindowThreadProcessId(backdrop, &process);
    RECT bounds{};
    if (process != ::GetCurrentProcessId() || !::IsWindowVisible(backdrop) || !::GetWindowRect(backdrop, &bounds) ||
        bounds.left > guardedArea.left || bounds.top > guardedArea.top || bounds.right < guardedArea.right ||
        bounds.bottom < guardedArea.bottom)
        return false;
    for (HWND window = ::GetTopWindow(nullptr); window; window = ::GetWindow(window, GW_HWNDNEXT)) {
        if (window == backdrop) return true;
        if (!::IsWindowVisible(window)) continue;
        ::GetWindowThreadProcessId(window, &process);
        if (process == ::GetCurrentProcessId()) continue;
        RECT overlap{};
        if (::GetWindowRect(window, &bounds) && ::IntersectRect(&overlap, &bounds, &guardedArea)) return false;
    }
    return false;
}

struct Capture {
    RECT area{};
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> bgra;
    [[nodiscard]] const std::uint8_t* Pixel(int x, int y) const {
        return &bgra[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
    }
    // Screen coordinates.
    [[nodiscard]] const std::uint8_t* At(LONG x, LONG y) const {
        return Pixel(static_cast<int>(x - area.left), static_cast<int>(y - area.top));
    }
};

std::optional<Capture> TryCapture(HWND backdrop, const RECT& guardedArea, const RECT& rect) {
    if (rect.left < guardedArea.left || rect.top < guardedArea.top || rect.right > guardedArea.right ||
        rect.bottom > guardedArea.bottom || !PrivateBackdropVisible(backdrop, guardedArea)) {
        return std::nullopt;
    }
    Capture capture;
    capture.area = rect;
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
    bool copied = false;
    HGDIOBJ old = nullptr;
    if (screen && memory && bitmap && bits) {
        old = ::SelectObject(memory, bitmap);
        copied = ::BitBlt(memory, 0, 0, capture.width, capture.height, screen, rect.left, rect.top, SRCCOPY | CAPTUREBLT) != FALSE;
    }
    if (copied) {
        const auto* begin = static_cast<const std::uint8_t*>(bits);
        capture.bgra.assign(begin, begin + static_cast<std::size_t>(capture.width) * static_cast<std::size_t>(capture.height) * 4);
    }
    if (old) ::SelectObject(memory, old);
    if (bitmap) ::DeleteObject(bitmap);
    if (memory) ::DeleteDC(memory);
    if (screen) ::ReleaseDC(nullptr, screen);
    if (!copied || !PrivateBackdropVisible(backdrop, guardedArea)) return std::nullopt;
    return capture;
}

// Another engineer's tests may briefly cover the band; retry a few times, never weaken the guard.
std::optional<Capture> CaptureWithRetry(HWND backdrop, const RECT& guardedArea, const RECT& rect) {
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (auto capture = TryCapture(backdrop, guardedArea, rect)) return capture;
        TEST_NOTE("private backdrop not exclusively visible; retrying the capture");
        ::Sleep(150);
    }
    return std::nullopt;
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
    winrt::check_hresult(frame->SetSize(static_cast<UINT>(capture.width), static_cast<UINT>(capture.height)));
    std::vector<std::uint8_t> opaque = capture.bgra;
    for (std::size_t i = 3; i < opaque.size(); i += 4) opaque[i] = 0xFF;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    winrt::check_hresult(frame->SetPixelFormat(&format));
    REQUIRE(format == GUID_WICPixelFormat32bppBGRA);
    winrt::check_hresult(frame->WritePixels(static_cast<UINT>(capture.height), static_cast<UINT>(capture.width) * 4,
                                            static_cast<UINT>(opaque.size()), opaque.data()));
    winrt::check_hresult(frame->Commit());
    winrt::check_hresult(encoder->Commit());
}

double Luminance(const std::uint8_t* pixel) { return 0.0722 * pixel[0] + 0.7152 * pixel[1] + 0.2126 * pixel[2]; }

int MinChannel(const std::uint8_t* pixel) { return std::min({static_cast<int>(pixel[0]), static_cast<int>(pixel[1]), static_cast<int>(pixel[2])}); }

double LuminanceVariance(const Capture& capture, const RECT& screenArea) {
    double sum = 0.0;
    double squares = 0.0;
    int count = 0;
    for (LONG y = screenArea.top; y < screenArea.bottom; ++y) {
        for (LONG x = screenArea.left; x < screenArea.right; ++x) {
            const double l = Luminance(capture.At(x, y));
            sum += l;
            squares += l * l;
            ++count;
        }
    }
    if (count == 0) return 0.0;
    const double mean = sum / count;
    return squares / count - mean * mean;
}

double MeanDifference(const Capture& a, const Capture& b, const RECT& screenArea) {
    double difference = 0.0;
    int samples = 0;
    for (LONG y = screenArea.top; y < screenArea.bottom; y += 2) {
        for (LONG x = screenArea.left; x < screenArea.right; x += 2) {
            const auto* before = a.At(x, y);
            const auto* after = b.At(x, y);
            for (int channel = 0; channel < 3; ++channel) {
                difference += std::abs(static_cast<int>(before[channel]) - static_cast<int>(after[channel]));
                ++samples;
            }
        }
    }
    return samples ? difference / samples : 0.0;
}

std::filesystem::path InspectionPath(const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / "AetherShellTests" / name;
    std::filesystem::create_directories(path.parent_path());
    return path;
}

// --- UI Automation client helpers (MTA client thread, as in the accessibility tests) ---

void NeedHr(HRESULT hr, const char* what) {
    if (FAILED(hr)) throw std::runtime_error(std::string("UIA call failed: ") + what);
}
void Need(bool condition, const char* what) {
    if (!condition) throw std::runtime_error(std::string("UIA expectation failed: ") + what);
}
std::wstring TakeBstr(BSTR value) {
    std::wstring result(value ? value : L"");
    ::SysFreeString(value);
    return result;
}

}  // namespace

AETHER_TEST("glass menu: Down Down Enter skips the disabled row and the separator and keeps the foreground rule") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut);
    const auto entries = BasicMenu();
    const auto request = RequestFor(harness, stage);
    int opened = 0;
    std::optional<ErrorCode> nested;
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND level, std::size_t depth) {
        ++opened;
        CHECK_EQ(depth, std::size_t{0});
        CHECK(::IsWindowVisible(level) != FALSE);
        CHECK(presenter.IsOpen());
        // The level never activates; the owner window got the foreground through the (fake) rule.
        CHECK((::GetWindowLongPtrW(level, GWL_EXSTYLE) & WS_EX_NOACTIVATE) != 0);
        CHECK_EQ(::GetCapture(), level);
        auto again = presenter.Show(entries, request);
        if (!again) nested = again.error().code;
        PostKey(harness.owner.hwnd, VK_DOWN);
        PostKey(harness.owner.hwnd, VK_DOWN);
        PostKey(harness.owner.hwnd, VK_RETURN);
    };
    presenter.SetTestHooks(hooks);
    auto selection = presenter.Show(entries, request);
    presenter.SetTestHooks({});
    REQUIRE(selection.ok());
    CHECK_FALSE(timedOut);
    CHECK_EQ(selection->command, 3u);
    CHECK_EQ(selection->previousForeground, harness.ops.previous);
    CHECK_EQ(opened, 1);
    CHECK(nested == ErrorCode::InvalidState);
    CHECK(presenter.LastPresentation() == GlassMenuPresenter::Presentation::Glass);
    CHECK_FALSE(presenter.IsOpen());
    CHECK(::GetCapture() == nullptr);
    // Activatable while open, owner made foreground, WM_NULL posted, style restored; a chosen command
    // keeps the foreground (WhenDismissed).
    CHECK(harness.ops.log == (std::vector<std::string>{"style activatable", "foreground owner", "post-null", "style noactivate"}));
}

AETHER_TEST("glass menu: Escape dismisses with 0 and gives the foreground back") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut);
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND, std::size_t) { PostKey(harness.owner.hwnd, VK_ESCAPE); };
    presenter.SetTestHooks(hooks);
    const auto entries = BasicMenu();
    auto selection = presenter.Show(entries, RequestFor(harness, stage));
    presenter.SetTestHooks({});
    REQUIRE(selection.ok());
    CHECK_FALSE(timedOut);
    CHECK_EQ(selection->command, 0u);
    CHECK(harness.ops.log == (std::vector<std::string>{"style activatable", "foreground owner", "post-null",
                                                        "style noactivate", "foreground previous"}));

    // Windows may refuse the foreground: the menu still opens and works, and the foreground that
    // never moved is not "given back".
    harness.ops.log.clear();
    harness.ops.allowForeground = false;
    bool opened = false;
    hooks.opened = [&](HWND, std::size_t) {
        opened = true;
        PostKey(harness.owner.hwnd, VK_DOWN);
        PostKey(harness.owner.hwnd, VK_RETURN);
    };
    presenter.SetTestHooks(hooks);
    auto refused = presenter.Show(entries, RequestFor(harness, stage));
    presenter.SetTestHooks({});
    REQUIRE(refused.ok());
    CHECK(opened);
    CHECK_FALSE(timedOut);
    // Another process taking the foreground meanwhile would end it with 0 (the refused-foreground watch).
    if (refused->command == 0) TEST_NOTE("menu ended by a foreground change of another process");
    else CHECK_EQ(refused->command, 1u);
    CHECK(harness.ops.log == (std::vector<std::string>{"style activatable", "foreground owner", "post-null", "style noactivate"}));
}

AETHER_TEST("glass menu: mnemonics choose a unique entry and cycle through repeated ones") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Light),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut, 20'000);
    const auto request = RequestFor(harness, stage);

    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND, std::size_t) { ::PostMessageW(harness.owner.hwnd, WM_CHAR, L'P', 1); };
    presenter.SetTestHooks(hooks);
    const auto basic = BasicMenu();
    auto unique = presenter.Show(basic, request);
    REQUIRE(unique.ok());
    CHECK_EQ(unique->command, 3u);  // "&Pin to Dock" (the disabled "P&roperties" has R)

    // A disabled entry's mnemonic does nothing; Escape then dismisses.
    hooks.opened = [&](HWND, std::size_t) {
        ::PostMessageW(harness.owner.hwnd, WM_CHAR, L'r', 1);
        PostKey(harness.owner.hwnd, VK_ESCAPE);
    };
    presenter.SetTestHooks(hooks);
    auto disabled = presenter.Show(basic, request);
    REQUIRE(disabled.ok());
    CHECK_EQ(disabled->command, 0u);

    const std::vector<MenuEntry> sizes{MenuEntry::Command(10, L"&Small"), MenuEntry::Command(11, L"&Smaller"),
                                       MenuEntry::Command(12, L"&Medium")};
    hooks.opened = [&](HWND, std::size_t) {
        ::PostMessageW(harness.owner.hwnd, WM_CHAR, L's', 1);
        ::PostMessageW(harness.owner.hwnd, WM_CHAR, L'S', 1);
        PostKey(harness.owner.hwnd, VK_RETURN);
    };
    presenter.SetTestHooks(hooks);
    auto cycled = presenter.Show(sizes, request);
    presenter.SetTestHooks({});
    REQUIRE(cycled.ok());
    CHECK_FALSE(timedOut);
    CHECK_EQ(cycled->command, 11u);
}

AETHER_TEST("glass menu: Right opens a submenu beside its row, Enter chooses the nested id and Left closes it") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut, 20'000);
    std::vector<MenuEntry> sizes{MenuEntry::Command(10, L"&Small"), MenuEntry::Command(11, L"&Medium"),
                                 MenuEntry::Command(12, L"&Large")};
    sizes[1].checked = true;
    for (auto& entry : sizes) entry.radio = true;
    const std::vector<MenuEntry> entries{MenuEntry::Command(1, L"&Open"), MenuEntry::Submenu(L"Dock &size", sizes),
                                         MenuEntry::Separator(), MenuEntry::Command(2, L"&Quit\tAlt+F4")};
    const auto request = RequestFor(harness, stage);

    std::vector<std::size_t> depths;
    bool besideRow = false;
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND level, std::size_t depth) {
        depths.push_back(depth);
        if (depth == 0) {
            PostKey(harness.owner.hwnd, VK_DOWN);
            PostKey(harness.owner.hwnd, VK_DOWN);
            PostKey(harness.owner.hwnd, VK_RIGHT);
            return;
        }
        CHECK(::IsWindowVisible(level) != FALSE);
        const auto parentRow = presenter.RowBoundsPx(0, 1);
        const auto childRow = presenter.RowBoundsPx(1, 0);
        CHECK(parentRow.has_value());
        CHECK(childRow.has_value());
        if (parentRow && childRow) {
            besideRow = childRow->left >= parentRow->right && std::abs(childRow->top - parentRow->top) <= stage.Px(2.0f);
        }
        PostKey(harness.owner.hwnd, VK_DOWN);
        PostKey(harness.owner.hwnd, VK_RETURN);
    };
    presenter.SetTestHooks(hooks);
    auto nested = presenter.Show(entries, request);
    REQUIRE(nested.ok());
    CHECK_EQ(nested->command, 11u);
    CHECK(depths == (std::vector<std::size_t>{0, 1}));
    CHECK(besideRow);

    // Left closes the submenu; the keyboard is back in the root level.
    hooks.opened = [&](HWND, std::size_t depth) {
        if (depth == 0) {
            PostKey(harness.owner.hwnd, VK_DOWN);
            PostKey(harness.owner.hwnd, VK_DOWN);
            PostKey(harness.owner.hwnd, VK_RIGHT);
            return;
        }
        PostKey(harness.owner.hwnd, VK_LEFT);
        PostKey(harness.owner.hwnd, VK_DOWN);
        PostKey(harness.owner.hwnd, VK_RETURN);
    };
    presenter.SetTestHooks(hooks);
    auto back = presenter.Show(entries, request);
    presenter.SetTestHooks({});
    REQUIRE(back.ok());
    CHECK_FALSE(timedOut);
    CHECK_EQ(back->command, 2u);
}

AETHER_TEST("glass menu: a click on an enabled row chooses it and a click on a disabled row does nothing") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut);
    bool located = false;
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND level, std::size_t) {
        const auto disabled = presenter.RowBoundsPx(0, 1);
        const auto pin = presenter.RowBoundsPx(0, 3);
        located = disabled && pin;
        if (!located) {
            PostKey(harness.owner.hwnd, VK_ESCAPE);
            return;
        }
        // Posted to the capture window in its client coordinates, as the system delivers them.
        PostClick(level, Center(*disabled));
        PostClick(level, Center(*pin));
    };
    presenter.SetTestHooks(hooks);
    const auto entries = BasicMenu();
    auto selection = presenter.Show(entries, RequestFor(harness, stage));
    presenter.SetTestHooks({});
    REQUIRE(selection.ok());
    CHECK(located);
    CHECK_FALSE(timedOut);
    CHECK_EQ(selection->command, 3u);
}

namespace {

struct ClickCounter {
    HWND hwnd = nullptr;
    int clicks = 0;
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            ::SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        auto* self = reinterpret_cast<ClickCounter*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)) {
            ++self->clicks;
            return 0;
        }
        if (message == WM_MOUSEACTIVATE) return MA_NOACTIVATE;
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
    explicit ClickCounter(const RECT& rect) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = &Proc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(GRAY_BRUSH));
        wc.lpszClassName = L"AetherShell.Tests.GlassMenuClicks";
        if (!::RegisterClassExW(&wc) && ::GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
        hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"", WS_POPUP, rect.left,
                                 rect.top, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, wc.hInstance, this);
        if (hwnd) ::ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    ~ClickCounter() {
        if (hwnd) ::DestroyWindow(hwnd);
    }
    ClickCounter(const ClickCounter&) = delete;
    ClickCounter& operator=(const ClickCounter&) = delete;
};

}  // namespace

AETHER_TEST("glass menu: a click outside every level dismisses with 0 and never reaches the window underneath") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    const RECT counterRect{stage.CenterX() - stage.Px(380), stage.anchor.bottom + stage.Px(40), stage.CenterX() - stage.Px(220),
                           stage.anchor.bottom + stage.Px(140)};
    ClickCounter counter(counterRect);
    REQUIRE(counter.hwnd != nullptr);
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut);
    bool outside = false;
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND level, std::size_t) {
        RECT bounds{};
        ::GetWindowRect(level, &bounds);
        const POINT target = Center(counterRect);
        outside = !::PtInRect(&bounds, target);
        PostClick(level, target);
    };
    presenter.SetTestHooks(hooks);
    const auto entries = BasicMenu();
    auto selection = presenter.Show(entries, RequestFor(harness, stage));
    presenter.SetTestHooks({});
    REQUIRE(selection.ok());
    CHECK_FALSE(timedOut);
    CHECK(outside);
    CHECK_EQ(selection->command, 0u);
    RunFor(harness.loop, 120);
    CHECK_EQ(counter.clicks, 0);
    // The counter does count clicks that are delivered to it.
    ::PostMessageW(counter.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4, 4));
    RunFor(harness.loop, 50);
    CHECK_EQ(counter.clicks, 1);
}

AETHER_TEST("glass menu: Cancel from a timer and WM_QUIT both end the menu with 0") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    presenter.Cancel();  // nothing open: no effect
    CHECK_FALSE(presenter.IsOpen());
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut, 20'000);
    bool cancelled = false;
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND, std::size_t) {
        timers.After(150, [&] {
            cancelled = presenter.IsOpen();
            presenter.Cancel();
        });
    };
    presenter.SetTestHooks(hooks);
    const auto entries = BasicMenu();
    const auto request = RequestFor(harness, stage);
    const auto start = std::chrono::steady_clock::now();
    auto selection = presenter.Show(entries, request);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    REQUIRE(selection.ok());
    CHECK(cancelled);
    CHECK_EQ(selection->command, 0u);
    CHECK(elapsed >= 140);
    CHECK_FALSE(presenter.IsOpen());

    hooks.opened = [&](HWND, std::size_t) { ::PostQuitMessage(17); };
    presenter.SetTestHooks(hooks);
    auto quit = presenter.Show(entries, request);
    presenter.SetTestHooks({});
    REQUIRE(quit.ok());
    CHECK_FALSE(timedOut);
    CHECK_EQ(quit->command, 0u);
    // WM_QUIT is only retrieved once no other posted message is pending: drain the queue until it
    // shows up (it must not leak into the next test).
    MSG message{};
    bool reposted = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1000);
    while (!reposted && std::chrono::steady_clock::now() < deadline) {
        if (!::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::MsgWaitForMultipleObjects(0, nullptr, FALSE, 10, QS_ALLINPUT);
            continue;
        }
        if (message.message == WM_QUIT) {
            reposted = true;
            break;
        }
        ::TranslateMessage(&message);
        ::DispatchMessageW(&message);
    }
    CHECK(reposted);
    if (reposted) CHECK_EQ(static_cast<int>(message.wParam), 17);
}

AETHER_TEST("glass menu: the hover pill animates and then no frames are requested") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut);
    int frames = 0;
    int beforeHover = -1;
    int settled = -1;
    int later = -1;
    GlassMenuPresenter::TestHooks hooks;
    hooks.rendered = [&](std::size_t) { ++frames; };
    hooks.opened = [&](HWND level, std::size_t) {
        timers.After(350, [&, level] {
            beforeHover = frames;  // the entrance runs on the compositor
            if (const auto row = presenter.RowBoundsPx(0, 0)) {
                ::PostMessageW(level, WM_MOUSEMOVE, 0, ClientPoint(level, Center(*row)));
            }
            timers.After(450, [&] {
                settled = frames;
                timers.After(500, [&] {
                    later = frames;
                    PostKey(harness.owner.hwnd, VK_ESCAPE);
                });
            });
        });
    };
    presenter.SetTestHooks(hooks);
    const auto entries = BasicMenu();
    auto selection = presenter.Show(entries, RequestFor(harness, stage));
    presenter.SetTestHooks({});
    REQUIRE(selection.ok());
    CHECK_FALSE(timedOut);
    TEST_NOTE("frames: before hover " + std::to_string(beforeHover) + ", settled " + std::to_string(settled) + ", 500 ms later " +
              std::to_string(later));
    CHECK(beforeHover >= 1);
    CHECK(settled > beforeHover + 1);  // the pill tweened over several frames
    CHECK_EQ(later, settled);          // idle again: nothing requested after the hover settled
}

AETHER_TEST("glass menu: a 250-entry menu is height-clamped and scrolls to keep the selection visible") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut, 15'000);
    std::vector<MenuEntry> entries;
    for (std::uint32_t id = 1; id <= 250; ++id) entries.push_back(MenuEntry::Command(id, L"Item " + std::to_wstring(id)));
    auto request = RequestFor(harness, stage);
    request.keyboard = true;
    bool firstVisible = false;
    bool lastHidden = false;
    bool clamped = false;
    bool wheelScrolled = false;
    bool lastShown = false;
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND level, std::size_t) {
        const auto first = presenter.RowBoundsPx(0, 0);
        firstVisible = first.has_value();
        lastHidden = !presenter.RowBoundsPx(0, 249).has_value();
        std::size_t visible = 0;
        while (visible < 250 && presenter.RowBoundsPx(0, visible)) ++visible;
        if (visible > 0) {
            const auto bottom = presenter.RowBoundsPx(0, visible - 1);
            clamped = visible < 250 && bottom && bottom->bottom <= stage.work.bottom;
        }
        TEST_NOTE("rows fully visible without scrolling: " + std::to_string(visible));
        if (first) {
            const POINT point = Center(*first);
            ::PostMessageW(level, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<SHORT>(-2 * WHEEL_DELTA))),
                           MAKELPARAM(static_cast<WORD>(static_cast<SHORT>(point.x)), static_cast<WORD>(static_cast<SHORT>(point.y))));
        }
        timers.After(450, [&] {
            wheelScrolled = !presenter.RowBoundsPx(0, 0).has_value();
            PostKey(harness.owner.hwnd, VK_END);
            timers.After(450, [&] {
                const auto last = presenter.RowBoundsPx(0, 249);
                lastShown = last && last->bottom <= stage.work.bottom && last->top >= stage.work.top;
                PostKey(harness.owner.hwnd, VK_RETURN);
            });
        });
    };
    presenter.SetTestHooks(hooks);
    auto selection = presenter.Show(entries, request);
    presenter.SetTestHooks({});
    REQUIRE(selection.ok());
    CHECK_FALSE(timedOut);
    CHECK(firstVisible);
    CHECK(lastHidden);
    CHECK(clamped);
    CHECK(wheelScrolled);
    CHECK(lastShown);
    CHECK_EQ(selection->command, 250u);  // End selected the last row, which Enter chose
}

AETHER_TEST("glass menu: the close fade continues after Show returns, a new Show cancels it, reduced motion is instant") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    const auto tokens = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    GlassMenuPresenter presenter(harness.device, harness.scheduler, tokens, graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut, 20'000);
    const auto entries = BasicMenu();
    const auto request = RequestFor(harness, stage);
    HWND first = nullptr;
    HWND second = nullptr;
    bool firstGoneWhenSecondOpened = false;
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND level, std::size_t) {
        first = level;
        PostKey(harness.owner.hwnd, VK_ESCAPE);
    };
    presenter.SetTestHooks(hooks);
    REQUIRE(presenter.Show(entries, request).ok());
    REQUIRE(first != nullptr);
    CHECK(::IsWindowVisible(first) != FALSE);  // still fading out

    hooks.opened = [&](HWND level, std::size_t) {
        second = level;
        firstGoneWhenSecondOpened = ::IsWindow(first) == FALSE;
        PostKey(harness.owner.hwnd, VK_ESCAPE);
    };
    presenter.SetTestHooks(hooks);
    REQUIRE(presenter.Show(entries, request).ok());
    CHECK(firstGoneWhenSecondOpened);
    REQUIRE(second != nullptr);
    CHECK(::IsWindowVisible(second) != FALSE);
    const auto faded = RunUntil(harness.loop, [&] { return ::IsWindow(second) == FALSE; }, 1500);
    TEST_NOTE("closing level released " + std::to_string(faded) + " ms after Show returned");
    CHECK(::IsWindow(second) == FALSE);
    CHECK(faded <= 1000);

    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    presenter.SetTheme(tokens, reduced);
    HWND instant = nullptr;
    hooks.opened = [&](HWND level, std::size_t) {
        instant = level;
        PostKey(harness.owner.hwnd, VK_ESCAPE);
    };
    presenter.SetTestHooks(hooks);
    REQUIRE(presenter.Show(entries, request).ok());
    presenter.SetTestHooks({});
    REQUIRE(instant != nullptr);
    CHECK(::IsWindowVisible(instant) == FALSE);  // hidden before Show returned
    RunFor(harness.loop, 100);
    CHECK(::IsWindow(instant) == FALSE);
    CHECK_FALSE(timedOut);
}

AETHER_TEST("glass menu: UI Automation sees a Menu with MenuItems and Invoke chooses the command") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut, 20'000);
    std::vector<MenuEntry> entries = BasicMenu();
    entries.push_back(MenuEntry::Submenu(L"Dock si&ze", {MenuEntry::Command(10, L"Small")}));
    std::thread client;
    std::string failure;
    std::atomic<bool> invoked{false};
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND level, std::size_t depth) {
        if (depth != 0 || client.joinable()) return;
        const HWND owner = harness.owner.hwnd;
        client = std::thread([level, owner, &failure, &invoked] {
            platform::ComApartment mta(COINIT_MULTITHREADED);
            try {
                Need(mta.IsUsable(), "MTA");
                winrt::com_ptr<IUIAutomation> automation;
                NeedHr(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                          automation.put_void()),
                       "CUIAutomation");
                winrt::com_ptr<IUIAutomationElement> root;
                NeedHr(automation->ElementFromHandle(level, root.put()), "ElementFromHandle");
                BSTR name = nullptr;
                NeedHr(root->get_CurrentName(&name), "root name");
                Need(TakeBstr(name) == kOwnerTitle, "root name is the owner's name");
                CONTROLTYPEID type = 0;
                NeedHr(root->get_CurrentControlType(&type), "root type");
                Need(type == UIA_MenuControlTypeId, "root is a Menu");
                winrt::com_ptr<IUIAutomationCondition> all;
                NeedHr(automation->CreateTrueCondition(all.put()), "condition");
                winrt::com_ptr<IUIAutomationElementArray> children;
                NeedHr(root->FindAll(TreeScope_Children, all.get(), children.put()), "children");
                int count = 0;
                NeedHr(children->get_Length(&count), "count");
                Need(count == 5, "five items (the separator is not exposed)");
                const wchar_t* names[] = {L"Open", L"Properties", L"Pin to Dock", L"Dock settings…", L"Dock size"};
                winrt::com_ptr<IUIAutomationElement> pin;
                for (int index = 0; index < count; ++index) {
                    winrt::com_ptr<IUIAutomationElement> child;
                    NeedHr(children->GetElement(index, child.put()), "item");
                    BSTR itemName = nullptr;
                    NeedHr(child->get_CurrentName(&itemName), "item name");
                    Need(TakeBstr(itemName) == names[index], "item name without the mnemonic marker");
                    NeedHr(child->get_CurrentControlType(&type), "item type");
                    Need(type == UIA_MenuItemControlTypeId, "item is a MenuItem");
                    BOOL enabled = FALSE;
                    NeedHr(child->get_CurrentIsEnabled(&enabled), "enabled");
                    Need((enabled != FALSE) == (index != 1), "enabled state");
                    BSTR help = nullptr;
                    NeedHr(child->get_CurrentHelpText(&help), "help text");
                    const std::wstring helpText = TakeBstr(help);
                    if (index == 0) Need(helpText == L"Enter", "accelerator help text");
                    if (index == 4) Need(helpText == L"Opens a submenu", "submenu help text");
                    RECT bounds{};
                    NeedHr(child->get_CurrentBoundingRectangle(&bounds), "bounds");
                    Need(bounds.right > bounds.left && bounds.bottom > bounds.top, "visible bounds");
                    if (index == 2) pin = child;
                }
                winrt::com_ptr<IUIAutomationInvokePattern> invoke;
                NeedHr(pin->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), invoke.put_void()),
                       "invoke pattern");
                Need(invoke != nullptr, "invoke available");
                NeedHr(invoke->Invoke(), "Invoke");
                invoked = true;
            } catch (const std::exception& ex) {
                failure = ex.what();
            } catch (...) {
                failure = "UIA client exception";
            }
            if (!invoked) PostKey(owner, VK_ESCAPE);
        });
    };
    presenter.SetTestHooks(hooks);
    auto selection = presenter.Show(entries, RequestFor(harness, stage));
    presenter.SetTestHooks({});
    if (client.joinable()) client.join();
    REQUIRE(selection.ok());
    CHECK(failure.empty());
    if (!failure.empty()) TEST_NOTE(failure);
    CHECK(invoked.load());
    CHECK_FALSE(timedOut);
    CHECK_EQ(selection->command, 3u);
}

AETHER_TEST("glass menu: UI Automation expands submenus, toggles checks and selects radio options") {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    graphics::MotionPolicy reduced;
    reduced.reduced = true;
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark), reduced, harness.ops);
    std::vector<MenuEntry> entries{
        MenuEntry::Command(1, L"Checked command"), MenuEntry::Command(2, L"Other option"),
        MenuEntry::Command(3, L"Selected option"),
        MenuEntry::Submenu(L"More", {MenuEntry::Command(10, L"Nested command")})};
    entries[0].checked = true;
    entries[1].radio = entries[2].radio = true;
    entries[2].checked = true;
    for (int action = 0; action < 3; ++action) {
        LoopTimers timers(harness.loop);
        bool timedOut = false;
        Watchdog(timers, presenter, timedOut, 20'000);
        std::thread client;
        std::string failure;
        std::atomic<bool> completed{false};
        GlassMenuPresenter::TestHooks hooks;
        hooks.opened = [&](HWND level, std::size_t depth) {
            if (depth != 0 || client.joinable()) return;
            const HWND owner = harness.owner.hwnd;
            client = std::thread([level, owner, action, &failure, &completed] {
                platform::ComApartment mta(COINIT_MULTITHREADED);
                try {
                    Need(mta.IsUsable(), "MTA");
                    winrt::com_ptr<IUIAutomation> automation;
                    NeedHr(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()), "CUIAutomation");
                    winrt::com_ptr<IUIAutomationElement> root;
                    NeedHr(automation->ElementFromHandle(level, root.put()), "menu root");
                    winrt::com_ptr<IUIAutomationCondition> all;
                    NeedHr(automation->CreateTrueCondition(all.put()), "condition");
                    winrt::com_ptr<IUIAutomationElementArray> children;
                    int childCount = 0;
                    Need(::IsWindow(level) && ::IsWindowVisible(level), "menu remains visible while UIA projects it");
                    NeedHr(root->FindAll(TreeScope_Children, all.get(), children.put()), "menu children");
                    NeedHr(children->get_Length(&childCount), "menu child count");
                    Need(childCount == 4, ("four menu children, actual " + std::to_string(childCount)).c_str());
                    winrt::com_ptr<IUIAutomationElement> target;
                    NeedHr(children->GetElement(action == 0 ? 3 : action == 1 ? 0 : 1, target.put()), "target");
                    if (action == 0) {
                        winrt::com_ptr<IUIAutomationExpandCollapsePattern> expand;
                        NeedHr(target->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, __uuidof(IUIAutomationExpandCollapsePattern), expand.put_void()), "ExpandCollapse");
                        NeedHr(expand->Expand(), "Expand");
                        ExpandCollapseState state = ExpandCollapseState_LeafNode;
                        NeedHr(expand->get_CurrentExpandCollapseState(&state), "expanded state");
                        Need(state == ExpandCollapseState_Expanded, "submenu is expanded");
                        winrt::com_ptr<IUIAutomationTreeWalker> walker;
                        NeedHr(automation->get_RawViewWalker(walker.put()), "walker");
                        winrt::com_ptr<IUIAutomationElement> submenu;
                        NeedHr(walker->GetFirstChildElement(target.get(), submenu.put()), "submenu navigation");
                        Need(submenu != nullptr, "submenu is a child of its opening item");
                        CONTROLTYPEID type = 0;
                        NeedHr(submenu->get_CurrentControlType(&type), "submenu type");
                        Need(type == UIA_MenuControlTypeId, "submenu is Menu");
                        NeedHr(expand->Collapse(), "Collapse");
                        NeedHr(expand->get_CurrentExpandCollapseState(&state), "collapsed state");
                        Need(state == ExpandCollapseState_Collapsed, "submenu is collapsed");
                        NeedHr(expand->Expand(), "reopen submenu");
                        submenu = nullptr;
                        NeedHr(walker->GetFirstChildElement(target.get(), submenu.put()), "reopened submenu");
                        winrt::com_ptr<IUIAutomationElement> leaf;
                        NeedHr(walker->GetFirstChildElement(submenu.get(), leaf.put()), "submenu leaf");
                        Need(leaf != nullptr, "nested command is reachable");
                        winrt::com_ptr<IUIAutomationInvokePattern> invoke;
                        NeedHr(leaf->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), invoke.put_void()), "nested Invoke");
                        NeedHr(invoke->Invoke(), "invoke nested command");
                    } else if (action == 1) {
                        winrt::com_ptr<IUIAutomationTogglePattern> toggle;
                        NeedHr(target->GetCurrentPatternAs(UIA_TogglePatternId, __uuidof(IUIAutomationTogglePattern), toggle.put_void()), "Toggle");
                        ToggleState state = ToggleState_Off;
                        NeedHr(toggle->get_CurrentToggleState(&state), "toggle state");
                        Need(state == ToggleState_On, "checked command is On");
                        NeedHr(toggle->Toggle(), "toggle command");
                    } else {
                        winrt::com_ptr<IUIAutomationSelectionPattern> selection;
                        NeedHr(root->GetCurrentPatternAs(UIA_SelectionPatternId, __uuidof(IUIAutomationSelectionPattern), selection.put_void()), "Selection");
                        winrt::com_ptr<IUIAutomationElementArray> selected;
                        NeedHr(selection->GetCurrentSelection(selected.put()), "selected options");
                        int count = 0;
                        NeedHr(selected->get_Length(&count), "selection length");
                        Need(count == 1, "one actual selected radio option");
                        winrt::com_ptr<IUIAutomationSelectionItemPattern> option;
                        NeedHr(target->GetCurrentPatternAs(UIA_SelectionItemPatternId, __uuidof(IUIAutomationSelectionItemPattern), option.put_void()), "SelectionItem");
                        BOOL isSelected = TRUE;
                        NeedHr(option->get_CurrentIsSelected(&isSelected), "radio state");
                        Need(isSelected == FALSE, "other option is not selected");
                        winrt::com_ptr<IUIAutomationElement> container;
                        NeedHr(option->get_CurrentSelectionContainer(container.put()), "selection container");
                        Need(container != nullptr, "radio exposes a container");
                        NeedHr(option->Select(), "select other option");
                    }
                    completed.store(true);
                } catch (const std::exception& ex) {
                    failure = "action " + std::to_string(action) + ": " + ex.what();
                }
                if (!completed.load()) PostKey(owner, VK_ESCAPE);
            });
        };
        presenter.SetTestHooks(hooks);
        const auto result = presenter.Show(entries, RequestFor(harness, stage));
        presenter.SetTestHooks({});
        if (client.joinable()) client.join();
        if (!failure.empty()) TEST_NOTE("menu UIA: " + failure);
        REQUIRE(failure.empty());
        REQUIRE(result.ok());
        CHECK(completed.load());
        CHECK_FALSE(timedOut);
        CHECK_EQ(result->command, action == 0 ? 10u : action == 1 ? 1u : 2u);
    }
}

namespace {

void RunPixelProbe(graphics::ThemeMode mode, const char* name) {
    Harness harness;
    if (SkipForHighContrast()) return;
    const Stage stage = MakeStage();
    // The private backdrop covers everything the menu can draw on, inside the band clear of the
    // Top Bar and the Dock.
    RECT guarded{stage.CenterX() - stage.Px(420), stage.anchor.top - stage.Px(40), stage.CenterX() + stage.Px(420),
                 stage.anchor.bottom + stage.Px(470)};
    guarded.left = std::max(guarded.left, stage.work.left);
    guarded.right = std::min(guarded.right, stage.work.right);
    guarded.top = std::max(guarded.top, stage.work.top + 90);
    guarded.bottom = std::min(guarded.bottom, stage.work.bottom - 170);
    REQUIRE(guarded.bottom - guarded.top > stage.Px(360));
    Backdrop backdrop(guarded);
    REQUIRE(backdrop.hwnd != nullptr);
    RunFor(harness.loop, 150);

    auto tokens = graphics::MakeDesignTokens(mode);
    tokens.transparencyEnabled = true;
    GlassMenuPresenter presenter(harness.device, harness.scheduler, tokens, graphics::MotionPolicy{}, harness.ops);
    LoopTimers timers(harness.loop);
    bool timedOut = false;
    Watchdog(timers, presenter, timedOut, 20'000);
    const auto entries = LogoMenu();
    auto request = RequestFor(harness, stage);
    request.keyboard = true;  // underlines and the first row's pill, as after a keyboard open

    std::optional<Capture> patternA;
    std::optional<Capture> patternB;
    std::optional<Capture> white;
    RECT window{};
    std::optional<RECT> firstRow;
    std::optional<RECT> lastRow;
    std::optional<RECT> plainRow;  // "Follow system colors": no icon, accelerator or chevron on its right
    GlassMenuPresenter::TestHooks hooks;
    hooks.opened = [&](HWND level, std::size_t depth) {
        if (depth != 0) return;
        timers.After(450, [&, level] {
            ::GetWindowRect(level, &window);
            firstRow = presenter.RowBoundsPx(0, 0);
            lastRow = presenter.RowBoundsPx(0, entries.size() - 1);
            plainRow = presenter.RowBoundsPx(0, 3);
            RECT area = window;
            ::InflateRect(&area, 4, 4);
            patternA = CaptureWithRetry(backdrop.hwnd, guarded, area);
            backdrop.SetPhase(1);
            timers.After(350, [&, area] {
                patternB = CaptureWithRetry(backdrop.hwnd, guarded, area);
                backdrop.SetPhase(2);
                timers.After(350, [&, area] {
                    white = CaptureWithRetry(backdrop.hwnd, guarded, area);
                    PostKey(harness.owner.hwnd, VK_ESCAPE);
                });
            });
        });
    };
    presenter.SetTestHooks(hooks);
    auto selection = presenter.Show(entries, request);
    presenter.SetTestHooks({});
    REQUIRE(selection.ok());
    CHECK_FALSE(timedOut);
    CHECK(presenter.LastPresentation() == GlassMenuPresenter::Presentation::Glass);
    REQUIRE(firstRow.has_value());
    REQUIRE(lastRow.has_value());
    const bool captured = patternA && patternB && white;
    if (!captured) TEST_NOTE("capture inconclusive: the private backdrop stayed covered by a foreign window");
    REQUIRE(captured);

    SavePng(*patternA, InspectionPath(std::string("glassmenu-") + name + ".png"));
    SavePng(*white, InspectionPath(std::string("glassmenu-") + name + "-white.png"));
    TEST_NOTE(std::string("saved own-window glassmenu-") + name + ".png and glassmenu-" + name +
              "-white.png; host blur " + (harness.device.Capabilities().hostBackdrop ? "available" : "unavailable"));

    // Content is drawn (text, icons, separators) and the panel is translucent glass: its pixels
    // follow the private backdrop (measured right of a plain label, where nothing is drawn on it).
    REQUIRE(plainRow.has_value());
    const RECT interior{firstRow->left, firstRow->top, firstRow->right, lastRow->bottom};
    CHECK(LuminanceVariance(*patternA, interior) > 20.0);
    const RECT background{plainRow->right - (plainRow->right - plainRow->left) / 4, plainRow->top + 4,
                          plainRow->right - stage.Px(12), plainRow->bottom - 4};
    const double difference = MeanDifference(*patternA, *patternB, background);
    TEST_NOTE("panel response to the private backdrop: mean RGB delta " + std::to_string(difference));
    if (harness.device.Capabilities().hostBackdrop) CHECK(difference > 8.0);
    else TEST_NOTE("host blur unavailable: the translucency check is informational");

    // Body text stays readable on the glass whatever is behind it: the panel color right of a
    // plain label against the token text color, over the colored pattern and over white.
    const auto panelColor = [&](const Capture& capture) {
        double sum[3] = {0.0, 0.0, 0.0};
        int count = 0;
        const LONG y0 = (plainRow->top + plainRow->bottom) / 2 - 3;
        for (LONG y = y0; y < y0 + 6; ++y) {
            for (LONG x = plainRow->right - stage.Px(40); x < plainRow->right - stage.Px(14); ++x) {
                const auto* pixel = capture.At(x, y);
                for (int channel = 0; channel < 3; ++channel) sum[channel] += pixel[channel];
                ++count;
            }
        }
        return graphics::Color{static_cast<float>(sum[2] / count / 255.0), static_cast<float>(sum[1] / count / 255.0),
                               static_cast<float>(sum[0] / count / 255.0), 1.0f};
    };
    const auto& text = tokens.textPrimary;
    const float overPattern = graphics::ContrastRatio(text, panelColor(*patternA));
    const float overWhite = graphics::ContrastRatio(text, panelColor(*white));
    TEST_NOTE("body text contrast on the panel: " + std::to_string(overPattern) + ":1 over the pattern, " +
              std::to_string(overWhite) + ":1 over white");
    CHECK(overPattern >= 4.5f);
    CHECK(overWhite >= 4.5f);

    // Over white: the window's outermost pixels show the backdrop unchanged (the shadow fades out
    // inside the window, so no rectangular fragment is cut at its edges or corners)...
    int edgeMinimum = 255;
    for (LONG x = window.left; x < window.right; ++x) {
        for (const LONG y : {window.top, window.top + 1, window.bottom - 2, window.bottom - 1})
            edgeMinimum = std::min(edgeMinimum, MinChannel(white->At(x, y)));
    }
    for (LONG y = window.top; y < window.bottom; ++y) {
        for (const LONG x : {window.left, window.left + 1, window.right - 2, window.right - 1})
            edgeMinimum = std::min(edgeMinimum, MinChannel(white->At(x, y)));
    }
    TEST_NOTE("darkest channel on the window edge over white: " + std::to_string(edgeMinimum));
    CHECK(edgeMinimum >= 250);
    // ...and the shadow below the panel lightens steadily towards that edge.
    const LONG centerX = (firstRow->left + firstRow->right) / 2;
    const LONG panelBottom = lastRow->bottom + stage.Px(graphics::MenuTokens{}.padding);
    double previous = -1.0;
    double darkest = 255.0;
    bool monotonic = true;
    for (LONG y = panelBottom + 2; y < window.bottom; ++y) {
        const double l = Luminance(white->At(centerX, y));
        darkest = std::min(darkest, l);
        if (previous >= 0.0 && l + 3.0 < previous) monotonic = false;
        previous = l;
    }
    TEST_NOTE("shadow under the panel: darkest luminance " + std::to_string(darkest));
    CHECK(monotonic);
    if (mode == graphics::ThemeMode::Dark) CHECK(darkest < 245.0);
}

}  // namespace

AETHER_TEST("glass menu pixels: a dark logo-like menu is translucent glass with an unclipped shadow") {
    RunPixelProbe(graphics::ThemeMode::Dark, "dark");
}

AETHER_TEST("glass menu pixels: a light logo-like menu is translucent glass with an unclipped shadow") {
    RunPixelProbe(graphics::ThemeMode::Light, "light");
}

AETHER_TEST("glass menu: invalid menus are rejected and high contrast uses the native fallback") {
    Harness harness;
    const Stage stage = MakeStage();
    GlassMenuPresenter presenter(harness.device, harness.scheduler, graphics::MakeDesignTokens(graphics::ThemeMode::Dark),
                                 graphics::MotionPolicy{}, harness.ops);
    auto request = RequestFor(harness, stage);
    auto empty = presenter.Show(std::span<const MenuEntry>{}, request);
    REQUIRE_FALSE(empty.ok());
    CHECK(empty.error().code == ErrorCode::InvalidArgument);
    const std::vector<MenuEntry> repeated{MenuEntry::Command(1, L"One"), MenuEntry::Command(1, L"Again")};
    auto duplicate = presenter.Show(repeated, request);
    REQUIRE_FALSE(duplicate.ok());
    CHECK(duplicate.error().code == ErrorCode::InvalidArgument);
    request.owner = nullptr;
    const auto entries = BasicMenu();
    auto ownerless = presenter.Show(entries, request);
    REQUIRE_FALSE(ownerless.ok());
    CHECK(ownerless.error().code == ErrorCode::InvalidArgument);
    CHECK(harness.ops.log.empty());  // nothing was shown and the foreground was never touched

    // High contrast: the themed presenter's system-colored native menu (Track is faked, so no menu
    // appears), with the same command ids.
    presenter.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast), graphics::MotionPolicy{});
    harness.ops.trackCommand = 4;
    request.owner = harness.owner.hwnd;
    auto contrast = presenter.Show(entries, request);
    REQUIRE(contrast.ok());
    CHECK_EQ(contrast->command, 4u);
    CHECK(presenter.LastPresentation() == GlassMenuPresenter::Presentation::Native);
    CHECK(std::find(harness.ops.log.begin(), harness.ops.log.end(), "track") != harness.ops.log.end());
    CHECK_FALSE(presenter.IsOpen());
}
