// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "system_controls/SystemControls.h"

#include "platform/windows/OsVersion.h"
#include "status/WinRtSupport.h"

#include <windows.h>
#include <roapi.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <thread>
#include <utility>
#include <vector>

namespace aether::controlcenter {
namespace {

namespace radios = winrt::Windows::Devices::Radios;
using Deadline = std::chrono::steady_clock::time_point;
using RadioList = winrt::Windows::Foundation::Collections::IVectorView<radios::Radio>;
constexpr unsigned kMaxRadios = 64;

Error RadioError(HRESULT code, const char* message) {
    ErrorCode category = ErrorCode::Platform;
    if (code == E_ACCESSDENIED) category = ErrorCode::AccessDenied;
    else if (code == E_NOTIMPL || code == REGDB_E_CLASSNOTREG || code == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED))
        category = ErrorCode::Unsupported;
    else if (code == RPC_E_WRONG_THREAD || code == RPC_E_CHANGED_MODE) category = ErrorCode::InvalidState;
    else if (code == HRESULT_FROM_WIN32(ERROR_CANCELLED)) category = ErrorCode::Cancelled;
    // Never copy hresult_error::message(): Windows may include device or user-specific content.
    return MakeError(category, message, code);
}

Result<void> ValidateWorkerCall(const CancellationToken& cancellation, std::chrono::milliseconds timeout) {
    if (timeout <= std::chrono::milliseconds::zero() || timeout > std::chrono::seconds(30))
        return MakeError(ErrorCode::InvalidArgument, "System control timeout must be between 1 and 30000 milliseconds");
    if (cancellation.IsCancelled()) return MakeError(ErrorCode::Cancelled, "System control request cancelled");
    APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
    const HRESULT known = ::CoGetApartmentType(&apartment, &qualifier);
    if (SUCCEEDED(known) && apartment != APTTYPE_MTA && apartment != APTTYPE_NA)
        return MakeError(ErrorCode::InvalidState, "System control operations require a worker MTA");
    return Ok();
}

template<class T>
Result<T> Await(const winrt::Windows::Foundation::IAsyncOperation<T>& operation,
                const CancellationToken& cancellation, Deadline deadline) {
    const auto cancel = [&] { try { operation.Cancel(); } catch (...) {} };
    if (cancellation.IsCancelled()) { cancel(); return MakeError(ErrorCode::Cancelled, "Radio request cancelled"); }
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) { cancel(); return MakeError(ErrorCode::Timeout, "Radio request timed out"); }
    // Exactly one wait_for per operation: C++/WinRT installs one completion handler here.
    // Cancellation is observed before/after this bounded wait; no cancellation callback can make
    // outbound WinRT calls on the application's UI thread during shutdown.
    const auto status = operation.wait_for(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(deadline - now));
    if (cancellation.IsCancelled()) { cancel(); return MakeError(ErrorCode::Cancelled, "Radio request cancelled"); }
    if (status == winrt::Windows::Foundation::AsyncStatus::Started) {
        cancel(); return MakeError(ErrorCode::Timeout, "Radio request timed out");
    }
    if (status == winrt::Windows::Foundation::AsyncStatus::Canceled)
        return MakeError(ErrorCode::Cancelled, "Radio request cancelled");
    return operation.GetResults(); // Error status throws and is translated at the public boundary.
}

bool KnownRadioKind(RadioKind kind) noexcept { return kind == RadioKind::WiFi || kind == RadioKind::Bluetooth; }
radios::RadioKind NativeKind(RadioKind kind) noexcept {
    return kind == RadioKind::WiFi ? radios::RadioKind::WiFi : radios::RadioKind::Bluetooth;
}

RadioStatus DescribeRadios(const RadioList& list, RadioKind kind, bool granted) {
    RadioStatus result;
    bool on = false, off = false, unknown = false;
    for (const auto& radio : list) {
        if (radio.Kind() != NativeKind(kind)) continue;
        result.present = true;
        switch (radio.State()) {
            case radios::RadioState::On: on = true; break;
            case radios::RadioState::Off: off = true; break;
            case radios::RadioState::Disabled: result.hardwareDisabled = true; off = true; break;
            default: unknown = true; break;
        }
    }
    if (!result.present) {
        result.error = MakeError(ErrorCode::Unavailable, "Windows did not expose a radio of this kind");
        return result;
    }
    if (!unknown && on != off) result.enabled = on;
    result.canRequestAccess = result.enabled.has_value() && !result.hardwareDisabled;
    result.canToggle = result.canRequestAccess && granted;
    if (result.hardwareDisabled) result.error = MakeError(ErrorCode::Unavailable, "A radio is disabled by hardware or system policy");
    else if (!result.enabled) result.error = MakeError(ErrorCode::Unavailable, "Radio states are mixed or unknown; use Windows settings");
    return result;
}

