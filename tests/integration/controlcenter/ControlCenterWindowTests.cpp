// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "controlcenter/ControlCenterWindow.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Win32UiDispatcher.h"

#include <UIAutomation.h>
#include <wincodec.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::controlcenter;

namespace {

void Need(bool value) { if (!value) throw std::runtime_error("own quick-controls assertion failed"); }
void Hr(HRESULT value) { if (FAILED(value)) throw std::runtime_error("own quick-controls UIA call failed"); }

void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    REQUIRE(timer != nullptr);
    LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != FALSE);
    const auto id = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    REQUIRE(id != 0);
    loop.Run(); loop.RemoveWaitHandle(id); ::CloseHandle(timer);
}

ControlCenterState Synthetic() {
    ControlCenterState state;
    state.network.state = status::ProviderState::Available;
    state.network.kind = status::ConnectionKind::WiFi;
    state.network.signalBars = std::uint8_t{4};
    state.audio = {status::ProviderState::Available, true, 0.6f, false, L"Synthetic output", {}};
    state.battery.state = status::ProviderState::Available;
    state.battery.batteryPresent = true; state.battery.percent = 78;
    state.system.windowsBuild = 22621;
    state.system.wifi.present = true; state.system.wifi.enabled = true; state.system.wifi.canToggle = true;
    state.system.bluetooth.present = true; state.system.bluetooth.enabled = false; state.system.bluetooth.canToggle = true;
    state.brightness = {42, true}; state.mediaAvailable = true;
    state.media.sessions.push_back({11, L"Synthetic title", L"Synthetic artist", MediaPlayback::Playing, true, true, true});
    state.media.sessions.push_back({22, L"Second synthetic title", L"Second synthetic artist", MediaPlayback::Paused, true, false, false});
    state.media.selectedSession = 11;
    return state;
}

struct Fixture {
    platform::ComApartment apartment;
    platform::MessageLoop loop;
    graphics::GraphicsDevice device;
    graphics::FrameScheduler scheduler;
    platform::Win32UiDispatcher dispatcher;
    std::unique_ptr<ControlCenterWindow> window;
    std::vector<ControlIntent> intents;
    bool wrongThread = false;
    Connection requested;
    ~Fixture() {
        requested.Disconnect(); window.reset(); dispatcher.Shutdown(); scheduler.Shutdown(); device.Shutdown();
    }
    void Initialize(ControlCenterState state = Synthetic()) {
        REQUIRE(SUCCEEDED(apartment.Status()));
        REQUIRE(device.Initialize().ok()); REQUIRE(scheduler.Initialize(loop).ok());
        REQUIRE(dispatcher.Initialize().ok());
        window = std::make_unique<ControlCenterWindow>(device, scheduler, dispatcher);
        REQUIRE(window->Initialize().ok());
        window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1});
        window->SetState(std::move(state));
        const DWORD owner = ::GetCurrentThreadId();
        // No application host, providers or action routes are connected. Every intent ends here.
        requested = window->Requested.Connect([this, owner](ControlIntent intent) {
            wrongThread |= ::GetCurrentThreadId() != owner; intents.push_back(std::move(intent));
        });
        Open();
    }
    void Open() {
        const auto monitor = platform::PrimaryMonitor();
        const RECT anchor{monitor.workArea.left + 120, monitor.workArea.top + 90,
                          monitor.workArea.left + 600, monitor.workArea.top + 110};
        REQUIRE(window->Toggle(anchor).ok()); REQUIRE(window->IsShown());
    }
};

