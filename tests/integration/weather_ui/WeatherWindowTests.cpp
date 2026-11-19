// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// D78: Aether Shell's weather window with a scripted Open-Meteo (no network): the home place's forecast
// and its country map, city search, the location permission banner, motion at rest, errors, and render
// receipts in Grain, Liquid, light and high contrast.
#include "AetherTest.h"
#include "../../unit/weather/WeatherTestSupport.h"
#include "platform/windows/ComApartment.h"
#include "platform/windows/MessageLoop.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/Win32UiDispatcher.h"
#include "weather/ui/WeatherWindow.h"

#include <UIAutomation.h>
#include <wincodec.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <functional>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace aether;
using namespace aether::weather;
using namespace aether::weather::ui;

namespace {

void Need(bool value) { if (!value) throw std::runtime_error("weather window assertion failed"); }
void Hr(HRESULT value) { if (FAILED(value)) throw std::runtime_error("weather window UIA call failed"); }

void RunFor(platform::MessageLoop& loop, DWORD milliseconds) {
    HANDLE timer = ::CreateWaitableTimerW(nullptr, TRUE, nullptr);
    REQUIRE(timer != nullptr);
    LARGE_INTEGER due{}; due.QuadPart = -static_cast<LONGLONG>(milliseconds) * 10000;
    REQUIRE(::SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) != FALSE);
    const auto id = loop.AddWaitHandle(timer, [] { ::PostQuitMessage(0); });
    REQUIRE(id != 0);
    loop.Run(); loop.RemoveWaitHandle(id); ::CloseHandle(timer);
}

// --- a scripted Open-Meteo ---
std::string Civil(std::chrono::sys_seconds value, bool withTime) {
    using namespace std::chrono;
    const auto day = floor<days>(value);
    const year_month_day date{day};
    std::string text = std::format("{:04}-{:02}-{:02}", static_cast<int>(date.year()), static_cast<unsigned>(date.month()),
                                   static_cast<unsigned>(date.day()));
    if (withTime) {
        const auto minutes = duration_cast<std::chrono::minutes>(value - day).count();
        text += std::format("T{:02}:{:02}", minutes / 60, minutes % 60);
    }
    return text;
}

constexpr int kOffset = 3600;  // Paris in November