Result<SystemAction> Settings(const wchar_t* uri) {
    SystemAction result; result.settingsUri = uri; return result;
}
Result<SystemAction> Shortcut(SystemShortcut shortcut, unsigned minimumBuild = 0) {
    SystemAction result; result.kind = SystemActionKind::KeyboardShortcut;
    result.shortcut = shortcut; result.minimumWindowsBuild = minimumBuild; return result;
}

} // namespace

Result<void> BeginRadioAccessRequest(std::function<void(Result<bool>)> completion) {
    if (!completion) return MakeError(ErrorCode::InvalidArgument, "Radio permission request needs a completion callback");
    APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
    const HRESULT known = ::CoGetApartmentType(&apartment, &qualifier);
    if (FAILED(known) || (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA))
        return MakeError(ErrorCode::InvalidState, "Radio permission must be requested from the UI STA after a user click");
    const HRESULT runtime = ::RoInitialize(RO_INIT_SINGLETHREADED);
    if (FAILED(runtime)) return RadioError(runtime, "Windows radio permission is unavailable");
    struct RuntimeScope { ~RuntimeScope() { ::RoUninitialize(); } } runtimeScope;
    try {
        auto operation = radios::Radio::RequestAccessAsync();
        operation.Completed([completion = std::move(completion)](const auto& finished,
            winrt::Windows::Foundation::AsyncStatus) noexcept {
            try {
                const bool granted = finished.GetResults() == radios::RadioAccessStatus::Allowed;
                try { completion(granted); } catch (...) {}
            } catch (const winrt::hresult_error& error) {
                try { completion(RadioError(error.code(), "Windows radio permission request failed")); } catch (...) {}
            } catch (...) {
                try { completion(MakeError(ErrorCode::Internal, "Radio permission completion failed")); } catch (...) {}
            }
        });
        return Ok();
    } catch (const winrt::hresult_error& error) {
        return RadioError(error.code(), "Windows radio permission could not be requested");
    } catch (...) { return MakeError(ErrorCode::Internal, "Radio permission request failed"); }
}

Result<SystemControlsSnapshot> QuerySystemControls(bool grantedAccess, CancellationToken cancellation,
                                                   std::chrono::milliseconds timeout) {
    try {
        if (const auto checked = ValidateWorkerCall(cancellation, timeout); !checked) return checked.error();
        SystemControlsSnapshot result;
        result.windowsBuild = platform::GetOsVersion().build;
        SYSTEM_POWER_STATUS power{};
        if (::GetSystemPowerStatus(&power)) {
            if (power.SystemStatusFlag <= 1) result.energySaver = power.SystemStatusFlag == 1;
            else result.powerError = MakeError(ErrorCode::Unavailable, "Windows power-saving state is unknown");
        } else result.powerError = MakeError(ErrorCode::Platform, "Windows power-saving state could not be read", ::GetLastError());
        STICKYKEYS keys{sizeof(keys), 0};
        if (::SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(keys), &keys, 0)) result.stickyKeys = StickyKeysState(keys.dwFlags);
        BOOL reader = FALSE;
        if (::SystemParametersInfoW(SPI_GETSCREENREADER, 0, &reader, 0)) result.screenReader = reader != FALSE;

        status::WinRtApartmentScope apartment;
        if (!apartment.IsUsable()) {
            const auto error = RadioError(apartment.Status(), "Windows radio enumeration is unavailable");
            result.wifi.error = error; result.bluetooth.error = error;
            return result;
        }
        try {
            auto list = Await(radios::Radio::GetRadiosAsync(), cancellation, std::chrono::steady_clock::now() + timeout);
            if (!list) {
                if (list.error().code == ErrorCode::Cancelled) return list.error();
                result.wifi.error = list.error(); result.bluetooth.error = list.error();
                return result;
            }
            if (list->Size() > kMaxRadios) {
                const auto error = MakeError(ErrorCode::Unsupported, "Windows exposed too many radios for this control");
                result.wifi.error = error; result.bluetooth.error = error;
                return result;
            }
            const auto read = [&](RadioKind kind, RadioStatus& output) {
                try { output = DescribeRadios(*list, kind, grantedAccess); }
                catch (const winrt::hresult_error& error) { output.error = RadioError(error.code(), "Windows radio state could not be read"); }
            };
            read(RadioKind::WiFi, result.wifi); read(RadioKind::Bluetooth, result.bluetooth);
        } catch (const winrt::hresult_error& error) {
            const auto detail = RadioError(error.code(), "Windows radio enumeration failed");
            result.wifi.error = detail; result.bluetooth.error = detail;
        }
        return result;
    } catch (...) { return MakeError(ErrorCode::Internal, "System controls could not be read"); }
}

