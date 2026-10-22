// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/ui/SettingsWindow.h"

#include "aether/BuildInfo.h"
#include "core/Strings.h"
#include "core/WorkerPool.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/WindowUtilities.h"
#include "settings/SettingsSerialization.h"
#include "updates/ReleaseNotes.h"

#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <map>
#include <stdexcept>
#include <vector>

namespace aether::settings::ui {
namespace {
constexpr wchar_t kClass[] = L"AetherShell.Settings";
constexpr int kNavigation = 100, kApply = 101, kReset = 102, kStatus = 103;
constexpr UINT_PTR kLiveApplyTimer = 19;
constexpr int kFirstField = 200, kCheckUpdate = 301, kInstallUpdate = 302, kPostpone = 303,
              kSkip = 304, kDisableUpdates = 305, kReleaseDetails = 306, kGeoSearch = 307,
              kGeoResults = 308, kOpenLogs = 309, kExport = 310, kAddPin = 311, kPinList = 312,
              kRemovePin = 313, kMovePinUp = 314, kMovePinDown = 315, kLicense = 316, kUpdateSummary = 317,
              kDesktopStatus = 318, kImportTaskbarPins = 319, kWindowsLocation = 320, kLocationSettings = 321;
bool CanDownloadOrInstall(const updates::UpdateSnapshot& value) {
    return value.state != updates::UpdateState::Checking && value.state != updates::UpdateState::Downloading &&
        (value.prepared.has_value() || static_cast<bool>(value.manifest));
}
constexpr wchar_t kMitLicense[] =
    L"MIT License\r\n\r\nCopyright (c) 2026 Ibrahim Bouillet\r\n\r\n"
    L"Permission is hereby granted, free of charge, to any person obtaining a copy "
    L"of this software and associated documentation files (the \"Software\"), to deal "
    L"in the Software without restriction, including without limitation the rights "
    L"to use, copy, modify, merge, publish, distribute, sublicense, and/or sell "
    L"copies of the Software, and to permit persons to whom the Software is "
    L"furnished to do so, subject to the following conditions:\r\n\r\n"
    L"The above copyright notice and this permission notice shall be included in all "
    L"copies or substantial portions of the Software.\r\n\r\n"
    L"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
    L"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
    L"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE "
    L"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
    L"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
    L"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
    L"SOFTWARE.";
enum class FieldKind { Check, Number, Choice, Key };
struct Binding {
    FieldKind kind;
    std::wstring label;
    std::function<std::wstring(const Settings&)> read;
    std::function<void(Settings&, std::wstring)> write;
    std::vector<std::wstring> choices;
    bool consentOnly = false;
};
std::wstring WindowText(HWND window) {
    const int count = ::GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(count) + 1, L'\0');
    ::GetWindowTextW(window, text.data(), count + 1); text.resize(static_cast<std::size_t>(count)); return text;
}
}

struct SettingsWindow::Impl {
    Impl(SettingsWindow& window, SettingsService& settings, IUiDispatcher& ui, SettingsCallbacks actions)
        : owner(window), service(settings), dispatcher(ui), callbacks(std::move(actions)), worker({1, L"Aether settings UI", ComApartment::MultiThreaded}) {}
    SettingsWindow& owner;
    SettingsService& service;
    IUiDispatcher& dispatcher;
    SettingsCallbacks callbacks;
    WorkerPool worker;
    CancellationSource geocodeCancel;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    HWND window = nullptr, navigation = nullptr, status = nullptr;
    HFONT font = nullptr, headingFont = nullptr;
    UINT dpi = 96;
    int scrollX = 0, scrollY = 0;
    SettingsPage page = SettingsPage::Dock;
    std::vector<Binding> bindings;
    std::vector<std::wstring> fieldBaseline;
    std::map<int, HWND> controls;
    std::vector<HWND> pageControls;
    struct PermanentControl { HWND handle; RECT dips; };
    std::vector<PermanentControl> permanentControls;
    std::vector<weather::LocationCandidate> locations;
    std::vector<apps::ApplicationInfo> applications;
    updates::UpdateSnapshot update;
    std::wstring lastActionStatus;
    bool startupBusy = false;
    bool building = false;
    bool livePending = false;
    std::uint64_t applyGeneration = 0;
    std::vector<std::function<void()>> closeWaiters;