std::wstring Name(IUIAutomationElement& element) {
    BSTR name = nullptr; Hr(element.get_CurrentName(&name));
    std::wstring result(name ? name : L""); ::SysFreeString(name); return result;
}
winrt::com_ptr<IUIAutomationElement> Root(IUIAutomation& automation, HWND hwnd) {
    winrt::com_ptr<IUIAutomationElement> root; Hr(automation.ElementFromHandle(hwnd, root.put())); Need(root != nullptr);
    CONTROLTYPEID type = 0; Hr(root->get_CurrentControlType(&type)); Need(type == UIA_PaneControlTypeId);
    UIA_HWND native = nullptr; Hr(root->get_CurrentNativeWindowHandle(&native)); Need(native == hwnd);
    Need(Name(*root) == L"Aether Shell quick controls");
    winrt::com_ptr<IUIAutomationCondition> all; Hr(automation.CreateTrueCondition(all.put()));
    winrt::com_ptr<IUIAutomationElementArray> children; Hr(root->FindAll(TreeScope_Children, all.get(), children.put()));
    int count = 0; Hr(children->get_Length(&count)); Need(count == 20);
    // Name, identity and children are mandatory: the default HWND Pane is not our provider.
    return root;
}
winrt::com_ptr<IUIAutomationElement> Item(IUIAutomation& automation, IUIAutomationElement& root, int id) {
    VARIANT value{}; value.vt = VT_BSTR;
    value.bstrVal = ::SysAllocString((L"item-" + std::to_wstring(id)).c_str()); Need(value.bstrVal != nullptr);
    winrt::com_ptr<IUIAutomationCondition> condition;
    const auto result = automation.CreatePropertyCondition(UIA_AutomationIdPropertyId, value, condition.put());
    ::VariantClear(&value); Hr(result);
    winrt::com_ptr<IUIAutomationElement> item; Hr(root.FindFirst(TreeScope_Children, condition.get(), item.put()));
    Need(item != nullptr); return item;
}
winrt::com_ptr<IUIAutomationRangeValuePattern> Range(IUIAutomationElement& item) {
    CONTROLTYPEID type = 0; Hr(item.get_CurrentControlType(&type)); Need(type == UIA_SliderControlTypeId);
    winrt::com_ptr<IUIAutomationRangeValuePattern> range;
    Hr(item.GetCurrentPatternAs(UIA_RangeValuePatternId, __uuidof(IUIAutomationRangeValuePattern), range.put_void()));
    Need(range != nullptr); return range;
}
void Invoke(IUIAutomationElement& item) {
    winrt::com_ptr<IUIAutomationInvokePattern> action;
    Hr(item.GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), action.put_void()));
    Need(action != nullptr); Hr(action->Invoke());
}

// A timed test process must also supply an outer timeout: an unresponsive COM provider cannot be
// safely interrupted by destroying its HWND. Keep the fixture alive and join, never detach a sender.
void Client(Fixture& fixture, const std::function<void(IUIAutomation&, HWND)>& action) {
    std::atomic<bool> finished{false}; bool passed = false;
    const HWND hwnd = fixture.window->Handle();
    std::thread client([&] {
        platform::ComApartment apartment(COINIT_MULTITHREADED);
        try {
            Hr(apartment.Status()); winrt::com_ptr<IUIAutomation> automation;
            Hr(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                  __uuidof(IUIAutomation), automation.put_void()));
            action(*automation, hwnd); passed = true;
        } catch (...) { /* Fixed failure below; never expose external UIA exception text. */ }
        finished.store(true);
    });
    const auto deadline = ::GetTickCount64() + 5000;
    while (!finished.load() && ::GetTickCount64() < deadline) RunFor(fixture.loop, 10);
    const bool withinDeadline = finished.load();
    client.join();
    REQUIRE(withinDeadline); REQUIRE(passed);
}

void Click(ControlCenterWindow& window, const RECT& screen, UINT button = WM_LBUTTONDOWN) {
    POINT point{screen.left + (screen.right - screen.left) / 2, screen.top + (screen.bottom - screen.top) / 2};
    REQUIRE(::ScreenToClient(window.Handle(), &point) != FALSE);
    ::SendMessageW(window.Handle(), button, MK_LBUTTON, MAKELPARAM(point.x, point.y));
    ::SendMessageW(window.Handle(), button == WM_RBUTTONDOWN ? WM_RBUTTONUP : WM_LBUTTONUP,
                   0, MAKELPARAM(point.x, point.y));
}

