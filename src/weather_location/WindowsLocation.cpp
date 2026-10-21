// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "weather_location/WindowsLocation.h"

#include <windows.h>
#include <roapi.h>
#include <winrt/Windows.Devices.Geolocation.h>
#include <winrt/Windows.Foundation.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace aether::weather {
namespace {
using namespace std::chrono_literals;
namespace geo = winrt::Windows::Devices::Geolocation;

Error LocationError(HRESULT code, const char* message) {
    ErrorCode category = ErrorCode::Unavailable;
    if (code == E_ACCESSDENIED) category = ErrorCode::AccessDenied;
    else if (code == E_ABORT || code == HRESULT_FROM_WIN32(ERROR_CANCELLED)) category = ErrorCode::Cancelled;
    else if (code == HRESULT_FROM_WIN32(ERROR_TIMEOUT)) category = ErrorCode::Timeout;
    else if (code == REGDB_E_CLASSNOTREG || code == HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)) category = ErrorCode::Unsupported;
    // Windows exception messages can include private content. Expose fixed text and numeric status.
    return MakeError(category, message, code);
}
struct RuntimeScope {
    explicit RuntimeScope(RO_INIT_TYPE type) : result(::RoInitialize(type)) {}
    ~RuntimeScope() { if (SUCCEEDED(result)) ::RoUninitialize(); }
    HRESULT result;
};
struct WaitSignal {
    HANDLE handle = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    ~WaitSignal() { if (handle) ::CloseHandle(handle); }
};

Result<geo::Geoposition> Await(const winrt::Windows::Foundation::IAsyncOperation<geo::Geoposition>& operation,
    const CancellationToken& cancellation, std::chrono::steady_clock::time_point deadline) {
    const auto cancel = [&] { try { operation.Cancel(); } catch (...) {} };
    auto signal = std::make_shared<WaitSignal>();
    if (!signal->handle) { const DWORD error=::GetLastError();cancel(); return LocationError(HRESULT_FROM_WIN32(error), "Location wait could not be created"); }
    // A late completion owns its event; it never captures stack data, the host or a provider.
    operation.Completed([signal](const auto&, auto) noexcept { (void)::SetEvent(signal->handle); });
    for (;;) {
        if (cancellation.IsCancelled()) { cancel(); return MakeError(ErrorCode::Cancelled, "Location request cancelled"); }
        const auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining <= std::chrono::steady_clock::duration::zero()) {
            cancel(); return MakeError(ErrorCode::Timeout, "Location request timed out");
        }
        const auto delay = std::clamp<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count(), 1, 25);
        const auto waited = ::WaitForSingleObject(signal->handle, static_cast<DWORD>(delay));
        if (waited == WAIT_OBJECT_0) {
            if (cancellation.IsCancelled()) { cancel(); return MakeError(ErrorCode::Cancelled, "Location request cancelled"); }
            return operation.GetResults();
        }
        if (waited != WAIT_TIMEOUT) { const DWORD error=::GetLastError();cancel(); return LocationError(HRESULT_FROM_WIN32(error), "Location wait failed"); }
    }
}
} // namespace

Result<WindowsLocation> CoarsenWindowsLocation(double latitude, double longitude) {
    if (!std::isfinite(latitude) || !std::isfinite(longitude) || latitude < -90 || latitude > 90 || longitude < -180 || longitude > 180)
        return MakeError(ErrorCode::InvalidArgument, "Windows location coordinates are invalid");
    WindowsLocation location;
    location.latitude = std::round(latitude * 10) / 10;
    location.longitude = std::round(longitude * 10) / 10;
    if (location.latitude == 0) location.latitude = 0; // normalize negative zero without changing valid equatorial readings
    if (location.longitude == 0) location.longitude = 0;
    return location; // Geoposition supplies no reliable country; never infer one from coordinates.
}
bool IsWindowsLocationSampleFresh(std::chrono::milliseconds age) noexcept { return age >= 0ms && age <= 30min; }