// A full detailed answer for the place asked, in the place's local time (UTC+1) from now.
std::string Forecast(double latitude, double longitude) {
    using namespace std::chrono;
    const auto local = floor<seconds>(system_clock::now()) + seconds{kOffset};
    const auto hourStart = floor<hours>(local);
    static constexpr int kCodes[25] = {2, 3, 61, 63, 3, 2, 1, 0, 0, 1, 2, 80, 61, 3, 2, 2, 1, 0, 0, 1, 3, 61, 63, 3, 2};
    std::string times, temperatures, codes, isDay, rain;
    for (int i = 0; i < 25; ++i) {
        const auto at = hourStart + hours{i};
        const double hour = duration<double, std::ratio<3600>>(at - floor<days>(at)).count();
        const double value = 9.0 + 3.0 * std::sin((hour - 9.0) / 24.0 * 2.0 * std::numbers::pi);
        const char* separator = i ? "," : "";
        times += std::format("{}\"{}\"", separator, Civil(at, true));
        temperatures += std::format("{}{:.1f}", separator, value);
        codes += std::format("{}{}", separator, kCodes[i]);
        isDay += std::format("{}{}", separator, hour >= 8.0 && hour < 17.0 ? 1 : 0);
        rain += std::format("{}{}", separator, kCodes[i] >= 61 ? 70 : 10);
    }
    static constexpr int kDayCodes[7] = {2, 61, 3, 0, 80, 71, 95};
    static constexpr double kHighs[7] = {12.4, 10.1, 13.0, 14.2, 11.5, 6.8, 9.9};
    static constexpr double kLows[7] = {7.1, 6.0, 5.2, 6.9, 4.0, 0.5, 3.2};
    static constexpr int kRain[7] = {20, 85, 10, 0, 60, 40, 75};
    static constexpr double kUv[7] = {1.4, 0.8, 1.9, 2.2, 1.1, 0.6, 0.9};
    std::string dates, dayCodes, highs, lows, sunrises, sunsets, dayRain, uv;
    const auto today = floor<days>(local);
    for (int i = 0; i < 7; ++i) {
        const auto day = sys_seconds{today + days{i}};
        const char* separator = i ? "," : "";
        dates += std::format("{}\"{}\"", separator, Civil(day, false));
        dayCodes += std::format("{}{}", separator, kDayCodes[i]);
        highs += std::format("{}{:.1f}", separator, kHighs[i]);
        lows += std::format("{}{:.1f}", separator, kLows[i]);
        sunrises += std::format("{}\"{}\"", separator, Civil(day + hours{8} + minutes{1 + i}, true));
        sunsets += std::format("{}\"{}\"", separator, Civil(day + hours{17} + minutes{8 - i}, true));
        dayRain += std::format("{}{}", separator, kRain[i]);
        uv += std::format("{}{:.1f}", separator, kUv[i]);
    }
    const std::string unit = std::format("\"{}C\"", testing::kDegree);
    return std::format(
        R"({{"latitude":{:.4f},"longitude":{:.4f},"utc_offset_seconds":{},"timezone":"Europe/Paris","elevation":36.0,)"
        R"("current_units":{{"temperature_2m":{}}},)"
        R"("current":{{"time":"{}","interval":900,"temperature_2m":11.3,"apparent_temperature":9.2,)"
        R"("relative_humidity_2m":82,"weather_code":2,"is_day":{},"cloud_cover":64,"pressure_msl":1014.2,)"
        R"("wind_speed_10m":14.8,"wind_direction_10m":225,"wind_gusts_10m":31.0}},)"
        R"("hourly_units":{{"temperature_2m":{}}},)"
        R"("hourly":{{"time":[{}],"temperature_2m":[{}],"weather_code":[{}],"is_day":[{}],"precipitation_probability":[{}]}},)"
        R"("daily_units":{{"temperature_2m_max":{}}},)"
        R"("daily":{{"time":[{}],"weather_code":[{}],"temperature_2m_max":[{}],"temperature_2m_min":[{}],)"
        R"("sunrise":[{}],"sunset":[{}],"precipitation_probability_max":[{}],"uv_index_max":[{}]}}}})",
        latitude, longitude, kOffset, unit, Civil(floor<minutes>(local), true), isDay.substr(0, 1), unit, times, temperatures, codes, isDay,
        rain, unit, dates, dayCodes, highs, lows, sunrises, sunsets, dayRain, uv);
}

std::vector<double> Coordinates(const std::string& url, std::string_view name) {
    std::vector<double> values;
    const auto start = url.find(std::string(name) + "=");
    if (start == std::string::npos) return values;
    const auto first = start + name.size() + 1;
    const auto end = url.find('&', first);
    std::string list = url.substr(first, end == std::string::npos ? std::string::npos : end - first);
    for (std::size_t at = 0; at <= list.size();) {
        const auto comma = list.find(',', at);
        values.push_back(std::stod(list.substr(at, comma == std::string::npos ? std::string::npos : comma - at)));
        if (comma == std::string::npos) break;
        at = comma + 1;
    }
    return values;
}

// The map's towns: warmer to the south, a little warmer to the east.
std::string Points(const std::string& url) {
    const auto latitudes = Coordinates(url, "latitude"), longitudes = Coordinates(url, "longitude");
    std::string body = "[";
    for (std::size_t i = 0; i < latitudes.size(); ++i) {
        const double longitude = i < longitudes.size() ? longitudes[i] : 0.0;
        const double value = 17.0 - (latitudes[i] - 42.5) * 1.15 + (longitude - 2.0) * 0.2;
        body += std::format(R"({}{{"location_id":{},"current_units":{{"temperature_2m":"{}C"}},)"
                            R"("current":{{"time":"2026-11-16T12:00","interval":900,"temperature_2m":{:.1f},"weather_code":2,"is_day":1}}}})",
                            i ? "," : "", i, testing::kDegree, value);
    }
    return body + "]";
}

