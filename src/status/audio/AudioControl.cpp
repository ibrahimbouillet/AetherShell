// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "status/audio/AudioControl.h"

#include "diagnostics/Logger.h"

#include <windows.h>
#include <objbase.h>

#include <exception>
#include <utility>

namespace aether::status::audio {
namespace {

// Used only if CoCreateGuid fails, which in practice it does not. It is still unique to Aether Shell
// (and never GUID_NULL), so the provider can keep recognizing its own changes.
constexpr GUID kFallbackEventContext = {
    0x6b1f3c2a, 0x8d4e, 0x4f7b, {0x9a, 0x51, 0x2c, 0x7e, 0x0d, 0x93, 0xb4, 0x66}};

}  // namespace

const GUID& AudioEventContext() noexcept {
    static const GUID context = [] {
        GUID guid{};
        if (FAILED(::CoCreateGuid(&guid)) || guid == GUID{}) guid = kFallbackEventContext;
        return guid;
    }();
    return context;
}

ChangeOrigin OriginFromEventContext(const GUID& eventContext) noexcept {
    return eventContext == AudioEventContext() ? ChangeOrigin::ThisProcess : ChangeOrigin::External;
}

CompletionOnce::~CompletionOnce() {
    if (!completion_) return;
    try {
        Complete(MakeError(ErrorCode::Cancelled, "the audio provider stopped before the request ran"));
    } catch (...) {
        // MakeError can only fail to allocate its message; the completion is then dropped.
    }
}

void CompletionOnce::Complete(Result<void> result) noexcept {
    ControlCompletion completion = std::exchange(completion_, nullptr);
    if (!completion) return;
    try {
        completion(std::move(result));
    } catch (const std::exception&) {
        AETHER_LOG_ERROR("status.audio", "control completion threw an exception");
    } catch (...) {
        AETHER_LOG_ERROR("status.audio", "control completion threw a non-standard exception");
    }
}

}  // namespace aether::status::audio
