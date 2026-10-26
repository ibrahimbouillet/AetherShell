// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "settings/ui/SettingsWindow.h"

#include "aether/BuildInfo.h"
#include "core/Strings.h"
#include "core/WorkerPool.h"
#include "diagnostics/Logger.h"
#include "graphics/Glyphs.h"
#include "graphics/SystemPalette.h"
#include "platform/windows/DpiUtilities.h"
#include "platform/windows/MonitorUtilities.h"
#include "platform/windows/WindowUtilities.h"
#include "settings/SettingsSerialization.h"
#include "settings/ui/SettingsFormat.h"
#include "ui/form/ConfirmDialog.h"
#include "ui/form/FormScrollView.h"
#include "ui/form/FormSurface.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwchar>
#include <map>
#include <optional>
#include <stdexcept>
#include <vector>

namespace aether::settings::ui {
namespace {
namespace form = ::aether::ui::form;
using graphics::Glyph;
using form::ButtonStyle;
using form::ControlRole;
using form::Severity;
using form::TextRole;

constexpr wchar_t kClass[] = L"AetherShell.Settings";
constexpr int kNavigation = 100, kApply = 101, kReset = 102, kStatus = 103, kScrollView = 120, kBrandLabel = 121, kCaptionTitle = 122;
// DWM attributes spelled out for older SDK headers (Windows 11 build 22000 and later).
constexpr DWORD kDwmCaptionButtonBounds = 5, kDwmCaptionColor = 35, kDwmTextColor = 36, kDwmColorDefault = 0xFFFFFFFFu;
constexpr UINT_PTR kLiveApplyTimer = 19;
constexpr int kFirstField = 200, kCheckUpdate = 301, kInstallUpdate = 302, kPostpone = 303,
              kSkip = 304, kDisableUpdates = 305, kReleaseDetails = 306, kGeoSearch = 307,
              kGeoResults = 308, kOpenLogs = 309, kExport = 310, kAddPin = 311, kPinList = 312,
              kRemovePin = 313, kMovePinUp = 314, kMovePinDown = 315, kLicense = 316, kUpdateSummary = 317,
              kDesktopStatus = 318, kImportTaskbarPins = 319, kWindowsLocation = 320, kLocationSettings = 321,
              kUpdateProgress = 322, kUpdateHeadline = 323;
constexpr int kFirstSlider = 400;  // auxiliary sliders of Number bindings: kFirstSlider + binding index
constexpr int kLocationField = kFirstField + 70, kReleaseNotes = kFirstField + 71, kAppChooser = kFirstField + 72;

constexpr wchar_t kApplyHint[] = L"Press Apply to confirm this change.";

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
    std::wstring helper;
    std::function<std::wstring(const Settings&)> read;
    std::function<void(Settings&, std::wstring)> write;
    std::vector<std::wstring> choices;
    bool consentOnly = false;
    NumberFormat number;
};

std::wstring WindowText(HWND window) {
    const int count = ::GetWindowTextLengthW(window);
    std::wstring text(static_cast<std::size_t>(count) + 1, L'\0');
    ::GetWindowTextW(window, text.data(), count + 1); text.resize(static_cast<std::size_t>(count)); return text;
}

form::Severity SeverityFor(StatusTone tone) noexcept {
    switch (tone) {
        case StatusTone::Info: return Severity::Info;
        case StatusTone::Success: return Severity::Success;
        case StatusTone::Warning: return Severity::Warning;
        case StatusTone::Error: return Severity::Error;
        case StatusTone::Busy: return Severity::Busy;
        case StatusTone::Neutral: break;
    }
    return Severity::None;
}

std::wstring FormatBytes(std::uint64_t bytes) {
    if (bytes < 1024) return std::to_wstring(bytes) + L" bytes";
    wchar_t text[48]{};
    const double kilo = static_cast<double>(bytes) / 1024.0;
    if (kilo < 1024.0) std::swprintf(text, 48, L"%.0f KB", kilo);
    else std::swprintf(text, 48, L"%.1f MB", kilo / 1024.0);
    return text;
}

std::wstring FormatLocalTime(std::chrono::system_clock::time_point point) {
    const auto ticks = std::chrono::duration_cast<std::chrono::duration<std::int64_t, std::ratio<1, 10'000'000>>>(
        point.time_since_epoch()).count() + 116444736000000000LL;  // 1601 epoch in 100 ns units
    if (ticks <= 0) return {};
    FILETIME utc{static_cast<DWORD>(ticks & 0xFFFFFFFF), static_cast<DWORD>(static_cast<std::uint64_t>(ticks) >> 32)};
    FILETIME local{};
    SYSTEMTIME time{};
    if (!::FileTimeToLocalFileTime(&utc, &local) || !::FileTimeToSystemTime(&local, &time)) return {};
    wchar_t date[64]{};
    wchar_t clock[64]{};
    ::GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_SHORTDATE, &time, nullptr, date, 64, nullptr);
    ::GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &time, nullptr, clock, 64);
    return std::wstring(date) + L" " + clock;
}

// One row of a settings card, or a paragraph between them. Rows only position the controls they
// reference; theme, DPI and size changes relayout them without recreating anything.
enum class RowKind { Switch, Control, Text, Buttons, Pins, Flex, List, Document, Progress };
struct Row {
    RowKind kind = RowKind::Control;
    HWND primary = nullptr;          // Switch, Text, List, Document, Progress; Flex: the stretching control
    HWND label = nullptr;            // Control: leading label
    HWND helper = nullptr;           // Control: leading helper text
    std::vector<HWND> trailing;      // Control: right-aligned controls; Buttons/Pins/Flex: buttons
    std::vector<float> widths;       // Control: fixed DIP widths of `trailing` (0 = preferred width)
    float height = 0.0f;             // List / Document content height (DIPs)
    TextRole role = TextRole::Body;  // Text rows
    bool hidden = false;
};
struct Section {
    HWND title = nullptr;
    std::vector<Row> rows;
    bool emphasis = false;
};
struct PageDraft {
    std::vector<std::wstring> values;
    std::vector<std::wstring> baseline;
    int scrollPosition = 0;
    UINT dpi = 96;
    int focusId = 0;
    DWORD selectionStart = 0;
    DWORD selectionEnd = 0;
    bool hasTextSelection = false;
    std::wstring locationQuery;
    std::vector<weather::LocationCandidate> locations;
    LRESULT locationSelection = LB_ERR;
    std::wstring pinId;
    std::wstring applicationId;
};

}  // namespace

struct SettingsWindow::Impl {
    Impl(SettingsWindow& window, SettingsService& settings, IUiDispatcher& ui, SettingsCallbacks actions)
        : owner(window), service(settings), dispatcher(ui), callbacks(std::move(actions)),
          worker({1, L"Aether settings UI", ComApartment::MultiThreaded}) {
        // Until the application's first SetTheme (which carries the Windows accent palette), the
        // window follows the Windows light/dark mode with the brand accent. No WinRT call here:
        // standalone callers may create and tear down COM apartments around each window.
        tokens = graphics::MakeDesignTokens(graphics::DetectAppThemeMode(graphics::ThemePreference::System));
        motion.reduced = tokens.mode == graphics::ThemeMode::HighContrast || !platform::SystemAnimationsEnabled();
    }
    SettingsWindow& owner;
    SettingsService& service;
    IUiDispatcher& dispatcher;
    SettingsCallbacks callbacks;
    WorkerPool worker;
    CancellationSource geocodeCancel;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    HWND window = nullptr, navigation = nullptr, status = nullptr, brandLabel = nullptr, applyButton = nullptr, resetButton = nullptr;
    HWND captionTitle = nullptr;
    form::FormSurface surface;
    form::FormScrollView scroll;
    bool surfaceReady = false;
    // Over Acrylic the standard caption is removed (WM_NCCALCSIZE) so the title strip shows the same
    // tint and grain as the rest of the window. DWM still draws the native caption buttons there and
    // hit-tests them (DwmDefWindowProc, Snap layouts); the window keeps WS_OVERLAPPEDWINDOW, its
    // system menu, keyboard commands, borders and resizing. Other materials keep the standard frame.
    bool customFrame = false;
    D2D1_RECT_F captionMark{};  // form client DIPs
    graphics::DesignTokens tokens;
    graphics::MotionPolicy motion;
    SettingsPage page = SettingsPage::Dock;
    std::vector<Binding> bindings;
    std::vector<std::wstring> fieldBaseline;
    std::map<SettingsPage, PageDraft> pageDrafts;
    bool rebuildOnShow = true;  // closing discards drafts; ordinary navigation retains them
    int lastFocusId = 0;
    std::map<int, HWND> controls;
    std::vector<HWND> pageControls;
    std::vector<weather::LocationCandidate> locations;
    std::vector<apps::ApplicationInfo> applications;
    updates::UpdateSnapshot update;
    std::wstring lastActionStatus;
    StatusTone lastActionTone = StatusTone::Neutral;
    bool startupBusy = false;
    bool building = false;
    bool livePending = false;
    bool syncing = false;      // a slider and its value field are being synchronized
    std::uint64_t applyGeneration = 0;
    std::vector<std::function<void()>> closeWaiters;
    // Page model
    HWND pageTitle = nullptr, pageDescription = nullptr;
    std::vector<Section> sections;
    D2D1_RECT_F brandMark{};   // form client DIPs
    D2D1_RECT_F aboutMark{};   // canvas DIPs (About page)

