// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// The Aether-owned quick-controls panel (D44) with synthetic snapshots only: no host, provider or
// action route is connected, so every intent ends in the fixture and no system setting changes.
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
#include <string>
#include <string_view>
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

ControlCenterState Synthetic(bool media = true) {
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
    state.system.stickyKeys = false;
    state.brightness = {42, true};
    if (media) {
        state.mediaAvailable = true;
        state.media.sessions.push_back({11, L"Synthetic title", L"Synthetic artist", MediaPlayback::Playing, true, true, true});
        state.media.sessions.push_back({22, L"Second synthetic title", L"Second synthetic artist", MediaPlayback::Paused, true, false, false});
        state.media.selectedSession = 11;
    }
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
    int closed = 0;
    bool wrongThread = false;
    Connection requested, closedConnection;
    RECT anchor{};
    ~Fixture() {
        requested.Disconnect(); closedConnection.Disconnect();
        window.reset(); dispatcher.Shutdown(); scheduler.Shutdown(); device.Shutdown();
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
        closedConnection = window->Closed.Connect([this] { ++closed; });
        Open();
    }
    // A Top Bar group-sized anchor inside the primary work area.
    void Open() {
        const auto monitor = platform::PrimaryMonitor();
        anchor = RECT{monitor.workArea.left + 420, monitor.workArea.top + 90, monitor.workArea.left + 700, monitor.workArea.top + 126};
        REQUIRE(window->Toggle(anchor).ok()); REQUIRE(window->IsShown());
    }
};

std::wstring Name(IUIAutomationElement& element) {
    BSTR name = nullptr; Hr(element.get_CurrentName(&name));
    std::wstring result(name ? name : L""); ::SysFreeString(name); return result;
}
std::wstring Help(IUIAutomationElement& element) {
    BSTR help = nullptr; Hr(element.get_CurrentHelpText(&help));
    std::wstring result(help ? help : L""); ::SysFreeString(help); return result;
}
bool EndsWith(const std::wstring& value, std::wstring_view suffix) {
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}
std::vector<int> ChildIds(IUIAutomation& automation, IUIAutomationElement& root) {
    winrt::com_ptr<IUIAutomationCondition> all; Hr(automation.CreateTrueCondition(all.put()));
    winrt::com_ptr<IUIAutomationElementArray> children; Hr(root.FindAll(TreeScope_Children, all.get(), children.put()));
    int count = 0; Hr(children->get_Length(&count));
    std::vector<int> ids;
    for (int i = 0; i < count; ++i) {
        winrt::com_ptr<IUIAutomationElement> child; Hr(children->GetElement(i, child.put()));
        BSTR id = nullptr; Hr(child->get_CurrentAutomationId(&id));
        const std::wstring value(id ? id : L""); ::SysFreeString(id);
        Need(value.rfind(L"item-", 0) == 0);
        ids.push_back(std::stoi(value.substr(5)));
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}
winrt::com_ptr<IUIAutomationElement> Root(IUIAutomation& automation, HWND hwnd) {
    winrt::com_ptr<IUIAutomationElement> root; Hr(automation.ElementFromHandle(hwnd, root.put())); Need(root != nullptr);
    CONTROLTYPEID type = 0; Hr(root->get_CurrentControlType(&type)); Need(type == UIA_PaneControlTypeId);
    UIA_HWND native = nullptr; Hr(root->get_CurrentNativeWindowHandle(&native)); Need(native == hwnd);
    // Name, identity and children are mandatory: the default HWND Pane is not our provider.
    Need(Name(*root) == L"Aether Shell quick controls");
    Need(!ChildIds(automation, *root).empty());
    return root;
}
winrt::com_ptr<IUIAutomationElement> Find(IUIAutomation& automation, IUIAutomationElement& root, int id) {
    VARIANT value{}; value.vt = VT_BSTR;
    value.bstrVal = ::SysAllocString((L"item-" + std::to_wstring(id)).c_str()); Need(value.bstrVal != nullptr);
    winrt::com_ptr<IUIAutomationCondition> condition;
    const auto result = automation.CreatePropertyCondition(UIA_AutomationIdPropertyId, value, condition.put());
    ::VariantClear(&value); Hr(result);
    winrt::com_ptr<IUIAutomationElement> item; Hr(root.FindFirst(TreeScope_Children, condition.get(), item.put()));
    return item;
}
winrt::com_ptr<IUIAutomationElement> Item(IUIAutomation& automation, IUIAutomationElement& root, int id) {
    auto item = Find(automation, root, id); Need(item != nullptr); return item;
}
bool Enabled(IUIAutomationElement& item) { BOOL enabled = FALSE; Hr(item.get_CurrentIsEnabled(&enabled)); return enabled != FALSE; }
winrt::com_ptr<IUIAutomationRangeValuePattern> Range(IUIAutomationElement& item) {
    CONTROLTYPEID type = 0; Hr(item.get_CurrentControlType(&type)); Need(type == UIA_SliderControlTypeId);
    winrt::com_ptr<IUIAutomationRangeValuePattern> range;
    Hr(item.GetCurrentPatternAs(UIA_RangeValuePatternId, __uuidof(IUIAutomationRangeValuePattern), range.put_void()));
    Need(range != nullptr); return range;
}
// The genuine toggle state, or nullopt when the element is not a two-state control at all.
std::optional<ToggleState> Toggled(IUIAutomationElement& item) {
    VARIANT available{}; Hr(item.GetCurrentPropertyValue(UIA_IsTogglePatternAvailablePropertyId, &available));
    const bool supported = available.vt == VT_BOOL && available.boolVal == VARIANT_TRUE; ::VariantClear(&available);
    if (!supported) return std::nullopt;
    winrt::com_ptr<IUIAutomationTogglePattern> toggle;
    Hr(item.GetCurrentPatternAs(UIA_TogglePatternId, __uuidof(IUIAutomationTogglePattern), toggle.put_void()));
    Need(toggle != nullptr); ToggleState state = ToggleState_Indeterminate; Hr(toggle->get_CurrentToggleState(&state));
    return state;
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
// Invokes are queued to the panel's own thread; let them run before inspecting the result.
void InvokeIds(Fixture& fixture, std::initializer_list<int> ids) {
    for (const int id : ids) {
        Client(fixture, [id](IUIAutomation& automation, HWND hwnd) { auto root = Root(automation, hwnd); Invoke(*Item(automation, *root, id)); });
        RunFor(fixture.loop, 20);
    }
}
std::vector<int> Ids(Fixture& fixture) {
    std::vector<int> ids;
    Client(fixture, [&](IUIAutomation& automation, HWND hwnd) { auto root = Root(automation, hwnd); ids = ChildIds(automation, *root); });
    return ids;
}
bool Has(const std::vector<int>& ids, int id) { return std::find(ids.begin(), ids.end(), id) != ids.end(); }

void Click(ControlCenterWindow& window, const RECT& screen, UINT button = WM_LBUTTONDOWN) {
    POINT point{screen.left + (screen.right - screen.left) / 2, screen.top + (screen.bottom - screen.top) / 2};
    REQUIRE(::ScreenToClient(window.Handle(), &point) != FALSE);
    ::SendMessageW(window.Handle(), button, MK_LBUTTON, MAKELPARAM(point.x, point.y));
    ::SendMessageW(window.Handle(), button == WM_RBUTTONDOWN ? WM_RBUTTONUP : WM_LBUTTONUP,
                   0, MAKELPARAM(point.x, point.y));
}
void Key(ControlCenterWindow& window, UINT key) { ::SendMessageW(window.Handle(), WM_KEYDOWN, key, 0); }

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
// Screen pixels of `crop`, kept only when the private backdrop is the sole content below our own
// windows before and after the copy (empty otherwise). Alpha is forced opaque.
std::vector<std::uint8_t> Grab(HWND backdrop, HWND panel, const RECT& guarded, const RECT& crop) {
    if (!Guarded(backdrop, panel, guarded)) return {};
    Need(crop.left >= guarded.left && crop.top >= guarded.top && crop.right <= guarded.right && crop.bottom <= guarded.bottom);
    const int width = crop.right - crop.left, height = crop.bottom - crop.top;
    HDC screen = ::GetDC(nullptr), memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen); return {};
    }
    const auto previous = ::SelectObject(memory, bitmap);
    const bool copied = ::BitBlt(memory, 0, 0, width, height, screen, crop.left, crop.top, SRCCOPY | CAPTUREBLT) != FALSE;
    std::vector<std::uint8_t> bytes;
    if (copied) bytes.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) + static_cast<std::size_t>(width) * height * 4);
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen);
    if (!copied || !Guarded(backdrop, panel, guarded)) return {}; // discard any unguarded pixels
    for (std::size_t i = 3; i < bytes.size(); i += 4) bytes[i] = 255;
    return bytes;
}
void SavePng(const std::vector<std::uint8_t>& bytes, int width, int height, const std::filesystem::path& file) {
    auto factory = winrt::create_instance<IWICImagingFactory>(CLSID_WICImagingFactory);
    winrt::com_ptr<IWICStream> stream; Hr(factory->CreateStream(stream.put()));
    Hr(stream->InitializeFromFilename(file.c_str(), GENERIC_WRITE));
    winrt::com_ptr<IWICBitmapEncoder> encoder; Hr(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.put()));
    Hr(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));
    winrt::com_ptr<IWICBitmapFrameEncode> frame; Hr(encoder->CreateNewFrame(frame.put(), nullptr)); Hr(frame->Initialize(nullptr));
    Hr(frame->SetSize(width, height)); WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Hr(frame->SetPixelFormat(&format)); Need(format == GUID_WICPixelFormat32bppBGRA);
    Hr(frame->WritePixels(height, width * 4, static_cast<UINT>(bytes.size()), const_cast<BYTE*>(bytes.data())));  // WIC reads only
    Hr(frame->Commit()); Hr(encoder->Commit());
}
bool CaptureOwn(HWND backdrop, HWND panel, const RECT& guarded, const RECT& crop, const std::filesystem::path& file) {
    const auto bytes = Grab(backdrop, panel, guarded, crop);
    if (bytes.empty()) return false;
    SavePng(bytes, crop.right - crop.left, crop.bottom - crop.top, file);
    return true;
}

