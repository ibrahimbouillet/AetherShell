// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "app/HiddenIcons.h"

#include <windows.h>
#include <dwmapi.h>
#include <Unknwn.h>
#include <UIAutomation.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace aether::app {
namespace {

constexpr wchar_t kTaskbarClass[] = L"Shell_TrayWnd";
// Windows 11 hosts the taskbar's content (system tray included) in this XAML island child window.
constexpr wchar_t kContentBridgeClass[] = L"Windows.UI.Composition.DesktopWindowContentBridge";
// The automation id every Windows 11 system tray button shares.
constexpr wchar_t kTrayButtonId[] = L"SystemTrayIcon";
// The overflow windows Explorer creates: Windows 11's XAML island and Windows 10's tray window.
constexpr std::array<std::wstring_view, 2> kOverflowClasses{L"TopLevelWindowForOverflowXamlIsland", L"NotifyIconOverflowWindow"};

std::wstring ClassOf(HWND window) {
    wchar_t name[96]{};
    const int length = ::GetClassNameW(window, name, static_cast<int>(std::size(name)));
    return std::wstring(name, length > 0 ? static_cast<std::size_t>(length) : 0);
}

std::wstring NameOf(IUIAutomationElement& element) {
    BSTR value = nullptr;
    if (FAILED(element.get_CurrentName(&value))) return {};
    std::wstring result(value ? value : L"");
    ::SysFreeString(value);
    return result;
}

// Invoke, else Toggle, else Expand: whichever supported action the button offers.
Result<void> Activate(IUIAutomationElement& element) {
    winrt::com_ptr<IUIAutomationInvokePattern> invoke;
    if (SUCCEEDED(element.GetCurrentPatternAs(UIA_InvokePatternId, __uuidof(IUIAutomationInvokePattern), invoke.put_void())) && invoke)
        return SUCCEEDED(invoke->Invoke()) ? Ok() : Result<void>(MakeError(ErrorCode::Platform, "the hidden-icons button did not accept Invoke"));
    winrt::com_ptr<IUIAutomationTogglePattern> toggle;
    if (SUCCEEDED(element.GetCurrentPatternAs(UIA_TogglePatternId, __uuidof(IUIAutomationTogglePattern), toggle.put_void())) && toggle)
        return SUCCEEDED(toggle->Toggle()) ? Ok() : Result<void>(MakeError(ErrorCode::Platform, "the hidden-icons button did not accept Toggle"));
    winrt::com_ptr<IUIAutomationExpandCollapsePattern> expand;
    if (SUCCEEDED(element.GetCurrentPatternAs(UIA_ExpandCollapsePatternId, __uuidof(IUIAutomationExpandCollapsePattern), expand.put_void())) && expand)
        return SUCCEEDED(expand->Expand()) ? Ok() : Result<void>(MakeError(ErrorCode::Platform, "the hidden-icons button did not expand"));
    return MakeError(ErrorCode::Unsupported, "the hidden-icons button offers no supported action");
}

bool Actionable(IUIAutomationElement& element) {
    for (const PROPERTYID pattern : {UIA_IsInvokePatternAvailablePropertyId, UIA_IsTogglePatternAvailablePropertyId,
                                     UIA_IsExpandCollapsePatternAvailablePropertyId}) {
        VARIANT value{};
        const bool available = SUCCEEDED(element.GetCurrentPropertyValue(pattern, &value)) && value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE;
        ::VariantClear(&value);
        if (available) return true;
    }
    return false;
}

class SystemTrayOverflow final : public ITrayOverflow {
public:
    // The taskbar may be hidden (Aether's replacement): its content stays reachable through UI
    // Automation and Explorer shows the overflow as a window of its own.
    Result<void> InvokeOverflowButton() override {
        const HWND taskbar = ::FindWindowW(kTaskbarClass, nullptr);
        if (!taskbar) return MakeError(ErrorCode::Unavailable, "the Windows taskbar is unavailable");
        winrt::com_ptr<IUIAutomation> automation;
        if (FAILED(::CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), automation.put_void())) || !automation)
            return MakeError(ErrorCode::Platform, "UI Automation is unavailable");
        winrt::com_ptr<IUIAutomationElement> button = Windows11Button(*automation, taskbar);
        if (!button) button = Windows10Button(*automation, taskbar);
        if (!button) return MakeError(ErrorCode::NotFound, "the taskbar shows no hidden-icons button");
        return Activate(*button);
    }

    bool OverflowOpen() override {
        const HWND taskbar = ::FindWindowW(kTaskbarClass, nullptr);
        DWORD explorer = 0;
        if (!taskbar || !::GetWindowThreadProcessId(taskbar, &explorer) || !explorer) return false;
        struct Search { DWORD explorer; bool found = false; } search{explorer};
        ::EnumWindows([](HWND window, LPARAM parameter) -> BOOL {
            auto& state = *reinterpret_cast<Search*>(parameter);
            DWORD process = 0;
            if (!::IsWindowVisible(window) || !::GetWindowThreadProcessId(window, &process) || process != state.explorer) return TRUE;
            const auto name = ClassOf(window);
            if (std::find(kOverflowClasses.begin(), kOverflowClasses.end(), name) == kOverflowClasses.end()) return TRUE;
            // A closed XAML overflow can stay "visible" but cloaked or empty; only a shown one counts.
            BOOL cloaked = FALSE;
            RECT bounds{};
            if ((SUCCEEDED(::DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) ||
                !::GetWindowRect(window, &bounds) || ::IsRectEmpty(&bounds))
                return TRUE;
            state.found = true;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.found;
    }

private:
    // Windows 11: among the system tray buttons (one shared automation id) inside this taskbar's
    // content, the actionable one whose text element shows a chevron glyph.
    static winrt::com_ptr<IUIAutomationElement> Windows11Button(IUIAutomation& automation, HWND taskbar) {
        const HWND bridge = ::FindWindowExW(taskbar, nullptr, kContentBridgeClass, nullptr);
        winrt::com_ptr<IUIAutomationElement> root;
        if (FAILED(automation.ElementFromHandle(bridge ? bridge : taskbar, root.put())) || !root) return nullptr;
        VARIANT id{};
        id.vt = VT_BSTR;
        id.bstrVal = ::SysAllocString(kTrayButtonId);
        if (!id.bstrVal) return nullptr;
        winrt::com_ptr<IUIAutomationCondition> condition;
        const HRESULT created = automation.CreatePropertyCondition(UIA_AutomationIdPropertyId, id, condition.put());
        ::VariantClear(&id);
        if (FAILED(created) || !condition) return nullptr;
        winrt::com_ptr<IUIAutomationElementArray> buttons;
        if (FAILED(root->FindAll(TreeScope_Descendants, condition.get(), buttons.put())) || !buttons) return nullptr;
        winrt::com_ptr<IUIAutomationTreeWalker> walker;
        if (FAILED(automation.get_RawViewWalker(walker.put())) || !walker) return nullptr;
        int count = 0;
        if (FAILED(buttons->get_Length(&count))) return nullptr;
        for (int index = 0; index < count; ++index) {
            winrt::com_ptr<IUIAutomationElement> button;
            if (FAILED(buttons->GetElement(index, button.put())) || !button) continue;
            winrt::com_ptr<IUIAutomationElement> part;
            if (FAILED(walker->GetFirstChildElement(button.get(), part.put()))) continue;
            for (int guard = 0; part && guard < 8; ++guard) {
                if (IsOverflowGlyph(NameOf(*part))) return button;  // its action decides Unsupported
                winrt::com_ptr<IUIAutomationElement> next;
                if (FAILED(walker->GetNextSiblingElement(part.get(), next.put()))) break;
                part = next;
            }
        }
        return nullptr;
    }

    // Windows 10: the Win32 chevron button inside the notification area (its own visibility style:
    // the taskbar itself may be hidden).
    static winrt::com_ptr<IUIAutomationElement> Windows10Button(IUIAutomation& automation, HWND taskbar) {
        const HWND notify = ::FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr);
        const HWND chevron = notify ? ::FindWindowExW(notify, nullptr, L"Button", nullptr) : nullptr;
        if (!chevron || !(::GetWindowLongPtrW(chevron, GWL_STYLE) & WS_VISIBLE)) return nullptr;
        winrt::com_ptr<IUIAutomationElement> element;
        if (FAILED(automation.ElementFromHandle(chevron, element.put())) || !element) return nullptr;
        return Actionable(*element) ? element : nullptr;
    }
};

}  // namespace