Result<void> SetRadioEnabled(RadioKind kind, bool enabled, bool grantedAccess,
                             CancellationToken cancellation, std::chrono::milliseconds timeout) {
    try {
        if (!KnownRadioKind(kind)) return MakeError(ErrorCode::InvalidArgument, "Unknown radio kind");
        if (!grantedAccess) return MakeError(ErrorCode::AccessDenied, "Radio control permission has not been granted; use Windows settings");
        if (const auto checked = ValidateWorkerCall(cancellation, timeout); !checked) return checked;
        status::WinRtApartmentScope apartment;
        if (!apartment.IsUsable()) return RadioError(apartment.Status(), "Windows radio control is unavailable");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        auto list = Await(radios::Radio::GetRadiosAsync(), cancellation, deadline);
        if (!list) return list.error();
        if (list->Size() > kMaxRadios) return MakeError(ErrorCode::Unsupported, "Windows exposed too many radios for this control");
        const auto reading = DescribeRadios(*list, kind, true);
        if (!reading.present) return MakeError(ErrorCode::Unavailable, "No controllable radio is available");
        if (!reading.canToggle) return reading.error.value_or(MakeError(ErrorCode::Unavailable, "Radio state cannot be changed"));
        if (reading.enabled == enabled) return Ok(); // already effective: do not submit a redundant state change
        std::vector<radios::Radio> selected;
        for (const auto& radio : *list) if (radio.Kind() == NativeKind(kind)) selected.push_back(radio);
        const auto target = enabled ? radios::RadioState::On : radios::RadioState::Off;
        for (const auto& radio : selected) {
            // Check before invoking the mutator: cancelling an already queued operation cannot
            // guarantee that Windows did not apply it.
            if (cancellation.IsCancelled()) return MakeError(ErrorCode::Cancelled, "Radio request cancelled; refresh the current state");
            if (std::chrono::steady_clock::now() >= deadline)
                return MakeError(ErrorCode::Timeout, "Radio request timed out before the next change; refresh the current state");
            auto changed = Await(radio.SetStateAsync(target), cancellation, deadline);
            if (!changed) return changed.error();
            if (*changed != radios::RadioAccessStatus::Allowed)
                return MakeError(ErrorCode::AccessDenied, "Windows denied a radio state change; refresh the current state or use Windows settings");
        }
        for (;;) {
            if (cancellation.IsCancelled()) return MakeError(ErrorCode::Cancelled, "Radio request cancelled; refresh the current state");
            if (std::all_of(selected.begin(), selected.end(), [&](const auto& radio) { return radio.State() == target; })) return Ok();
            if (std::chrono::steady_clock::now() >= deadline)
                return MakeError(ErrorCode::Timeout, "Radio request was not confirmed; refresh the current state or use Windows settings");
            std::this_thread::sleep_for(std::chrono::milliseconds(25)); // worker only, bounded by the shared deadline
        }
    } catch (const winrt::hresult_error& error) {
        return RadioError(error.code(), "Windows radio state change failed; refresh the current state or use Windows settings");
    } catch (...) { return MakeError(ErrorCode::Internal, "Radio state change failed; refresh the current state"); }
}

std::optional<bool> StickyKeysState(unsigned long flags) noexcept {
    if (!(flags & SKF_AVAILABLE)) return std::nullopt;
    return (flags & SKF_STICKYKEYSON) != 0;
}

Result<void> SetStickyKeys(bool enabled) {
    try {
        STICKYKEYS keys{sizeof(keys), 0};
        if (!::SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(keys), &keys, 0))
            return MakeError(ErrorCode::Platform, "Sticky Keys state could not be read", ::GetLastError());
        if (!StickyKeysState(keys.dwFlags)) return MakeError(ErrorCode::Unsupported, "Sticky Keys is not available on this system");
        if (*StickyKeysState(keys.dwFlags) == enabled) return Ok();
        keys.dwFlags = enabled ? (keys.dwFlags | SKF_STICKYKEYSON) : (keys.dwFlags & ~static_cast<DWORD>(SKF_STICKYKEYSON));
        if (!::SystemParametersInfoW(SPI_SETSTICKYKEYS, sizeof(keys), &keys, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE))
            return MakeError(ErrorCode::Platform, "Windows did not change Sticky Keys", ::GetLastError());
        STICKYKEYS check{sizeof(check), 0};
        if (!::SystemParametersInfoW(SPI_GETSTICKYKEYS, sizeof(check), &check, 0) || StickyKeysState(check.dwFlags) != enabled)
            return MakeError(ErrorCode::Unavailable, "Sticky Keys did not take the requested state; refresh the current state");
        return Ok();
    } catch (...) { return MakeError(ErrorCode::Internal, "Sticky Keys change failed"); }
}

