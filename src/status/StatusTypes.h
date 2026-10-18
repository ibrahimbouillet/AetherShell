// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Immutable status snapshots published by the system status providers and
// consumed by the Top Status Bar. Every field that the platform cannot report
// reliably is optional or carries an explicit state — unknown values are never
// presented as zero or invented.
#pragma once

#include "core/Result.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace aether::status {

// Availability of a provider's information.
enum class ProviderState {
    Initializing,  // no reading yet
    Available,     // reading is current
    Unavailable,   // the hardware/service is absent or stopped (e.g. no battery, audio service down)
    Unsupported,   // this Windows version or configuration cannot provide the information
    Error,         // the provider failed; see `detail`
};

// Contract shared by all status providers.
//  * Start() is called on the UI thread; it must not block on slow work.
//  * The listener receives an initial snapshot as soon as one is available, then one per change.
//  * The listener may be invoked on any thread, never concurrently with itself, and never after
//    Stop() has returned. Consumers marshal to the UI thread with IUiDispatcher.
//  * Stop() is idempotent, noexcept, and releases every resource (threads, COM objects, events).
template <class Snapshot>
class IStatusProvider {
public:
    using Listener = std::function<void(const Snapshot&)>;
    virtual ~IStatusProvider() = default;
    virtual Result<void> Start(Listener listener) = 0;
    virtual void Stop() noexcept = 0;
    // Thread-safe copy of the most recent snapshot.
    [[nodiscard]] virtual Snapshot Current() const = 0;
};

// ---------------------------------------------------------------------------------------------
// Clock
// ---------------------------------------------------------------------------------------------
struct ClockSnapshot {
    ProviderState state = ProviderState::Initializing;
    std::chrono::system_clock::time_point utc{};
    // Local wall-clock time as reported by Windows for the current time zone (GetLocalTime).
    std::int32_t year = 0, month = 0, day = 0, dayOfWeek = 0, hour = 0, minute = 0, second = 0;
    std::wstring timeZoneName;  // e.g. "Romance Daylight Time"
    bool daylightSavingActive = false;
};

// ---------------------------------------------------------------------------------------------
// Battery and power
// ---------------------------------------------------------------------------------------------
enum class ChargeState { Unknown, Charging, Discharging, Full, NotCharging };
enum class PowerSource { Unknown, External, Battery };

struct BatterySnapshot {
    ProviderState state = ProviderState::Initializing;  // Unavailable on PCs without a battery
    bool batteryPresent = false;
    std::optional<int> percent;                 // 0..100 when known
    ChargeState charge = ChargeState::Unknown;
    PowerSource source = PowerSource::Unknown;
    std::optional<std::chrono::seconds> remaining;  // estimated time left on battery, when known
    bool energySaverOn = false;
    std::string detail;
};

// ---------------------------------------------------------------------------------------------
// Keyboard / input language
// ---------------------------------------------------------------------------------------------
struct KeyboardSnapshot {
    ProviderState state = ProviderState::Initializing;
    std::wstring localeName;    // BCP-47 tag of the active input language, e.g. "fr-FR"
    std::wstring shortLabel;    // compact label for the bar, e.g. "FR"
    std::wstring languageName;  // localized display name, e.g. "French (France)"
    std::wstring layoutName;    // keyboard layout name when available, e.g. "French"
    // True when the value was read from the foreground window's thread (the language the user is
    // typing in). False when it fell back to Aether Shell's own thread.
    bool fromForegroundWindow = false;
    std::string detail;
};

// ---------------------------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------------------------
enum class ConnectionKind { Unknown, None, WiFi, Ethernet, Cellular, Other };
enum class InternetConnectivity { Unknown, None, LocalOnly, Constrained, Internet };

struct NetworkSnapshot {
    ProviderState state = ProviderState::Initializing;
    ConnectionKind kind = ConnectionKind::Unknown;
    InternetConnectivity connectivity = InternetConnectivity::Unknown;
    std::optional<std::uint8_t> signalBars;  // 0..5 as reported by Windows for wireless; never estimated
    std::wstring profileName;                // only populated when Windows exposes it without extra permission
    std::string detail;
};

// ---------------------------------------------------------------------------------------------
// Audio output and microphone
// ---------------------------------------------------------------------------------------------
struct AudioSnapshot {
    ProviderState state = ProviderState::Initializing;
    bool deviceAvailable = false;     // a default render endpoint exists and is active
    std::optional<float> volume;      // master volume scalar 0..1
    std::optional<bool> muted;
    std::wstring deviceName;
    std::string detail;
};

enum class Support { Unknown, Supported, Unsupported };

struct MicrophoneSnapshot {
    ProviderState state = ProviderState::Initializing;
    bool deviceAvailable = false;       // a default capture endpoint exists and is active
    std::optional<bool> muted;          // endpoint mute
    std::optional<float> inputVolume;   // endpoint input volume scalar 0..1 (gain, not live level)
    std::wstring deviceName;
    // Whether "an app is currently recording" can be reported. Windows does not expose a documented,
    // reliable API for this on all supported versions, so providers report Unsupported.
    Support inUseDetection = Support::Unsupported;
    std::string detail;
};

// ---------------------------------------------------------------------------------------------
// Weather (optional, network based)
// ---------------------------------------------------------------------------------------------
enum class WeatherState {
    Disabled,              // the user has not enabled weather
    LocationNotConfigured, // enabled, but no location chosen
    Loading,               // first request in flight
    Available,             // fresh data
    Cached,                // last good data is shown because a refresh failed or is pending
    Offline,               // no connectivity and no usable cache
    ProviderUnavailable,   // provider returned an error status (5xx, rate limit)
    RequestFailed,         // request failed for another reason (timeout, invalid response)
    Unsupported,
};

enum class TemperatureUnit { Celsius, Fahrenheit };

struct WeatherSnapshot {
    WeatherState state = WeatherState::Disabled;
    std::optional<double> temperature;  // in `unit`
    TemperatureUnit unit = TemperatureUnit::Celsius;
    std::optional<int> conditionCode;   // provider-neutral WMO weather interpretation code
    std::wstring conditionText;         // e.g. "Partly cloudy"
    std::wstring locationName;
    std::optional<std::chrono::system_clock::time_point> observedAt;  // provider observation time
    std::optional<std::chrono::system_clock::time_point> fetchedAt;   // when Aether Shell received it
    std::wstring attribution;           // required provider attribution text
    std::string detail;
};

}  // namespace aether::status