LONG Px(const ControlCenterWindow& window, float dips) { return static_cast<LONG>(std::lround(dips * static_cast<float>(window.Dpi()) / 96.0f)); }

} // namespace

AETHER_TEST("controlcenter integration: the main view exposes the reference layout and routes range intents") {
    Fixture fixture; fixture.Initialize();
    // Media card (403, 400-402), page one's split Wi-Fi/Bluetooth and four action tiles, the paging
    // column, both sliders with mute and output, and the battery/Settings footer.
    const std::vector<int> expected{100, 101, 102, 103, 104, 105, 200, 201, 300, 301, 302, 310, 400, 401, 402, 403, 500, 501, 600, 601, 602};
    CHECK(Ids(fixture) == expected);
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        for (const auto [id, expected] : {std::pair{300, 42.0}, std::pair{301, 60.0}}) {
            auto item = Item(automation, *root, id); auto range = Range(*item);
            BOOL readOnly = TRUE; Need(Enabled(*item));
            Hr(range->get_CurrentIsReadOnly(&readOnly)); Need(!readOnly);
            double value = -1; Hr(range->get_CurrentValue(&value)); Need(value == expected);
            Hr(range->get_CurrentMinimum(&value)); Need(value == 0);
            Hr(range->get_CurrentMaximum(&value)); Need(value == 100);
            Hr(range->SetValue(id == 300 ? 31 : 67));
            // Only the host can replace the observed snapshot; intent alone must not fake readback.
            Hr(range->get_CurrentValue(&value)); Need(value == expected);
        }
        // Wi-Fi reports its connection state, never a network name (FR-008).
        auto wifi = Item(automation, *root, 100);
        Need(Toggled(*wifi) == ToggleState_On); Need(Help(*wifi) == L"Connected");
        Need(Name(*wifi) == L"Wi-Fi, on, Connected");
        auto bluetooth = Item(automation, *root, 101);
        Need(Toggled(*bluetooth) == ToggleState_Off); Need(Name(*bluetooth) == L"Bluetooth, off");
        // Settings-only capabilities are never presented as working switches.
        for (const int id : {102, 104}) {
            auto tile = Item(automation, *root, id);
            Need(!Toggled(*tile).has_value()); Need(EndsWith(Name(*tile), L", opens Windows Settings"));
        }
        Need(Name(*Item(automation, *root, 102)) == L"Airplane mode, opens Windows Settings");
        Need(!Toggled(*Item(automation, *root, 103)).has_value());
        Need(Name(*Item(automation, *root, 500)) == L"Battery 78 percent, Power and battery settings");
        Need(Name(*Item(automation, *root, 601)) == L"Page 1 of 2");
        auto play = Item(automation, *root, 401); Need(Name(*play) == L"Pause"); Invoke(*play);
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

AETHER_TEST("controlcenter integration: the media card exists only for a real session and transport follows capabilities") {
    auto state = Synthetic();
    state.media.sessions.resize(1);
    state.media.sessions[0].canTogglePlayPause = false;
    state.media.sessions[0].canPrevious = false;
    state.media.sessions[0].canNext = false;
    Fixture fixture; fixture.Initialize(std::move(state));
    const RECT withMedia = fixture.window->PanelBoundsPx();
    Client(fixture, [&](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        for (const int id : {400, 401, 402, 403}) Need(!Enabled(*Item(automation, *root, id)));
        Need(Help(*Item(automation, *root, 403)) == L"Media session active");
    });
    // A provider reporting no session removes the card; the panel shrinks below its fixed top.
    fixture.window->SetState(Synthetic(false));
    const RECT without = fixture.window->PanelBoundsPx();
    CHECK_EQ(without.top, withMedia.top); CHECK_EQ(without.right, withMedia.right);
    CHECK_NEAR(static_cast<double>(withMedia.bottom - without.bottom), static_cast<double>(Px(*fixture.window, 72.0f)), 2.0);
    const auto ids = Ids(fixture);
    for (const int id : {400, 401, 402, 403}) CHECK_FALSE(Has(ids, id));
    // Available media without a selected session is still no session.
    auto noSelection = Synthetic();
    noSelection.media.selectedSession.reset();
    fixture.window->SetState(std::move(noSelection));
    CHECK_FALSE(Has(Ids(fixture), 403));
    CHECK(fixture.intents.empty()); CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: split tiles toggle on the main part and open details on the arrow") {
    Fixture fixture; fixture.Initialize(); RECT wifi{}, wifiArrow{}, bluetooth{}, bluetoothArrow{};
    Client(fixture, [&](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        const auto bounds = [&](int id, RECT& rect) {
            auto item = Item(automation, *root, id); CONTROLTYPEID type = 0;
            Hr(item->get_CurrentControlType(&type)); Need(type == UIA_ButtonControlTypeId);
            Hr(item->get_CurrentBoundingRectangle(&rect)); Need(rect.right > rect.left && rect.bottom > rect.top);
        };
        bounds(100, wifi); bounds(200, wifiArrow); bounds(101, bluetooth); bounds(201, bluetoothArrow);
        Need(Name(*Item(automation, *root, 200)) == L"Wi-Fi details");
    });
    for (const auto& pair : {std::pair{wifi, wifiArrow}, std::pair{bluetooth, bluetoothArrow}}) {
        CHECK_EQ(pair.first.right, pair.second.left); CHECK_EQ(pair.first.top, pair.second.top);
        CHECK_EQ(pair.first.bottom, pair.second.bottom);
        CHECK(pair.first.right - pair.first.left > pair.second.right - pair.second.left);
    }
    Click(*fixture.window, wifi); Click(*fixture.window, bluetooth);
    REQUIRE_EQ(fixture.intents.size(), 2u);
    CHECK(fixture.intents[0].kind == ControlIntentKind::SystemTile); CHECK(fixture.intents[0].control == SystemControl::WiFi);
    CHECK(fixture.intents[0].enabled == false);
    CHECK(fixture.intents[1].kind == ControlIntentKind::SystemTile); CHECK(fixture.intents[1].control == SystemControl::Bluetooth);
    CHECK(fixture.intents[1].enabled == true);
    // The arrow opens the Wi-Fi view without acting; the right button never acts.
    Click(*fixture.window, wifiArrow, WM_RBUTTONDOWN); CHECK(Has(Ids(fixture), 100));
    Click(*fixture.window, wifiArrow); CHECK_EQ(fixture.intents.size(), 2u);
    CHECK(Ids(fixture) == (std::vector<int>{400, 401, 402, 403, 500, 501, 700, 711, 720, 721}));
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        auto toggle = Item(automation, *root, 711); Need(Name(*toggle) == L"Wi-Fi"); Need(Toggled(*toggle) == ToggleState_On);
        Need(Name(*Item(automation, *root, 720)) == L"Show available networks");
        Need(Name(*Item(automation, *root, 721)) == L"More Wi-Fi settings");
    });
    InvokeIds(fixture, {720, 721, 711});
    REQUIRE_EQ(fixture.intents.size(), 5u);
    CHECK(fixture.intents[2].kind == ControlIntentKind::NetworkList);
    CHECK(fixture.intents[3].kind == ControlIntentKind::SystemSettings); CHECK(fixture.intents[3].control == SystemControl::WiFi);
    CHECK(fixture.intents[4].kind == ControlIntentKind::SystemTile); CHECK(fixture.intents[4].enabled == false);
    // Escape returns to the main view first, then closes.
    Key(*fixture.window, VK_ESCAPE); CHECK(fixture.window->IsShown()); CHECK(Has(Ids(fixture), 100));
    Key(*fixture.window, VK_ESCAPE); CHECK_FALSE(fixture.window->IsShown()); CHECK_EQ(fixture.closed, 1);
    CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: an unknown or unswitchable radio opens its details instead of a toggle") {
    auto state = Synthetic(false);
    state.system.wifi.enabled.reset();
    state.system.bluetooth.canToggle = false;
    state.system.bluetooth.canRequestAccess = false;
    Fixture fixture; fixture.Initialize(std::move(state));
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        auto wifi = Item(automation, *root, 100);
        Need(Toggled(*wifi) == ToggleState_Indeterminate); Need(Name(*wifi) == L"Wi-Fi, state unknown"); Need(Help(*wifi) == L"Wi-Fi");
    });
    InvokeIds(fixture, {100});
    auto ids = Ids(fixture);
    CHECK(Has(ids, 711)); CHECK_FALSE(Has(ids, 100));
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd); auto toggle = Item(automation, *root, 711);
        Need(!Enabled(*toggle)); Need(Toggled(*toggle) == ToggleState_Indeterminate);
    });
    InvokeIds(fixture, {700, 101});
    ids = Ids(fixture);
    CHECK(Has(ids, 720)); CHECK_FALSE(Has(ids, 721));
    InvokeIds(fixture, {720});
    REQUIRE_EQ(fixture.intents.size(), 1u);
    CHECK(fixture.intents[0].kind == ControlIntentKind::SystemSettings); CHECK(fixture.intents[0].control == SystemControl::Bluetooth);
    CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: pages turn by button, dots, keys and wheel without losing actions") {
    Fixture fixture; fixture.Initialize(Synthetic(false));
    InvokeIds(fixture, {600});
    auto ids = Ids(fixture);
    for (const int id : {106, 107, 108, 109, 110, 111}) CHECK(Has(ids, id));
    for (const int id : {100, 101, 200, 201}) CHECK_FALSE(Has(ids, id));
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        Need(Name(*Item(automation, *root, 600)) == L"First page");
        VARIANT status{}; Hr(Item(automation, *root, 602)->GetCurrentPropertyValue(UIA_ItemStatusPropertyId, &status));
        const bool current = status.vt == VT_BSTR && std::wstring(status.bstrVal ? status.bstrVal : L"") == L"Current page";
        ::VariantClear(&status); Need(current);
        for (const int id : {106, 107, 108}) Need(!Toggled(*Item(automation, *root, id)).has_value());
        Need(Name(*Item(automation, *root, 111)) == L"All settings");
    });
    InvokeIds(fixture, {106, 109, 111});
    REQUIRE_EQ(fixture.intents.size(), 3u);
    CHECK(fixture.intents[0].kind == ControlIntentKind::SystemTile); CHECK(fixture.intents[0].control == SystemControl::NightLight);
    CHECK_FALSE(fixture.intents[0].enabled.has_value());
    CHECK(fixture.intents[1].control == SystemControl::Cast);
    CHECK(fixture.intents[2].control == SystemControl::Settings);
    InvokeIds(fixture, {601}); CHECK(Has(Ids(fixture), 100));
    Key(*fixture.window, VK_NEXT); CHECK(Has(Ids(fixture), 106));
    Key(*fixture.window, VK_PRIOR); CHECK(Has(Ids(fixture), 100));
    const RECT panel = fixture.window->PanelBoundsPx();
    const POINT overTiles{panel.left + Px(*fixture.window, 80.0f), panel.top + Px(*fixture.window, 60.0f)};
    ::SendMessageW(fixture.window->Handle(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(overTiles.x, overTiles.y));
    CHECK(Has(Ids(fixture), 106));
    // A small touchpad delta does not flip pages by itself.
    ::SendMessageW(fixture.window->Handle(), WM_MOUSEWHEEL, MAKEWPARAM(0, 20), MAKELPARAM(overTiles.x, overTiles.y));
    CHECK(Has(Ids(fixture), 106));
    CHECK_EQ(fixture.intents.size(), 3u); CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: accessibility and sound output details route explicit intents only") {
    Fixture fixture; fixture.Initialize(Synthetic(false));
    InvokeIds(fixture, {103});
    CHECK(Ids(fixture) == (std::vector<int>{500, 501, 700, 720, 721, 722, 723, 724, 725}));
    CHECK(fixture.intents.empty());
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        auto sticky = Item(automation, *root, 724); Need(Name(*sticky) == L"Sticky keys"); Need(Toggled(*sticky) == ToggleState_Off);
        for (const int id : {720, 721, 722, 723, 725}) Need(!Toggled(*Item(automation, *root, id)).has_value());
        Need(Enabled(*Item(automation, *root, 722)));
    });
    InvokeIds(fixture, {720, 721, 722, 723, 724, 725});
    REQUIRE_EQ(fixture.intents.size(), 6u);
    const AccessibilityFeature features[] = {AccessibilityFeature::Magnifier, AccessibilityFeature::Narrator,
        AccessibilityFeature::LiveCaptions, AccessibilityFeature::OnScreenKeyboard};
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(fixture.intents[i].kind == ControlIntentKind::Accessibility); CHECK(fixture.intents[i].feature == features[i]);
    }
    CHECK(fixture.intents[4].kind == ControlIntentKind::StickyKeys); CHECK(fixture.intents[4].enabled == true);
    CHECK(fixture.intents[5].kind == ControlIntentKind::Accessibility); CHECK(fixture.intents[5].feature == AccessibilityFeature::Settings);
    // The switch shows only what Windows reports: the request alone does not flip it.
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd); Need(Toggled(*Item(automation, *root, 724)) == ToggleState_Off);
    });
    auto unknown = Synthetic(false); unknown.system.stickyKeys.reset(); unknown.system.windowsBuild = 19045;
    fixture.window->SetState(std::move(unknown));
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        auto sticky = Item(automation, *root, 724); Need(!Enabled(*sticky)); Need(Toggled(*sticky) == ToggleState_Indeterminate);
        Need(!Enabled(*Item(automation, *root, 722)));
    });
    fixture.intents.clear();
    InvokeIds(fixture, {700, 310});
    CHECK(Ids(fixture) == (std::vector<int>{500, 501, 700, 720, 721, 722}));
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        auto mute = Item(automation, *root, 720); Need(Name(*mute) == L"Mute"); Need(Toggled(*mute) == ToggleState_Off);
    });
    InvokeIds(fixture, {720, 721, 722});
    REQUIRE_EQ(fixture.intents.size(), 3u);
    CHECK(fixture.intents[0].kind == ControlIntentKind::ToggleMute);
    CHECK(fixture.intents[1].kind == ControlIntentKind::VolumeMixer);
    CHECK(fixture.intents[2].kind == ControlIntentKind::SoundSettings);
    // Back restores the main view with focus on the control the detail was opened from.
    Key(*fixture.window, VK_ESCAPE);
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd); BOOL focus = FALSE; Hr(Item(automation, *root, 310)->get_CurrentHasKeyboardFocus(&focus)); Need(focus);
    });
    CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: the panel opens below its Top Bar group from the first frame and stays on screen") {
    Fixture fixture; fixture.Initialize(Synthetic(false));
    auto& window = *fixture.window;
    const auto check = [&](const RECT& anchor) {
        if (window.IsShown()) window.Close();
        REQUIRE(window.Toggle(anchor).ok()); REQUIRE(window.IsShown());
        const RECT panel = window.PanelBoundsPx();
        RECT placed{}; REQUIRE(::GetWindowRect(window.Handle(), &placed) != FALSE);
        // The HWND is already at its final place when it becomes visible (no move after showing).
        const RECT bounds = window.BoundsPx();
        CHECK(placed.left == bounds.left && placed.top == bounds.top && placed.right == bounds.right && placed.bottom == bounds.bottom);
        MONITORINFO info{sizeof(info)}; REQUIRE(::GetMonitorInfoW(::MonitorFromRect(&anchor, MONITOR_DEFAULTTONEAREST), &info) != FALSE);
        const RECT& area = info.rcWork;
        CHECK(panel.left >= area.left); CHECK(panel.top >= area.top); CHECK(panel.right <= area.right); CHECK(panel.bottom <= area.bottom);
        return panel;
    };
    const auto monitor = platform::PrimaryMonitor();
    const RECT& area = monitor.workArea;
    // Ordinary placement: right edges meet, the top sits 6 DIPs below the group.
    RECT anchor{area.left + 500, area.top + 4, area.left + 720, area.top + 40};
    RECT panel = check(anchor);
    CHECK(std::abs(panel.right - anchor.right) <= 1);
    CHECK(std::abs(panel.top - (anchor.bottom + Px(window, 6.0f))) <= 1);
    CHECK_NEAR(static_cast<double>(panel.right - panel.left), static_cast<double>(Px(window, 360.0f)), 2.0);
    // A group at the far right of the screen keeps the right-edge alignment and the work area.
    anchor = RECT{area.right - 260, area.top + 4, area.right - 4, area.top + 40};
    panel = check(anchor);
    CHECK(panel.right <= area.right - Px(window, 8.0f) + 1);
    // A group near the left edge (and one left of every monitor) is clamped, never placed off screen.
    anchor = RECT{area.left + 2, area.top + 4, area.left + 60, area.top + 40};
    panel = check(anchor);
    CHECK(panel.left >= area.left + Px(window, 8.0f) - 1); CHECK(std::abs(panel.top - (anchor.bottom + Px(window, 6.0f))) <= 1);
    (void)check(RECT{area.left - 4000, area.top - 4000, area.left - 3800, area.top - 3960});
    // A group near the bottom keeps the whole panel inside the work area.
    (void)check(RECT{area.left + 500, area.bottom - 40, area.left + 720, area.bottom - 4});
    // Repeated clicks on the group close and reopen at the same place.
    anchor = RECT{area.left + 500, area.top + 4, area.left + 720, area.top + 40};
    panel = check(anchor);
    REQUIRE(window.Toggle(anchor).ok()); CHECK_FALSE(window.IsShown()); CHECK(window.PanelBoundsPx().right == 0);
    REQUIRE(window.Toggle(anchor).ok()); CHECK(window.IsShown());
    const RECT again = window.PanelBoundsPx();
    CHECK(again.left == panel.left && again.top == panel.top && again.right == panel.right && again.bottom == panel.bottom);
    // Losing activation (a click elsewhere) closes it once.
    const int closedBefore = fixture.closed;
    ::SendMessageW(window.Handle(), WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), 0);
    CHECK_FALSE(window.IsShown()); CHECK_EQ(fixture.closed, closedBefore + 1);
    CHECK(fixture.intents.empty()); CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: keyboard opening shows focus and moves through the grid geometrically") {
    Fixture fixture; fixture.Initialize(Synthetic(false));
    fixture.window->Close();
    REQUIRE(fixture.window->Toggle(fixture.anchor, true).ok());
    const auto focused = [&] {
        int id = 0;
        Client(fixture, [&](IUIAutomation& automation, HWND hwnd) {
            auto root = Root(automation, hwnd);
            for (const int candidate : ChildIds(automation, *root)) {
                BOOL focus = FALSE; Hr(Item(automation, *root, candidate)->get_CurrentHasKeyboardFocus(&focus));
                if (focus) id = candidate;
            }
        });
        return id;
    };
    CHECK_EQ(focused(), 100);
    Key(*fixture.window, VK_RIGHT); CHECK_EQ(focused(), 200);
    Key(*fixture.window, VK_RIGHT); CHECK_EQ(focused(), 101);
    Key(*fixture.window, VK_DOWN); CHECK_EQ(focused(), 104);
    Key(*fixture.window, VK_LEFT); CHECK_EQ(focused(), 103);
    Key(*fixture.window, VK_RETURN); CHECK(Has(Ids(fixture), 724)); CHECK_EQ(focused(), 700);
    Key(*fixture.window, VK_ESCAPE); CHECK_EQ(focused(), 103);
    Key(*fixture.window, VK_TAB); CHECK(focused() != 103);
    CHECK(fixture.intents.empty()); CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: unknown ranges stay unavailable and closed queues cannot act") {
    Fixture fixture; fixture.Initialize(ControlCenterState{});
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd);
        for (const int id : {300, 301}) {
            auto item = Item(automation, *root, id); Need(!Enabled(*item));
            VARIANT value{}; Hr(item->GetCurrentPropertyValue(UIA_IsRangeValuePatternAvailablePropertyId, &value));
            const bool absent = value.vt == VT_BOOL && value.boolVal == VARIANT_FALSE; ::VariantClear(&value); Need(absent);
            Hr(item->GetCurrentPropertyValueEx(UIA_RangeValueValuePropertyId, TRUE, &value));
            BOOL unsupported = FALSE; const auto checked = automation.CheckNotSupported(value, &unsupported);
            ::VariantClear(&value); Hr(checked); Need(unsupported); // unavailable is not a fabricated zero
        }
        Need(!Enabled(*Item(automation, *root, 302)));
        // Unknown battery state still offers the power settings, without a fabricated level.
        Need(Name(*Item(automation, *root, 500)) == L"Power and battery settings");
        for (const int id : {100, 101}) Need(Toggled(*Item(automation, *root, id)) == ToggleState_Indeterminate);
    });
    fixture.window->SetState(Synthetic(false));
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd); auto volume = Item(automation, *root, 301); Hr(volume->SetFocus());
    });
    Key(*fixture.window, VK_RIGHT); Key(*fixture.window, VK_RIGHT);
    REQUIRE_EQ(fixture.intents.size(), 2u); CHECK(fixture.intents[0].percent == 61); CHECK(fixture.intents[1].percent == 62);
    Key(*fixture.window, VK_END);
    REQUIRE_EQ(fixture.intents.size(), 3u); CHECK(fixture.intents.back().kind == ControlIntentKind::Volume);
    CHECK(fixture.intents.back().percent == 100);
    Key(*fixture.window, VK_ESCAPE); CHECK_FALSE(fixture.window->IsShown());
    Key(*fixture.window, VK_END); CHECK_EQ(fixture.intents.size(), 3u);
    fixture.Open(); fixture.intents.clear();
    std::vector<MSG> deferred;
    const auto filter = fixture.loop.AddMessageFilter([&](const MSG& message) {
        if (message.hwnd == fixture.window->Handle() && message.message >= WM_APP && message.message < WM_APP + 4096) {
            deferred.push_back(message); return true;
        }
        return false;
    });
    Client(fixture, [](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd); Invoke(*Item(automation, *root, 501));
        Hr(Range(*Item(automation, *root, 301))->SetValue(77));
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
    int saved = 0;
    const auto receipt = [&](const wchar_t* name) {
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(fixture.window->Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // This presentation-only class exposes no frame counter. Pump its scheduler (entrance and
        // view transitions settle well within this) before an optional receipt.
        RunFor(fixture.loop, 450);
        const RECT crop = fixture.window->BoundsPx();
        // A transient overlap (another window passing over the guarded area) gets two more chances.
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (CaptureOwn(backdrop.hwnd, fixture.window->Handle(), guarded, crop, output / name)) {
                ++saved;
                return;
            }
            RunFor(fixture.loop, 200);
        }
        test::Note("Quick-controls visual receipt unavailable: own-backdrop capture guard refused; no pixels saved.");
    };
    for (const auto mode : {graphics::ThemeMode::Dark, graphics::ThemeMode::Light}) {
        const bool dark = mode == graphics::ThemeMode::Dark;
        fixture.window->SetTheme(graphics::MakeDesignTokens(mode), graphics::MotionPolicy{true, 1});
        if (fixture.window->IsShown()) fixture.window->Close();
        fixture.window->SetState(Synthetic(false));
        fixture.Open();
        receipt(dark ? L"controlcenter-dark.png" : L"controlcenter-light.png");
        fixture.window->SetState(Synthetic(true));
        receipt(dark ? L"controlcenter-dark-media.png" : L"controlcenter-light-media.png");
        InvokeIds(fixture, {600});
        receipt(dark ? L"controlcenter-dark-page2.png" : L"controlcenter-light-page2.png");
        InvokeIds(fixture, {601, 103});
        receipt(dark ? L"controlcenter-dark-accessibility.png" : L"controlcenter-light-accessibility.png");
        InvokeIds(fixture, {700, 200});
        receipt(dark ? L"controlcenter-dark-wifi.png" : L"controlcenter-light-wifi.png");
    }
    if (saved) test::Note("Quick-controls receipts saved: " + std::to_string(saved) + " in " + output.string());
    CHECK(fixture.intents.empty()); CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter motion: entrance, hover, press, paging, detail and slider sequences settle to idle") {
    Fixture fixture; fixture.Initialize(Synthetic(false));
    auto& window = *fixture.window;
    RECT guarded = window.BoundsPx(); ::InflateRect(&guarded, 96, 96);
    Backdrop backdrop; backdrop.Create(guarded);
    // The real pointer is elsewhere: keep the system's immediate leave notification from cancelling
    // the synthetic pointer, and send leave explicitly where the sequence needs it.
    const auto filter = fixture.loop.AddMessageFilter([&](const MSG& message) {
        return message.hwnd == window.Handle() && message.message == WM_MOUSELEAVE;
    });
    const auto output = test::MakeTempDirectory("controlcenter-motion");
    const auto at = [&](float x, float y) { return MAKELPARAM(Px(window, x), Px(window, y)); };
    const auto pointer = [&](UINT message, float x, float y) {
        ::SendMessageW(window.Handle(), message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0, at(x, y));
    };
    const auto settle = [&] {
        int quiet = 0;
        const auto start = ::GetTickCount64();
        for (int attempt = 0; attempt < 40 && quiet < 2; ++attempt) {
            const auto before = window.RenderCount(); RunFor(fixture.loop, 60);
            quiet = window.RenderCount() == before ? quiet + 1 : 0;
        }
        return quiet == 2 ? static_cast<long long>(::GetTickCount64() - start) : -1LL;
    };
    int sequences = 0;
    // Records ~22 frames (16 ms apart) after `action`; `during(i)` may interrupt the sequence.
    const auto record = [&](const char* name, const std::function<void()>& action, const std::function<void(int)>& during = {}) {
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        const long long before = settle();
        const RECT crop = window.BoundsPx();
        const int width = crop.right - crop.left, height = crop.bottom - crop.top;
        const auto renders = window.RenderCount();
        action();
        std::vector<std::vector<std::uint8_t>> frames;
        int distinct = 0, refused = 0;
        for (int i = 0; i < 22; ++i) {
            if (during) during(i);
            RunFor(fixture.loop, 16);
            // Another topmost shell surface may rise above the private backdrop on activation changes.
            ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            ::SetWindowPos(window.Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            auto bytes = Grab(backdrop.hwnd, window.Handle(), guarded, crop);
            if (bytes.empty()) { ++refused; continue; }
            if (frames.empty() || bytes != frames.back()) ++distinct;
            frames.push_back(std::move(bytes));
        }
        const auto rendered = window.RenderCount() - renders;
        const long long after = settle();
        CHECK(after >= 0);  // every sequence returns to zero frames
        test::Note(std::string(name) + ": renders " + std::to_string(rendered) + " in ~352 ms, distinct captured frames " +
                   std::to_string(distinct) + "/" + std::to_string(frames.size()) + ", settled " + std::to_string(after) +
                   " ms after the sequence (pre-settle " + std::to_string(before) + " ms)" +
                   (refused ? ", guard refused " + std::to_string(refused) : ""));
        if (frames.empty()) return;
        // Half-scale contact sheet, six frames per row, in capture order.
        const int cols = 6, rows = static_cast<int>((frames.size() + cols - 1) / cols);
        const int cw = width / 2, ch = height / 2;
        std::vector<std::uint8_t> sheet(static_cast<std::size_t>(cw * cols) * (ch * rows) * 4, 255);
        for (std::size_t n = 0; n < frames.size(); ++n) {
            const int ox = static_cast<int>(n % cols) * cw, oy = static_cast<int>(n / cols) * ch;
            for (int y = 0; y < ch; ++y) for (int x = 0; x < cw; ++x) for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
                    sum += frames[n][(static_cast<std::size_t>(y * 2 + dy) * width + (x * 2 + dx)) * 4 + c];
                sheet[(static_cast<std::size_t>(oy + y) * (cw * cols) + (ox + x)) * 4 + c] = static_cast<std::uint8_t>(sum / 4);
            }
        }
        SavePng(sheet, cw * cols, ch * rows, output / (std::string("motion-") + name + ".png"));
        SavePng(frames[frames.size() / 3], width, height, output / (std::string("motion-") + name + "-third.png"));
        ++sequences;
    };
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{});
    window.Close();
    record("entrance", [&] { REQUIRE(window.Toggle(fixture.anchor).ok()); });
    // Window DIPs (no media card): Bluetooth main part, airplane tile, Wi-Fi arrow, volume thumb.
    record("hover-retarget", [&] { pointer(WM_MOUSEMOVE, 164, 55); },
           [&](int i) { if (i == 4) pointer(WM_MOUSEMOVE, 296, 55); if (i == 12) ::SendMessageW(window.Handle(), WM_MOUSELEAVE, 0, 0); });
    record("press-release", [&] { pointer(WM_MOUSEMOVE, 296, 55); pointer(WM_LBUTTONDOWN, 296, 55); },
           [&](int i) { if (i == 6) pointer(WM_LBUTTONUP, 296, 55); });
    record("page-interrupted", [&] { Key(window, VK_NEXT); }, [&](int i) { if (i == 5) Key(window, VK_PRIOR); });
    record("page", [&] { Key(window, VK_NEXT); });
    Key(window, VK_PRIOR); (void)settle();
    record("detail-interrupted", [&] { pointer(WM_MOUSEMOVE, 103, 55); pointer(WM_LBUTTONDOWN, 103, 55); pointer(WM_LBUTTONUP, 103, 55); },
           [&](int i) { if (i == 6) Key(window, VK_ESCAPE); });
    record("detail", [&] { pointer(WM_MOUSEMOVE, 103, 55); pointer(WM_LBUTTONDOWN, 103, 55); pointer(WM_LBUTTONUP, 103, 55); });
    record("back", [&] { Key(window, VK_ESCAPE); });
    const float volumeY = 8.0f + 215.0f + 90.5f;
    record("slider-drag", [&] { pointer(WM_MOUSEMOVE, 211, volumeY); pointer(WM_LBUTTONDOWN, 211, volumeY); },
           [&](int i) {
               if (i >= 2 && i <= 14) pointer(WM_MOUSEMOVE, 211.0f + static_cast<float>(i - 1) * 6.0f, volumeY);
               if (i == 15) pointer(WM_LBUTTONUP, 211.0f + 13.0f * 6.0f, volumeY);
           });
    // Hidden: no frames at all.
    window.Close();
    const auto hidden = window.RenderCount(); RunFor(fixture.loop, 300);
    CHECK_EQ(window.RenderCount(), hidden);
    fixture.loop.RemoveMessageFilter(filter);
    // Only synthetic intents: the airplane tile (Settings route) and the dragged volume requests.
    REQUIRE_FALSE(fixture.intents.empty());
    CHECK(fixture.intents.front().kind == ControlIntentKind::SystemTile);
    CHECK(fixture.intents.front().control == SystemControl::FlightMode);
    CHECK(fixture.intents.back().kind == ControlIntentKind::Volume);
    CHECK(fixture.intents.back().percent == 92);
    if (sequences) test::Note("Quick-controls motion sheets: " + std::to_string(sequences) + " in " + output.string());
    CHECK_FALSE(fixture.wrongThread);
}