constexpr std::string_view kLyon = R"({"results":[
    {"id":2996944,"name":"Lyon","latitude":45.74846,"longitude":4.84671,"admin1":"Auvergne-Rhône-Alpes","country":"France"},
    {"id":5125086,"name":"Lyons","latitude":43.06395,"longitude":-76.99025,"admin1":"New York","country":"United States"}
]})";

testing::FakeHttpClient::Handler OpenMeteo() {
    return [](const net::HttpRequest& request, const CancellationToken&) -> Result<net::HttpResponse> {
        net::HttpResponse response;
        response.status = 200;
        response.finalUrl = request.url;
        if (request.url.find("geocoding-api") != std::string::npos) {
            response.body = std::string(kLyon);
        } else if (request.url.find("&hourly=") != std::string::npos) {
            const auto latitude = Coordinates(request.url, "latitude"), longitude = Coordinates(request.url, "longitude");
            if (latitude.empty() || longitude.empty()) return MakeError(ErrorCode::InvalidArgument, "no coordinates");
            response.body = Forecast(latitude.front(), longitude.front());
        } else {
            response.body = Points(request.url);
        }
        return response;
    };
}

WeatherHome Paris() {
    WeatherHome home;
    home.source = HomeSource::ChosenCity;
    home.latitude = 48.8566;
    home.longitude = 2.3522;
    home.name = L"Paris, Île-de-France, France";
    home.permission = LocationPermission::NotAsked;
    return home;
}

struct Fixture {
    platform::ComApartment apartment;
    platform::MessageLoop loop;
    graphics::GraphicsDevice device;
    graphics::FrameScheduler scheduler;
    platform::Win32UiDispatcher dispatcher;
    testing::FakeHttpClient http;
    ForecastClient forecast{http};
    GeocodingClient geocoding{http};
    std::unique_ptr<WeatherWindow> window;
    int accessRequests = 0, settingsRequests = 0, weatherSettings = 0, closed = 0;
    std::vector<std::wstring> links;
    std::vector<Connection> connections;
    ~Fixture() {
        for (auto& connection : connections) connection.Disconnect();
        window.reset(); dispatcher.Shutdown(); scheduler.Shutdown(); device.Shutdown();
    }
    void Initialize(WeatherHome home = Paris(), graphics::MotionPolicy motion = graphics::MotionPolicy{true, 1.0f}) {
        REQUIRE(SUCCEEDED(apartment.Status()));
        REQUIRE(device.Initialize().ok()); REQUIRE(scheduler.Initialize(loop).ok());
        REQUIRE(dispatcher.Initialize().ok());
        http.SetHandler(OpenMeteo());
        window = std::make_unique<WeatherWindow>(WeatherWindowContext{device, scheduler, dispatcher, &forecast, &geocoding});
        REQUIRE(window->Initialize().ok());
        window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), motion);
        window->SetHome(home);
        connections.push_back(window->LocationAccessRequested.Connect([this] { ++accessRequests; }));
        connections.push_back(window->LocationSettingsRequested.Connect([this] { ++settingsRequests; }));
        connections.push_back(window->WeatherSettingsRequested.Connect([this] { ++weatherSettings; }));
        connections.push_back(window->LinkRequested.Connect([this](const std::wstring& link) { links.push_back(link); }));
        connections.push_back(window->Closed.Connect([this] { ++closed; }));
    }
    // Centred on the primary monitor, as from Settings.
    void Open() {
        const auto& area = platform::PrimaryMonitor().workArea;
        const RECT anchor{(area.left + area.right) / 2, (area.top + area.bottom) / 2, (area.left + area.right) / 2 + 1,
                          (area.top + area.bottom) / 2 + 1};
        REQUIRE(window->Open(anchor, false).ok()); REQUIRE(window->IsOpen());
    }
    bool WaitFor(const std::function<bool()>& done, DWORD milliseconds) {
        const auto deadline = ::GetTickCount64() + milliseconds;
        while (!done() && ::GetTickCount64() < deadline) RunFor(loop, 10);
        return done();
    }
    // Waits for the forecast and the map's towns, then for their answer to reach the window.
    void Loaded(std::size_t requests = 2) {
        REQUIRE(WaitFor([&] { return http.RequestCount() >= requests; }, 5000));
        RunFor(loop, 300);
    }
};

