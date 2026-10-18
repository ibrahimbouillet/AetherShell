// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/audio/AudioProvider.h"

#include "status/audio/EndpointProviderCore.h"

#include <exception>
#include <string>
#include <utility>

namespace aether::status::audio {
namespace {

EndpointMonitorOptions OutputOptions() {
    EndpointMonitorOptions options;
    options.flow = eRender;
    options.role = eConsole;  // see the header for why not eMultimedia/eCommunications
    options.label = "output";
    options.threadName = L"Aether audio output";
    return options;
}

}  // namespace

struct AudioProvider::Impl {
    EndpointProviderCore<AudioSnapshot> core{OutputOptions(), &MakeAudioSnapshot};
};

AudioProvider::AudioProvider() : impl_(std::make_unique<Impl>()) {}

AudioProvider::~AudioProvider() { Stop(); }

Result<void> AudioProvider::Start(Listener listener) {
    if (!listener) return MakeError(ErrorCode::InvalidArgument, "the status listener must not be empty");
    try {
        return impl_->core.Start(
            [listener = std::move(listener)](const AudioSnapshot& snapshot, ChangeOrigin) { listener(snapshot); });
    } catch (const std::exception& ex) {
        return MakeError(ErrorCode::Internal, std::string("could not start the audio output provider: ") + ex.what());
    }
}

Result<void> AudioProvider::StartWithOrigin(OriginListener listener) { return impl_->core.Start(std::move(listener)); }

void AudioProvider::Stop() noexcept { impl_->core.Stop(); }

AudioSnapshot AudioProvider::Current() const { return impl_->core.Current(); }

Result<void> AudioProvider::SetVolume(float level, std::chrono::milliseconds timeout) {
    return impl_->core.SetVolume(level, timeout);
}

Result<void> AudioProvider::SetMuted(bool muted, std::chrono::milliseconds timeout) {
    return impl_->core.SetMuted(muted, timeout);
}

Result<void> AudioProvider::ToggleMute(std::chrono::milliseconds timeout) { return impl_->core.ToggleMute(timeout); }

void AudioProvider::SetVolumeAsync(float level, ControlCompletion completion) {
    impl_->core.SetVolumeAsync(level, std::move(completion));
}

void AudioProvider::SetMutedAsync(bool muted, ControlCompletion completion) {
    impl_->core.SetMutedAsync(muted, std::move(completion));
}

void AudioProvider::ToggleMuteAsync(ControlCompletion completion) {
    impl_->core.ToggleMuteAsync(std::move(completion));
}

}  // namespace aether::status::audio
