// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Pure, COM-free building blocks of the audio providers: the raw endpoint
// reading produced on the COM thread, its mapping to the published snapshots,
// snapshot comparison (deduplication), volume sanitizing and HRESULT
// classification. Everything here is deterministic and unit tested.
#pragma once

#include "core/Result.h"
#include "status/StatusTypes.h"

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace aether::status::audio {

// Outcome of the most recent attempt to bind the default endpoint of one data-flow direction.
enum class EndpointBinding {
    Initializing,        // no attempt has completed yet
    Bound,               // the default endpoint is active and IAudioEndpointVolume is available
    NoDevice,            // Windows reports no active default endpoint for the flow and role
    ServiceUnavailable,  // the Windows Audio service / endpoint builder could not be reached
    Failed,              // any other failure; `detail` says what failed
};

[[nodiscard]] std::string_view ToString(EndpointBinding binding) noexcept;

// Raw reading of one endpoint, built on the provider's COM thread.
struct EndpointReading {
    EndpointBinding binding = EndpointBinding::Initializing;
    std::wstring deviceName;      // PKEY_Device_FriendlyName; empty when Windows did not provide it
    std::optional<float> volume;  // master volume scalar as reported by Core Audio
    std::optional<bool> muted;    // endpoint mute
    std::string detail;           // UTF-8 explanation of a failure or partial reading; no secrets
};

// Volume reported by Windows -> published value: NaN and infinities are unknown (nullopt), finite
// values are clamped into [0, 1].
[[nodiscard]] std::optional<float> SanitizeVolume(float raw) noexcept;

// Level requested through SetVolume: NaN and infinities are rejected with
// ErrorCode::InvalidArgument; finite values are clamped into [0, 1].
[[nodiscard]] Result<float> NormalizeRequestedVolume(float requested);

// Replaces the previous endpoint levels with a new read outcome. A failed read becomes unknown,
// never a stale value; independently successful fields remain usable. The optional name diagnostic
// is retained while level diagnostics are rebuilt from the current outcomes.
void ApplyEndpointLevelReadings(EndpointReading& reading, HRESULT volumeStatus, float volume,
                               HRESULT muteStatus, bool muted, std::string_view nameDetail = {});

// Snapshot construction. Values are only carried over while the endpoint is bound, so a snapshot
// for a missing device or a failed binding never shows a stale volume, mute state or name.
[[nodiscard]] AudioSnapshot MakeAudioSnapshot(const EndpointReading& reading);
// inUseDetection is always Support::Unsupported: Windows has no documented, reliable API on every
// supported version to tell whether an application is currently recording.
[[nodiscard]] MicrophoneSnapshot MakeMicrophoneSnapshot(const EndpointReading& reading);

// Field-by-field equality used to suppress duplicate notifications.
[[nodiscard]] bool SameSnapshot(const AudioSnapshot& a, const AudioSnapshot& b) noexcept;
[[nodiscard]] bool SameSnapshot(const MicrophoneSnapshot& a, const MicrophoneSnapshot& b) noexcept;

// Maps a failing Core Audio / COM HRESULT to the binding state it implies (never returns Bound or
// Initializing).
[[nodiscard]] EndpointBinding ClassifyFailure(HRESULT hr) noexcept;

// True when a failure means the bound endpoint vanished or the audio service went away, so the
// provider must rebind rather than keep using its current interfaces.
[[nodiscard]] bool RequiresRebind(HRESULT hr) noexcept;

}  // namespace aether::status::audio