    int Px(int dips) const { return ::MulDiv(dips, static_cast<int>(dpi), 96); }
    HWND Add(const wchar_t* cls, std::wstring title, DWORD style, int x, int y, int w, int h, int id = 0, bool pageChild = true) {
        HWND control = ::CreateWindowExW(cls == std::wstring_view(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, title.c_str(),
            WS_CHILD | WS_VISIBLE | style, Px(x)-scrollX, Px(y)-scrollY, Px(w), Px(h), window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (id) controls[id] = control;
        if (pageChild) pageControls.push_back(control);
        else permanentControls.push_back({control, {x, y, x + w, y + h}});
        return control;
    }
    void Message(std::wstring text) { if (status) ::SetWindowTextW(status, text.c_str()); }
    void BuildPage();
    void Apply(bool explicitConsent);
    void ScheduleLive();
    void SetStartupBusy(bool busy);
    void RefreshFonts();
    void LayoutPermanent();
    void UpdateScrollbars();
    void ScrollTo(int x, int y);
    void RevealFocus();
    std::vector<std::wstring> ReadFields() const;
    void RestoreFields(const std::vector<std::wstring>& values);
    Result<void> ResetAfterConsent();
    void SearchLocation();
    void Action(int id);
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); self->window = hwnd; ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        try { if (self) return self->Handle(message, wParam, lParam); }
        catch (...) { if (self) self->Message(L"This action could not be completed."); return 0; }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
};

void SettingsWindow::Impl::BuildPage() {
    building = true;
    for (HWND control : pageControls) ::DestroyWindow(control);
    pageControls.clear();
    std::erase_if(controls, [](const auto& pair) { return pair.first >= kFirstField; });
    bindings.clear(); locations.clear();
    const auto& current = service.Current();
    auto check = [&](std::wstring label, auto get, auto set, bool consentOnly = false) {
        bindings.push_back(Binding{FieldKind::Check, std::move(label), [get](const Settings& s) { return get(s) ? L"1" : L"0"; },
            [set](Settings& s, std::wstring value) { set(s, value == L"1"); }, {}, consentOnly});
    };
    auto number = [&](std::wstring label, auto get, auto set) {
        bindings.push_back(Binding{FieldKind::Number, std::move(label), [get](const Settings& s) { return std::to_wstring(get(s)); },
            [set](Settings& s, std::wstring value) { wchar_t* end{}; const double parsed = std::wcstod(value.c_str(), &end); if (end == value.c_str() || *end || !std::isfinite(parsed)) throw std::runtime_error("invalid numeric setting"); set(s, std::clamp(parsed, -1'000'000.0, 1'000'000.0)); }, {}});
    };
    auto choice = [&](std::wstring label, std::vector<std::wstring> choices, auto get, auto set) {
        bindings.push_back(Binding{FieldKind::Choice, std::move(label), [get](const Settings& s) { return std::to_wstring(static_cast<int>(get(s))); },
            [set](Settings& s, std::wstring value) { set(s, static_cast<int>(std::wcstol(value.c_str(), nullptr, 10))); }, std::move(choices)});
    };
    auto key = [&](std::wstring label, auto get, auto set) {
        bindings.push_back(Binding{FieldKind::Key, std::move(label), [get](const Settings& s){return std::to_wstring(get(s));},
            [set](Settings& s, std::wstring value){set(s, static_cast<std::uint32_t>(std::wcstoul(value.c_str(),nullptr,10)));}, {}});
    };
    if (page == SettingsPage::Dock) {
        number(L"Icon size (32–96 DIPs)", [](const Settings& s){return s.dock.iconSize;}, [](Settings& s,double v){s.dock.iconSize=static_cast<int>(v);});
        number(L"Bottom margin", [](const Settings& s){return s.dock.bottomMargin;}, [](Settings& s,double v){s.dock.bottomMargin=static_cast<int>(v);});
        number(L"Minimum side margin", [](const Settings& s){return s.dock.sideMargin;}, [](Settings& s,double v){s.dock.sideMargin=static_cast<int>(v);});
        number(L"Icon spacing", [](const Settings& s){return s.dock.iconSpacing;}, [](Settings& s,double v){s.dock.iconSpacing=static_cast<int>(v);});
        number(L"Glass opacity (0.30–1.00)", [](const Settings& s){return s.dock.opacity;}, [](Settings& s,double v){s.dock.opacity=v;});
        number(L"Hover magnification (1.00–2.00)", [](const Settings& s){return s.dock.magnification;}, [](Settings& s,double v){s.dock.magnification=v;});
        check(L"Show running applications", [](const Settings& s){return s.dock.showRunningApps;}, [](Settings& s,bool v){s.dock.showRunningApps=v;});
        check(L"Reserve screen space for the Dock", [](const Settings& s){return s.dock.reserveSpace;}, [](Settings& s,bool v){s.dock.reserveSpace=v;});
        Add(L"STATIC", L"Pinned applications", 0, 218, 398, 260, 24);
        HWND pins = Add(L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | WS_BORDER | WS_VSCROLL, 218, 428, 300, 100, kPinList);
        for (const auto& pin : current.dock.pinned) ::SendMessageW(pins, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pin.displayName.c_str()));
        Add(L"BUTTON", L"Move up", WS_TABSTOP, 530, 428, 130, 26, kMovePinUp);
        Add(L"BUTTON", L"Move down", WS_TABSTOP, 530, 462, 130, 26, kMovePinDown);
        Add(L"BUTTON", L"Remove", WS_TABSTOP, 530, 496, 130, 26, kRemovePin);
        applications = callbacks.catalog ? callbacks.catalog() : std::vector<apps::ApplicationInfo>{};
        HWND chooser = Add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 218, 537, 300, 210, kFirstField + 72);
        for (const auto& app : applications) ::SendMessageW(chooser, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(app.displayName.c_str()));
        Add(L"BUTTON", L"Add application", WS_TABSTOP, 530, 537, 130, 28, kAddPin);
    } else if (page == SettingsPage::TopBar) {
        number(L"Top margin", [](const Settings& s){return s.topBar.topMargin;}, [](Settings& s,double v){s.topBar.topMargin=static_cast<int>(v);});
        number(L"Glass opacity", [](const Settings& s){return s.topBar.opacity;}, [](Settings& s,double v){s.topBar.opacity=v;});
        choice(L"Clock format", {L"System",L"12 hour",L"24 hour"}, [](const Settings& s){return s.topBar.clockFormat;}, [](Settings& s,int v){s.topBar.clockFormat=static_cast<ClockFormat>(v);});
        choice(L"Date style", {L"Weekday / day / month",L"Short",L"Long"}, [](const Settings& s){return s.topBar.dateStyle;}, [](Settings& s,int v){s.topBar.dateStyle=static_cast<DateStyle>(v);});
        check(L"Show seconds", [](const Settings& s){return s.topBar.showSeconds;}, [](Settings& s,bool v){s.topBar.showSeconds=v;});
        check(L"Show date", [](const Settings& s){return s.topBar.showDate;}, [](Settings& s,bool v){s.topBar.showDate=v;});
        check(L"Show battery", [](const Settings& s){return s.topBar.showBattery;}, [](Settings& s,bool v){s.topBar.showBattery=v;});
        check(L"Hide battery when absent", [](const Settings& s){return s.topBar.hideBatteryWhenAbsent;}, [](Settings& s,bool v){s.topBar.hideBatteryWhenAbsent=v;});
        check(L"Show input language", [](const Settings& s){return s.topBar.showKeyboard;}, [](Settings& s,bool v){s.topBar.showKeyboard=v;});
        check(L"Show network", [](const Settings& s){return s.topBar.showNetwork;}, [](Settings& s,bool v){s.topBar.showNetwork=v;});
        check(L"Show audio output", [](const Settings& s){return s.topBar.showVolume;}, [](Settings& s,bool v){s.topBar.showVolume=v;});
        check(L"Show microphone", [](const Settings& s){return s.topBar.showMicrophone;}, [](Settings& s,bool v){s.topBar.showMicrophone=v;});
        check(L"Show weather when enabled", [](const Settings& s){return s.topBar.showWeather;}, [](Settings& s,bool v){s.topBar.showWeather=v;});
        check(L"Reserve screen space above windows", [](const Settings& s){return s.topBar.reserveSpace;}, [](Settings& s,bool v){s.topBar.reserveSpace=v;});
    } else if (page == SettingsPage::Weather) {
        check(L"Enable weather", [](const Settings& s){return s.weather.enabled;}, [](Settings& s,bool v){s.weather.enabled=v;});
        choice(L"Temperature", {L"Celsius",L"Fahrenheit"}, [](const Settings& s){return s.weather.unit;}, [](Settings& s,int v){s.weather.unit=static_cast<TemperatureUnit>(v);});
        number(L"Refresh interval (15–180 minutes)", [](const Settings& s){return s.weather.refreshMinutes;}, [](Settings& s,double v){s.weather.refreshMinutes=static_cast<int>(v);});
        check(L"Locate automatically with Windows", [](const Settings& s){return s.weather.useWindowsLocation;}, [](Settings& s,bool v){s.weather.useWindowsLocation=v;});
        Add(L"STATIC", L"Windows asks for location permission. Approximate coordinates are sent to Open-Meteo.com; a coarse location key is cached locally. If location is blocked, weather is unavailable.", 0, 218, 278, 460, 56);
        Add(L"BUTTON", L"Use Windows location", WS_TABSTOP | BS_PUSHBUTTON, 218, 342, 224, 32, kWindowsLocation);
        Add(L"BUTTON", L"Location settings", WS_TABSTOP | BS_PUSHBUTTON, 454, 342, 204, 32, kLocationSettings);
        Add(L"STATIC", callbacks.weatherLocationStatus ? callbacks.weatherLocationStatus() : L"Choose automatic location or search for a place below.", 0, 218, 382, 440, 32);
        Add(L"EDIT", current.weather.locationName, WS_TABSTOP | ES_AUTOHSCROLL, 218, 426, 330, 30, kFirstField + 70);
        Add(L"BUTTON", L"Search", WS_TABSTOP | BS_PUSHBUTTON, 558, 426, 100, 30, kGeoSearch);
        Add(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, 218, 468, 440, 115, kGeoResults);
        Add(L"STATIC", L"Weather data by Open-Meteo.com. Manual search sends only the text you enter.", 0, 218, 598, 440, 40);
    } else if (page == SettingsPage::Updates) {
        check(L"Check for updates automatically", [](const Settings& s){return s.updates.automaticChecks;}, [](Settings& s,bool v){s.updates.automaticChecks=v;});
        number(L"Check interval (6–168 hours)", [](const Settings& s){return s.updates.checkIntervalHours;}, [](Settings& s,double v){s.updates.checkIntervalHours=static_cast<int>(v);});
        choice(L"Release channel", {L"Stable",L"Prerelease"}, [](const Settings& s){return s.updates.channel;}, [](Settings& s,int v){s.updates.channel=static_cast<UpdateChannel>(v);});
        check(L"Show update notifications", [](const Settings& s){return s.updates.notifications;}, [](Settings& s,bool v){s.updates.notifications=v;});
        auto state = L"Installed: " + text::Widen(build::kVersion) + L"    Status: " + text::Widen(updates::ToString(update.state));
        if(update.state==updates::UpdateState::Downloading) {
            state+=L"\r\nReceived "+std::to_wstring(update.progress.received);
            if(update.progress.total) {
                const auto percent=static_cast<unsigned>(std::clamp(100.0*static_cast<double>(update.progress.received)/static_cast<double>(update.progress.total),0.0,100.0));
                state+=L" of "+std::to_wstring(update.progress.total)+L" bytes ("+std::to_wstring(percent)+L"%).";
            } else state+=L" bytes; total unknown.";
        } else if(update.downloadSize) state+=L"\r\nDownload size: "+std::to_wstring(*update.downloadSize)+L" bytes.";
        if(update.failure) {
            state+=L"\r\nFailure: "+text::Widen(ToString(update.failure->error.code))+L". ";
            state+=CanDownloadOrInstall(update)?L"Retry Download / install.":L"Use Check now to try again.";
        }
        Add(L"STATIC", state, 0, 218, 265, 455, 60, kUpdateSummary);
        Add(L"EDIT", text::Widen(update.releaseNotes), WS_VSCROLL | ES_MULTILINE | ES_READONLY, 218, 330, 440, 125, kFirstField + 71);
        Add(L"BUTTON", L"Check now", WS_TABSTOP, 218, 470, 100, 30, kCheckUpdate);
        HWND install = Add(L"BUTTON", L"Download / install", WS_TABSTOP, 328, 470, 155, 30, kInstallUpdate);
        ::EnableWindow(install, CanDownloadOrInstall(update));
        Add(L"BUTTON", L"Later", WS_TABSTOP, 493, 470, 75, 30, kPostpone);
        Add(L"BUTTON", L"Skip", WS_TABSTOP, 578, 470, 80, 30, kSkip);
        Add(L"BUTTON", L"Release details", WS_TABSTOP, 218, 512, 140, 28, kReleaseDetails);
        Add(L"BUTTON", L"Disable notifications", WS_TABSTOP, 370, 512, 180, 28, kDisableUpdates);
    } else if (page == SettingsPage::General) {
        check(L"Start Aether Shell when I sign in (Apply to consent)", [](const Settings& s){return s.general.startWithWindows;}, [](Settings& s,bool v){s.general.startWithWindows=v;}, true);
        choice(L"Motion", {L"Follow Windows",L"Reduced",L"Full"}, [](const Settings& s){return s.general.motion;}, [](Settings& s,int v){s.general.motion=static_cast<MotionPreference>(v);});
        check(L"Enable global keyboard shortcuts", [](const Settings& s){return s.general.hotkeysEnabled;}, [](Settings& s,bool v){s.general.hotkeysEnabled=v;});
        key(L"Search shortcut key", [](const Settings& s){return s.general.searchHotkey.virtualKey;}, [](Settings& s,std::uint32_t v){s.general.searchHotkey.virtualKey=v;});
        key(L"Dock shortcut key", [](const Settings& s){return s.general.focusDockHotkey.virtualKey;}, [](Settings& s,std::uint32_t v){s.general.focusDockHotkey.virtualKey=v;});
        const auto modifierIndex = [](std::uint32_t v) { return v >= 1 && v <= 15 ? static_cast<int>(v - 1) : 2; };
        const auto modifierBits = [](int index) { return index >= 0 && index < 15 ? static_cast<std::uint32_t>(index + 1) : 3u; };
        const std::vector<std::wstring> modifiers{L"Alt",L"Ctrl",L"Ctrl + Alt",L"Shift",L"Alt + Shift",L"Ctrl + Shift",L"Ctrl + Alt + Shift",
            L"Windows",L"Alt + Windows",L"Ctrl + Windows",L"Ctrl + Alt + Windows",L"Shift + Windows",L"Alt + Shift + Windows",
            L"Ctrl + Shift + Windows",L"Ctrl + Alt + Shift + Windows"};
        choice(L"Search shortcut modifiers", modifiers, [modifierIndex](const Settings& s){return modifierIndex(s.general.searchHotkey.modifiers);}, [modifierBits](Settings& s,int v){s.general.searchHotkey.modifiers=modifierBits(v);});
        choice(L"Dock shortcut modifiers", modifiers, [modifierIndex](const Settings& s){return modifierIndex(s.general.focusDockHotkey.modifiers);}, [modifierBits](Settings& s,int v){s.general.focusDockHotkey.modifiers=modifierBits(v);});
        Add(L"STATIC", callbacks.hotkeyStatus ? callbacks.hotkeyStatus() : L"Shortcut status unavailable", 0, 218, 405, 440, 48);
        Add(L"BUTTON", L"Open log folder", WS_TABSTOP, 218, 470, 150, 30, kOpenLogs);
        Add(L"BUTTON", L"Export diagnostics…", WS_TABSTOP, 380, 470, 180, 30, kExport);
    } else if (page == SettingsPage::Appearance) {
        choice(L"Color mode", {L"Follow Windows", L"Light", L"Dark"}, [](const Settings& s){return s.general.themeMode;}, [](Settings& s,int v){s.general.themeMode=static_cast<ThemePreference>(v);});
        choice(L"Accent color", {L"Windows", L"Aether"}, [](const Settings& s){return s.general.accentSource;}, [](Settings& s,int v){s.general.accentSource=static_cast<AccentSource>(v);});
        number(L"Animation duration (0.5–1.5)", [](const Settings& s){return s.general.animationSpeed;}, [](Settings& s,double v){s.general.animationSpeed=v;});
        choice(L"Dock icon preset", {L"Small — 32 DIPs", L"Medium — 40 DIPs", L"Large — 48 DIPs", L"Keep custom size"},
            [](const Settings& s){return s.dock.iconSize==32?0:s.dock.iconSize==40?1:s.dock.iconSize==48?2:3;},
            [](Settings& s,int v){if(v>=0 && v<3)s.dock.iconSize=v==0?32:v==1?40:48;});
        check(L"Show Windows Start in the Dock", [](const Settings& s){return s.dock.showStartButton;}, [](Settings& s,bool v){s.dock.showStartButton=v;});
        check(L"Show Task View in the Dock", [](const Settings& s){return s.dock.showTaskViewButton;}, [](Settings& s,bool v){s.dock.showTaskViewButton=v;});
        check(L"Reserve extra space for Dock animations", [](const Settings& s){return s.dock.reserveAnimationSpace;}, [](Settings& s,bool v){s.dock.reserveAnimationSpace=v;});
        choice(L"Weather position", {L"Left", L"Right"}, [](const Settings& s){return s.topBar.weatherPlacement;}, [](Settings& s,int v){s.topBar.weatherPlacement=static_cast<WeatherPlacement>(v);});
        choice(L"Keyboard indicator", {L"Compact", L"Two lines"}, [](const Settings& s){return s.topBar.keyboardIndicator;}, [](Settings& s,int v){s.topBar.keyboardIndicator=static_cast<KeyboardIndicatorStyle>(v);});
        check(L"Add newly discovered Windows taskbar pins", [](const Settings& s){return s.general.syncTaskbarPins;}, [](Settings& s,bool v){s.general.syncTaskbarPins=v;});
        Add(L"STATIC", L"High contrast and reduced motion take precedence. A custom Dock size is kept until you choose a preset.", 0, 218, 512, 450, 48);
    } else if (page == SettingsPage::Desktop) {
        check(L"Replace the Windows taskbar (Apply to consent)",
            [](const Settings& s){return s.general.replaceWindowsTaskbar;},
            [](Settings& s,bool v){s.general.replaceWindowsTaskbar=v;}, true);
        Add(L"STATIC", L"Press Apply to enable or disable this mode. Aether Shell hides the Windows "
            L"taskbar while running and reserves space below maximized applications.", 0, 218, 150, 450, 62);
        Add(L"STATIC", L"Windows taskbar state is restored when this mode is disabled or Aether Shell exits. "
            L"A separate recovery helper also restores it if the application stops unexpectedly. "
            L"If recovery fails, restart Windows Explorer in Task Manager.", 0, 218, 222, 450, 100);
        Add(L"STATIC", L"Windows Settings remains the native Windows interface. Its taskbar page is not "
            L"replaced by Aether Shell; configure this mode here.", 0, 218, 334, 450, 62);
        Add(L"STATIC", L"", 0, 218, 410, 450, 60, kDesktopStatus);
        owner.RefreshDesktopStatus();
        HWND import = Add(L"BUTTON", L"Import Windows taskbar pins", WS_TABSTOP, 218, 484, 270, 32, kImportTaskbarPins);
        ::EnableWindow(import, static_cast<bool>(callbacks.importTaskbarPins));
        Add(L"STATIC", L"Reads pinned shortcuts without changing Windows pins. Some Windows 11 pins "
            L"or their order may be unavailable.", 0, 218, 526, 450, 40);
    } else {
        Add(L"STATIC", L"Aether Shell", 0, 218, 98, 430, 36);
        Add(L"STATIC", L"Version " + text::Widen(build::kVersion) + L"\r\nCreated by Ibrahim Bouillet\r\n\r\nA native Windows desktop interface running alongside Explorer.\r\nWeather data: Open-Meteo.com (CC BY 4.0).\r\nNo third-party C++ runtime libraries are bundled.", 0, 218, 152, 440, 140);
        Add(L"EDIT", kMitLicense, WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, 218, 300, 440, 215, kLicense);
        Add(L"BUTTON", L"Official project", WS_TABSTOP, 218, 530, 150, 30, kReleaseDetails);
    }
    int y = 100;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        auto& binding = bindings[i]; const int id = kFirstField + static_cast<int>(i);
        const auto value = binding.read(current);
        if (binding.kind == FieldKind::Check) {
            HWND control = Add(L"BUTTON", binding.label, WS_TABSTOP | BS_AUTOCHECKBOX, 218, y, 450, 25, id);
            ::SendMessageW(control, BM_SETCHECK, value == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
        } else {
            Add(L"STATIC", binding.label, 0, 218, y + 4, 300, 24);
            if (binding.kind == FieldKind::Choice) {
                HWND combo = Add(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 520, y, 150, 200, id);
                for (const auto& option : binding.choices) ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.c_str()));
                ::SendMessageW(combo, CB_SETCURSEL, std::wcstol(value.c_str(), nullptr, 10), 0);
            } else if (binding.kind == FieldKind::Key) {
                HWND hotkey = Add(HOTKEY_CLASSW, L"", WS_TABSTOP, 520, y, 150, 28, id);
                ::SendMessageW(hotkey, HKM_SETRULES, 0, 0);
                ::SendMessageW(hotkey, HKM_SETHOTKEY, static_cast<WPARAM>(std::wcstoul(value.c_str(), nullptr, 10)), 0);
            } else Add(L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL, 520, y, 150, 28, id);
        }
        y += page == SettingsPage::TopBar ? 32 : page == SettingsPage::Dock ? 34 : 40;
    }
    building = false;
    fieldBaseline = ReadFields();
    SetStartupBusy(startupBusy);
    if (page == SettingsPage::Desktop) {
        Message(lastActionStatus.empty() ? L"Taskbar replacement changes require Apply to consent." : lastActionStatus);
    } else Message(L"Changes save automatically. Apply retries saving; startup requires Apply to consent.");
}

