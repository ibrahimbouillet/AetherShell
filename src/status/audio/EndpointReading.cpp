// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/audio/EndpointReading.h"

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace aether::status::audio {

std::string_view ToString(EndpointBinding binding) noexcept {
    switch (binding) {
        case EndpointBinding::Initializing: return "Initializing";
        case EndpointBinding::Bound: return "Bound";
        case EndpointBinding::NoDevice: return "NoDevice";
        case EndpointBinding::ServiceUnavailable: return "ServiceUnavailable";
        case EndpointBinding::Failed: return "Failed";
    }
    return "Unknown";
}

std::optional<float> SanitizeVolume(float raw) noexcept {
    if (!std::isfinite(raw)) return std::nullopt;
    return std::clamp(raw, 0.0f, 1.0f);
}

Result<float> NormalizeRequestedVolume(float requested) {
    if (!std::isfinite(requested)) {
        return MakeError(ErrorCode::InvalidArgument, "volume level must be a finite number between 0 and 1");
    }
    return std::clamp(requested, 0.0f, 1.0f);
}

void ApplyEndpointLevelReadings(EndpointReading& reading, HRESULT volumeStatus, float volume,
                               HRESULT muteStatus, bool muted, std::string_view nameDetail) {
    reading.volume = SUCCEEDED(volumeStatus) ? SanitizeVolume(volume) : std::nullopt;
    reading.muted = SUCCEEDED(muteStatus) ? std::optional<bool>(muted) : std::nullopt;
    reading.detail = nameDetail;
    const auto append = [&](HRESULT hr, std::string_view field) {
        if (SUCCEEDED(hr)) return;
        if (!reading.detail.empty()) reading.detail += "; ";
        reading.detail += std::format("{} unavailable (0x{:08X})", field, static_cast<std::uint32_t>(hr));
    };
    append(volumeStatus, "volume level");
    append(muteStatus, "mute state");
}

namespace {

// Shared by both snapshot types, which have the same leading fields.
template <class Snapshot>
void FillCommon(Snapshot& snapshot, const EndpointReading& reading) {
    snapshot.detail = reading.detail;
    switch (reading.binding) {
        case EndpointBinding::Initializing: snapshot.state = ProviderState::Initializing; break;
        case EndpointBinding::Bound:
            snapshot.state = ProviderState::Available;
            snapshot.deviceAvailable = true;
            snapshot.muted = reading.muted;
            snapshot.deviceName = reading.deviceName;
            break;
        case EndpointBinding::NoDevice:
        case EndpointBinding::ServiceUnavailable: snapshot.state = ProviderState::Unavailable; break;
        case EndpointBinding::Failed: snapshot.state = ProviderState::Error; break;
    }
}

std::optional<float> BoundVolume(const EndpointReading& reading) noexcept {
    if (reading.binding != EndpointBinding::Bound || !reading.volume) return std::nullopt;
    return SanitizeVolume(*reading.volume);
}

}  // namespace

AudioSnapshot MakeAudioSnapshot(const EndpointReading& reading) {
    AudioSnapshot snapshot;
    FillCommon(snapshot, reading);
    snapshot.volume = BoundVolume(reading);
    return snapshot;
}

MicrophoneSnapshot MakeMicrophoneSnapshot(const EndpointReading& reading) {
    MicrophoneSnapshot snapshot;
    FillCommon(snapshot, reading);
    snapshot.inputVolume = BoundVolume(reading);
    snapshot.inUseDetection = Support::Unsupported;
    return snapshot;
}

bool SameSnapshot(const AudioSnapshot& a, const AudioSnapshot& b) noexcept {
    return a.state == b.state && a.deviceAvailable == b.deviceAvailable && a.volume == b.volume &&
           a.muted == b.muted && a.deviceName == b.deviceName && a.detail == b.detail;
}

bool SameSnapshot(const MicrophoneSnapshot& a, const MicrophoneSnapshot& b) noexcept {
    return a.state == b.state && a.deviceAvailable == b.deviceAvailable && a.muted == b.muted &&
           a.inputVolume == b.inputVolume && a.deviceName == b.deviceName &&
           a.inUseDetection == b.inUseDetection && a.detail == b.detail;
}

EndpointBinding ClassifyFailure(HRESULT hr) noexcept {
    switch (hr) {
        // The default endpoint does not exist, or the bound one was unplugged/disabled meanwhile.
        case E_NOTFOUND:
        case AUDCLNT_E_DEVICE_INVALIDATED:
        case HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED):
        case HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND): return EndpointBinding::NoDevice;

        // The audio services (Audiosrv / AudioEndpointBuilder) are stopped, restarting or unreachable.
        case AUDCLNT_E_SERVICE_NOT_RUNNING:
        case HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE):
        case HRESULT_FROM_WIN32(ERROR_SERVICE_DISABLED):
        case HRESULT_FROM_WIN32(RPC_S_SERVER_UNAVAILABLE):
        case HRESULT_FROM_WIN32(RPC_S_CALL_FAILED):
        case RPC_E_DISCONNECTED:
        case RPC_E_SERVER_DIED:
        case RPC_E_SERVER_DIED_DNE:
        case CO_E_SERVER_EXEC_FAILURE:
        case REGDB_E_CLASSNOTREG: return EndpointBinding::ServiceUnavailable;

        default: return EndpointBinding::Failed;
    }
}

bool RequiresRebind(HRESULT hr) noexcept {
    const EndpointBinding binding = ClassifyFailure(hr);
    return binding == EndpointBinding::NoDevice || binding == EndpointBinding::ServiceUnavailable;
}

}  // namespace aether::status::audio