// UI Automation from another thread, as a screen reader would.
void Client(Fixture& fixture, const std::function<void(IUIAutomation&, HWND)>& action) {
    std::atomic<bool> finished{false}; bool passed = false;
    const HWND hwnd = fixture.window->Handle();
    std::thread client([&] {
        platform::ComApartment apartment(COINIT_MULTITHREADED);
        try {
            Hr(apartment.Status()); winrt::com_ptr<IUIAutomation> automation;
            Hr(::CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void()));
            action(*automation, hwnd); passed = true;
        } catch (...) {}
        finished.store(true);
    });
    const auto deadline = ::GetTickCount64() + 5000;
    while (!finished.load() && ::GetTickCount64() < deadline) RunFor(fixture.loop, 10);
    const bool withinDeadline = finished.load();
    client.join();
    REQUIRE(withinDeadline); REQUIRE(passed);
}

std::wstring Read(BSTR value) { std::wstring text(value ? value : L""); ::SysFreeString(value); return text; }

// The window's elements by id ("item-N"), with their names; the root must be the weather pane.
std::map<int, std::wstring> Names(Fixture& fixture) {
    std::map<int, std::wstring> names;
    Client(fixture, [&](IUIAutomation& automation, HWND hwnd) {
        winrt::com_ptr<IUIAutomationElement> root; Hr(automation.ElementFromHandle(hwnd, root.put())); Need(root != nullptr);
        BSTR rootName = nullptr; Hr(root->get_CurrentName(&rootName)); Need(Read(rootName) == L"Weather");
        winrt::com_ptr<IUIAutomationCondition> all; Hr(automation.CreateTrueCondition(all.put()));
        winrt::com_ptr<IUIAutomationElementArray> children; Hr(root->FindAll(TreeScope_Children, all.get(), children.put()));
        int count = 0; Hr(children->get_Length(&count));
        for (int i = 0; i < count; ++i) {
            winrt::com_ptr<IUIAutomationElement> child; Hr(children->GetElement(i, child.put()));
            BSTR id = nullptr, name = nullptr; Hr(child->get_CurrentAutomationId(&id)); Hr(child->get_CurrentName(&name));
            const auto key = Read(id), label = Read(name);
            if (key.rfind(L"item-", 0) == 0) names[std::stoi(key.substr(5))] = label;
        }
    });
    return names;
}

void InvokeIds(Fixture& fixture, std::initializer_list<int> ids) {
    for (const int id : ids) {
        Client(fixture, [id](IUIAutomation& automation, HWND hwnd) {
            winrt::com_ptr<IUIAutomationElement> root; Hr(automation.ElementFromHandle(hwnd, root.put()));
            VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = ::SysAllocString((L"item-" + std::to_wstring(id)).c_str());
            winrt::com_ptr<IUIAutomationCondition> condition;
            const auto created = automation.CreatePropertyCondition(UIA_AutomationIdPropertyId, value, condition.put());
            ::VariantClear(&value); Hr(created);
            winrt::com_ptr<IUIAutomationElement> item; Hr(root->FindFirst(TreeScope_Children, condition.get(), item.put())); Need(item != nullptr);
            winrt::com_ptr<IUIAutomationInvokePattern> invoke;
            Hr(item->GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), invoke.put_void()));
            Need(invoke != nullptr); Hr(invoke->Invoke());
        });
        RunFor(fixture.loop, 30);
    }
}