bool IsOverflowGlyph(std::wstring_view text) noexcept {
    // ChevronDown/Up, ChevronUp/DownSmall, ChevronUp/DownMed (the taskbar's up or down overflow arrow).
    constexpr std::array<wchar_t, 6> kChevrons{L'\xE70D', L'\xE70E', L'\xE96D', L'\xE96E', L'\xE971', L'\xE972'};
    return text.size() == 1 && std::find(kChevrons.begin(), kChevrons.end(), text.front()) != kChevrons.end();
}

Result<void> OpenTrayOverflow(ITrayOverflow& tray, const CancellationToken& cancel, const OverflowTiming& timing,
                              const std::function<void(std::chrono::milliseconds)>& sleep) {
    const auto wait = [&](std::chrono::milliseconds duration) {
        if (sleep) sleep(duration);
        else std::this_thread::sleep_for(duration);
    };
    std::optional<Error> last;
    for (int attempt = 0; attempt < std::max(1, timing.attempts); ++attempt) {
        if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "hidden icons cancelled");
        if (tray.OverflowOpen()) return Ok();
        auto invoked = tray.InvokeOverflowButton();
        if (!invoked) {
            if (invoked.error().code != ErrorCode::NotFound && invoked.error().code != ErrorCode::Unavailable) return invoked.error();
            last = invoked.error();  // Explorer may still be populating (or restarting) its taskbar
            wait(timing.interval);
            continue;
        }
        // Invoked once: the overflow must now appear; never invoke again (that would close it).
        for (auto waited = std::chrono::milliseconds{0}; waited < timing.openWait; waited += timing.interval) {
            if (cancel.IsCancelled()) return MakeError(ErrorCode::Cancelled, "hidden icons cancelled");
            if (tray.OverflowOpen()) return Ok();
            wait(timing.interval);
        }
        return tray.OverflowOpen() ? Ok() : Result<void>(MakeError(ErrorCode::Unavailable, "Windows did not show the hidden icons"));
    }
    if (last) return *last;
    return MakeError(ErrorCode::Unavailable, "the Windows taskbar is unavailable");
}

std::unique_ptr<ITrayOverflow> MakeSystemTrayOverflow() { return std::make_unique<SystemTrayOverflow>(); }

}  // namespace aether::app
