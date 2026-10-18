// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Types shared by the control API of AudioProvider and MicrophoneProvider.
#pragma once

#include "core/Result.h"

#include <guiddef.h>

#include <chrono>
#include <functional>

namespace aether::status::audio {

// Who caused a published change. Core Audio echoes every volume/mute change to all registered
// callbacks, including the changes Aether Shell makes itself; the per-process event-context GUID
// passed to the IAudioEndpointVolume setters lets the provider tell them apart (for example so the
// Top Bar can ignore the echo of its own volume slider while the user is dragging it).
enum class ChangeOrigin {
    External,     // the user, another application, Windows, a device change, or the initial reading
    ThisProcess,  // the result of SetVolume/SetMuted/ToggleMute called in this process
};

// Receives the outcome of an asynchronous control request exactly once. It normally runs on the
// provider's COM thread. When the request cannot be queued (provider not running) it runs
// synchronously on the calling thread; when it is discarded because the provider stopped first, it
// runs on the thread calling Stop() with ErrorCode::Cancelled. It must not block.
using ControlCompletion = std::function<void(Result<void>)>;

// Upper bound for the synchronous control calls (SetVolume, SetMuted, ToggleMute). The Core Audio
// call itself normally completes in well under a millisecond; the bound only matters when the
// Windows Audio service is hung. UI code that must never wait should use the *Async variants.
inline constexpr std::chrono::milliseconds kControlTimeout{2000};

// Per-process event-context GUID passed as pguidEventContext to IAudioEndpointVolume::SetMute and
// IAudioEndpointVolume::SetMasterVolumeLevelScalar. Generated once (CoCreateGuid) on first use and
// never GUID_NULL.
[[nodiscard]] const GUID& AudioEventContext() noexcept;

// ThisProcess when `eventContext` (AUDIO_VOLUME_NOTIFICATION_DATA::guidEventContext) is
// AudioEventContext(), External otherwise.
[[nodiscard]] ChangeOrigin OriginFromEventContext(const GUID& eventContext) noexcept;

// Owns a ControlCompletion and guarantees it runs exactly once: Complete() runs it with the given
// result; destroying the object first runs it with ErrorCode::Cancelled. Control requests capture a
// shared_ptr to one, so a request discarded by MtaWorker::Stop() still reports back. Exceptions
// thrown by the completion are caught and logged.
class CompletionOnce {
public:
    explicit CompletionOnce(ControlCompletion completion) noexcept : completion_(std::move(completion)) {}
    ~CompletionOnce();
    CompletionOnce(const CompletionOnce&) = delete;
    CompletionOnce& operator=(const CompletionOnce&) = delete;

    void Complete(Result<void> result) noexcept;

private:
    ControlCompletion completion_;
};

}  // namespace aether::status::audio