bool Has(const std::wstring& text, std::wstring_view part) { return text.find(part) != std::wstring::npos; }
bool Requested(const Fixture& fixture, std::string_view part) {
    for (const auto& request : fixture.http.Requests())
        if (request.url.find(part) != std::string::npos) return true;
    return false;
}

// --- render receipts over a private backdrop ---
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
        cls.lpszClassName = L"Aether.Weather.TestBackdrop";
        REQUIRE(::RegisterClassW(&cls) || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        hwnd = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"", WS_POPUP, bounds.left,
                                 bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top, nullptr, nullptr, cls.hInstance, nullptr);
        REQUIRE(hwnd != nullptr); ::ShowWindow(hwnd, SW_SHOWNOACTIVATE); ::UpdateWindow(hwnd);
    }
};
bool Guarded(HWND backdrop, HWND panel, const RECT& area) {
    RECT bounds{}; DWORD owner = 0; ::GetWindowThreadProcessId(backdrop, &owner);
    if (owner != ::GetCurrentProcessId() || !::IsWindowVisible(backdrop) || !::GetWindowRect(backdrop, &bounds) || bounds.left > area.left ||
        bounds.top > area.top || bounds.right < area.right || bounds.bottom < area.bottom) return false;
    for (HWND other = ::GetTopWindow(nullptr); other; other = ::GetWindow(other, GW_HWNDNEXT)) {
        if (other == backdrop) return true;
        if (other == panel || !::IsWindowVisible(other)) continue;
        RECT rect{}, overlap{};
        if (::GetWindowRect(other, &rect) && ::IntersectRect(&overlap, &rect, &area)) return false;
    }
    return false;
}
bool Capture(HWND backdrop, HWND panel, const RECT& crop, const std::filesystem::path& file) {
    if (!Guarded(backdrop, panel, crop)) return false;
    const int width = crop.right - crop.left, height = crop.bottom - crop.top;
    HDC screen = ::GetDC(nullptr), memory = ::CreateCompatibleDC(screen);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width; info.bmiHeader.biHeight = -height; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    void* bits = nullptr; HBITMAP bitmap = ::CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) { if (bitmap) ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen); return false; }
    const auto previous = ::SelectObject(memory, bitmap);
    const bool copied = ::BitBlt(memory, 0, 0, width, height, screen, crop.left, crop.top, SRCCOPY | CAPTUREBLT) != FALSE;
    std::vector<std::uint8_t> bytes;
    if (copied) bytes.assign(static_cast<std::uint8_t*>(bits), static_cast<std::uint8_t*>(bits) + static_cast<std::size_t>(width) * height * 4);
    ::SelectObject(memory, previous); ::DeleteObject(bitmap); ::DeleteDC(memory); ::ReleaseDC(nullptr, screen);
    if (!copied || !Guarded(backdrop, panel, crop)) return false;
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
    Hr(frame->Commit()); Hr(encoder->Commit());
    return true;
}

}  // namespace

AETHER_TEST("weather_ui integration: opens on the home place with its forecast, its country's temperatures and an accessible summary") {
    Fixture fixture; fixture.Initialize();
    CHECK_EQ(fixture.http.RequestCount(), 0u);  // nothing is asked before the window opens
    fixture.Open();
    fixture.Loaded();
    CHECK(Requested(fixture, "&hourly="));
    CHECK(Requested(fixture, "current=temperature_2m,weather_code,is_day"));
    CHECK_FALSE(Requested(fixture, "geocoding-api"));
    const auto names = Names(fixture);
    REQUIRE(names.contains(100));
    CHECK(Has(names.at(100), L"Paris"));
    CHECK(Has(names.at(100), L"11"));
    for (const int id : {1, 2, 3, 8, 110, 140, 160, 165}) CHECK(names.contains(id));
    CHECK_EQ(names.at(1), std::wstring(L"Close"));
    CHECK(names.at(110).rfind(L"Now", 0) == 0);
    CHECK(names.at(140).rfind(L"Today", 0) == 0);
    CHECK(names.contains(146));  // seven days
    REQUIRE(names.contains(190));
    CHECK(Has(names.at(190), L"France"));
    CHECK_FALSE(names.contains(5));  // a chosen city needs no permission banner
    CHECK_FALSE(names.contains(7));
    // Inside the work area.
    const RECT panel = fixture.window->PanelBoundsPx();
    const auto& area = platform::PrimaryMonitor().workArea;
    CHECK(panel.left >= area.left); CHECK(panel.top >= area.top); CHECK(panel.right <= area.right); CHECK(panel.bottom <= area.bottom);
}