    // --- geometry helpers ---
    [[nodiscard]] float Dip(float px) const { return surfaceReady ? surface.ToDip(px) : px * 96.0f / static_cast<float>(DpiNow()); }
    [[nodiscard]] int Px(float dip) const {
        return static_cast<int>(std::lround(surfaceReady ? surface.ToPx(dip) : dip * static_cast<float>(DpiNow()) / 96.0f));
    }
    [[nodiscard]] UINT DpiNow() const { return surfaceReady ? surface.Dpi() : (window ? platform::DpiForWindow(window) : ::GetDpiForSystem()); }
    // Origin plus size: a control's pixel size depends only on its DIP size, not on where it sits.
    [[nodiscard]] RECT PxRect(float left, float top, float right, float bottom) const {
        const int x = Px(left);
        const int y = Px(top);
        return RECT{x, y, x + Px(right - left), y + Px(bottom - top)};
    }
    void Place(HWND control, float left, float top, float right, float bottom) {
        if (!control) return;
        const RECT rect = PxRect(left, top, std::max(left + 1.0f, right), std::max(top + 1.0f, bottom));
        if (surfaceReady && surface.Spec(control)) surface.SetFrame(control, rect);
        else ::SetWindowPos(control, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    [[nodiscard]] float TextHeight(HWND control, TextRole role, float width) const {
        if (!control) return 0.0f;
        const auto text = WindowText(control);
        if (text.empty()) return 0.0f;
        return std::ceil(surface.MeasureText(text, role, std::max(1.0f, width), true).height);
    }
    [[nodiscard]] float Preferred(HWND control) const { return surfaceReady ? surface.PreferredWidth(control) : 120.0f; }

    // --- window frame ---
    // The resize border hidden above a maximized window (its frame extends past the monitor).
    [[nodiscard]] int FrameInsetPx() const {
        const UINT dpi = DpiNow();
        return platform::SystemMetricForDpi(SM_CYSIZEFRAME, dpi) + platform::SystemMetricForDpi(SM_CXPADDEDBORDER, dpi);
    }
    // Height of the title strip in client pixels: down to the bottom of DWM's caption buttons.
    [[nodiscard]] int CaptionHeightPx() const {
        if (!customFrame || !window) return 0;
        const int inset = ::IsZoomed(window) ? FrameInsetPx() : 0;
        RECT buttons{};
        if (SUCCEEDED(::DwmGetWindowAttribute(window, kDwmCaptionButtonBounds, &buttons, sizeof(buttons))) && buttons.bottom > inset)
            return buttons.bottom - inset;
        return platform::SystemMetricForDpi(SM_CYCAPTION, DpiNow()) + FrameInsetPx();
    }
    // Chooses the frame for the current material and recomputes the non-client area when it changes.
    void UpdateFrame() {
        if (!window) return;
        const bool wanted = surfaceReady && surface.Material() == form::FormMaterial::Acrylic;
        const bool solid = surfaceReady && surface.Material() == form::FormMaterial::Solid;
        // Opaque material on Windows 11: the standard caption takes the window's own colors.
        const COLORREF caption = solid ? tokens.form.windowSolid.ToColorRef() : kDwmColorDefault;
        const COLORREF title = solid ? tokens.textPrimary.ToColorRef() : kDwmColorDefault;
        (void)::DwmSetWindowAttribute(window, kDwmCaptionColor, &caption, sizeof(caption));
        (void)::DwmSetWindowAttribute(window, kDwmTextColor, &title, sizeof(title));
        if (captionTitle) ::ShowWindow(captionTitle, wanted ? SW_SHOWNA : SW_HIDE);
        if (wanted == customFrame) return;
        customFrame = wanted;
        ::SetWindowPos(window, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    LRESULT HitTest(WPARAM wParam, LPARAM lParam) {
        const LRESULT hit = ::DefWindowProcW(window, WM_NCHITTEST, wParam, lParam);
        if (hit != HTCLIENT) return hit;
        POINT point{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
        ::ScreenToClient(window, &point);
        RECT client{};
        ::GetClientRect(window, &client);
        if (!::IsZoomed(window) && point.y < FrameInsetPx()) {
            const int corner = FrameInsetPx() * 2;
            return point.x < corner ? HTTOPLEFT : point.x >= client.right - corner ? HTTOPRIGHT : HTTOP;
        }
        if (point.y < CaptionHeightPx()) {
            const RECT mark{Px(captionMark.left), Px(captionMark.top), Px(captionMark.right), Px(captionMark.bottom)};
            return ::PtInRect(&mark, point) ? HTSYSMENU : HTCAPTION;
        }
        return HTCLIENT;
    }
    [[nodiscard]] float ControlHeight() const {
        return std::max(tokens.form.controlHeight,
                        std::ceil(surface.MeasureText(L"Ag", TextRole::Body, 10000.0f, false).height) + 12.0f);
    }
    [[nodiscard]] float ButtonHeight(HWND control, float width) const {
        const auto& f = tokens.form;
        const auto* spec = surface.Spec(control);
        const float glyph = spec && spec->glyph ? f.glyphSize + 8.0f : 0.0f;
        return std::max(ControlHeight(), TextHeight(control, TextRole::Body,
                        std::max(1.0f, width - 2.0f * f.buttonPaddingX - glyph)) + 12.0f);
    }

    // --- control creation ---
    HWND Make(HWND parent, const wchar_t* cls, const std::wstring& text, DWORD style, int id = 0, DWORD exStyle = 0) {
        HWND control = ::CreateWindowExW(exStyle, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1, parent,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (!control) return nullptr;
        if (id) controls[id] = control;
        if (parent != window) pageControls.push_back(control);
        return control;
    }
    void Style(HWND control, form::ControlSpec spec) {
        if (!control) return;
        if (surfaceReady) {
            if (auto registered = surface.Register(control, spec); !registered)
                AETHER_LOG_WARN("settings", "control styling unavailable ({})", registered.error().Describe());
        } else if (spec.role != ControlRole::Container) {
            ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(::GetStockObject(DEFAULT_GUI_FONT)), FALSE);
        }
    }
    HWND Label(HWND parent, const std::wstring& text, TextRole role, int id = 0, Severity severity = Severity::None, bool wrap = true) {
        HWND label = Make(parent, L"STATIC", text, SS_OWNERDRAW | SS_NOPREFIX, id);
        form::ControlSpec spec;
        spec.role = ControlRole::Label;
        spec.text = role;
        spec.severity = severity;
        spec.wrap = wrap;
        Style(label, spec);
        return label;
    }
    HWND Button(HWND parent, const std::wstring& text, int id, ButtonStyle style = ButtonStyle::Secondary,
                std::optional<Glyph> glyph = std::nullopt, DWORD extra = 0) {
        HWND button = Make(parent, L"BUTTON", text, WS_TABSTOP | BS_PUSHBUTTON | BS_MULTILINE | extra, id);
        form::ControlSpec spec;
        spec.role = ControlRole::Button;
        spec.button = style;
        spec.glyph = glyph;
        Style(button, spec);
        return button;
    }
    [[nodiscard]] HWND Canvas() const { return scroll.Canvas(); }
    Section& BeginSection(const std::wstring& title, bool emphasis = false) {
        Section section;
        if (!title.empty()) section.title = Label(Canvas(), title, TextRole::Section, 0, Severity::None, false);
        section.emphasis = emphasis;
        sections.push_back(std::move(section));
        return sections.back();
    }
    Row& AddRow(RowKind kind) {
        if (sections.empty()) BeginSection(L"");
        sections.back().rows.push_back(Row{});
        Row& row = sections.back().rows.back();
        row.kind = kind;
        return row;
    }
    void TextRow(const std::wstring& text, TextRole role = TextRole::Helper, int id = 0, Severity severity = Severity::None) {
        HWND label = Label(Canvas(), text, role, id, severity);
        Row& row = AddRow(RowKind::Text);
        row.primary = label;
        row.role = role;
    }
    void ButtonsRow(std::vector<HWND> buttons) {
        Row& row = AddRow(RowKind::Buttons);
        row.trailing = std::move(buttons);
    }

    // --- bindings ---
    template <class Get, class Set>
    void Check(std::wstring label, std::wstring helper, Get get, Set set, bool consentOnly = false) {
        bindings.push_back(Binding{FieldKind::Check, std::move(label), std::move(helper),
            [get](const Settings& s) { return get(s) ? std::wstring(L"1") : std::wstring(L"0"); },
            [set](Settings& s, std::wstring value) { set(s, value == L"1"); }, {}, consentOnly, {}});
        CreateBinding(bindings.size() - 1);
    }
    template <class Get, class Set>
    void Number(std::wstring label, std::wstring helper, NumberFormat format, Get get, Set set) {
        bindings.push_back(Binding{FieldKind::Number, std::move(label), std::move(helper),
            [get, format](const Settings& s) { return FormatNumber(static_cast<double>(get(s)), format); },
            [set, format](Settings& s, std::wstring value) {
                const auto parsed = ParseNumber(value, format);
                if (!parsed) throw std::runtime_error("invalid numeric setting");
                set(s, std::clamp(*parsed, -1'000'000.0, 1'000'000.0));
            }, {}, false, format});
        CreateBinding(bindings.size() - 1);
    }
    template <class Get, class Set>
    void Choice(std::wstring label, std::wstring helper, std::vector<std::wstring> choices, Get get, Set set) {
        bindings.push_back(Binding{FieldKind::Choice, std::move(label), std::move(helper),
            [get](const Settings& s) { return std::to_wstring(static_cast<int>(get(s))); },
            [set](Settings& s, std::wstring value) { set(s, static_cast<int>(std::wcstol(value.c_str(), nullptr, 10))); },
            std::move(choices), false, {}});
        CreateBinding(bindings.size() - 1);
    }
    template <class Get, class Set>
    void Key(std::wstring label, std::wstring helper, Get get, Set set) {
        bindings.push_back(Binding{FieldKind::Key, std::move(label), std::move(helper),
            [get](const Settings& s) { return std::to_wstring(get(s)); },
            [set](Settings& s, std::wstring value) { set(s, static_cast<std::uint32_t>(std::wcstoul(value.c_str(), nullptr, 10))); },
            {}, false, {}});
        CreateBinding(bindings.size() - 1);
    }

    void CreateBinding(std::size_t index) {
        const auto& binding = bindings[index];
        const int id = kFirstField + static_cast<int>(index);
        const auto value = binding.read(service.Current());
        HWND canvas = Canvas();
        if (binding.kind == FieldKind::Check) {
            HWND box = Make(canvas, L"BUTTON", binding.label, WS_TABSTOP | BS_AUTOCHECKBOX, id);
            ::SendMessageW(box, BM_SETCHECK, value == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
            form::ControlSpec spec;
            spec.role = ControlRole::Switch;
            spec.description = binding.helper;
            Style(box, spec);
            AddRow(RowKind::Switch).primary = box;
            return;
        }
        Row row;
        row.kind = RowKind::Control;
        row.label = Label(canvas, binding.label, TextRole::Body);
        if (!binding.helper.empty()) row.helper = Label(canvas, binding.helper, TextRole::Helper);
        if (binding.kind == FieldKind::Choice) {
            HWND combo = Make(canvas, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, id);
            for (const auto& option : binding.choices) ::SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(option.c_str()));
            ::SendMessageW(combo, CB_SETCURSEL, std::wcstol(value.c_str(), nullptr, 10), 0);
            form::ControlSpec spec;
            spec.role = ControlRole::ComboBox;
            spec.accessibleName = binding.label;
            spec.description = binding.helper;
            Style(combo, spec);
            row.trailing = {combo};
            row.widths = {0.0f};
        } else if (binding.kind == FieldKind::Key) {
            HWND hotkey = Make(canvas, HOTKEY_CLASSW, L"", WS_TABSTOP, id);
            ::SendMessageW(hotkey, HKM_SETRULES, 0, 0);
            ::SendMessageW(hotkey, HKM_SETHOTKEY, static_cast<WPARAM>(std::wcstoul(value.c_str(), nullptr, 10)), 0);
            form::ControlSpec spec;
            spec.role = ControlRole::Field;
            spec.accessibleName = binding.label;
            spec.description = binding.helper;
            Style(hotkey, spec);
            row.trailing = {hotkey};
            row.widths = {tokens.form.comboMinWidth};
        } else {
            const auto& format = binding.number;
            HWND slider = Make(canvas, TRACKBAR_CLASSW, binding.label, WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, kFirstSlider + static_cast<int>(index));
            ::SendMessageW(slider, TBM_SETRANGE, FALSE, MAKELPARAM(0, SliderSteps(format)));
            ::SendMessageW(slider, TBM_SETPAGESIZE, 0, std::max(1, SliderSteps(format) / 10));
            if (const auto stored = ParseNumber(value, format)) ::SendMessageW(slider, TBM_SETPOS, TRUE, SliderPosition(*stored, format));
            form::ControlSpec sliderSpec;
            sliderSpec.role = ControlRole::Slider;
            sliderSpec.accessibleName = binding.label;
            sliderSpec.description = binding.helper;
            Style(slider, sliderSpec);
            HWND edit = Make(canvas, L"EDIT", value, WS_TABSTOP | ES_AUTOHSCROLL | ES_RIGHT, id);
            form::ControlSpec fieldSpec;
            fieldSpec.role = ControlRole::Field;
            fieldSpec.accessibleName = binding.label;
            fieldSpec.description = binding.helper;
            Style(edit, fieldSpec);
            HWND unit = Label(canvas, format.unit, TextRole::Caption, 0, Severity::None, false);
            row.trailing = {slider, edit, unit};
            row.widths = {tokens.form.sliderWidth, tokens.form.valueFieldWidth, 28.0f};
        }
        if (sections.empty()) BeginSection(L"");
        sections.back().rows.push_back(std::move(row));
    }

    void Message(std::wstring text, StatusTone tone = StatusTone::Neutral) {
        if (!status) return;
        ::SetWindowTextW(status, text.c_str());
        if (surfaceReady) {
            form::ControlSpec spec;
            spec.role = ControlRole::Label;
            spec.text = TextRole::Status;
            spec.severity = SeverityFor(tone);
            spec.wrap = true;
            surface.SetSpec(status, spec);
        }
        ::InvalidateRect(status, nullptr, FALSE);
        if (!building) Layout();
    }

    void BuildPage();
    void BuildUpdatesStatus();
    void RefreshUpdates();
    void Layout();
    void LayoutCanvas();
    float LayoutRow(Row& row, float left, float top, float width, std::vector<form::FormDivider>& dividers, bool separated);
    void Apply(bool explicitConsent);
    void ScheduleLive();
    void SetStartupBusy(bool busy);
    std::vector<std::wstring> ReadFields() const;
    void RestoreFields(const std::vector<std::wstring>& values);
    void RefreshCleanFields();
    void RememberFocus();
    void SavePageDraft();
    void RestorePageDraft();
    void DiscardPageDrafts();
    Result<void> ResetAfterConsent();
    void SearchLocation();
    void Action(int id);
    void SyncSliderFromField(std::size_t index);
    void SyncFieldFromSlider(std::size_t index);
    void RefreshConsentHint(std::size_t index);
    void Navigate(int delta);
    void ApplyTheme();
    bool ConfirmReset();
    LRESULT Handle(UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK Proc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) { self = static_cast<Impl*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); self->window = hwnd; ::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self)); }
        try { if (self) return self->Handle(message, wParam, lParam); }
        catch (...) { if (self) self->Message(L"This action could not be completed.", StatusTone::Error); return 0; }
        return ::DefWindowProcW(hwnd, message, wParam, lParam);
    }
};

void SettingsWindow::Impl::BuildPage() {
    building = true;
    for (HWND control : pageControls) if (::IsWindow(control)) ::DestroyWindow(control);
    pageControls.clear();
    std::erase_if(controls, [](const auto& pair) { return pair.first >= kFirstField; });
    bindings.clear(); locations.clear(); sections.clear();
    pageTitle = pageDescription = nullptr;
    aboutMark = {};
    const auto& current = service.Current();
    HWND canvas = Canvas();
    const auto header = [&](const wchar_t* title, const wchar_t* description) {
        pageTitle = Label(canvas, title, TextRole::Title, 0, Severity::None, false);
        pageDescription = Label(canvas, description, TextRole::Helper);
    };
    const auto pixels = [](double minimum, double maximum) { NumberFormat f; f.minimum = minimum; f.maximum = maximum; f.unit = L"px"; return f; };
    const auto percent = [] { NumberFormat f; f.minimum = 30; f.maximum = 100; f.scale = 100; f.unit = L"%"; return f; };
    const auto factor = [](double minimum, double maximum) {
        NumberFormat f; f.minimum = minimum; f.maximum = maximum; f.step = 0.05; f.decimals = 2; f.unit = L"Ã"; return f;
    };
    if (page == SettingsPage::Dock) {
        header(L"Dock", L"Size, spacing, motion and the applications pinned to the Dock.");
        BeginSection(L"Size and spacing");
        const auto icon = pixels(32, 96);
        Number(L"Icon size", RangeText(icon) + L", scaled with the Windows display scale.", icon,
               [](const Settings& s){return s.dock.iconSize;}, [](Settings& s,double v){s.dock.iconSize=static_cast<int>(v);});
        const auto bottom = pixels(0, 200);
        Number(L"Bottom margin", L"Space between the Dock and the bottom of the screen.", bottom,
               [](const Settings& s){return s.dock.bottomMargin;}, [](Settings& s,double v){s.dock.bottomMargin=static_cast<int>(v);});
        const auto side = pixels(0, 400);
        Number(L"Minimum side margin", L"Space kept free at the left and right screen edges.", side,
               [](const Settings& s){return s.dock.sideMargin;}, [](Settings& s,double v){s.dock.sideMargin=static_cast<int>(v);});
        const auto spacing = pixels(0, 32);
        Number(L"Icon spacing", L"Space between neighbouring icons.", spacing,
               [](const Settings& s){return s.dock.iconSpacing;}, [](Settings& s,double v){s.dock.iconSpacing=static_cast<int>(v);});
        BeginSection(L"Glass and motion");
        Number(L"Glass opacity", L"Lower values show more of the blurred desktop behind the Dock.", percent(),
               [](const Settings& s){return s.dock.opacity;}, [](Settings& s,double v){s.dock.opacity=v;});
        Number(L"Hover magnification", L"How much an icon grows under the pointer; 1 turns it off.", factor(1.0, 2.0),
               [](const Settings& s){return s.dock.magnification;}, [](Settings& s,double v){s.dock.magnification=v;});
        BeginSection(L"Behavior");
        Check(L"Show running applications", L"Open applications that are not pinned appear after the pinned ones.",
              [](const Settings& s){return s.dock.showRunningApps;}, [](Settings& s,bool v){s.dock.showRunningApps=v;});
        Check(L"Reserve screen space for the Dock", L"Maximized windows stay above the Dock instead of under it.",
              [](const Settings& s){return s.dock.reserveSpace;}, [](Settings& s,bool v){s.dock.reserveSpace=v;});
        BeginSection(L"Pinned applications");
        HWND pins = Make(canvas, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, kPinList);
        for (const auto& pin : current.dock.pinned) ::SendMessageW(pins, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(pin.displayName.c_str()));
        { form::ControlSpec spec; spec.role = ControlRole::List; Style(pins, spec); }
        Row& pinRow = AddRow(RowKind::Pins);
        pinRow.primary = pins;
        pinRow.height = 168.0f;
        pinRow.trailing = {Button(canvas, L"Move &up", kMovePinUp, ButtonStyle::Secondary, Glyph::ArrowUp),
                           Button(canvas, L"Move &down", kMovePinDown, ButtonStyle::Secondary, Glyph::ArrowDown),
                           Button(canvas, L"&Remove", kRemovePin, ButtonStyle::Secondary, Glyph::Minus)};
        applications = callbacks.catalog ? callbacks.catalog() : std::vector<apps::ApplicationInfo>{};
        HWND chooser = Make(canvas, L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, kAppChooser);
        for (const auto& app : applications) ::SendMessageW(chooser, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(app.displayName.c_str()));
        { form::ControlSpec spec; spec.role = ControlRole::ComboBox; Style(chooser, spec); }
        Row& add = AddRow(RowKind::Flex);
        add.primary = chooser;
        add.trailing = {Button(canvas, L"&Add application", kAddPin, ButtonStyle::Secondary, Glyph::Plus)};
        TextRow(L"Pins keep their identity and launch settings. Removing a pin also stops the automatic "
                L"Windows taskbar sync from adding it again.");
    } else if (page == SettingsPage::TopBar) {
        header(L"Top Bar", L"Position, clock and the status items shown in the Top Status Bar.");
        BeginSection(L"Position and glass");
        const auto top = pixels(0, 100);
        Number(L"Top margin", L"Space between the Top Bar and the top of the screen.", top,
               [](const Settings& s){return s.topBar.topMargin;}, [](Settings& s,double v){s.topBar.topMargin=static_cast<int>(v);});
        Number(L"Glass opacity", L"Lower values show more of the blurred desktop behind the bar.", percent(),
               [](const Settings& s){return s.topBar.opacity;}, [](Settings& s,double v){s.topBar.opacity=v;});
        BeginSection(L"Clock");
        Choice(L"Clock format", L"", {L"Follow Windows",L"12-hour",L"24-hour"}, [](const Settings& s){return s.topBar.clockFormat;}, [](Settings& s,int v){s.topBar.clockFormat=static_cast<ClockFormat>(v);});
        Choice(L"Date style", L"", {L"Weekday, day and month",L"Short",L"Long"}, [](const Settings& s){return s.topBar.dateStyle;}, [](Settings& s,int v){s.topBar.dateStyle=static_cast<DateStyle>(v);});
        Check(L"Show seconds", L"", [](const Settings& s){return s.topBar.showSeconds;}, [](Settings& s,bool v){s.topBar.showSeconds=v;});
        Check(L"Show date", L"", [](const Settings& s){return s.topBar.showDate;}, [](Settings& s,bool v){s.topBar.showDate=v;});
        BeginSection(L"Status items");
        Check(L"Show battery", L"", [](const Settings& s){return s.topBar.showBattery;}, [](Settings& s,bool v){s.topBar.showBattery=v;});
        Check(L"Hide battery when absent", L"Desktop PCs without a battery show nothing instead of an unknown battery.",
              [](const Settings& s){return s.topBar.hideBatteryWhenAbsent;}, [](Settings& s,bool v){s.topBar.hideBatteryWhenAbsent=v;});
        Check(L"Show input language", L"", [](const Settings& s){return s.topBar.showKeyboard;}, [](Settings& s,bool v){s.topBar.showKeyboard=v;});
        Check(L"Show network", L"", [](const Settings& s){return s.topBar.showNetwork;}, [](Settings& s,bool v){s.topBar.showNetwork=v;});
        Check(L"Show audio output", L"", [](const Settings& s){return s.topBar.showVolume;}, [](Settings& s,bool v){s.topBar.showVolume=v;});
        Check(L"Show microphone", L"", [](const Settings& s){return s.topBar.showMicrophone;}, [](Settings& s,bool v){s.topBar.showMicrophone=v;});
        Check(L"Show weather when enabled", L"Weather itself is turned on in Weather settings.",
              [](const Settings& s){return s.topBar.showWeather;}, [](Settings& s,bool v){s.topBar.showWeather=v;});
        BeginSection(L"Behavior");
        Check(L"Reserve screen space above windows", L"Maximized windows stay below the Top Bar.",
              [](const Settings& s){return s.topBar.reserveSpace;}, [](Settings& s,bool v){s.topBar.reserveSpace=v;});
        Check(L"Use Windows Quick Settings (Windows 11)", L"Off: Aether's quick controls open below the Top Bar. On: Windows' own panel opens at the bottom of the screen.",
              [](const Settings& s){return s.topBar.windowsQuickSettings;}, [](Settings& s,bool v){s.topBar.windowsQuickSettings=v;});
    } else if (page == SettingsPage::Weather) {
        header(L"Weather", L"An optional forecast in the Top Bar, provided by Open-Meteo.");
        BeginSection(L"Forecast");
        Check(L"Enable weather", L"Off by default: the forecast needs network requests.",
              [](const Settings& s){return s.weather.enabled;}, [](Settings& s,bool v){s.weather.enabled=v;});
        Choice(L"Temperature", L"", {L"Celsius",L"Fahrenheit"}, [](const Settings& s){return s.weather.unit;}, [](Settings& s,int v){s.weather.unit=static_cast<TemperatureUnit>(v);});
        NumberFormat refresh; refresh.minimum = 15; refresh.maximum = 180; refresh.step = 5; refresh.unit = L"min";
        Number(L"Refresh interval", RangeText(refresh) + L".", refresh,
               [](const Settings& s){return s.weather.refreshMinutes;}, [](Settings& s,double v){s.weather.refreshMinutes=static_cast<int>(v);});
        BeginSection(L"Location");
        Check(L"Locate automatically with Windows", L"Uses the Windows location service after you allow it.",
              [](const Settings& s){return s.weather.useWindowsLocation;}, [](Settings& s,bool v){s.weather.useWindowsLocation=v;});
        TextRow(L"Windows asks for location permission. Approximate coordinates are sent to Open-Meteo.com; a coarse "
                L"location key is cached locally. If location is blocked, weather is unavailable.");
        ButtonsRow({Button(canvas, L"Use &Windows location", kWindowsLocation, ButtonStyle::Secondary, Glyph::Location),
                    Button(canvas, L"Location &settings", kLocationSettings, ButtonStyle::Subtle, Glyph::OpenExternal)});
        TextRow(callbacks.weatherLocationStatus ? callbacks.weatherLocationStatus() : L"Choose automatic location or search for a place below.",
                TextRole::Status, 0, Severity::Info);
        BeginSection(L"Search for a place");
        HWND field = Make(canvas, L"EDIT", current.weather.locationName, WS_TABSTOP | ES_AUTOHSCROLL, kLocationField);
        { form::ControlSpec spec; spec.role = ControlRole::Field; Style(field, spec); }
        Row& search = AddRow(RowKind::Flex);
        search.primary = field;
        search.trailing = {Button(canvas, L"Searc&h", kGeoSearch, ButtonStyle::Secondary, Glyph::Search)};
        HWND results = Make(canvas, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT | WS_VSCROLL, kGeoResults);
        { form::ControlSpec spec; spec.role = ControlRole::List; Style(results, spec); }
        Row& list = AddRow(RowKind::List);
        list.primary = results;
        list.height = 120.0f;
        TextRow(L"Weather data by Open-Meteo.com (CC BY 4.0). Manual search sends only the text you enter.");
    } else if (page == SettingsPage::Updates) {
        header(L"Updates", L"Signed releases from the official GitHub repository, verified before installation.");
        BuildUpdatesStatus();
        BeginSection(L"Preferences");
        Check(L"Check for updates automatically", L"", [](const Settings& s){return s.updates.automaticChecks;}, [](Settings& s,bool v){s.updates.automaticChecks=v;});
        NumberFormat interval; interval.minimum = 6; interval.maximum = 168; interval.unit = L"h";
        Number(L"Check interval", RangeText(interval) + L".", interval,
               [](const Settings& s){return s.updates.checkIntervalHours;}, [](Settings& s,double v){s.updates.checkIntervalHours=static_cast<int>(v);});
        Choice(L"Release channel", L"Prereleases arrive earlier and change more often.", {L"Stable",L"Prerelease"},
               [](const Settings& s){return s.updates.channel;}, [](Settings& s,int v){s.updates.channel=static_cast<UpdateChannel>(v);});
        Check(L"Show update notifications", L"", [](const Settings& s){return s.updates.notifications;}, [](Settings& s,bool v){s.updates.notifications=v;});
    } else if (page == SettingsPage::General) {
        header(L"General", L"Startup, motion, keyboard shortcuts and diagnostics.");
        BeginSection(L"Startup");
        Check(L"Start Aether Shell when I sign in", L"Changing this needs Apply. Task Manager can also turn it off.",
              [](const Settings& s){return s.general.startWithWindows;}, [](Settings& s,bool v){s.general.startWithWindows=v;}, true);
        BeginSection(L"Motion");
        Choice(L"Motion", L"Reduced motion removes decorative movement everywhere.", {L"Follow Windows",L"Reduced",L"Full"},
               [](const Settings& s){return s.general.motion;}, [](Settings& s,int v){s.general.motion=static_cast<MotionPreference>(v);});
        BeginSection(L"Keyboard shortcuts");
        Check(L"Enable global keyboard shortcuts", L"", [](const Settings& s){return s.general.hotkeysEnabled;}, [](Settings& s,bool v){s.general.hotkeysEnabled=v;});
        Key(L"Search shortcut key", L"", [](const Settings& s){return s.general.searchHotkey.virtualKey;}, [](Settings& s,std::uint32_t v){s.general.searchHotkey.virtualKey=v;});
        Key(L"Dock shortcut key", L"", [](const Settings& s){return s.general.focusDockHotkey.virtualKey;}, [](Settings& s,std::uint32_t v){s.general.focusDockHotkey.virtualKey=v;});
        const auto modifierIndex = [](std::uint32_t v) { return v >= 1 && v <= 15 ? static_cast<int>(v - 1) : 2; };
        const auto modifierBits = [](int index) { return index >= 0 && index < 15 ? static_cast<std::uint32_t>(index + 1) : 3u; };
        const std::vector<std::wstring> modifiers{L"Alt",L"Ctrl",L"Ctrl + Alt",L"Shift",L"Alt + Shift",L"Ctrl + Shift",L"Ctrl + Alt + Shift",
            L"Windows",L"Alt + Windows",L"Ctrl + Windows",L"Ctrl + Alt + Windows",L"Shift + Windows",L"Alt + Shift + Windows",
            L"Ctrl + Shift + Windows",L"Ctrl + Alt + Shift + Windows"};
        Choice(L"Search shortcut modifiers", L"", modifiers, [modifierIndex](const Settings& s){return modifierIndex(s.general.searchHotkey.modifiers);}, [modifierBits](Settings& s,int v){s.general.searchHotkey.modifiers=modifierBits(v);});
        Choice(L"Dock shortcut modifiers", L"", modifiers, [modifierIndex](const Settings& s){return modifierIndex(s.general.focusDockHotkey.modifiers);}, [modifierBits](Settings& s,int v){s.general.focusDockHotkey.modifiers=modifierBits(v);});
        TextRow(callbacks.hotkeyStatus ? callbacks.hotkeyStatus() : L"Shortcut status unavailable", TextRole::Status, 0, Severity::Info);
        BeginSection(L"Diagnostics");
        TextRow(L"The log folder holds this computer's Aether Shell logs. Export diagnostics saves the app version "
                L"and Windows build only - no logs or personal data.");
        ButtonsRow({Button(canvas, L"Open &log folder", kOpenLogs, ButtonStyle::Secondary, Glyph::Folder),
                    Button(canvas, L"&Export diagnostics...", kExport, ButtonStyle::Secondary, Glyph::Download)});
    } else if (page == SettingsPage::Appearance) {
        header(L"Appearance", L"Color mode, accent, motion speed and the look of the Dock and Top Bar.");
        BeginSection(L"Theme");
        Choice(L"Color mode", L"High contrast always takes precedence.", {L"Follow Windows", L"Light", L"Dark"},
               [](const Settings& s){return s.general.themeMode;}, [](Settings& s,int v){s.general.themeMode=static_cast<ThemePreference>(v);});
        Choice(L"Accent color", L"", {L"Windows", L"Aether"}, [](const Settings& s){return s.general.accentSource;}, [](Settings& s,int v){s.general.accentSource=static_cast<AccentSource>(v);});
        BeginSection(L"Motion");
        Number(L"Animation duration", L"1 is the standard speed; higher values are slower. Reduced motion takes precedence.", factor(0.5, 1.5),
               [](const Settings& s){return s.general.animationSpeed;}, [](Settings& s,double v){s.general.animationSpeed=v;});
        BeginSection(L"Dock");
        Choice(L"Dock icon preset", L"A custom size from Dock settings is kept until you choose a preset.",
            {L"Small - 32 px", L"Medium - 40 px", L"Large - 48 px", L"Keep custom size"},
            [](const Settings& s){return s.dock.iconSize==32?0:s.dock.iconSize==40?1:s.dock.iconSize==48?2:3;},
            [](Settings& s,int v){if(v>=0 && v<3)s.dock.iconSize=v==0?32:v==1?40:48;});
        Check(L"Show Windows Start in the Dock", L"", [](const Settings& s){return s.dock.showStartButton;}, [](Settings& s,bool v){s.dock.showStartButton=v;});
        Check(L"Show Task View in the Dock", L"", [](const Settings& s){return s.dock.showTaskViewButton;}, [](Settings& s,bool v){s.dock.showTaskViewButton=v;});
        Check(L"Reserve extra space for Dock animations", L"Keeps magnification and launch bounce clear of maximized windows.",
              [](const Settings& s){return s.dock.reserveAnimationSpace;}, [](Settings& s,bool v){s.dock.reserveAnimationSpace=v;});
        BeginSection(L"Top Bar");
        Choice(L"Weather position", L"", {L"Left", L"Right"}, [](const Settings& s){return s.topBar.weatherPlacement;}, [](Settings& s,int v){s.topBar.weatherPlacement=static_cast<WeatherPlacement>(v);});
        Choice(L"Keyboard indicator", L"", {L"Compact", L"Two lines"}, [](const Settings& s){return s.topBar.keyboardIndicator;}, [](Settings& s,int v){s.topBar.keyboardIndicator=static_cast<KeyboardIndicatorStyle>(v);});
        BeginSection(L"Windows taskbar pins");
        Check(L"Add newly discovered Windows taskbar pins", L"While the taskbar is replaced, new Windows pins are added to the Dock.",
              [](const Settings& s){return s.general.syncTaskbarPins;}, [](Settings& s,bool v){s.general.syncTaskbarPins=v;});
    } else if (page == SettingsPage::Desktop) {
        header(L"Desktop", L"Reversible replacement of the Windows taskbar by the Dock.");
        BeginSection(L"Taskbar replacement");
        Check(L"Replace the Windows taskbar", L"Changing this needs Apply.",
            [](const Settings& s){return s.general.replaceWindowsTaskbar;},
            [](Settings& s,bool v){s.general.replaceWindowsTaskbar=v;}, true);
        TextRow(L"While Aether Shell runs, it hides the Windows taskbar and reserves space below maximized applications.", TextRole::Body);
        TextRow(L"Windows taskbar state is restored when this mode is disabled or Aether Shell exits. A separate recovery "
                L"helper also restores it if the application stops unexpectedly. If recovery fails, restart Windows "
                L"Explorer in Task Manager.");
        TextRow(L"Windows Settings remains the native Windows interface. Its taskbar page is not replaced by Aether Shell; "
                L"configure this mode here.");
        TextRow(L"", TextRole::Status, kDesktopStatus, Severity::Info);
        owner.RefreshDesktopStatus();
        BeginSection(L"Pinned applications from Windows");
        HWND import = Button(canvas, L"&Import Windows taskbar pins", kImportTaskbarPins, ButtonStyle::Secondary, Glyph::Taskbar);
        ::EnableWindow(import, static_cast<bool>(callbacks.importTaskbarPins));
        ButtonsRow({import});
        TextRow(L"Reads pinned shortcuts without changing Windows pins. Some Windows 11 pins or their order may be unavailable.");
    } else {
        header(L"Aether Shell", (L"Version " + text::Widen(build::kVersion)).c_str());
        BeginSection(L"About");
        TextRow(L"Created by Ibrahim Bouillet", TextRole::Strong);
        TextRow(L"A native Windows desktop interface - a floating Dock and Top Status Bar - running alongside Explorer.", TextRole::Body);
        TextRow(L"Weather data: Open-Meteo.com (CC BY 4.0). No third-party C++ runtime libraries are bundled.");
        ButtonsRow({Button(canvas, L"&Official project", kReleaseDetails, ButtonStyle::Secondary, Glyph::OpenExternal)});
        BeginSection(L"Licence");
        HWND license = Make(canvas, L"EDIT", kMitLicense, WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL, kLicense);
        { form::ControlSpec spec; spec.role = ControlRole::Document; Style(license, spec); }
        Row& document = AddRow(RowKind::Document);
        document.primary = license;
        document.height = 240.0f;
    }
    building = false;
    fieldBaseline = ReadFields();
    SetStartupBusy(startupBusy);
    if (!lastActionStatus.empty() && page == SettingsPage::Desktop) Message(lastActionStatus, lastActionTone);
    else if (page == SettingsPage::Desktop) Message(L"Taskbar replacement changes need Apply.", StatusTone::Info);
    else if (page == SettingsPage::General) Message(L"Changes save automatically. Startup needs Apply.");
    else Message(L"Changes save automatically.");
    if (surfaceReady) {
        scroll.ScrollTo(0);
        Layout();
        surface.FadeIn(scroll.Viewport());
    }
}

void SettingsWindow::Impl::BuildUpdatesStatus() {
    HWND canvas = Canvas();
    BeginSection(L"Status", true);
    TextRow(L"", TextRole::Strong, kUpdateHeadline);
    TextRow(L"", TextRole::Status, kUpdateSummary, Severity::Info);
    HWND progress = Make(canvas, PROGRESS_CLASSW, L"", 0, kUpdateProgress);
    ::SendMessageW(progress, PBM_SETRANGE32, 0, 1000);
    { form::ControlSpec spec; spec.role = ControlRole::Progress; Style(progress, spec); }
    AddRow(RowKind::Progress).primary = progress;
    ButtonsRow({Button(canvas, L"Check &now", kCheckUpdate, ButtonStyle::Secondary, Glyph::Updates),
                Button(canvas, L"Download and install", kInstallUpdate, ButtonStyle::Primary, Glyph::Download)});
    // Later and Skip only make sense for an offered release; RefreshUpdates hides them otherwise.
    ButtonsRow({Button(canvas, L"&Later", kPostpone, ButtonStyle::Subtle),
                Button(canvas, L"S&kip this version", kSkip, ButtonStyle::Subtle),
                Button(canvas, L"&Release details", kReleaseDetails, ButtonStyle::Subtle, Glyph::OpenExternal),
                Button(canvas, L"Disable &notifications", kDisableUpdates, ButtonStyle::Subtle)});
    BeginSection(L"Release notes");
    HWND notes = Make(canvas, L"EDIT", text::Widen(update.releaseNotes), WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY, kReleaseNotes);
    { form::ControlSpec spec; spec.role = ControlRole::Document; Style(notes, spec); }
    Row& document = AddRow(RowKind::Document);
    document.primary = notes;
    document.height = 140.0f;
    RefreshUpdates();
}

// The Updates page presentation of the coordinator's snapshot, updated in place: no control is
// recreated, so focus, scroll position and edits elsewhere on the page survive every refresh.
void SettingsWindow::Impl::RefreshUpdates() {
    if (page != SettingsPage::Updates || !controls.contains(kUpdateSummary)) return;
    const auto version = [this] {
        return update.manifest ? text::Widen(update.manifest->version.ToString()) : std::wstring(L"the new version");
    };
    std::wstring headline;
    std::wstring summary = L"Installed: " + text::Widen(build::kVersion) + L".";
    StatusTone tone = StatusTone::Info;
    std::optional<double> fraction;
    bool showProgress = false;
    switch (update.state) {
        case updates::UpdateState::Disabled:
            headline = L"Automatic update checks are off";
            summary += L" Check now looks for a newer release once.";
            break;
        case updates::UpdateState::Idle:
            headline = update.lastCheck ? L"Last checked " + FormatLocalTime(*update.lastCheck) : L"Not checked yet";
            break;
        case updates::UpdateState::Checking:
            headline = L"Checking for updates...";
            tone = StatusTone::Busy;
            showProgress = true;
            break;
        case updates::UpdateState::UpToDate:
            headline = L"Aether Shell is up to date";
            tone = StatusTone::Success;
            if (update.latestVersion) summary += L" Latest published release: " + text::Widen(update.latestVersion->ToString()) + L".";
            break;
        case updates::UpdateState::UpdateAvailable:
            headline = L"Version " + version() + L" is available";
            if (update.downloadSize) summary += L" Download size: " + FormatBytes(*update.downloadSize) + L".";
            break;
        case updates::UpdateState::Downloading:
            headline = L"Downloading version " + version() + L"...";
            tone = StatusTone::Busy;
            showProgress = true;
            summary += L" Received " + FormatBytes(update.progress.received);
            if (update.progress.total) {
                fraction = std::clamp(static_cast<double>(update.progress.received) / static_cast<double>(update.progress.total), 0.0, 1.0);
                const auto percent = static_cast<unsigned>(std::lround(*fraction * 100.0));
                summary += L" of " + FormatBytes(update.progress.total) + L" (" + std::to_wstring(percent) + L"%).";
            } else summary += L"; total size unknown.";
            break;
        case updates::UpdateState::ReadyToInstall:
            headline = L"Version " + version() + L" is ready to install";
            tone = StatusTone::Success;
            summary += L" Verified with the signed release manifest and SHA-256.";
            break;
        case updates::UpdateState::Error:
            headline = L"The update could not be completed";
            tone = StatusTone::Error;
            break;
    }
    if (update.failure) {
        tone = StatusTone::Error;
        summary += L" Failure: " + text::Widen(ToString(update.failure->error.code)) + L". ";
        summary += CanDownloadOrInstall(update) ? L"Choose Retry Download to try again." : L"Use Check now to try again.";
    }
    if (update.nextCheck && update.automaticChecks && update.state != updates::UpdateState::Checking &&
        update.state != updates::UpdateState::Downloading)
        summary += L" Next automatic check: " + FormatLocalTime(*update.nextCheck) + L".";
    ::SetWindowTextW(controls[kUpdateHeadline], headline.c_str());
    ::SetWindowTextW(controls[kUpdateSummary], summary.c_str());
    if (surfaceReady) {
        form::ControlSpec spec;
        spec.role = ControlRole::Label;
        spec.text = TextRole::Status;
        spec.severity = SeverityFor(tone);
        surface.SetSpec(controls[kUpdateSummary], spec);
    }
    HWND progress = controls[kUpdateProgress];
    if (surfaceReady) surface.SetProgress(progress, fraction);
    else if (fraction) ::SendMessageW(progress, PBM_SETPOS, static_cast<WPARAM>(std::lround(*fraction * 1000.0)), 0);
    ::ShowWindow(progress, showProgress ? SW_SHOWNA : SW_HIDE);
    for (auto& section : sections)
        for (auto& row : section.rows) {
            if (row.primary == progress) row.hidden = !showProgress;
            if (row.primary == controls[kReleaseNotes]) row.hidden = update.releaseNotes.empty();
        }
    const auto notes = text::Widen(update.releaseNotes);
    if (WindowText(controls[kReleaseNotes]) != notes) ::SetWindowTextW(controls[kReleaseNotes], notes.c_str());
    ::ShowWindow(controls[kReleaseNotes], update.releaseNotes.empty() ? SW_HIDE : SW_SHOWNA);
    HWND install = controls[kInstallUpdate];
    const wchar_t* label = update.prepared ? L"&Install update" : update.failure && CanDownloadOrInstall(update) ? L"Retry &Download"
                                                                                                             : L"&Download and install";
    if (WindowText(install) != label) ::SetWindowTextW(install, label);
    ::EnableWindow(install, !startupBusy && CanDownloadOrInstall(update));
    const bool offer = static_cast<bool>(update.manifest);
    for (const int id : {kPostpone, kSkip}) {
        ::EnableWindow(controls[id], !startupBusy && offer);
        ::ShowWindow(controls[id], offer ? SW_SHOWNA : SW_HIDE);
    }
    ::EnableWindow(controls[kCheckUpdate], !startupBusy && update.state != updates::UpdateState::Checking &&
                                                update.state != updates::UpdateState::Downloading);
}

// --- layout ---------------------------------------------------------------------------------

void SettingsWindow::Impl::Layout() {
    if (!window || !surfaceReady || !scroll.Canvas()) return;
    RECT client{};
    ::GetClientRect(window, &client);
    const float width = Dip(static_cast<float>(client.right));
    const float height = Dip(static_cast<float>(client.bottom));
    const auto& f = tokens.form;
    const bool narrow = width < f.narrowWidth;
    const float navWidth = narrow ? f.navRailWidth : f.navWidth;
    const float contentLeft = navWidth + f.paneGap;
    const float actionGap = f.relatedGap + 2.0f * f.focusMargin;
    const float actionWidth = std::max(1.0f, width - 2.0f * f.outer);
    const float applyWidth = std::min(Preferred(applyButton), actionWidth);
    const float resetWidth = std::min(Preferred(resetButton), actionWidth);
    const float applyHeight = ButtonHeight(applyButton, applyWidth);
    const float resetHeight = ButtonHeight(resetButton, resetWidth);
    const bool stackActions = resetWidth + actionGap + applyWidth > actionWidth;
    const float actionsHeight = stackActions ? resetHeight + actionGap + applyHeight : std::max(resetHeight, applyHeight);
    const float applyRight = width - f.outer;
    const float resetRight = stackActions ? applyRight : applyRight - applyWidth - actionGap;
    const float inlineStatusRight = resetRight - resetWidth - f.controlGap - f.focusMargin;
    const bool stackStatus = narrow || stackActions || inlineStatusRight - contentLeft < 120.0f;
    const float statusLeft = stackStatus ? f.outer : contentLeft;
    const float statusRight = stackStatus ? width - f.outer : inlineStatusRight;
    const float statusHeight = std::max(18.0f, TextHeight(status, TextRole::Status,
                                      std::max(1.0f, statusRight - statusLeft - 24.0f)));
    const float footerHeight = std::max(f.footerHeight,
        stackStatus ? statusHeight + f.relatedGap + actionsHeight + 16.0f : std::max(statusHeight, actionsHeight) + 16.0f);
    const float footerTop = std::max(0.0f, height - footerHeight);

    // The title strip of the integrated frame: a small mark and the window title on the same glass.
    const float caption = Dip(static_cast<float>(CaptionHeightPx()));
    if (customFrame) {
        RECT buttons{};
        float buttonsLeft = width - 138.0f;
        if (SUCCEEDED(::DwmGetWindowAttribute(window, kDwmCaptionButtonBounds, &buttons, sizeof(buttons))) && buttons.right > buttons.left)
            buttonsLeft = Dip(static_cast<float>(buttons.left));
        captionMark = D2D1::RectF(14.0f, (caption - 16.0f) / 2.0f, 30.0f, (caption + 16.0f) / 2.0f);
        Place(captionTitle, 40.0f, 0.0f, std::max(41.0f, buttonsLeft - 8.0f), caption);
    } else {
        captionMark = D2D1::RectF();
    }
    brandMark = D2D1::RectF(narrow ? (navWidth - 22.0f) / 2.0f : 22.0f, caption + 18.0f, narrow ? (navWidth + 22.0f) / 2.0f : 44.0f,
                            caption + 40.0f);
    ::ShowWindow(brandLabel, narrow ? SW_HIDE : SW_SHOWNA);
    Place(brandLabel, 52.0f, caption + 14.0f, navWidth - 8.0f, caption + 44.0f);
    surface.SetNavigationCompact(navigation, narrow);
    const float navTop = caption + 56.0f;
    const float navBottom = std::min(navTop + 8.0f * f.navItemHeight + 4.0f, footerTop - 8.0f);
    Place(navigation, 8.0f, navTop, navWidth - 4.0f, navBottom);

    const float buttonTop = stackStatus ? footerTop + 8.0f + statusHeight + f.relatedGap
                                       : footerTop + (footerHeight - actionsHeight) / 2.0f;
    const float applyTop = stackActions ? buttonTop + resetHeight + actionGap : buttonTop + (actionsHeight - applyHeight) / 2.0f;
    const float resetTop = stackActions ? buttonTop : buttonTop + (actionsHeight - resetHeight) / 2.0f;
    Place(applyButton, applyRight - applyWidth, applyTop, applyRight, applyTop + applyHeight);
    Place(resetButton, resetRight - resetWidth, resetTop, resetRight, resetTop + resetHeight);
    const float statusTop = stackStatus ? footerTop + 8.0f : footerTop + (footerHeight - statusHeight) / 2.0f;
    Place(status, statusLeft, statusTop, statusRight, statusTop + statusHeight);

    const RECT viewport = PxRect(contentLeft, caption, width, footerTop);
    scroll.SetFrame(viewport);
    LayoutCanvas();
}

void SettingsWindow::Impl::LayoutCanvas() {
    if (!surfaceReady || !Canvas()) return;
    const auto& f = tokens.form;
    const RECT frame = scroll.Frame();
    const float available = Dip(static_cast<float>(frame.right - frame.left - scroll.GutterPx())) - 12.0f;
    const float width = std::max(200.0f, std::min(available, f.contentMaxWidth));
    std::vector<form::FormCard> cards;
    std::vector<form::FormDivider> dividers;
    float y = 20.0f;
    float headerLeft = 0.0f;
    if (page == SettingsPage::About) {
        aboutMark = D2D1::RectF(0.0f, y, 48.0f, y + 48.0f);
        headerLeft = 64.0f;
    }
    if (pageTitle) {
        const float titleHeight = std::max(32.0f, TextHeight(pageTitle, TextRole::Title, width - headerLeft));
        Place(pageTitle, headerLeft, y, width, y + titleHeight);
        y += titleHeight + 2.0f;
    }
    if (pageDescription) {
        const float descriptionHeight = TextHeight(pageDescription, TextRole::Helper, width - headerLeft);
        Place(pageDescription, headerLeft, y, width, y + descriptionHeight);
        y += descriptionHeight;
    }
    if (page == SettingsPage::About) y = std::max(y, aboutMark.bottom);
    y += f.sectionGap;
    for (auto& section : sections) {
        const bool visibleRows = std::any_of(section.rows.begin(), section.rows.end(), [](const Row& row) { return !row.hidden; });
        if (section.title) ::ShowWindow(section.title, visibleRows ? SW_SHOWNA : SW_HIDE);
        if (!visibleRows) continue;
        if (section.title) {
            const float titleHeight = std::max(20.0f, TextHeight(section.title, TextRole::Section, width));
            Place(section.title, 2.0f, y, width, y + titleHeight);
            y += titleHeight + f.sectionTitleGap;
        }
        const float cardTop = y;
        bool previousSetting = false;
        bool any = false;
        for (auto& row : section.rows) {
            const bool setting = row.kind == RowKind::Switch || row.kind == RowKind::Control;
            if (row.hidden) {
                for (HWND control : {row.primary, row.label, row.helper}) if (control) ::ShowWindow(control, SW_HIDE);
                continue;
            }
            y += LayoutRow(row, 0.0f, y, width, dividers, any && (setting || previousSetting));
            previousSetting = setting;
            any = true;
        }
        if (any) {
            if (section.rows.back().kind == RowKind::Text || section.rows.back().kind == RowKind::Buttons) y += 4.0f;
            cards.push_back(form::FormCard{PxRect(0.0f, cardTop, width, y), Canvas(), section.emphasis});
        }
        y += f.sectionGap;
    }
    scroll.SetContentHeight(Px(y));
    // The footer separator spans the window under the content.
    RECT client{};
    ::GetClientRect(window, &client);
    const int footerTop = static_cast<int>(frame.bottom);
    dividers.push_back(form::FormDivider{RECT{0, footerTop, client.right, footerTop + 1}, nullptr});
    surface.SetCards(std::move(cards));
    surface.SetDividers(std::move(dividers));
    ::InvalidateRect(window, nullptr, FALSE);
}

float SettingsWindow::Impl::LayoutRow(Row& row, float left, float top, float width, std::vector<form::FormDivider>& dividers, bool separated) {
    const auto& f = tokens.form;
    const float controlHeight = ControlHeight();
    const float padX = f.cardPaddingX;
    const float padY = f.rowPaddingY;
    const float inner = width - 2.0f * padX;
    const float x = left + padX;
    if (separated) {
        const int y = Px(top);
        dividers.push_back(form::FormDivider{RECT{Px(left + padX), y, Px(left + width - padX), y + 1}, Canvas()});
    }
    const auto show = [](HWND control) { if (control && !::IsWindowVisible(control)) ::ShowWindow(control, SW_SHOWNA); };
    switch (row.kind) {
        case RowKind::Switch: {
            const auto* spec = surface.Spec(row.primary);
            const float textWidth = std::max(60.0f, inner - f.switchWidth - f.controlGap);
            float block = TextHeight(row.primary, TextRole::Body, textWidth);
            if (spec && !spec->description.empty())
                block += f.labelGap + std::ceil(surface.MeasureText(spec->description, TextRole::Helper, textWidth, true).height);
            const float height = std::max(f.rowMinHeight, block + 2.0f * padY);
            show(row.primary);
            Place(row.primary, left + 2.0f, top + 1.0f, left + width - 2.0f, top + height - 1.0f);
            return height;
        }
        case RowKind::Control: {
            std::vector<float> widths;
            float trailing = 0.0f;
            for (std::size_t i = 0; i < row.trailing.size(); ++i) {
                float w = i < row.widths.size() && row.widths[i] > 0.0f ? row.widths[i] : Preferred(row.trailing[i]);
                widths.push_back(w);
                trailing += w + (i ? f.relatedGap : 0.0f);
            }
            const float textAvailable = inner - trailing - f.controlGap;
            const bool stacked = textAvailable < 160.0f;
            const float textWidth = stacked ? inner : textAvailable;
            const float labelHeight = std::max(20.0f, TextHeight(row.label, TextRole::Body, textWidth));
            const float helperHeight = row.helper ? TextHeight(row.helper, TextRole::Helper, textWidth) : 0.0f;
            const float textBlock = labelHeight + (row.helper ? f.labelGap + helperHeight : 0.0f);
            float height = 0.0f;
            float controlsTop = 0.0f;
            float textTop = 0.0f;
            float controlsLeft = 0.0f;
            if (stacked) {
                height = 2.0f * padY + textBlock + f.relatedGap + controlHeight;
                textTop = top + padY;
                controlsTop = textTop + textBlock + f.relatedGap;
                controlsLeft = x;
                if (trailing > inner && !widths.empty()) {  // narrow: the first (slider or field) gives way
                    widths[0] = std::max(64.0f, widths[0] - (trailing - inner));
                }
            } else {
                height = std::max(f.rowMinHeight, std::max(textBlock, controlHeight) + 2.0f * padY);
                textTop = top + (height - textBlock) / 2.0f;
                controlsTop = top + (height - controlHeight) / 2.0f;
                controlsLeft = x + inner - trailing;
            }
            show(row.label);
            Place(row.label, x, textTop, x + textWidth, textTop + labelHeight);
            if (row.helper) {
                show(row.helper);
                Place(row.helper, x, textTop + labelHeight + f.labelGap, x + textWidth, textTop + labelHeight + f.labelGap + helperHeight);
            }
            float cx = controlsLeft;
            for (std::size_t i = 0; i < row.trailing.size(); ++i) {
                show(row.trailing[i]);
                Place(row.trailing[i], cx, controlsTop, cx + widths[i], controlsTop + controlHeight);
                cx += widths[i] + f.relatedGap;
            }
            return height;
        }
        case RowKind::Text: {
            const float textHeight = std::max(18.0f, TextHeight(row.primary, row.role, inner));
            show(row.primary);
            Place(row.primary, x, top + padY, x + inner, top + padY + textHeight);
            return textHeight + 2.0f * padY;
        }
        case RowKind::Progress: {
            show(row.primary);
            Place(row.primary, x, top + 2.0f, x + inner, top + 8.0f);
            return 8.0f + padY;
        }
        case RowKind::Buttons: {
            float cx = x;
            float cy = top + padY;
            float lineHeight = 0.0f;
            for (HWND button : row.trailing) {
                if ((::GetWindowLongPtrW(button, GWL_STYLE) & WS_VISIBLE) == 0) continue;  // hidden by its owner
                const float w = std::min(Preferred(button), inner);
                if (cx > x && cx + w > x + inner) {
                    cx = x;
                    cy += lineHeight + f.relatedGap + 2.0f * f.focusMargin;
                    lineHeight = 0.0f;
                }
                const float h = ButtonHeight(button, w);
                Place(button, cx, cy, cx + w, cy + h);
                lineHeight = std::max(lineHeight, h);
                cx += w + f.relatedGap + 2.0f * f.focusMargin;
            }
            return cy + lineHeight + padY - top;
        }
        case RowKind::Pins: {
            float column = 0.0f;
            for (HWND button : row.trailing) column = std::max(column, std::min(Preferred(button), inner));
            const bool stacked = 120.0f + column + f.controlGap + f.focusMargin > inner;
            const float listRight = stacked ? x + inner : x + inner - column - f.controlGap - f.focusMargin;
            show(row.primary);
            Place(row.primary, x, top + padY, listRight, top + padY + row.height);
            float cy = stacked ? top + padY + row.height + f.relatedGap + f.focusMargin : top + padY;
            for (HWND button : row.trailing) {
                show(button);
                const float h = ButtonHeight(button, column);
                Place(button, stacked ? x : x + inner - column, cy, stacked ? x + column : x + inner, cy + h);
                cy += h + f.relatedGap + 2.0f * f.focusMargin;
            }
            return std::max(top + padY + row.height, cy - f.relatedGap - 2.0f * f.focusMargin) + padY - top;
        }
        case RowKind::Flex: {
            float buttons = 0.0f;
            for (HWND button : row.trailing) buttons += std::min(Preferred(button), inner) + f.relatedGap + 2.0f * f.focusMargin;
            const bool stacked = buttons + 120.0f > inner;
            const float flexRight = x + inner - buttons;
            show(row.primary);
            Place(row.primary, x, top + padY, stacked ? x + inner : flexRight, top + padY + controlHeight);
            float cx = stacked ? x : flexRight + f.relatedGap + f.focusMargin;
            float cy = stacked ? top + padY + controlHeight + f.relatedGap + f.focusMargin : top + padY;
            float lineHeight = controlHeight;
            for (HWND button : row.trailing) {
                const float w = std::min(Preferred(button), inner);
                if (cx > x && cx + w > x + inner) {
                    cx = x;
                    cy += lineHeight + f.relatedGap + 2.0f * f.focusMargin;
                    lineHeight = 0.0f;
                }
                show(button);
                const float h = ButtonHeight(button, w);
                Place(button, cx, cy, cx + w, cy + h);
                lineHeight = std::max(lineHeight, h);
                cx += w + f.relatedGap + 2.0f * f.focusMargin;
            }
            return cy + lineHeight + padY - top;
        }
        case RowKind::List:
        case RowKind::Document: {
            show(row.primary);
            Place(row.primary, x, top + padY, x + inner, top + padY + row.height);
            return row.height + 2.0f * padY;
        }
    }
    return 0.0f;
}

// --- editing ----------------------------------------------------------------------------------

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
    const auto previous = ReadFields();
    building = true;
    for (std::size_t i = 0; i < std::min(values.size(), bindings.size()); ++i) {
        if (i < previous.size() && previous[i] == values[i]) continue;
        HWND control = controls.at(kFirstField + static_cast<int>(i));
        if (bindings[i].kind == FieldKind::Check) ::SendMessageW(control, BM_SETCHECK, values[i] == L"1" ? BST_CHECKED : BST_UNCHECKED, 0);
        else if (bindings[i].kind == FieldKind::Choice) ::SendMessageW(control, CB_SETCURSEL, std::wcstol(values[i].c_str(), nullptr, 10), 0);
        else if (bindings[i].kind == FieldKind::Key) ::SendMessageW(control, HKM_SETHOTKEY, std::wcstoul(values[i].c_str(), nullptr, 10), 0);
        else { ::SetWindowTextW(control, values[i].c_str()); SyncSliderFromField(i); }
    }
    building = false;
}
void SettingsWindow::Impl::RefreshCleanFields() {
    auto values = ReadFields();
    for (std::size_t i = 0; i < std::min(values.size(), fieldBaseline.size()); ++i) {
        if (values[i] != fieldBaseline[i]) continue;
        values[i] = bindings[i].read(service.Current());
        fieldBaseline[i] = values[i];
    }
    RestoreFields(values);
    for (std::size_t i = 0; i < bindings.size(); ++i) if (bindings[i].consentOnly) RefreshConsentHint(i);
}
void SettingsWindow::Impl::RememberFocus() {
    const HWND focused = ::GetFocus();
    if (!focused || !Canvas() || !::IsChild(Canvas(), focused)) return;
    const int id = ::GetDlgCtrlID(focused);
    if (id && controls.contains(id) && controls.at(id) == focused) lastFocusId = id;
}
void SettingsWindow::Impl::SavePageDraft() {
    if (!window || rebuildOnShow) return;
    RememberFocus();
    PageDraft draft;
    draft.values = ReadFields();
    draft.baseline = fieldBaseline;
    draft.scrollPosition = scroll.Position();
    draft.dpi = DpiNow();
    draft.focusId = lastFocusId;
    if (controls.contains(lastFocusId)) {
        wchar_t cls[32]{};
        const HWND focused = controls.at(lastFocusId);
        ::GetClassNameW(focused, cls, 32);
        if (text::EqualsIgnoreCase(cls, L"Edit")) {
            ::SendMessageW(focused, EM_GETSEL, reinterpret_cast<WPARAM>(&draft.selectionStart), reinterpret_cast<LPARAM>(&draft.selectionEnd));
            draft.hasTextSelection = true;
        }
    }
    if (page == SettingsPage::Weather) {
        draft.locationQuery = WindowText(controls.at(kLocationField));
        draft.locations = locations;
        draft.locationSelection = ::SendMessageW(controls.at(kGeoResults), LB_GETCURSEL, 0, 0);
    }
    if (page == SettingsPage::Dock) {
        const auto pin = ::SendMessageW(controls.at(kPinList), LB_GETCURSEL, 0, 0);
        const auto& pins = service.Current().dock.pinned;
        if (pin != LB_ERR && static_cast<std::size_t>(pin) < pins.size()) draft.pinId = pins[static_cast<std::size_t>(pin)].id;
        const auto app = ::SendMessageW(controls.at(kAppChooser), CB_GETCURSEL, 0, 0);
        if (app != CB_ERR && static_cast<std::size_t>(app) < applications.size()) draft.applicationId = applications[static_cast<std::size_t>(app)].id;
    }
    pageDrafts[page] = std::move(draft);
}
void SettingsWindow::Impl::RestorePageDraft() {
    lastFocusId = 0;
    const auto found = pageDrafts.find(page);
    if (found == pageDrafts.end()) return;
    const auto& draft = found->second;
    auto values = ReadFields();
    // Only staged edits override the fresh model. An untouched field can reflect external changes.
    for (std::size_t i = 0; i < std::min(values.size(), draft.values.size()); ++i)
        if (i >= draft.baseline.size() || draft.values[i] != draft.baseline[i]) values[i] = draft.values[i];
    RestoreFields(values);
    if (page == SettingsPage::Weather) {
        ::SetWindowTextW(controls.at(kLocationField), draft.locationQuery.c_str());
        locations = draft.locations;
        HWND results = controls.at(kGeoResults);
        for (const auto& location : locations) {
            const auto label = location.name + L", " + location.admin1 + L", " + location.country;
            ::SendMessageW(results, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        ::SendMessageW(results, LB_SETCURSEL, static_cast<WPARAM>(draft.locationSelection), 0);
    }
    if (page == SettingsPage::Dock) {
        const auto& pins = service.Current().dock.pinned;
        for (std::size_t i = 0; i < pins.size(); ++i)
            if (pins[i].id == draft.pinId) ::SendMessageW(controls.at(kPinList), LB_SETCURSEL, i, 0);
        for (std::size_t i = 0; i < applications.size(); ++i)
            if (applications[i].id == draft.applicationId) ::SendMessageW(controls.at(kAppChooser), CB_SETCURSEL, i, 0);
    }
    for (std::size_t i = 0; i < bindings.size(); ++i) if (bindings[i].consentOnly) RefreshConsentHint(i);
    if (draft.focusId && controls.contains(draft.focusId)) {
        const HWND focus = controls.at(draft.focusId);
        if (::IsWindowEnabled(focus)) ::SetFocus(focus);
        if (draft.hasTextSelection) ::SendMessageW(focus, EM_SETSEL, draft.selectionStart, draft.selectionEnd);
        lastFocusId = draft.focusId;
    }
    scroll.ScrollTo(::MulDiv(draft.scrollPosition, static_cast<int>(DpiNow()), static_cast<int>(draft.dpi)));
}
void SettingsWindow::Impl::DiscardPageDrafts() {
    pageDrafts.clear();
    rebuildOnShow = true;
    lastFocusId = 0;
}
void SettingsWindow::Impl::SyncSliderFromField(std::size_t index) {
    if (index >= bindings.size() || bindings[index].kind != FieldKind::Number || syncing) return;
    const auto found = controls.find(kFirstSlider + static_cast<int>(index));
    HWND field = controls.at(kFirstField + static_cast<int>(index));
    const auto value = ParseNumber(WindowText(field), bindings[index].number);
    const bool valid = value && InRange(*value, bindings[index].number);
    if (surfaceReady) surface.SetError(field, !value.has_value());
    if (found != controls.end() && valid) ::SendMessageW(found->second, TBM_SETPOS, TRUE, SliderPosition(*value, bindings[index].number));
}
void SettingsWindow::Impl::SyncFieldFromSlider(std::size_t index) {
    if (index >= bindings.size() || bindings[index].kind != FieldKind::Number) return;
    const auto found = controls.find(kFirstSlider + static_cast<int>(index));
    if (found == controls.end()) return;
    const auto& format = bindings[index].number;
    const int position = static_cast<int>(::SendMessageW(found->second, TBM_GETPOS, 0, 0));
    const auto text = FormatNumber(StoredFromSlider(position, format), format);
    HWND field = controls.at(kFirstField + static_cast<int>(index));
    if (WindowText(field) == text) return;
    syncing = true;
    ::SetWindowTextW(field, text.c_str());  // EN_CHANGE schedules the same debounced live apply
    syncing = false;
    if (surfaceReady) surface.SetError(field, false);
}
void SettingsWindow::Impl::RefreshConsentHint(std::size_t index) {
    if (index >= bindings.size() || !bindings[index].consentOnly || !surfaceReady) return;
    HWND control = controls.at(kFirstField + static_cast<int>(index));
    const auto value = ::SendMessageW(control, BM_GETCHECK, 0, 0) == BST_CHECKED ? std::wstring(L"1") : std::wstring(L"0");
    const bool staged = index < fieldBaseline.size() && value != fieldBaseline[index];
    form::ControlSpec spec = surface.Spec(control) ? *surface.Spec(control) : form::ControlSpec{};
    spec.role = ControlRole::Switch;
    spec.description = bindings[index].helper + (staged ? std::wstring(L" ") + kApplyHint : std::wstring());
    surface.SetSpec(control, spec);
    if (staged) Message(kApplyHint, StatusTone::Info);
    LayoutCanvas();
}
void SettingsWindow::Impl::ScheduleLive() {
    if (building || startupBusy) return;
    livePending = true; ::SetTimer(window, kLiveApplyTimer, 350, nullptr);
}
void SettingsWindow::Impl::SetStartupBusy(bool busy) {
    startupBusy = busy;
    for (HWND control : {navigation, applyButton, resetButton}) if (control) ::EnableWindow(control, !busy);
    for (HWND control : pageControls) ::EnableWindow(control, !busy);
    if (!busy && controls.contains(kInstallUpdate)) RefreshUpdates();
    if (!busy && controls.contains(kImportTaskbarPins))
        ::EnableWindow(controls.at(kImportTaskbarPins), static_cast<bool>(callbacks.importTaskbarPins));
    if (!busy) {
        auto ready=std::move(closeWaiters);closeWaiters.clear();
        for(auto& callback:ready) try { callback(); } catch (...) { Message(L"The close action could not be completed.", StatusTone::Error); }
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
    catch (...) { Message(L"Enter a complete number before saving; the field marked in red is not one.", StatusTone::Warning); return; }
    if (auto validated = Validate(next); !validated) { Message(L"Settings could not be validated.", StatusTone::Error); return; }
    const auto generation = ++applyGeneration;
    const bool startupChange = explicitConsent && next.general.startWithWindows != service.Current().general.startWithWindows;
    const auto commit = [this, weak = std::weak_ptr<bool>(alive), mutate, generation, startupChange, acceptedFields, values] {
        auto result = service.Update(mutate, [this, weak, generation, startupChange](Result<void> persisted) {
            if (const auto live = weak.lock(); live && *live) {
                if (startupChange) SetStartupBusy(false);
                if (generation == applyGeneration) {
                    if (persisted) Message(L"Settings saved.", StatusTone::Success);
                    else Message(L"Settings changed in memory; saving failed. Apply again to retry.", StatusTone::Error);
                }
            }
        });
        if (!result) { if (startupChange) SetStartupBusy(false); Message(L"Settings could not be applied.", StatusTone::Error); }
        else {
            for (const auto index : acceptedFields) if (index < fieldBaseline.size()) fieldBaseline[index] = values[index];
            for (const auto index : acceptedFields) RefreshConsentHint(index);
            if (!result->empty()) Message(L"Some values were corrected to their supported range.", StatusTone::Warning);
        }
    };
    if (startupChange) {
        if (!callbacks.setStartup) { Message(L"Startup registration is unavailable in this session.", StatusTone::Warning); return; }
        SetStartupBusy(true); Message(L"Changing startup registration...", StatusTone::Busy);
        const auto weak = std::weak_ptr<bool>(alive);
        try {
            callbacks.setStartup(next.general.startWithWindows, [this, weak, commit](Result<void> result) {
                if (const auto live = weak.lock(); live && *live) {
                    if (result) commit();
                    else { SetStartupBusy(false); Message(L"Windows did not change startup registration. Check Task Manager's startup setting.", StatusTone::Error); }
                }
            });
        } catch (...) { SetStartupBusy(false); Message(L"Startup registration could not be changed.", StatusTone::Error); }
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
                if (persisted) Message(L"Defaults saved; startup is disabled.", StatusTone::Success);
                else Message(L"Defaults applied; saving failed. Apply again to retry.", StatusTone::Error);
            }
        });
        if (!result) { SetStartupBusy(false); Message(L"Defaults could not be applied.", StatusTone::Error); }
        else if (window) { pageDrafts.clear(); lastFocusId = 0; BuildPage(); }
    };
    SetStartupBusy(true);
    if (callbacks.setStartup) {
        try {
            callbacks.setStartup(false, [this, weak, reset](Result<void> result) {
                if (const auto live = weak.lock(); live && *live) {
                    if (result) reset();
                    else { SetStartupBusy(false); Message(L"Startup was not disabled; settings were kept.", StatusTone::Error); }
                }
            });
        } catch (...) { SetStartupBusy(false); return MakeError(ErrorCode::Internal, "startup callback failed"); }
    } else reset();
    return Ok();
}