namespace {
// Every view visible before a change is still composed right after it with the same opacity and
// offset, and nothing visible appears out of nowhere: the change continues from the frame on screen.
bool ContinuesFrom(const std::vector<ControlCenterWindow::ComposedView>& before,
                   const std::vector<ControlCenterWindow::ComposedView>& after, std::string& why) {
    const auto find = [](const auto& list, int view) -> const ControlCenterWindow::ComposedView* {
        for (const auto& item : list) if (item.view == view) return &item;
        return nullptr;
    };
    for (const auto& item : before) {
        if (item.opacity <= 0.02f) continue;
        const auto* next = find(after, item.view);
        if (!next || std::fabs(next->opacity - item.opacity) > 0.03f || std::fabs(next->offsetDip - item.offsetDip) > 0.75f) {
            why = "view " + std::to_string(item.view) + " opacity " + std::to_string(item.opacity) + " offset " + std::to_string(item.offsetDip) +
                  (next ? " became " + std::to_string(next->opacity) + " / " + std::to_string(next->offsetDip) : std::string(" vanished"));
            return false;
        }
    }
    for (const auto& item : after) {
        if (item.opacity > 0.02f && !find(before, item.view)) {
            why = "view " + std::to_string(item.view) + " appeared at " + std::to_string(item.opacity);
            return false;
        }
    }
    return true;
}
void PointerAt(ControlCenterWindow& window, UINT message, POINT screen) {
    REQUIRE(::ScreenToClient(window.Handle(), &screen) != FALSE);
    ::SendMessageW(window.Handle(), message, message == WM_LBUTTONDOWN ? MK_LBUTTON : 0, MAKELPARAM(screen.x, screen.y));
}
POINT CenterOf(const RECT& rect) { return POINT{(rect.left + rect.right) / 2, (rect.top + rect.bottom) / 2}; }
RECT Bounds(Fixture& fixture, int id) {
    RECT rect{};
    Client(fixture, [&](IUIAutomation& automation, HWND hwnd) {
        auto root = Root(automation, hwnd); Hr(Item(automation, *root, id)->get_CurrentBoundingRectangle(&rect));
    });
    return rect;
}
}  // namespace