AETHER_TEST("weather_ui integration: a searched city replaces the place until My location brings the home back") {
    Fixture fixture; fixture.Initialize();
    fixture.Open();
    fixture.Loaded();
    const HWND edit = ::FindWindowExW(fixture.window->Handle(), nullptr, L"EDIT", nullptr);
    REQUIRE(edit != nullptr);
    ::SetWindowTextW(edit, L"Lyon");
    REQUIRE(fixture.WaitFor([&] { return Requested(fixture, "geocoding-api"); }, 3000));
    RunFor(fixture.loop, 200);
    auto names = Names(fixture);
    REQUIRE(names.contains(20));
    CHECK(Has(names.at(20), L"Lyon"));
    CHECK(names.contains(21));
    const auto before = fixture.http.RequestCount();
    InvokeIds(fixture, {20});
    fixture.Loaded(before + 2);
    CHECK(Requested(fixture, "latitude=45.7"));
    names = Names(fixture);
    CHECK(Has(names.at(100), L"Lyon"));
    CHECK_FALSE(names.contains(20));  // the list closed
    wchar_t text[16]{};
    CHECK_EQ(::GetWindowTextW(edit, text, 16), 0);
    InvokeIds(fixture, {3});
    RunFor(fixture.loop, 400);
    names = Names(fixture);
    CHECK(Has(names.at(100), L"Paris"));
}

AETHER_TEST("weather_ui integration: the permission banner asks the host and never reads the position itself") {
    WeatherHome nowhere;
    nowhere.permission = LocationPermission::NotAsked;
    Fixture fixture; fixture.Initialize(nowhere);
    fixture.Open();
    RunFor(fixture.loop, 200);
    CHECK_EQ(fixture.http.RequestCount(), 0u);  // no place, no request
    auto names = Names(fixture);
    REQUIRE(names.contains(5));
    CHECK_EQ(names.at(5), std::wstring(L"Allow"));
    CHECK(Has(names.at(100), L"Search for a city"));
    InvokeIds(fixture, {5});
    CHECK_EQ(fixture.accessRequests, 1);
    // Windows' own prompt may take the focus: the window stays open meanwhile.
    ::SendMessageW(fixture.window->Handle(), WM_ACTIVATE, WA_INACTIVE, 0);
    CHECK(fixture.window->IsOpen());
    nowhere.permission = LocationPermission::Denied;
    fixture.window->SetHome(nowhere);
    names = Names(fixture);
    CHECK_FALSE(names.contains(5));
    REQUIRE(names.contains(6));
    InvokeIds(fixture, {6});
    CHECK_EQ(fixture.settingsRequests, 1);
    WeatherHome approximate;
    approximate.source = HomeSource::ApproximateLocation;
    approximate.latitude = 48.85;
    approximate.longitude = 2.35;
    approximate.permission = LocationPermission::Granted;
    fixture.window->SetHome(approximate);
    fixture.Loaded();
    names = Names(fixture);
    REQUIRE(names.contains(5));
    CHECK_EQ(names.at(5), std::wstring(L"Use precise location"));
    CHECK(Has(names.at(100), L"Paris"));  // named after the nearest town
    approximate.source = HomeSource::PreciseLocation;
    fixture.window->SetHome(approximate);
    names = Names(fixture);
    CHECK_FALSE(names.contains(5));
    CHECK_FALSE(names.contains(6));
}