Result<SystemAction> ActionFor(AccessibilityFeature feature) {
    switch (feature) {
        case AccessibilityFeature::Magnifier: return Shortcut(SystemShortcut::Magnifier);
        case AccessibilityFeature::Narrator: return Shortcut(SystemShortcut::Narrator);
        case AccessibilityFeature::OnScreenKeyboard: return Shortcut(SystemShortcut::OnScreenKeyboard);
        case AccessibilityFeature::LiveCaptions: return Shortcut(SystemShortcut::LiveCaptions, 22621);
        case AccessibilityFeature::Settings: return Settings(L"ms-settings:easeofaccess");
    }
    return MakeError(ErrorCode::InvalidArgument, "Unknown accessibility feature");
}

// https://support.microsoft.com/accessibility/windows/keyboard-shortcuts-in-windows
Result<SystemAction> NotificationsAction(unsigned windowsBuild) {
    if (windowsBuild >= 22000) return Shortcut(SystemShortcut::NotificationCenter, 22000);
    if (windowsBuild >= 10240) return Shortcut(SystemShortcut::ActionCenter, 10240);
    return MakeError(ErrorCode::Unsupported, "Windows notifications need Windows 10 or later");
}

Result<SystemAction> CalendarAction(unsigned windowsBuild) {
    if (windowsBuild >= 10240) return Shortcut(SystemShortcut::Calendar, 10240);
    return MakeError(ErrorCode::Unsupported, "the Windows calendar flyout needs Windows 10 or later");
}

Result<SystemAction> ActionFor(SystemControl control) {
    try {
        // Official fixed routes only. No undocumented registry, COM radio-manager or private shell API.
        // https://learn.microsoft.com/windows/apps/develop/launch/launch-settings
        switch (control) {
            case SystemControl::WiFi: return Settings(L"ms-settings:network-wifi");
            case SystemControl::Bluetooth: return Settings(L"ms-settings:bluetooth");
            case SystemControl::FlightMode: return Settings(L"ms-settings:network-airplanemode");
            case SystemControl::EnergySaver: return Settings(L"ms-settings:batterysaver");
            case SystemControl::MobileHotspot: return Settings(L"ms-settings:network-mobilehotspot");
            case SystemControl::NearbySharing: return Settings(L"ms-settings:crossdevice"); // documented Shared experiences page
            case SystemControl::NightLight: return Settings(L"ms-settings:nightlight");
            case SystemControl::Accessibility: return Shortcut(SystemShortcut::Accessibility);
            case SystemControl::LiveCaptions: return Shortcut(SystemShortcut::LiveCaptions, 22621);
            case SystemControl::Cast: return Shortcut(SystemShortcut::Cast);
            case SystemControl::Project: return Shortcut(SystemShortcut::Project);
            case SystemControl::Settings: return Settings(L"ms-settings:");
        }
        return MakeError(ErrorCode::InvalidArgument, "Unknown system control");
    } catch (...) { return MakeError(ErrorCode::Internal, "System control route could not be prepared"); }
}

Result<TileKind> TileKindFor(SystemControl control, const SystemControlsSnapshot& snapshot) {
    switch (control) {
        case SystemControl::WiFi: return snapshot.wifi.canRequestAccess ? TileKind::Toggle : TileKind::Action;
        case SystemControl::Bluetooth: return snapshot.bluetooth.canRequestAccess ? TileKind::Toggle : TileKind::Action;
        case SystemControl::LiveCaptions: return snapshot.windowsBuild >= 22621 ? TileKind::Action : TileKind::Unsupported;
        case SystemControl::FlightMode: case SystemControl::EnergySaver: case SystemControl::MobileHotspot:
        case SystemControl::NearbySharing: case SystemControl::NightLight: case SystemControl::Accessibility:
        case SystemControl::Cast: case SystemControl::Project: case SystemControl::Settings: return TileKind::Action;
    }
    return MakeError(ErrorCode::InvalidArgument, "Unknown system control");
}

} // namespace aether::controlcenter
