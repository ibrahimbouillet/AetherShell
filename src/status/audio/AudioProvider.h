// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Default audio output for the Top Status Bar: master volume, mute and device
// name of the default render endpoint, plus the volume/mute controls behind the
// bar's audio menu.
//
// Endpoint role: eConsole. Windows' own UI (Sound settings, the Sound control
// panel's "Set Default", the taskbar volume flyout) sets the console and
// multimedia defaults together, so the two only differ when a third-party tool
// sets them separately; eConsole is the role used by Microsoft's documented
// endpoint-volume examples, and the "Default Communication Device" (eCommunications)
// is deliberately not followed. Per-application output overrides (Volume mixer)
// are not reflected: they do not change the default endpoint.
//
// States: Available (deviceAvailable, volume, muted, deviceName), Unavailable
// (no active default output device, or the Windows Audio service cannot be
// reached; `detail` says which), Error (unexpected Core Audio failure; `detail`).
// Bindings that failed because of the service or an error are retried every 30 s
// and on every device notification. See docs/modules/status_audio.md.
#pragma once

#include "core/Result.h"
#include "status/StatusTypes.h"
#include "status/audio/AudioControl.h"

#include <chrono>
#include <functional>
#include <memory>

namespace aether::status::audio {

class AudioProvider final : public IStatusProvider<AudioSnapshot> {
public:
    // Same delivery guarantees as Listener; also says who caused the change.
    using OriginListener = std::function<void(const AudioSnapshot&, ChangeOrigin)>;

    AudioProvider();
    ~AudioProvider() override;  // Stop()
    AudioProvider(const AudioProvider&) = delete;
    AudioProvider& operator=(const AudioProvider&) = delete;

    // IStatusProvider. The listener runs on the provider's COM thread. Stop() may be called from the
    // listener; Start() and destruction from the listener are not supported. A stopped provider can
    // be started again.
    Result<void> Start(Listener listener) override;
    Result<void> StartWithOrigin(OriginListener listener);
    void Stop() noexcept override;
    [[nodiscard]] AudioSnapshot Current() const override;

    // Control. Runs on the provider's COM thread and reports the real Core Audio outcome; the new
    // state then arrives through the listener with ChangeOrigin::ThisProcess. Errors: InvalidState
    // (not running), Unavailable (no usable device), InvalidArgument (NaN/infinite level), Timeout,
    // Platform (Core Audio failure, HRESULT in platformCode). Levels are clamped into [0, 1].
    //
    // The synchronous forms wait at most `timeout` (normally well under a millisecond); UI code that
    // must never wait uses the *Async forms, whose completion runs once on the COM thread (see
    // ControlCompletion).
    Result<void> SetVolume(float level, std::chrono::milliseconds timeout = kControlTimeout);
    Result<void> SetMuted(bool muted, std::chrono::milliseconds timeout = kControlTimeout);
    Result<void> ToggleMute(std::chrono::milliseconds timeout = kControlTimeout);
    void SetVolumeAsync(float level, ControlCompletion completion);
    void SetMutedAsync(bool muted, ControlCompletion completion);
    void ToggleMuteAsync(ControlCompletion completion);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::status::audio