// The latitude of the last forecast request (the "&hourly=" one), or NaN.
double LastForecastLatitude(const Fixture& fixture) {
    const auto requests = fixture.http.Requests();
    for (auto it = requests.rbegin(); it != requests.rend(); ++it)
        if (it->url.find("&hourly=") != std::string::npos) {
            const auto latitude = Coordinates(it->url, "latitude");
            if (!latitude.empty()) return latitude.front();
        }
    return std::nan("");
}

AETHER_TEST("weather_ui integration: small moves that add up move the followed place, a wobble does not (A10)") {
    WeatherHome here;
    here.source = HomeSource::PreciseLocation;
    here.latitude = 48.85;
    here.longitude = 2.35;
    here.permission = LocationPermission::Granted;
    Fixture fixture; fixture.Initialize(here);
    fixture.Open();
    fixture.Loaded();
    CHECK(std::abs(LastForecastLatitude(fixture) - 48.85) < 0.001);
    // Two steps of 0.012 degrees: neither alone passes 0.02, together they do.
    for (int step = 1; step <= 2; ++step) {
        here.latitude = 48.85 + 0.012 * step;
        fixture.window->SetHome(here);
        RunFor(fixture.loop, 50);
    }
    REQUIRE(fixture.WaitFor([&] { return std::abs(LastForecastLatitude(fixture) - 48.874) < 0.001; }, 5000));
    RunFor(fixture.loop, 300);
    // A wobble around the place now shown: no new request.
    const auto requests = fixture.http.RequestCount();
    for (const double wobble : {0.008, -0.009, 0.005}) {
        here.latitude = 48.874 + wobble;
        fixture.window->SetHome(here);
        RunFor(fixture.loop, 50);
    }
    RunFor(fixture.loop, 300);
    CHECK_EQ(fixture.http.RequestCount(), requests);
}

AETHER_TEST("weather_ui integration: the illustrations move, then stand still and draw nothing under reduced motion") {
    Fixture fixture; fixture.Initialize(Paris(), graphics::MotionPolicy{false, 1.0f});
    fixture.Open();
    fixture.Loaded();
    CHECK(fixture.window->Animating());
    const auto moving = fixture.window->RenderCount();
    RunFor(fixture.loop, 300);
    CHECK(fixture.window->RenderCount() > moving + 3);
    fixture.window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1.0f});
    RunFor(fixture.loop, 300);
    CHECK_FALSE(fixture.window->Animating());
    const auto still = fixture.window->RenderCount();
    RunFor(fixture.loop, 800);
    CHECK_EQ(fixture.window->RenderCount(), still);  // NFR-002: idle draws no frame
    fixture.window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast), graphics::MotionPolicy{false, 1.0f});
    RunFor(fixture.loop, 300);
    CHECK_FALSE(fixture.window->Animating());
}

AETHER_TEST("weather_ui integration: a failed load says why and Try again loads it") {
    Fixture fixture; fixture.Initialize();
    fixture.http.FailWith(ErrorCode::Network);
    fixture.Open();
    fixture.Loaded(1);
    auto names = Names(fixture);
    CHECK_EQ(names.at(100), std::wstring(L"No connection to the weather service."));
    REQUIRE(names.contains(7));
    fixture.http.SetHandler(OpenMeteo());
    const auto before = fixture.http.RequestCount();
    InvokeIds(fixture, {7});
    fixture.Loaded(before + 2);
    names = Names(fixture);
    CHECK(Has(names.at(100), L"Paris"));
    CHECK_FALSE(names.contains(7));
}