AETHER_TEST("controlcenter integration: interrupted and rapid detail transitions continue from the composed frame") {
    Fixture fixture; fixture.Initialize(Synthetic(false));
    auto& window = *fixture.window;
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{});
    const RECT wifiArrow = Bounds(fixture, 200);
    window.Close();
    REQUIRE(window.Toggle(fixture.anchor, true).ok());
    Key(window, VK_RIGHT);  // keyboard focus on the Wi-Fi arrow
    const auto now = [] { return graphics::FrameScheduler::Now(); };
    // Both sides at one instant: a tween evaluated before its (re)start reports its start value, so
    // `after` is the composition the change begins from, independent of how long handling took.
    const auto step = [&](const char* what, UINT key) {
        const double at = now();
        const auto before = window.ComposedViews(at);
        Key(window, key);
        const auto after = window.ComposedViews(at);
        std::string why;
        const bool continuous = ContinuesFrom(before, after, why);
        if (!continuous) TEST_NOTE(std::string(what) + ": " + why);
        CHECK(continuous);
    };
    // Into Wi-Fi, then back partway through the slide: the arriving detail leaves from where it is.
    step("open Wi-Fi", VK_RETURN);
    RunFor(fixture.loop, 70);
    step("Escape during entry", VK_ESCAPE);  // focus returns to the Wi-Fi arrow
    // Rapid alternation, each change mid-flight.
    for (int i = 0; i < 4; ++i) {
        RunFor(fixture.loop, 30 + 10 * i);
        if (i % 2 == 0) step("reopen Wi-Fi", VK_RETURN);
        else step("back again", VK_ESCAPE);
    }
    // Leaving Wi-Fi, then straight into Bluetooth: Wi-Fi keeps fading where it was.
    RunFor(fixture.loop, 50);
    Key(window, VK_RIGHT);  // Bluetooth's main part
    Key(window, VK_RIGHT);  // Bluetooth's arrow
    step("Bluetooth while Wi-Fi leaves", VK_RETURN);
    RunFor(fixture.loop, 600);
    auto settled = window.ComposedViews(now());
    REQUIRE_EQ(settled.size(), 1u);
    CHECK_EQ(settled[0].view, 2);
    CHECK(settled[0].opacity >= 0.999f);
    CHECK(std::fabs(settled[0].offsetDip) < 0.01f);
    // Reduced motion: every change lands at once, with no transition layers.
    window.SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1});
    Key(window, VK_ESCAPE);
    settled = window.ComposedViews(now());
    REQUIRE_EQ(settled.size(), 1u);
    CHECK_EQ(settled[0].view, 0);
    CHECK(settled[0].opacity >= 0.999f);
    Click(window, wifiArrow);
    settled = window.ComposedViews(now());
    REQUIRE_EQ(settled.size(), 1u);
    CHECK_EQ(settled[0].view, 1);
    // Only navigation happened: no detail row was invoked.
    CHECK(fixture.intents.empty());
    CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: media arriving at a bottom-clamped anchor keeps the whole panel on screen") {
    Fixture fixture; fixture.Initialize(Synthetic(false));
    auto& window = *fixture.window;
    const auto area = platform::PrimaryMonitor().workArea;
    const RECT anchor{area.left + 500, area.bottom - 40, area.left + 720, area.bottom - 4};
    window.Close();
    REQUIRE(window.Toggle(anchor).ok());
    const auto inside = [&](const RECT& panel) {
        return panel.left >= area.left && panel.top >= area.top && panel.right <= area.right && panel.bottom <= area.bottom;
    };
    const RECT without = window.PanelBoundsPx();
    CHECK(inside(without));
    window.SetState(Synthetic(true));
    REQUIRE(window.IsShown());
    const RECT with = window.PanelBoundsPx();
    if (!inside(with)) TEST_NOTE("with media: " + std::to_string(with.top) + ".." + std::to_string(with.bottom));
    CHECK(inside(with));
    CHECK(with.bottom - with.top > without.bottom - without.top);
    CHECK(std::abs(with.right - anchor.right) <= 1);
    // Every control stays reachable: the media card and the footer lie inside the work area.
    for (const int id : {403, 401, 501}) {
        const RECT item = Bounds(fixture, id);
        CHECK(item.top >= area.top);
        CHECK(item.bottom <= area.bottom);
    }
    window.SetState(Synthetic(false));
    CHECK(inside(window.PanelBoundsPx()));
    CHECK(fixture.intents.empty());
    CHECK_FALSE(fixture.wrongThread);
}