void SettingsWindow::Impl::SearchLocation() {
    if (!callbacks.geocoding) { Message(L"Location search is unavailable.", StatusTone::Warning); return; }
    const auto query = WindowText(controls[kLocationField]);
    geocodeCancel.Cancel(); geocodeCancel = CancellationSource{};
    const auto cancel = geocodeCancel.Token(); const auto weak = std::weak_ptr<bool>(alive);
    auto* client = callbacks.geocoding;
    Message(L"Searching Open-Meteo locations...", StatusTone::Busy);
    worker.Post([this, weak, client, cancel, query] {
        auto result = client->Search(query, L"en", cancel);
        dispatcher.Post([this, weak, cancel, result = std::move(result)]() mutable {
            if (const auto live = weak.lock(); !live || !*live || cancel.IsCancelled() || page != SettingsPage::Weather) return;
            if (!result) { Message(L"Location search failed. Check your connection and try again.", StatusTone::Error); return; }
            locations = std::move(*result);
            ::SendMessageW(controls[kGeoResults], LB_RESETCONTENT, 0, 0);
            for (const auto& location : locations) {
                const auto label = location.name + L", " + location.admin1 + L", " + location.country;
                ::SendMessageW(controls[kGeoResults], LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
            }
            Message(locations.empty() ? L"No place matched that search." : L"Choose a location to save it.",
                    locations.empty() ? StatusTone::Warning : StatusTone::Info);
        });
    });
}

bool SettingsWindow::Impl::ConfirmReset() {
    SettingsConfirmation question;
    question.title = L"Reset all settings?";
    question.message = L"Preferences and pinned applications return to their defaults and startup at sign-in is turned off.";
    question.detail = L"Your Windows taskbar choice is kept. A backup of the current settings file is kept.";
    question.confirm = L"&Reset";
    if (callbacks.confirm) return callbacks.confirm(question);
    form::ConfirmRequest request;
    request.owner = window;
    request.title = question.title;
    request.message = question.message;
    request.detail = question.detail;
    request.severity = Severity::Warning;
    request.primary = question.confirm;
    request.dangerousPrimary = true;
    request.defaultChoice = form::ConfirmChoice::Cancel;
    const auto choice = form::ShowConfirmation(request, tokens, motion, callbacks.scheduler);
    return choice && *choice == form::ConfirmChoice::Primary;
}

void SettingsWindow::Impl::Action(int id) {
    if (startupBusy) return;
    const auto pinSaved = [this, weak = std::weak_ptr<bool>(alive)](Result<void> persisted) {
        if (const auto live = weak.lock(); live && *live) {
            if (persisted) Message(L"Pinned applications saved.", StatusTone::Success);
            else Message(L"Pins changed in memory; saving failed. Apply again to retry.", StatusTone::Error);
        }
    };
    if (id == kApply) Apply(true);
    else if (id == kReset) {
        if (ConfirmReset())
            if (auto result = ResetAfterConsent(); !result) Message(L"Defaults could not be restored because startup registration is unavailable.", StatusTone::Error);
    } else if (id == kAddPin) {
        if (livePending) Apply(false);
        const auto selected = ::SendMessageW(controls[kAppChooser], CB_GETCURSEL, 0, 0);
        if (selected != CB_ERR && static_cast<std::size_t>(selected) < applications.size()) {
            const auto app = applications[static_cast<std::size_t>(selected)];
            const auto result = service.Update([app](Settings& s){
                PinnedApp pin; pin.id=app.id; pin.displayName=app.displayName; pin.parsingName=app.parsingName;
                pin.appUserModelId=app.appUserModelId; pin.executablePath=app.executablePath;
                pin.shortcutPath=app.shortcutPath; pin.arguments=app.arguments;
                s.dock.pinned.push_back(std::move(pin));
                std::erase_if(s.dock.dismissedPins,[&](const auto& item){return text::EqualsIgnoreCase(item,app.id);});
            }, pinSaved); BuildPage();
            if (!result) Message(L"Pinned application could not be added.", StatusTone::Error);
        } else Message(L"Choose an application to add first.", StatusTone::Info);
    } else if (id == kRemovePin || id == kMovePinUp || id == kMovePinDown) {
        if (livePending) Apply(false);
        const auto selected = ::SendMessageW(controls[kPinList], LB_GETCURSEL, 0, 0);
        if (selected != LB_ERR) {
            const auto result = service.Update([id,selected](Settings& s){ const auto at=static_cast<std::size_t>(selected); if(at>=s.dock.pinned.size())return;
                if(id==kRemovePin){
                    const auto removed=s.dock.pinned[at].id;
                    s.dock.pinned.erase(s.dock.pinned.begin()+static_cast<std::ptrdiff_t>(at));
                    std::erase_if(s.dock.dismissedPins,[&](const auto& item){return text::EqualsIgnoreCase(item,removed);});
                    if(s.dock.dismissedPins.size()>=128)s.dock.dismissedPins.erase(s.dock.dismissedPins.begin());
                    s.dock.dismissedPins.push_back(removed);
                }
                else if(id==kMovePinUp && at>0)std::swap(s.dock.pinned[at],s.dock.pinned[at-1]);
                else if(id==kMovePinDown && at+1<s.dock.pinned.size())std::swap(s.dock.pinned[at],s.dock.pinned[at+1]); }, pinSaved);
            const auto keep = id == kMovePinUp && selected > 0 ? selected - 1 : id == kMovePinDown ? selected + 1 : selected;
            BuildPage();
            if (id != kRemovePin && controls.contains(kPinList)) ::SendMessageW(controls[kPinList], LB_SETCURSEL, static_cast<WPARAM>(keep), 0);
            if (!result) Message(L"Pinned applications could not be changed.", StatusTone::Error);
        } else Message(L"Choose a pinned application first.", StatusTone::Info);
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

void SettingsWindow::Impl::Navigate(int delta) {
    if (startupBusy || !navigation) return;
    const int count = static_cast<int>(::SendMessageW(navigation, LB_GETCOUNT, 0, 0));
    if (count <= 0) return;
    const int next = (static_cast<int>(page) + delta + count) % count;
    ::SendMessageW(navigation, LB_SETCURSEL, static_cast<WPARAM>(next), 0);
    ::SendMessageW(window, WM_COMMAND, MAKEWPARAM(kNavigation, LBN_SELCHANGE), reinterpret_cast<LPARAM>(navigation));
}

void SettingsWindow::Impl::ApplyTheme() {
    if (!window) return;
    if (surfaceReady) surface.SetTheme(tokens, motion);
    UpdateFrame();
    for (const auto& [id, control] : controls)
        if (id >= kFirstField && id < kFirstField + 70) {
            const auto index = static_cast<std::size_t>(id - kFirstField);
            if (index < bindings.size() && bindings[index].consentOnly) RefreshConsentHint(index);
        }
    Layout();
}

LRESULT SettingsWindow::Impl::Handle(UINT message, WPARAM wParam, LPARAM lParam) {
    if (customFrame) {
        // DWM's caption buttons first: hover, press, Snap layouts and their hit testing.
        LRESULT result = 0;
        if (::DwmDefWindowProc(window, message, wParam, lParam, &result)) return result;
        if (message == WM_NCCALCSIZE && wParam) {
            // Keep the side and bottom borders; the client starts at the window's top edge (below the
            // hidden resize border while maximized).
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            const LONG top = params->rgrc[0].top;
            const LRESULT standard = ::DefWindowProcW(window, message, wParam, lParam);
            params->rgrc[0].top = top + (::IsZoomed(window) ? FrameInsetPx() : 0);
            return standard;
        }
        if (message == WM_NCHITTEST) return HitTest(wParam, lParam);
    }
    if (message == WM_DWMCOMPOSITIONCHANGED) UpdateFrame();
    if (surfaceReady) {
        if (auto handled = surface.HandleMessage(message, wParam, lParam)) return *handled;
    }
    if (message == WM_COMMAND) {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == kNavigation && code == LBN_SELCHANGE) {
            if (startupBusy) return 0;
            if (livePending) Apply(false);
            const auto selected = ::SendMessageW(navigation, LB_GETCURSEL, 0, 0);
            if (selected == LB_ERR || selected == static_cast<LRESULT>(page)) return 0;
            SavePageDraft();
            page = static_cast<SettingsPage>(selected); geocodeCancel.Cancel(); BuildPage(); RestorePageDraft(); return 0;
        }
        if (id >= kFirstField && static_cast<std::size_t>(id-kFirstField) < bindings.size()) {
            const auto index = static_cast<std::size_t>(id - kFirstField);
            if (bindings[index].kind == FieldKind::Number && code == EN_CHANGE && !building) SyncSliderFromField(index);
            if (bindings[index].consentOnly && code == BN_CLICKED) RefreshConsentHint(index);
            if (!bindings[index].consentOnly && (code == EN_CHANGE || code == CBN_SELCHANGE || code == BN_CLICKED)) ScheduleLive();
            return 0;
        }
        if (id == kGeoResults && code == LBN_SELCHANGE) { ScheduleLive(); return 0; }
        if (code == BN_CLICKED) { Action(id); return 0; }
        return 0;
    }
    if (message == WM_HSCROLL && lParam) {
        const int id = ::GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        if (id >= kFirstSlider && static_cast<std::size_t>(id - kFirstSlider) < bindings.size() && !building) {
            SyncFieldFromSlider(static_cast<std::size_t>(id - kFirstSlider));
        }
        return 0;
    }
    if (message == WM_HSCROLL) return 0;  // no horizontal scrolling: the content reflows to the width
    if (message == WM_VSCROLL) { if (surfaceReady) scroll.HandleScrollCode(LOWORD(wParam), static_cast<short>(HIWORD(wParam))); return 0; }
    if (message == WM_MOUSEWHEEL) { if (surfaceReady) ::SendMessageW(scroll.Viewport(), WM_MOUSEWHEEL, wParam, lParam); return 0; }
    if (message == WM_TIMER && wParam == kLiveApplyTimer) { Apply(false); return 0; }
    if (message == WM_CLOSE) { if (livePending) Apply(false); DiscardPageDrafts(); ::ShowWindow(window, SW_HIDE); owner.Closed.Emit(); return 0; }
    if (message == WM_GETMINMAXINFO) {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        const UINT dpi = DpiNow();
        RECT frame{0, 0, ::MulDiv(380, static_cast<int>(dpi), 96), ::MulDiv(340, static_cast<int>(dpi), 96)};
        ::AdjustWindowRectExForDpi(&frame, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, dpi);
        info->ptMinTrackSize = POINT{frame.right - frame.left, frame.bottom - frame.top};
        return 0;
    }
    if (message == WM_DPICHANGED) {
        const int position = surfaceReady ? scroll.Position() : 0;
        const int range = surfaceReady ? std::max(1, scroll.Range()) : 1;
        const auto& rect = *reinterpret_cast<RECT*>(lParam);
        MONITORINFO monitor{sizeof(monitor)}; ::GetMonitorInfoW(::MonitorFromRect(&rect,MONITOR_DEFAULTTONEAREST),&monitor);
        const int width=std::min(static_cast<int>(rect.right-rect.left),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left));
        const int height=std::min(static_cast<int>(rect.bottom-rect.top),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
        const int left=std::clamp(static_cast<int>(rect.left),static_cast<int>(monitor.rcWork.left),static_cast<int>(monitor.rcWork.right)-width);
        const int top=std::clamp(static_cast<int>(rect.top),static_cast<int>(monitor.rcWork.top),static_cast<int>(monitor.rcWork.bottom)-height);
        ::SetWindowPos(window, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
        Layout();
        if (surfaceReady && position) scroll.ScrollTo(::MulDiv(position, std::max(1, scroll.Range()), range));
        return 0;
    }
    if (message == WM_SIZE) { if (wParam != SIZE_MINIMIZED) Layout(); return 0; }
    if (message == WM_DESTROY) { ::KillTimer(window,kLiveApplyTimer); geocodeCancel.Cancel(); scroll.Destroy(); surface.Detach(); surfaceReady = false; window = nullptr; return 0; }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

SettingsWindow::SettingsWindow(SettingsService& service, IUiDispatcher& dispatcher, SettingsCallbacks callbacks)
    : impl_(std::make_unique<Impl>(*this, service, dispatcher, std::move(callbacks))) {}
SettingsWindow::~SettingsWindow() {
    *impl_->alive = false; impl_->geocodeCancel.Cancel(); impl_->worker.Shutdown();
    if (impl_->window) ::DestroyWindow(impl_->window);
}
Result<void> SettingsWindow::Show(SettingsPage page) try {
    auto& impl = *impl_;
    if (!impl.service.Loaded()) return MakeError(ErrorCode::InvalidState, "settings are still loading");
    if (impl.startupBusy) return MakeError(ErrorCode::InvalidState, "startup change is pending");
    if (impl.livePending) impl.Apply(false);
    if (impl.window && !impl.rebuildOnShow && impl.page == page) {
        impl.RefreshCleanFields();
        ::ShowWindow(impl.window, ::IsIconic(impl.window) ? SW_RESTORE : SW_SHOW); ::SetForegroundWindow(impl.window); return Ok();
    }
    impl.SavePageDraft();
    if (impl.page != page) impl.geocodeCancel.Cancel();
    impl.page = page;
    if (!impl.window) {
        WNDCLASSEXW wc{sizeof(wc)}; wc.lpfnWndProc = &Impl::Proc; wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW); wc.lpszClassName = kClass;
        wc.hIcon = ::LoadIconW(wc.hInstance, L"IDI_AETHER");
        if (auto registered = platform::RegisterWindowClassOnce(wc); !registered) return registered.error();
        const UINT systemDpi = ::GetDpiForSystem();
        const auto area = platform::PrimaryMonitor().workArea;
        const int width = std::min(::MulDiv(960, static_cast<int>(systemDpi), 96), static_cast<int>((area.right - area.left) * 9 / 10));
        const int height = std::min(::MulDiv(700, static_cast<int>(systemDpi), 96), static_cast<int>((area.bottom - area.top) * 9 / 10));
        const auto window = ::CreateWindowExW(WS_EX_CONTROLPARENT, kClass, L"Aether Shell Settings", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
            area.left+(area.right-area.left-width)/2, area.top+(area.bottom-area.top-height)/2, width, height, nullptr, nullptr, ::GetModuleHandleW(nullptr), impl_.get());
        if (!window) return MakeError(ErrorCode::Platform, "settings window could not be created");
        if (auto attached = impl.surface.Attach(window, impl.tokens, impl.motion, impl.callbacks.scheduler); attached) {
            impl.surfaceReady = true;
            impl.surface.SetDecorator([this](ID2D1DeviceContext* target, const D2D1_RECT_F&, const graphics::DesignTokens& tokens) {
                auto& state = *impl_;
                if (state.customFrame && state.captionMark.right > state.captionMark.left)
                    graphics::PaintBrandMark(target, state.captionMark, tokens);
                graphics::PaintBrandMark(target, state.brandMark, tokens);
                if (state.page != SettingsPage::About || state.aboutMark.right <= state.aboutMark.left || !state.scroll.Canvas()) return;
                // Includes a scroll the surface is presenting, so the mark moves with its page.
                const POINT origin = state.surface.ClientOriginPx(state.scroll.Canvas());
                const RECT viewport = state.scroll.Frame();
                const float dx = state.Dip(static_cast<float>(origin.x));
                const float dy = state.Dip(static_cast<float>(origin.y));
                target->PushAxisAlignedClip(D2D1::RectF(state.Dip(static_cast<float>(viewport.left)), state.Dip(static_cast<float>(viewport.top)),
                                                        state.Dip(static_cast<float>(viewport.right)), state.Dip(static_cast<float>(viewport.bottom))),
                                            D2D1_ANTIALIAS_MODE_ALIASED);
                graphics::PaintBrandMark(target, D2D1::RectF(state.aboutMark.left + dx, state.aboutMark.top + dy,
                                                             state.aboutMark.right + dx, state.aboutMark.bottom + dy), tokens);
                target->PopAxisAlignedClip();
            });
        } else {
            AETHER_LOG_ERROR("settings", "settings presentation unavailable ({})", attached.error().Describe());
            ::DestroyWindow(window);
            impl.window = nullptr;
            return attached.error();
        }
        impl.brandLabel = impl.Label(window, L"Settings", TextRole::Strong, kBrandLabel, Severity::None, false);
        // The title shown in the integrated title strip (a static label is transparent to hit testing,
        // so the strip still drags the window). The window text stays the accessible/Alt+Tab title.
        impl.captionTitle = impl.Label(window, L"Aether Shell Settings", TextRole::Caption, kCaptionTitle, Severity::None, false);
        impl.navigation = impl.Make(window, L"LISTBOX", L"", WS_TABSTOP | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, kNavigation);
        for (const auto* name : {L"Dock",L"Top Bar",L"Weather",L"Updates",L"General",L"About",L"Desktop",L"Appearance"}) ::SendMessageW(impl.navigation, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name));
        { form::ControlSpec spec; spec.role = ControlRole::Navigation; impl.Style(impl.navigation, spec); }
        impl.surface.SetNavigationGlyphs(impl.navigation, {Glyph::Dock, Glyph::TopBar, Glyph::Weather, Glyph::Updates, Glyph::General,
                                                          Glyph::About, Glyph::Desktop, Glyph::Appearance});
        if (auto created = impl.scroll.Create(impl.surface, window, kScrollView); !created) {
            ::DestroyWindow(window);
            impl.window = nullptr;
            return created.error();
        }
        impl.status = impl.Label(window, L"", TextRole::Status, kStatus, Severity::None, true);
        impl.resetButton = impl.Button(window, L"Reset defaults...", kReset, ButtonStyle::Secondary, Glyph::Reset);
        impl.applyButton = impl.Button(window, L"&Apply", kApply, ButtonStyle::Primary, std::nullopt, BS_DEFPUSHBUTTON);
        impl.UpdateFrame();
    }
    ::SendMessageW(impl.navigation, LB_SETCURSEL, static_cast<WPARAM>(page), 0); impl.BuildPage();
    impl.rebuildOnShow = false;
    impl.RestorePageDraft();
    ::ShowWindow(impl.window, ::IsIconic(impl.window) ? SW_RESTORE : SW_SHOW); ::SetForegroundWindow(impl.window); return Ok();
} catch (...) { return MakeError(ErrorCode::Internal,"settings window could not be shown"); }
Result<void> SettingsWindow::PrepareToClose(std::function<void()> ready) {
    try {
        if(!ready)return MakeError(ErrorCode::InvalidArgument,"close completion is empty");
        if(impl_->livePending)impl_->Apply(false);
        impl_->DiscardPageDrafts();
        if(impl_->window)::ShowWindow(impl_->window,SW_HIDE);
        if(impl_->startupBusy)impl_->closeWaiters.push_back(std::move(ready));else ready();
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal,"settings close could not be prepared"); }
}
void SettingsWindow::Close() { if (impl_->window) { if (impl_->livePending) impl_->Apply(false); impl_->DiscardPageDrafts(); ::ShowWindow(impl_->window, SW_HIDE); } }
bool SettingsWindow::PreTranslate(MSG& message) {
    auto& impl = *impl_;
    if (!impl.window || !::IsWindowVisible(impl.window)) return false;
    if (message.hwnd != impl.window && !::IsChild(impl.window, message.hwnd)) return false;
    impl.RememberFocus();
    if (message.message == WM_KEYDOWN && (::GetKeyState(VK_CONTROL) & 0x8000)) {
        const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        if (message.wParam == VK_TAB || message.wParam == VK_NEXT || message.wParam == VK_PRIOR) {
            impl.Navigate(message.wParam == VK_PRIOR || (message.wParam == VK_TAB && shift) ? -1 : 1);
            return true;
        }
    }
    if (!::IsDialogMessageW(impl.window, &message)) return false;
    // IsDialogMessage claims every message of the window: only keyboard input reveals the focus, so a
    // wheel or indicator scroll is not undone by the next mouse move.
    if (impl.surfaceReady && (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN)) impl.scroll.RevealFocus(impl.Px(12.0f));
    return true;
}
HWND SettingsWindow::Handle() const noexcept { return impl_->window; }
HWND SettingsWindow::Control(int id) const noexcept { const auto found = impl_->controls.find(id); return found == impl_->controls.end() ? nullptr : found->second; }
void SettingsWindow::SetUpdateSnapshot(updates::UpdateSnapshot snapshot) {
    impl_->update = std::move(snapshot);
    if (impl_->window && impl_->page == SettingsPage::Updates) {
        impl_->RefreshUpdates();
        impl_->LayoutCanvas();
    }
}
void SettingsWindow::RefreshDesktopStatus() {
    if (!impl_->window || impl_->page != SettingsPage::Desktop || !impl_->controls.contains(kDesktopStatus)) return;
    try {
        const auto status = impl_->callbacks.taskbarStatus ? impl_->callbacks.taskbarStatus() :
            L"Desktop status is unavailable in this session.";
        ::SetWindowTextW(impl_->controls.at(kDesktopStatus), status.c_str());
    } catch (...) { ::SetWindowTextW(impl_->controls.at(kDesktopStatus), L"Desktop status is unavailable."); }
    if (!impl_->building) impl_->LayoutCanvas();
}
void SettingsWindow::ReportActionStatus(std::wstring_view message) { ReportActionStatus(message, StatusTone::Info); }
void SettingsWindow::ReportActionStatus(std::wstring_view message, StatusTone tone) {
    try {
        impl_->lastActionStatus = std::wstring(message);
        impl_->lastActionTone = tone;
        impl_->Message(impl_->lastActionStatus, tone);
    } catch (...) {}
}
Result<void> SettingsWindow::ResetAfterConsent() {
    try { return impl_->ResetAfterConsent(); }
    catch (...) { return MakeError(ErrorCode::Internal, "settings reset failed"); }
}
void SettingsWindow::SetTheme(const graphics::DesignTokens& tokens, const graphics::MotionPolicy& motion) {
    try {
        // The application re-sends its theme after every settings change. Only a real change of
        // material, colors, text scale or motion restyles and lays out the open page again.
        if (impl_->window && form::SameAppearance(impl_->tokens, tokens) && impl_->motion.reduced == motion.reduced &&
            impl_->motion.durationScale == motion.durationScale) {
            return;
        }
        impl_->tokens = tokens;
        impl_->motion = motion;
        impl_->ApplyTheme();
    } catch (...) {
        AETHER_LOG_WARN("settings", "settings theme update failed");
    }
}
SettingsWindow::ContentScroll SettingsWindow::ScrollMetrics() const noexcept {
    if (!impl_->surfaceReady) return {};
    return ContentScroll{impl_->scroll.Position(), impl_->scroll.Range(), impl_->scroll.Page()};
}

}  // namespace aether::settings::ui