AETHER_TEST("weather_ui integration: Escape, the close button, the attribution and the settings button") {
    Fixture fixture; fixture.Initialize();
    fixture.Open();
    fixture.Loaded();
    ::SendMessageW(fixture.window->Handle(), WM_KEYDOWN, VK_ESCAPE, 0);
    CHECK_FALSE(fixture.window->IsOpen());
    CHECK_EQ(fixture.closed, 1);
    fixture.Open();
    RunFor(fixture.loop, 100);
    InvokeIds(fixture, {8, 2});
    REQUIRE_EQ(fixture.links.size(), 1u);
    CHECK_EQ(fixture.links.front(), std::wstring(L"https://open-meteo.com/"));
    CHECK_EQ(fixture.weatherSettings, 1);
    InvokeIds(fixture, {1});
    CHECK_FALSE(fixture.window->IsOpen());
    CHECK_EQ(fixture.closed, 2);
    // Below the Top Bar's weather item when opened from it.
    const auto& area = platform::PrimaryMonitor().workArea;
    const RECT item{area.right - 260, area.top + 4, area.right - 200, area.top + 32};
    REQUIRE(fixture.window->Toggle(item).ok());
    const RECT panel = fixture.window->PanelBoundsPx();
    CHECK(panel.top >= item.bottom);
    CHECK(panel.right <= area.right);
    REQUIRE(fixture.window->Toggle(item).ok());
    CHECK_FALSE(fixture.window->IsOpen());
}

AETHER_TEST("weather_ui integration: optional render receipts in Grain, Liquid, light and high contrast") {
    Fixture fixture; fixture.Initialize();
    fixture.Open();
    fixture.Loaded();
    RECT guarded = fixture.window->BoundsPx(); ::InflateRect(&guarded, 40, 40);
    Backdrop backdrop; backdrop.Create(guarded);
    const auto output = test::MakeTempDirectory("weather-window");
    int saved = 0;
    const auto receipt = [&](const wchar_t* name) {
        ::SetWindowPos(backdrop.hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ::SetWindowPos(fixture.window->Handle(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        RunFor(fixture.loop, 600);
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (Capture(backdrop.hwnd, fixture.window->Handle(), fixture.window->BoundsPx(), output / name)) { ++saved; return; }
            RunFor(fixture.loop, 200);
        }
        test::Note("Weather window receipt unavailable: the capture guard refused; no pixels saved.");
    };
    receipt(L"weather-dark.png");
    auto liquid = graphics::MakeDesignTokens(graphics::ThemeMode::Dark);
    liquid.liquid = true;
    fixture.window->SetTheme(liquid, graphics::MotionPolicy{true, 1.0f});
    receipt(L"weather-dark-liquid.png");
    fixture.window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Light), graphics::MotionPolicy{true, 1.0f});
    receipt(L"weather-light.png");
    auto liquidLight = graphics::MakeDesignTokens(graphics::ThemeMode::Light);
    liquidLight.liquid = true;
    fixture.window->SetTheme(liquidLight, graphics::MotionPolicy{true, 1.0f});
    receipt(L"weather-light-liquid.png");
    fixture.window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::HighContrast), graphics::MotionPolicy{true, 1.0f});
    receipt(L"weather-high-contrast.png");
    fixture.window->SetTheme(graphics::MakeDesignTokens(graphics::ThemeMode::Dark), graphics::MotionPolicy{true, 1.0f});
    const HWND edit = ::FindWindowExW(fixture.window->Handle(), nullptr, L"EDIT", nullptr);
    REQUIRE(edit != nullptr);
    ::SetWindowTextW(edit, L"Lyon");
    REQUIRE(fixture.WaitFor([&] { return Requested(fixture, "geocoding-api"); }, 3000));
    receipt(L"weather-dark-search.png");
    WeatherHome nowhere;
    nowhere.permission = LocationPermission::NotAsked;
    fixture.window->SetHome(nowhere);
    fixture.window->Close();
    fixture.Open();
    receipt(L"weather-dark-permission.png");
    test::Note("Weather window receipts saved: " + std::to_string(saved) + " in " + output.string());
}
