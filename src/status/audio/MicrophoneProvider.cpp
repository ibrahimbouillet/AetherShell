// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/audio/MicrophoneProvider.h"

#include "status/audio/EndpointProviderCore.h"

#include <exception>
#include <string>
#include <utility>

namespace aether::status::audio {
namespace {

EndpointMonitorOptions InputOptions() {
    EndpointMonitorOptions options;
    options.flow = eCapture;
    options.role = eConsole;  // see the header for why not eCommunications
    options.label = "input";
    options.threadName = L"Aether audio input";
    return options;
}

}  // namespace

struct MicrophoneProvider::Impl {
    EndpointProviderCore<MicrophoneSnapshot> core{InputOptions(), &MakeMicrophoneSnapshot};
};

MicrophoneProvider::MicrophoneProvider() : impl_(std::make_unique<Impl>()) {}

MicrophoneProvider::~MicrophoneProvider() { Stop(); }

Result<void> MicrophoneProvider::Start(Listener listener) {
    if (!listener) return MakeError(ErrorCode::InvalidArgument, "the status listener must not be empty");
    try {
        return impl_->core.Start([listener = std::move(listener)](const MicrophoneSnapshot& snapshot, ChangeOrigin) {
            listener(snapshot);
        });
    } catch (const std::exception& ex) {
        return MakeError(ErrorCode::Internal, std::string("could not start the microphone provider: ") + ex.what());
    }
}

Result<void> MicrophoneProvider::StartWithOrigin(OriginListener listener) {
    return impl_->core.Start(std::move(listener));
}

void MicrophoneProvider::Stop() noexcept { impl_->core.Stop(); }

MicrophoneSnapshot MicrophoneProvider::Current() const { return impl_->core.Current(); }

Result<void> MicrophoneProvider::SetMuted(bool muted, std::chrono::milliseconds timeout) {
    return impl_->core.SetMuted(muted, timeout);
}

Result<void> MicrophoneProvider::ToggleMute(std::chrono::milliseconds timeout) {
    return impl_->core.ToggleMute(timeout);
}

void MicrophoneProvider::SetMutedAsync(bool muted, ControlCompletion completion) {
    impl_->core.SetMutedAsync(muted, std::move(completion));
}

void MicrophoneProvider::ToggleMuteAsync(ControlCompletion completion) {
    impl_->core.ToggleMuteAsync(std::move(completion));
}

}  // namespace aether::status::audio
