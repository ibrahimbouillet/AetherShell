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
// The overflow windows Explorer creates: Windows 11's XAML island and Windows 10's tray window.
constexpr std::array<std::wstring_view, 2> kOverflowClasses{L"TopLevelWindowForOverflowXamlIsland", L"NotifyIconOverflowWindow"};
// Windows 11's system tray draws its overflow button with this view type; UI Automation reports a
// XAML element's runtime type as its class name, independent of the display language.
constexpr std::wstring_view kChevronView = L"SystemTray.ChevronIconView";

std::wstring ClassOf(HWND window) {
    wchar_t name[96]{};
    const int length = ::GetClassNameW(window, name, static_cast<int>(std::size(name)));
    return std::wstring(name, length > 0 ? static_cast<std::size_t>(length) : 0);
}

std::wstring ClassOf(IUIAutomationElement& element) {
    BSTR value = nullptr;
    if (FAILED(element.get_CurrentClassName(&value))) return {};
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
    bool TaskbarVisible() override {
        const HWND taskbar = ::FindWindowW(kTaskbarClass, nullptr);
        return taskbar && ::IsWindowVisible(taskbar);
    }

    Result<void> InvokeOverflowButton() override {
        const HWND taskbar = ::FindWindowW(kTaskbarClass, nullptr);
        if (!taskbar || !::IsWindowVisible(taskbar)) return MakeError(ErrorCode::Unavailable, "the Windows taskbar is not shown");
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
    // Windows 11: the system tray's overflow view, or its nearest actionable ancestor (the button
    // hosting it), inside this taskbar only.
    static winrt::com_ptr<IUIAutomationElement> Windows11Button(IUIAutomation& automation, HWND taskbar) {
        winrt::com_ptr<IUIAutomationElement> root;
        if (FAILED(automation.ElementFromHandle(taskbar, root.put())) || !root) return nullptr;
        VARIANT name{};
        name.vt = VT_BSTR;
        name.bstrVal = ::SysAllocStringLen(kChevronView.data(), static_cast<UINT>(kChevronView.size()));
        if (!name.bstrVal) return nullptr;
        winrt::com_ptr<IUIAutomationCondition> condition;
        const HRESULT created = automation.CreatePropertyCondition(UIA_ClassNamePropertyId, name, condition.put());
        ::VariantClear(&name);
        if (FAILED(created) || !condition) return nullptr;
        winrt::com_ptr<IUIAutomationElement> view;
        if (FAILED(root->FindFirst(TreeScope_Descendants, condition.get(), view.put())) || !view) return nullptr;
        winrt::com_ptr<IUIAutomationTreeWalker> walker;
        if (FAILED(automation.get_ControlViewWalker(walker.put())) || !walker) return nullptr;
        winrt::com_ptr<IUIAutomationElement> current = view;
        for (int depth = 0; current && depth < 4; ++depth) {
            if (Actionable(*current)) return current;
            winrt::com_ptr<IUIAutomationElement> parent;
            if (FAILED(walker->GetParentElement(current.get(), parent.put()))) return nullptr;
            current = parent;
        }
        return nullptr;
    }

    // Windows 10: the Win32 chevron button inside the notification area.
    static winrt::com_ptr<IUIAutomationElement> Windows10Button(IUIAutomation& automation, HWND taskbar) {
        const HWND notify = ::FindWindowExW(taskbar, nullptr, L"TrayNotifyWnd", nullptr);
        const HWND chevron = notify ? ::FindWindowExW(notify, nullptr, L"Button", nullptr) : nullptr;
        if (!chevron || !::IsWindowVisible(chevron)) return nullptr;
        winrt::com_ptr<IUIAutomationElement> element;
        if (FAILED(automation.ElementFromHandle(chevron, element.put())) || !element) return nullptr;
        return Actionable(*element) ? element : nullptr;
    }
};

}  // namespace

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
        if (!tray.TaskbarVisible()) {
            wait(timing.interval);  // Explorer is still bringing its taskbar back
            continue;
        }
        auto invoked = tray.InvokeOverflowButton();
        if (!invoked) {
            if (invoked.error().code != ErrorCode::NotFound && invoked.error().code != ErrorCode::Unavailable) return invoked.error();
            last = invoked.error();  // the taskbar's content may still be populating
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
    return MakeError(ErrorCode::Timeout, "the Windows taskbar did not become available");
}

bool OverflowNeedsReveal(const TaskbarUpdate& state) noexcept { return state.active && !state.suspended; }

std::unique_ptr<ITrayOverflow> MakeSystemTrayOverflow() { return std::make_unique<SystemTrayOverflow>(); }

}  // namespace aether::app