LRESULT CALLBACK BackdropProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{}; HDC dc = ::BeginPaint(hwnd, &paint); RECT area{}; ::GetClientRect(hwnd, &area);
        for (LONG x = 0; x < area.right; x += 24) {
            HBRUSH brush = ::CreateSolidBrush((x / 24) % 2 ? RGB(34, 76, 105) : RGB(98, 55, 40));
            RECT band{x, 0, x + 24, area.bottom}; ::FillRect(dc, &band, brush); ::DeleteObject(brush);
        }
        ::EndPaint(hwnd, &paint); return 0;
    }
    return ::DefWindowProcW(hwnd, message, wParam, lParam);
}
struct Backdrop {
    HWND hwnd = nullptr;
    ~Backdrop() { if (hwnd) ::DestroyWindow(hwnd); }
    void Create(const RECT& bounds) {
        WNDCLASSW cls{}; cls.hInstance = ::GetModuleHandleW(nullptr); cls.lpfnWndProc = BackdropProc;
        cls.lpszClassName = L"Aether.ControlCenter.TestBackdrop";
        REQUIRE(::RegisterClassW(&cls) || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName,
            L"", WS_POPUP, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top,
            nullptr, nullptr, cls.hInstance, nullptr);
        REQUIRE(hwnd != nullptr); ::ShowWindow(hwnd, SW_SHOWNOACTIVATE); ::UpdateWindow(hwnd);
    }
};
bool Guarded(HWND backdrop, HWND panel, const RECT& area) {
    RECT bounds{}; DWORD owner = 0; ::GetWindowThreadProcessId(backdrop, &owner);
    if (owner != ::GetCurrentProcessId() || !::IsWindowVisible(backdrop) || !::GetWindowRect(backdrop, &bounds) ||
        bounds.left > area.left || bounds.top > area.top || bounds.right < area.right || bounds.bottom < area.bottom) return false;
    ::GetWindowThreadProcessId(panel, &owner); if (owner != ::GetCurrentProcessId()) return false;
    for (HWND other = ::GetTopWindow(nullptr); other; other = ::GetWindow(other, GW_HWNDNEXT)) {
        if (other == backdrop) return true;
        if (other == panel || !::IsWindowVisible(other)) continue;
        RECT rect{}, overlap{};
        if (::GetWindowRect(other, &rect) && ::IntersectRect(&overlap, &rect, &area)) return false;
    }
    return false;
}
bool CaptureOwn(HWND backdrop, HWND panel, const RECT& guarded, const RECT& crop, const std::filesystem::path& file) {
    if (!Guarded(backdrop, panel, guarded)) return false;
    Need(crop.left >= guarded.left && crop.top >= guarded.top && crop.right <= guarded.right && crop.bottom <= guarded.bottom);
    const int width = crop.right - crop.left, height = crop.bottom - crop.top;
    HDC screen = ::GetDC(nullptr), memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen); return false;
    }
    const auto previous = ::SelectObject(memory, bitmap);
    const bool copied = ::BitBlt(memory, 0, 0, width, height, screen, crop.left, crop.top, SRCCOPY | CAPTUREBLT) != FALSE;
    std::vector<std::uint8_t> bytes;
    if (copied) bytes.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) + static_cast<std::size_t>(width) * height * 4);
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen);
    if (!copied || !Guarded(backdrop, panel, guarded)) return false; // discard any unguarded pixels
    for (std::size_t i = 3; i < bytes.size(); i += 4) bytes[i] = 255;
    auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICStream> stream; Hr(factory->CreateStream(stream.put()));
    Hr(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder; Hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    Hr(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame; Hr(encoder->CreateNewFrame(frame.put(), nullptr)); Hr(frame->Initialize(nullptr));
    Hr(frame->SetSize(width, height)); WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Hr(frame->SetPixelFormat(&format)); Need(format == GUID_WICPixelFormat32bppBGRA);
    Hr(frame->WritePixels(height, width * 4, static_cast<UINT>(bytes.size()), bytes.data()));
    Hr(frame->Commit()); Hr(encoder->Commit()); return true;
}
} // namespace

