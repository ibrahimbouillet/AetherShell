// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Default microphone for the Top Status Bar: endpoint mute, endpoint input
// volume (the recording gain set in Sound settings, not a live signal level) and
// device name of the default capture endpoint, plus mute control.
//
// Endpoint role: eConsole, the "Default Device" of Sound settings > Input, which
// is what recording applications use unless they pick a device themselves. The
// "Default Communication Device" (eCommunications), used by voice-chat
// applications that ask for it, can be a different microphone; it is not
// followed, so a user with a separate communications headset sees the default
// device here.
//
// "Is an application recording right now?" is NOT reported: inUseDetection is
// always Support::Unsupported. Windows has no documented, reliable API for it
// across the supported versions (the taskbar's privacy indicator relies on
// internal capability-access state that is not a documented API), and neither the
// existence of an endpoint nor a peak meter proves that anything is recording, so
// nothing is inferred from them.
//
// States and retries are the same as AudioProvider. See docs/modules/status_audio.md.
#pragma once

#include "core/Result.h"
#include "status/StatusTypes.h"
#include "status/audio/AudioControl.h"

#include <chrono>
#include <functional>
#include <memory>

namespace aether::status::audio {

class MicrophoneProvider final : public IStatusProvider<MicrophoneSnapshot> {
public:
    using OriginListener = std::function<void(const MicrophoneSnapshot&, ChangeOrigin)>;

    MicrophoneProvider();
    ~MicrophoneProvider() override;  // Stop()
    MicrophoneProvider(const MicrophoneProvider&) = delete;
    MicrophoneProvider& operator=(const MicrophoneProvider&) = delete;

    // Same contract as AudioProvider.
    Result<void> Start(Listener listener) override;
    Result<void> StartWithOrigin(OriginListener listener);
    void Stop() noexcept override;
    [[nodiscard]] MicrophoneSnapshot Current() const override;

    // Endpoint mute control; same semantics and errors as AudioProvider::SetMuted/ToggleMute. The
    // input gain is deliberately not exposed for writing.
    Result<void> SetMuted(bool muted, std::chrono::milliseconds timeout = kControlTimeout);
    Result<void> ToggleMute(std::chrono::milliseconds timeout = kControlTimeout);
    void SetMutedAsync(bool muted, ControlCompletion completion);
    void ToggleMuteAsync(ControlCompletion completion);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace aether::status::audio