std::vector<std::wstring> SettingsWindow::Impl::ReadFields() const {
    std::vector<std::wstring> values;
    for (std::size_t i = 0; i < bindings.size(); ++i) {
        HWND control = controls.at(kFirstField + static_cast<int>(i)); const auto& binding = bindings[i];
        const auto value = binding.kind == FieldKind::Check ? (::SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED ? L"1" : L"0") :
                           binding.kind == FieldKind::Choice ? std::to_wstring(::SendMessageW(control, CB_GETCURSEL, 0, 0)) :
                           binding.kind == FieldKind::Key ? std::to_wstring(LOBYTE(::SendMessageW(control, HKM_GETHOTKEY, 0, 0))) : WindowText(control);
        values.push_back(value);
    }
    return values;
}
void SettingsWindow::Impl::RestoreFields(const std::vector<std::wstring>& values) {
    building = true;
    for (std::size_t i = 0; i < std::min(values.size(), bindings.size()); ++i) {
        HWND control = controls.at(kFirstField + static_cast<int>(i));
        if (bindings[i].kind == FieldKind::Check) ::SendMessageW(control, BM_SETCHECK, values[i] == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
        else if (bindings[i].kind == FieldKind::Choice) ::SendMessageW(control, CB_SETCURSEL, std::wcstol(values[i].c_str(), nullptr, 10), 0);
        else if (bindings[i].kind == FieldKind::Key) ::SendMessageW(control, HKM_SETHOTKEY, std::wcstoul(values[i].c_str(), nullptr, 10), 0);
        else ::SetWindowTextW(control, values[i].c_str());
    }
    building = false;
}
void SettingsWindow::Impl::ScheduleLive() {
    if (building || startupBusy) return;
    livePending = true; ::SetTimer(window, kLiveApplyTimer, 350, nullptr);
}
void SettingsWindow::Impl::SetStartupBusy(bool busy) {
    startupBusy = busy;
    for (const auto& control : permanentControls) ::EnableWindow(control.handle, !busy);
    for (HWND control : pageControls) ::EnableWindow(control, !busy);
    if (!busy && controls.contains(kInstallUpdate))
        ::EnableWindow(controls.at(kInstallUpdate), CanDownloadOrInstall(update));
    if (!busy && controls.contains(kImportTaskbarPins))
        ::EnableWindow(controls.at(kImportTaskbarPins), static_cast<bool>(callbacks.importTaskbarPins));
    if (!busy) {
        auto ready=std::move(closeWaiters);closeWaiters.clear();
        for(auto& callback:ready) try { callback(); } catch (...) { Message(L"The close action could not be completed."); }
    }
}
void SettingsWindow::Impl::Apply(bool explicitConsent) {
    if (startupBusy) return;
    ::KillTimer(window, kLiveApplyTimer); livePending = false;
    const auto values = ReadFields();
    std::vector<std::pair<std::function<void(Settings&, std::wstring)>, std::wstring>> edits;
    std::vector<std::size_t> acceptedFields;
    for (std::size_t i = 0; i < bindings.size(); ++i)
        if ((explicitConsent || !bindings[i].consentOnly) && (i >= fieldBaseline.size() || values[i] != fieldBaseline[i])) {
            edits.emplace_back(bindings[i].write, values[i]); acceptedFields.push_back(i);
        }
    std::optional<weather::LocationCandidate> location;
    if (page == SettingsPage::Weather && controls.contains(kGeoResults)) {
        const auto selected = ::SendMessageW(controls[kGeoResults], LB_GETCURSEL, 0, 0);
        if (selected != LB_ERR && static_cast<std::size_t>(selected) < locations.size()) location = locations[static_cast<std::size_t>(selected)];
    }
    const auto mutate = [edits = std::move(edits), location](Settings& value) {
        for (const auto& [write, input] : edits) write(value, input);
        if (location) {
            value.weather.useWindowsLocation = false;
            value.weather.latitude = location->latitude; value.weather.longitude = location->longitude;
            value.weather.locationName = location->name + L", " + location->country;
        }
    };
    Settings next = service.Current();
    try { mutate(next); }
    catch (...) { Message(L"Enter a complete numeric value before saving."); return; }
    if (auto validated = Validate(next); !validated) { Message(L"Settings could not be validated."); return; }
    const auto generation = ++applyGeneration;
    const bool startupChange = explicitConsent && next.general.startWithWindows != service.Current().general.startWithWindows;
    const auto commit = [this, weak = std::weak_ptr<bool>(alive), mutate, generation, startupChange, acceptedFields, values] {
        auto result = service.Update(mutate, [this, weak, generation, startupChange](Result<void> persisted) {
            if (const auto live = weak.lock(); live && *live) {
                if (startupChange) SetStartupBusy(false);
                if (generation == applyGeneration) Message(persisted ? L"Settings saved." : L"Settings changed in memory; saving failed. Apply again to retry.");
            }
        });
        if (!result) { if (startupChange) SetStartupBusy(false); Message(L"Settings could not be applied."); }
        else {
            for (const auto index : acceptedFields) if (index < fieldBaseline.size()) fieldBaseline[index] = values[index];
            if (!result->empty()) Message(L"Some values were corrected to their supported range.");
        }
    };
    if (startupChange) {
        if (!callbacks.setStartup) { Message(L"Startup registration is unavailable in this session."); return; }
        SetStartupBusy(true); Message(L"Changing startup registration…");
        const auto weak = std::weak_ptr<bool>(alive);
        try {
            callbacks.setStartup(next.general.startWithWindows, [this, weak, commit](Result<void> result) {
                if (const auto live = weak.lock(); live && *live) {
                    if (result) commit();
                    else { SetStartupBusy(false); Message(L"Windows did not change startup registration. Check Task Manager's startup setting."); }
                }
            });
        } catch (...) { SetStartupBusy(false); Message(L"Startup registration could not be changed."); }
    } else commit();
}
Result<void> SettingsWindow::Impl::ResetAfterConsent() {
    if (!service.Loaded()) return MakeError(ErrorCode::InvalidState, "settings are still loading");
    if (startupBusy) return MakeError(ErrorCode::InvalidState, "startup change is pending");
    if (!callbacks.setStartup && service.Current().general.startWithWindows)
        return MakeError(ErrorCode::Unavailable, "startup registration is unavailable");
    ::KillTimer(window, kLiveApplyTimer); livePending = false; ++applyGeneration;
    const auto weak = std::weak_ptr<bool>(alive);
    const auto reset = [this, weak] {
        auto result = service.ResetToDefaults([this, weak](Result<void> persisted) {
            if (const auto live = weak.lock(); live && *live) {
                SetStartupBusy(false);
                Message(persisted ? L"Defaults saved; startup is disabled." : L"Defaults applied; saving failed. Apply again to retry.");
            }
        });
        if (!result) { SetStartupBusy(false); Message(L"Defaults could not be applied."); }
        else if (window) BuildPage();
    };
    SetStartupBusy(true);
    if (callbacks.setStartup) {
        try {
            callbacks.setStartup(false, [this, weak, reset](Result<void> result) {
                if (const auto live = weak.lock(); live && *live) {
                    if (result) reset();
                    else { SetStartupBusy(false); Message(L"Startup was not disabled; settings were kept."); }
                }
            });
        } catch (...) { SetStartupBusy(false); return MakeError(ErrorCode::Internal, "startup callback failed"); }
    } else reset();
    return Ok();
}
void SettingsWindow::Impl::LayoutPermanent() {
    for (const auto& control : permanentControls) {
        const auto& rect = control.dips;
        ::SetWindowPos(control.handle, nullptr, Px(rect.left)-scrollX, Px(rect.top)-scrollY, Px(rect.right-rect.left), Px(rect.bottom-rect.top), SWP_NOZORDER | SWP_NOACTIVATE);
    }
}
void SettingsWindow::Impl::ScrollTo(int x, int y) {
    RECT client{}; ::GetClientRect(window,&client);
    const int nextX=std::clamp(x,0,std::max(0,Px(696)-static_cast<int>(client.right)));
    const int nextY=std::clamp(y,0,std::max(0,Px(680)-static_cast<int>(client.bottom)));
    const int dx=scrollX-nextX, dy=scrollY-nextY;
    scrollX=nextX; scrollY=nextY;
    ::SetScrollPos(window,SB_HORZ,scrollX,TRUE); ::SetScrollPos(window,SB_VERT,scrollY,TRUE);
    if(dx || dy) {
        // Move every child, including controls wholly outside the viewport. ScrollWindowEx with
        // SW_SCROLLCHILDREN only moves intersecting children and would strand offscreen controls.
        std::vector<std::pair<HWND,RECT>> positions;
        for(HWND child=::GetWindow(window,GW_CHILD);child;child=::GetWindow(child,GW_HWNDNEXT)) {
            RECT rect{};::GetWindowRect(child,&rect);::MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
            positions.emplace_back(child,rect);
        }
        HDWP batch=::BeginDeferWindowPos(static_cast<int>(positions.size()));
        for(const auto& [child,rect]:positions) if(batch)
            batch=::DeferWindowPos(batch,child,nullptr,rect.left+dx,rect.top+dy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        if(!batch || !::EndDeferWindowPos(batch)) for(const auto& [child,rect]:positions)
            ::SetWindowPos(child,nullptr,rect.left+dx,rect.top+dy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        ::InvalidateRect(window,nullptr,TRUE);
    }
}
void SettingsWindow::Impl::UpdateScrollbars() {
    RECT client{}; ::GetClientRect(window,&client);
    SCROLLINFO horizontal{sizeof(horizontal),SIF_RANGE|SIF_PAGE,0,Px(696)-1,static_cast<UINT>(std::max<LONG>(0,client.right))};
    SCROLLINFO vertical{sizeof(vertical),SIF_RANGE|SIF_PAGE,0,Px(680)-1,static_cast<UINT>(std::max<LONG>(0,client.bottom))};
    ::SetScrollInfo(window,SB_HORZ,&horizontal,TRUE); ::SetScrollInfo(window,SB_VERT,&vertical,TRUE);
    ScrollTo(scrollX,scrollY);
}
void SettingsWindow::Impl::RevealFocus() {
    const HWND focus=::GetFocus(); if(!focus || !::IsChild(window,focus))return;
    RECT rect{},client{}; ::GetWindowRect(focus,&rect); ::MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2); ::GetClientRect(window,&client);
    int x=scrollX,y=scrollY;
    if(rect.left<0)x+=rect.left; else if(rect.right>client.right)x+=rect.right-client.right;
    if(rect.top<0)y+=rect.top; else if(rect.bottom>client.bottom)y+=rect.bottom-client.bottom;
    ScrollTo(x,y);
}
void SettingsWindow::Impl::RefreshFonts() {
    HFONT replacement = ::CreateFontW(-Px(14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!replacement) return;
    const HFONT previous = font; font = replacement;
    for (HWND child = ::GetWindow(window, GW_CHILD); child; child = ::GetWindow(child, GW_HWNDNEXT))
        ::SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    if (previous) ::DeleteObject(previous);
}

void SettingsWindow::Impl::SearchLocation() {
    if (!callbacks.geocoding) { Message(L"Location search is unavailable."); return; }
    const auto query = WindowText(controls[kFirstField + 70]);
    geocodeCancel.Cancel(); geocodeCancel = CancellationSource{};
    const auto cancel = geocodeCancel.Token(); const auto weak = std::weak_ptr<bool>(alive);
    auto* client = callbacks.geocoding;
    Message(L"Searching Open-Meteo locations…");
    worker.Post([this, weak, client, cancel, query] {
        auto result = client->Search(query, L"en", cancel);
        dispatcher.Post([this, weak, cancel, result = std::move(result)]() mutable {
            if (const auto live = weak.lock(); !live || !*live || cancel.IsCancelled() || page != SettingsPage::Weather) return;
            if (!result) { Message(L"Location search failed. Check your connection and try again."); return; }
            locations = std::move(*result);
            ::SendMessageW(controls[kGeoResults], LB_RESETCONTENT, 0, 0);
            for (const auto& location : locations) {
                const auto label = location.name + L", " + location.admin1 + L", " + location.country;
                ::SendMessageW(controls[kGeoResults], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            Message(L"Choose a location to save it.");
        });
    });
}

void SettingsWindow::Impl::Action(int id) {
    if (startupBusy) return;
    const auto pinSaved = [this, weak = std::weak_ptr<bool>(alive)](Result<void> persisted) {
        if (const auto live = weak.lock(); live && *live)
            Message(persisted ? L"Pinned applications saved." : L"Pins changed in memory; saving failed. Apply again to retry.");
    };
    if (id == kApply) Apply(true);
    else if (id == kReset) {
        if (::MessageBoxW(window, L"Reset settings and disable startup? A backup of the existing file will be kept.", L"Aether Shell", MB_OKCANCEL | MB_ICONQUESTION) == IDOK)
            if (auto result = ResetAfterConsent(); !result) Message(L"Defaults could not be restored because startup registration is unavailable.");
    } else if (id == kAddPin) {
        if (livePending) Apply(false);
        const auto selected = ::SendMessageW(controls[kFirstField+72], CB_GETCURSEL, 0, 0);
        if (selected != CB_ERR && static_cast<std::size_t>(selected) < applications.size()) {
            const auto app = applications[static_cast<std::size_t>(selected)];
            const auto result = service.Update([app](Settings& s){
                PinnedApp pin; pin.id=app.id; pin.displayName=app.displayName; pin.parsingName=app.parsingName;
                pin.appUserModelId=app.appUserModelId; pin.executablePath=app.executablePath;
                pin.shortcutPath=app.shortcutPath; pin.arguments=app.arguments;
                s.dock.pinned.push_back(std::move(pin));
                std::erase_if(s.dock.dismissedPins,[&](const auto& id){return text::EqualsIgnoreCase(id,app.id);});
            }, pinSaved); BuildPage();
            if (!result) Message(L"Pinned application could not be added.");
        }
    } else if (id == kRemovePin || id == kMovePinUp || id == kMovePinDown) {
        if (livePending) Apply(false);
        const auto selected = ::SendMessageW(controls[kPinList], LB_GETCURSEL, 0, 0);
        if (selected != LB_ERR) {
            const auto result = service.Update([id,selected](Settings& s){ const auto at=static_cast<std::size_t>(selected); if(at>=s.dock.pinned.size())return;
                if(id==kRemovePin){
                    const auto removed=s.dock.pinned[at].id;
                    s.dock.pinned.erase(s.dock.pinned.begin()+at);
                    std::erase_if(s.dock.dismissedPins,[&](const auto& item){return text::EqualsIgnoreCase(item,removed);});
                    if(s.dock.dismissedPins.size()>=128)s.dock.dismissedPins.erase(s.dock.dismissedPins.begin());
                    s.dock.dismissedPins.push_back(removed);
                }
                else if(id==kMovePinUp && at>0)std::swap(s.dock.pinned[at],s.dock.pinned[at-1]);
                else if(id==kMovePinDown && at+1<s.dock.pinned.size())std::swap(s.dock.pinned[at],s.dock.pinned[at+1]); }, pinSaved); BuildPage();
            if (!result) Message(L"Pinned applications could not be changed.");
        }
    } else if (id == kWindowsLocation && callbacks.requestWindowsLocation) {
        if(livePending)Apply(false);
        callbacks.requestWindowsLocation();
    } else if (id == kLocationSettings && callbacks.openLocationSettings) callbacks.openLocationSettings();
    else if (id == kGeoSearch) SearchLocation();
    else if (id == kCheckUpdate && callbacks.checkForUpdates) callbacks.checkForUpdates();
    else if (id == kInstallUpdate && callbacks.downloadAndInstall) callbacks.downloadAndInstall();
    else if (id == kPostpone && callbacks.postpone) callbacks.postpone();
    else if (id == kSkip && callbacks.skip) callbacks.skip();
    else if (id == kDisableUpdates && callbacks.disableNotifications) callbacks.disableNotifications();
    else if (id == kOpenLogs && callbacks.openLogFolder) callbacks.openLogFolder();
    else if (id == kExport && callbacks.exportDiagnostics) callbacks.exportDiagnostics();
    else if (id == kImportTaskbarPins && callbacks.importTaskbarPins) callbacks.importTaskbarPins();
    else if (id == kReleaseDetails) {
        // Fixed project URL; untrusted release-note text is never interpreted as a link.
        ::ShellExecuteW(window, L"open", L"https://github.com/ibrahimbouillet/AetherShell/releases", nullptr, nullptr, SW_SHOWNORMAL);
    }
}

LRESULT SettingsWindow::Impl::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == kNavigation && HIWORD(wParam) == LBN_SELCHANGE) {
            if (startupBusy) return 0;
            if (livePending) Apply(false);
            page = static_cast<SettingsPage>(::SendMessageW(navigation, LB_GETCURSEL, 0, 0)); geocodeCancel.Cancel(); BuildPage(); return 0;
        }
        if (id >= kFirstField && static_cast<std::size_t>(id-kFirstField) < bindings.size()) {
            if (!bindings[static_cast<std::size_t>(id-kFirstField)].consentOnly &&
                (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == CBN_SELCHANGE || HIWORD(wParam) == BN_CLICKED)) ScheduleLive();
            return 0;
        }
        if (id == kGeoResults && HIWORD(wParam) == LBN_SELCHANGE) { ScheduleLive(); return 0; }
        if (HIWORD(wParam) == BN_CLICKED) { Action(id); return 0; }
    } else if (message == WM_TIMER && wParam == kLiveApplyTimer) { Apply(false); return 0; }
    else if (message == WM_CLOSE) { if (livePending) Apply(false); ::ShowWindow(window, SW_HIDE); owner.Closed.Emit(); return 0; }
    else if (message == WM_DPICHANGED) {
        const auto fields = ReadFields();
        auto candidates = locations;
        const auto query = controls.contains(kFirstField+70) ? WindowText(controls.at(kFirstField+70)) : L"";
        const auto selection = controls.contains(kGeoResults) ? ::SendMessageW(controls.at(kGeoResults), LB_GETCURSEL, 0, 0) : LB_ERR;
        const UINT oldDpi=dpi; dpi = HIWORD(wParam);
        scrollX=::MulDiv(scrollX,static_cast<int>(dpi),static_cast<int>(oldDpi)); scrollY=::MulDiv(scrollY,static_cast<int>(dpi),static_cast<int>(oldDpi));
        const auto& rect = *reinterpret_cast<RECT*>(lParam);
        MONITORINFO monitor{sizeof(monitor)}; ::GetMonitorInfoW(::MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&monitor);
        const int width=std::min(static_cast<int>(rect.right-rect.left),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left));
        const int height=std::min(static_cast<int>(rect.bottom-rect.top),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
        const int left=std::clamp(static_cast<int>(rect.left),static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width);
        const int top=std::clamp(static_cast<int>(rect.top),static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height);
        ::SetWindowPos(window, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        RefreshFonts(); LayoutPermanent(); BuildPage(); RestoreFields(fields);
        if (controls.contains(kFirstField+70)) ::SetWindowTextW(controls.at(kFirstField+70), query.c_str());
        locations = std::move(candidates);
        if (controls.contains(kGeoResults)) {
            for (const auto& location : locations) {
                const auto label = location.name + L", " + location.admin1 + L", " + location.country;
                ::SendMessageW(controls.at(kGeoResults), LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            ::SendMessageW(controls.at(kGeoResults), LB_SETCURSEL, selection, 0);
        }
        return 0;
    } else if (message == WM_SIZE) { UpdateScrollbars(); return 0; }
    else if (message == WM_VSCROLL || message == WM_HSCROLL) {
        const int bar=message==WM_VSCROLL?SB_VERT:SB_HORZ;
        SCROLLINFO info{sizeof(info),SIF_ALL}; ::GetScrollInfo(window,bar,&info);
        int position=info.nPos;
        switch(LOWORD(wParam)) {
            case SB_LINEUP:position-=Px(32);break; case SB_LINEDOWN:position+=Px(32);break;
            case SB_PAGEUP:position-=static_cast<int>(info.nPage);break; case SB_PAGEDOWN:position+=static_cast<int>(info.nPage);break;
            case SB_THUMBTRACK:position=info.nTrackPos;break; case SB_TOP:position=0;break; case SB_BOTTOM:position=info.nMax;break;
            default:return 0;
        }
        ScrollTo(bar==SB_HORZ?position:scrollX,bar==SB_VERT?position:scrollY); return 0;
    } else if (message == WM_MOUSEWHEEL) {
        ScrollTo(scrollX,scrollY-::MulDiv(static_cast<short>(HIWORD(wParam)),Px(96),WHEEL_DELTA));return 0;
    } else if (message == WM_DESTROY) { ::KillTimer(window,kLiveApplyTimer); geocodeCancel.Cancel(); window = nullptr; return 0; }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

SettingsWindow::SettingsWindow(SettingsService& service, IUiDispatcher& dispatcher, SettingsCallbacks callbacks)
    : impl_(std::make_unique<Impl>(*this, service, dispatcher, std::move(callbacks))) {}
SettingsWindow::~SettingsWindow() {
    *impl_->alive = false; impl_->geocodeCancel.Cancel(); impl_->worker.Shutdown();
    if (impl_->window) ::DestroyWindow(impl_->window);
    if (impl_->font) ::DeleteObject(impl_->font);
    if (impl_->headingFont) ::DeleteObject(impl_->headingFont);
}
Result<void> SettingsWindow::Show(SettingsPage page) try {
    if (!impl_->service.Loaded()) return MakeError(ErrorCode::InvalidState, "settings are still loading");
    if (impl_->startupBusy) return MakeError(ErrorCode::InvalidState, "startup change is pending");
    if (impl_->livePending) impl_->Apply(false);
    impl_->page = page;
    if (!impl_->window) {
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = &Impl::Proc; wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1); wc.lpszClassName = kClass;
        if (auto registered = platform::RegisterWindowClassOnce(wc); !registered) return registered.error();
        impl_->dpi = ::GetDpiForSystem();
        const auto area=platform::PrimaryMonitor().workArea;
        const int width=std::min(impl_->Px(744),static_cast<int>(area.right-area.left));
        const int height=std::min(impl_->Px(740),static_cast<int>(area.bottom-area.top));
        const auto window = ::CreateWindowExW(WS_EX_CONTROLPARENT, kClass, L"Aether Shell Settings", WS_OVERLAPPEDWINDOW | WS_VSCROLL | WS_HSCROLL,
            area.left+(area.right-area.left-width)/2, area.top+(area.bottom-area.top-height)/2, width, height, nullptr, nullptr, ::GetModuleHandleW(nullptr), impl_.get());
        if (!window) return MakeError(ErrorCode::Platform, "settings window could not be created");
        impl_->dpi = ::GetDpiForWindow(window); impl_->RefreshFonts();
        platform::ApplyImmersiveDarkMode(window, platform::SystemPrefersDarkApps());
        impl_->Add(L"STATIC", L"Aether Shell", 0, 24, 22, 620, 34, 0, false);
        impl_->navigation = impl_->Add(L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | WS_BORDER, 24, 86, 165, 456, kNavigation, false);
        for (const auto* name : {L"Dock",L"Top Bar",L"Weather",L"Updates",L"General",L"About",L"Desktop",L"Appearance"}) ::SendMessageW(impl_->navigation, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        impl_->status = impl_->Add(L"STATIC", L"", 0, 24, 570, 650, 48, kStatus, false);
        impl_->Add(L"BUTTON", L"Reset defaults…", WS_TABSTOP, 24, 626, 170, 32, kReset, false);
        impl_->Add(L"BUTTON", L"Apply", WS_TABSTOP | BS_DEFPUSHBUTTON, 550, 626, 120, 32, kApply, false);
    }
    ::SendMessageW(impl_->navigation, LB_SETCURSEL, static_cast<WPARAM>(page), 0); impl_->BuildPage();
    impl_->UpdateScrollbars();
    ::ShowWindow(impl_->window, SW_SHOWNORMAL); ::SetForegroundWindow(impl_->window); return Ok();
} catch (...) { return MakeError(ErrorCode::Internal,"settings window could not be shown"); }
Result<void> SettingsWindow::PrepareToClose(std::function<void()> ready) {
    try {
        if(!ready)return MakeError(ErrorCode::InvalidArgument,"close completion is empty");
        if(impl_->livePending)impl_->Apply(false);
        if(impl_->window)::ShowWindow(impl_->window,SW_HIDE);
        if(impl_->startupBusy)impl_->closeWaiters.push_back(std::move(ready));else ready();
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal,"settings close could not be prepared"); }
}
void SettingsWindow::Close() { if (impl_->window) { if (impl_->livePending) impl_->Apply(false); ::ShowWindow(impl_->window, SW_HIDE); } }
bool SettingsWindow::PreTranslate(MSG& message) {
    if (!impl_->window || !::IsWindowVisible(impl_->window) || !::IsDialogMessageW(impl_->window,&message))return false;
    impl_->RevealFocus();return true;
}
HWND SettingsWindow::Handle() const noexcept { return impl_->window; }
HWND SettingsWindow::Control(int id) const noexcept { const auto found = impl_->controls.find(id); return found == impl_->controls.end() ? nullptr : found->second; }
void SettingsWindow::SetUpdateSnapshot(updates::UpdateSnapshot snapshot) {
    impl_->update = std::move(snapshot);
    if (impl_->window && impl_->page == SettingsPage::Updates) { const auto fields = impl_->ReadFields(); impl_->BuildPage(); impl_->RestoreFields(fields); }
}
void SettingsWindow::RefreshDesktopStatus() {
    if (!impl_->window || impl_->page != SettingsPage::Desktop || !impl_->controls.contains(kDesktopStatus)) return;
    try {
        const auto status = impl_->callbacks.taskbarStatus ? impl_->callbacks.taskbarStatus() :
            L"Desktop status is unavailable in this session.";
        ::SetWindowTextW(impl_->controls.at(kDesktopStatus), status.c_str());
    } catch (...) { ::SetWindowTextW(impl_->controls.at(kDesktopStatus), L"Desktop status is unavailable."); }
}
void SettingsWindow::ReportActionStatus(std::wstring_view message) {
    try { impl_->lastActionStatus = std::wstring(message); impl_->Message(impl_->lastActionStatus); } catch (...) {}
}
Result<void> SettingsWindow::ResetAfterConsent() {
    try { return impl_->ResetAfterConsent(); }
    catch (...) { return MakeError(ErrorCode::Internal, "settings reset failed"); }
}

}  // namespace aether::settings::ui