Result<void> BeginWindowsLocationAccess(std::function<void(Result<bool>)> completion) {
    if (!completion) return MakeError(ErrorCode::InvalidArgument, "Location permission needs a completion callback");
    try {
        APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
        const HRESULT known = ::CoGetApartmentType(&apartment, &qualifier);
        if (FAILED(known) || (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA))
            return MakeError(ErrorCode::InvalidState, "Location permission requires the foreground UI STA");
        DWORD process = 0;
        const DWORD thread = ::GetWindowThreadProcessId(::GetForegroundWindow(), &process);
        if (process != ::GetCurrentProcessId() || thread != ::GetCurrentThreadId())
            return MakeError(ErrorCode::InvalidState, "Location permission requires this application's foreground window");
        RuntimeScope runtime(RO_INIT_SINGLETHREADED);
        if (FAILED(runtime.result)) return LocationError(runtime.result, "Windows location permission is unavailable");
        auto operation = geo::Geolocator::RequestAccessAsync();
        operation.Completed([completion = std::move(completion)](const auto& finished, auto) noexcept {
            try { const bool allowed = finished.GetResults() == geo::GeolocationAccessStatus::Allowed;
                try { completion(allowed); } catch (...) {}
            } catch (const winrt::hresult_error& error) {
                try { completion(LocationError(error.code(), "Windows location permission failed")); } catch (...) {}
            } catch (...) {
                try { completion(MakeError(ErrorCode::Internal, "Windows location permission failed")); } catch (...) {}
            }
        });
        return Ok();
    } catch (const winrt::hresult_error& error) { return LocationError(error.code(), "Windows location permission could not be requested"); }
    catch (...) { return MakeError(ErrorCode::Internal, "Windows location permission could not be requested"); }
}

Result<WindowsLocation> ReadWindowsLocation(bool appConsent, CancellationToken cancellation, std::chrono::milliseconds timeout) {
    try {
        // Every safe unit case returns here, before COM initialization or location discovery.
        if (timeout <= 0ms || timeout > 30s) return MakeError(ErrorCode::InvalidArgument, "Location timeout must be between 1 and 30000 milliseconds");
        if (cancellation.IsCancelled()) return MakeError(ErrorCode::Cancelled, "Location request cancelled");
        if (!appConsent) return MakeError(ErrorCode::AccessDenied, "Location consent has not been given in this application");
        APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
        if (SUCCEEDED(::CoGetApartmentType(&apartment, &qualifier)) && apartment != APTTYPE_MTA && apartment != APTTYPE_NA)
            return MakeError(ErrorCode::InvalidState, "Location reads require a worker MTA");
        RuntimeScope runtime(RO_INIT_MULTITHREADED);
        if (FAILED(runtime.result)) return LocationError(runtime.result, "Windows location is unavailable");
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        geo::Geolocator locator;
        // A saved app choice is never an OS grant. Read current status, and let the documented
        // read API reject any changed privacy permission. RequestAccessAsync is UI-click-only.
        const auto status=locator.LocationStatus();
        if(status==geo::PositionStatus::Disabled)return MakeError(ErrorCode::AccessDenied,"Windows location is disabled or denied");
        if(status==geo::PositionStatus::NotAvailable)return MakeError(ErrorCode::Unsupported,"Windows location is not available on this device");
        locator.DesiredAccuracyInMeters(winrt::box_value(std::uint32_t{10000}).as<winrt::Windows::Foundation::IReference<std::uint32_t>>());
        if (cancellation.IsCancelled()) return MakeError(ErrorCode::Cancelled, "Location request cancelled");
        auto operation = locator.GetGeopositionAsync(30min, timeout);
        auto position = Await(operation, cancellation, deadline);
        if (!position) return position.error();
        const auto coordinate = position->Coordinate();
        const auto timestamp = coordinate.Timestamp();
        const auto now = winrt::clock::now();
        if (timestamp <= winrt::Windows::Foundation::DateTime{} || timestamp > now ||
            !IsWindowsLocationSampleFresh(std::chrono::ceil<std::chrono::milliseconds>(now - timestamp)))
            return MakeError(ErrorCode::Unavailable, "Windows location is stale or has an invalid timestamp");
        const auto point = coordinate.Point().Position();
        auto location=CoarsenWindowsLocation(point.Latitude, point.Longitude);
        if(!location)return MakeError(ErrorCode::Unavailable,"Windows location coordinates are unavailable");
        return location;
    } catch (const winrt::hresult_error& error) { return LocationError(error.code(), "Windows location could not be read; review location privacy settings"); }
    catch (...) { return MakeError(ErrorCode::Internal, "Windows location could not be read"); }
}

} // namespace aether::weather