AETHER_TEST("controlcenter integration: UIA discovers synthetic children and routes range intents") {
    Fixture fixture; fixture.Initialize();
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        for (const auto [id, expected] : {std::pair{300, 42.0}, std::pair{301, 60.0}}) {
            auto item = Item(automation, *root, id); auto range = Range(*item);
            BOOL enabled = FALSE, readOnly = TRUE; Hr(item->get_CurrentIsEnabled(&enabled)); Need(enabled);
            Hr(range->get_CurrentIsReadOnly(&readOnly)); Need(!readOnly);
            double value = -1; Hr(range->get_CurrentValue(&value)); Need(value == expected);
            Hr(range->get_CurrentMinimum(&value)); Need(value == 0);
            Hr(range->get_CurrentMaximum(&value)); Need(value == 100);
            Hr(range->SetValue(id == 300 ? 31 : 67));
            // Only the host can replace the observed snapshot; intent alone must not fake readback.
            Hr(range->get_CurrentValue(&value)); Need(value == expected);
        }
        auto play = Item(automation, *root, 401); Invoke(*play);
        auto source = Item(automation, *root, 403); Invoke(*source);
    });
    RunFor(fixture.loop, 30);
    REQUIRE_EQ(fixture.intents.size(), 4u);
    CHECK(fixture.intents[0].kind == ControlIntentKind::Brightness); CHECK(fixture.intents[0].percent == 31);
    CHECK(fixture.intents[1].kind == ControlIntentKind::Volume); CHECK(fixture.intents[1].percent == 67);
    CHECK(fixture.intents[2].kind == ControlIntentKind::MediaPlayPause); CHECK_EQ(fixture.intents[2].session, 11u);
    CHECK(fixture.intents[3].kind == ControlIntentKind::SelectMedia); CHECK_EQ(fixture.intents[3].session, 22u);
    CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: split radio arrows have separate accessible hit bounds") {
    Fixture fixture; fixture.Initialize(); RECT wifi{}, wifiArrow{}, bluetooth{}, bluetoothArrow{};
    Client(fixture, [&](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        const auto bounds = [&](int id, RECT& rect) {
            auto item = Item(automation, *root, id); CONTROLTYPEID type = 0;
            Hr(item->get_CurrentControlType(&type)); Need(type == UIA_ButtonControlTypeId);
            Hr(item->get_CurrentBoundingRectangle(&rect)); Need(rect.right > rect.left && rect.bottom > rect.top);
        };
        bounds(100, wifi); bounds(200, wifiArrow); bounds(101, bluetooth); bounds(201, bluetoothArrow);
    });
    for (const auto& pair : {std::pair{wifi, wifiArrow}, std::pair{bluetooth, bluetoothArrow}}) {
        CHECK_EQ(pair.first.right, pair.second.left); CHECK_EQ(pair.first.top, pair.second.top);
        CHECK_EQ(pair.first.bottom, pair.second.bottom);
    }
    Click(*fixture.window, wifiArrow); Click(*fixture.window, wifi);
    Click(*fixture.window, bluetoothArrow); Click(*fixture.window, bluetooth);
    REQUIRE_EQ(fixture.intents.size(), 4u);
    CHECK(fixture.intents[0].kind == ControlIntentKind::SystemSettings); CHECK(fixture.intents[0].control == SystemControl::WiFi);
    CHECK_FALSE(fixture.intents[0].enabled.has_value());
    CHECK(fixture.intents[1].kind == ControlIntentKind::SystemTile); CHECK(fixture.intents[1].enabled == false);
    CHECK(fixture.intents[2].kind == ControlIntentKind::SystemSettings); CHECK(fixture.intents[2].control == SystemControl::Bluetooth);
    CHECK(fixture.intents[3].kind == ControlIntentKind::SystemTile); CHECK(fixture.intents[3].enabled == true);
    Click(*fixture.window, wifiArrow, WM_RBUTTONDOWN); CHECK_EQ(fixture.intents.size(), 4u);
    CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: unknown ranges stay unavailable and closed queues cannot act") {
    Fixture fixture; fixture.Initialize(ControlCenterState{});
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        for (const int id : {300, 301}) {
            auto item = Item(automation, *root, id); BOOL enabled = TRUE;
            Hr(item->get_CurrentIsEnabled(&enabled)); Need(!enabled);
            VARIANT value{}; Hr(item->GetCurrentPropertyValue(UIA_IsRangeValuePatternAvailablePropertyId, &value));
            const bool absent = value.vt == VT_BOOL && value.boolVal == VARIANT_FALSE; ::VariantClear(&value); Need(absent);
            Hr(item->GetCurrentPropertyValueEx(UIA_RangeValueValuePropertyId, TRUE, &value));
            BOOL unsupported = FALSE; const auto checked = automation.CheckNotSupported(value, &unsupported);
            ::VariantClear(&value); Hr(checked); Need(unsupported); // unavailable is not a fabricated zero
        }
    });
    fixture.window->SetState(Synthetic());
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd); auto volume = Item(automation, *root, 301); Hr(volume->SetFocus());
    });
    ::SendMessageW(fixture.window->Handle(), WM_KEYDOWN, VK_RIGHT, 0);
    ::SendMessageW(fixture.window->Handle(), WM_KEYDOWN, VK_RIGHT, 0);
    REQUIRE_EQ(fixture.intents.size(), 2u); CHECK(fixture.intents[0].percent == 61); CHECK(fixture.intents[1].percent == 62);
    ::SendMessageW(fixture.window->Handle(), WM_KEYDOWN, VK_END, 0);
    REQUIRE_EQ(fixture.intents.size(), 3u); CHECK(fixture.intents.back().kind == ControlIntentKind::Volume);
    CHECK(fixture.intents.back().percent == 100);
    ::SendMessageW(fixture.window->Handle(), WM_KEYDOWN, VK_ESCAPE, 0); CHECK_FALSE(fixture.window->IsShown());
    ::SendMessageW(fixture.window->Handle(), WM_KEYDOWN, VK_END, 0); CHECK_EQ(fixture.intents.size(), 3u);
    fixture.Open(); fixture.intents.clear();
    std::vector<MSG> deferred;
    const auto filter = fixture.loop.AddMessageFilter([&](const MSG& message) {
        if (message.hwnd == fixture.window->Handle() && message.message >= WM_APP && message.message < WM_APP + 4096) {
            deferred.push_back(message); return true;
        }
        return false;
    });
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd); auto sound = Item(automation, *root, 310); Invoke(*sound);
        auto volume = Item(automation, *root, 301); Hr(Range(*volume)->SetValue(77));
    });
    RunFor(fixture.loop, 20); fixture.loop.RemoveMessageFilter(filter);
    REQUIRE_EQ(deferred.size(), 2u); CHECK(fixture.intents.empty());
    fixture.window->Close();
    for (const auto& message : deferred) ::DispatchMessageW(&message);
    RunFor(fixture.loop, 20); CHECK(fixture.intents.empty());
    fixture.Open();
    RECT suggested = fixture.window->BoundsPx(); const UINT dpi = fixture.window->Dpi() == 144 ? 96 : 144;
    ::SendMessageW(fixture.window->Handle(), WM_DPICHANGED, MAKELPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&suggested));
    CHECK_EQ(fixture.window->Dpi(), dpi); CHECK_FALSE(fixture.window->IsShown()); CHECK(fixture.intents.empty());
    CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: optional own-backdrop dark and light render receipts") {
    Fixture fixture; fixture.Initialize();
    RECT guarded = fixture.window->BoundsPx(); ::InflateRect(&guarded, 96, 96);
    Backdrop backdrop; backdrop.Create(guarded);
    // A unique directory ensures a refused capture cannot be mistaken for an older receipt.
    const auto output = test::MakeTempDirectory("controlcenter-render");
    for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::Light}) {
        fixture.window->SetTheme(graphics::MakeDesignTokens(mode), graphics::MotionPolicy{true, 1});
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(fixture.window->Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // This presentation-only class exposes no frame counter. Pump its scheduler before an
        // optional receipt; functional/UIA assertions live in the other cases.
        RunFor(fixture.loop, 120);
        const bool captured = CaptureOwn(backdrop.hwnd, fixture.window->Handle(), guarded, fixture.window->BoundsPx(),
            output / (mode == graphics::ThemeMode::Dark ? L"controlcenter-dark.png" : L"controlcenter-light.png"));
        if (!captured) test::Note("Quick-controls visual receipt unavailable: own-backdrop capture guard refused; no pixels saved.");
    }
    CHECK(fixture.intents.empty()); CHECK_FALSE(fixture.wrongThread);
}