AETHER_TEST("controlcenter integration: release activates only the target that was pressed") {
    Fixture fixture; fixture.Initialize(Synthetic(false));
    auto& window = *fixture.window;
    window.Close();
    REQUIRE(window.Toggle(fixture.anchor, true).ok());  // keyboard opening: focus on the Wi-Fi tile
    const RECT wifi = Bounds(fixture, 100), wifiArrow = Bounds(fixture, 200), bluetooth = Bounds(fixture, 101);
    const POINT gap{wifiArrow.right + (bluetooth.left - wifiArrow.right) / 2, (wifiArrow.top + wifiArrow.bottom) / 2};
    // Press on empty space, release over the focused tile: nothing was pressed, nothing happens.
    PointerAt(window, WM_LBUTTONDOWN, gap);
    PointerAt(window, WM_LBUTTONUP, CenterOf(wifi));
    CHECK(fixture.intents.empty());
    // Press one tile, release over another.
    PointerAt(window, WM_LBUTTONDOWN, CenterOf(bluetooth));
    PointerAt(window, WM_LBUTTONUP, CenterOf(wifi));
    CHECK(fixture.intents.empty());
    // A press cancelled by capture loss owns nothing afterwards.
    PointerAt(window, WM_LBUTTONDOWN, CenterOf(wifi));
    ::SendMessageW(window.Handle(), WM_CAPTURECHANGED, 0, 0);
    PointerAt(window, WM_LBUTTONUP, CenterOf(wifi));
    CHECK(fixture.intents.empty());
    // An ordinary click still acts once.
    PointerAt(window, WM_LBUTTONDOWN, CenterOf(wifi));
    PointerAt(window, WM_LBUTTONUP, CenterOf(wifi));
    REQUIRE_EQ(fixture.intents.size(), 1u);
    CHECK(fixture.intents[0].kind == ControlIntentKind::SystemTile);
    CHECK(fixture.intents[0].control == SystemControl::WiFi);
    CHECK_FALSE(fixture.wrongThread);
}
