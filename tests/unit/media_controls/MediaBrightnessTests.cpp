// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#include "AetherTest.h"
#include "media_controls/MediaBrightness.h"

#include <chrono>
#include <limits>

namespace {
using namespace aether;
using namespace aether::controlcenter;
using namespace std::chrono_literals;

template<class T>
void ExpectError(const Result<T>& result, ErrorCode code) {
    REQUIRE_FALSE(result.ok());
    CHECK_EQ(result.error().code, code);
}
} // namespace

// This console test runner has no STA. The adapter may initialize its worker MTA, but every call
// below is rejected before session discovery, display enumeration, WMI or a device mutator.
AETHER_TEST("media_controls invalid brightness is rejected before platform access") {
    MediaBrightness provider;
    for (const int percent : {std::numeric_limits<int>::min(), -1, 101, std::numeric_limits<int>::max()})
        ExpectError(provider.SetBrightness(percent), ErrorCode::InvalidArgument);
}

AETHER_TEST("media_controls invalid deadlines reject all discovery and control methods") {
    MediaBrightness provider;
    for (const auto timeout : {0ms, -1ms, 10001ms}) {
        ExpectError(provider.ReadMedia({}, timeout), ErrorCode::InvalidArgument);
        ExpectError(provider.ControlMedia(0, MediaAction::TogglePlayPause, {}, timeout), ErrorCode::InvalidArgument);
        ExpectError(provider.ReadBrightness({}, timeout), ErrorCode::InvalidArgument);
        // Valid percentage reaches the deadline guard, never the display topology or WMI path.
        ExpectError(provider.SetBrightness(50, {}, timeout), ErrorCode::InvalidArgument);
    }
}

AETHER_TEST("media_controls pre-cancelled reads do not discover sessions or displays") {
    MediaBrightness provider;
    CancellationSource source;
    source.Cancel();
    ExpectError(provider.ReadMedia(source.Token()), ErrorCode::Cancelled);
    ExpectError(provider.ReadBrightness(source.Token()), ErrorCode::Cancelled);
}

AETHER_TEST("media_controls pre-cancelled commands do not refresh or mutate devices") {
    MediaBrightness provider;
    CancellationSource source;
    source.Cancel();
    for (const auto action : {MediaAction::TogglePlayPause, MediaAction::Next, MediaAction::Previous})
        ExpectError(provider.ControlMedia(0, action, source.Token()), ErrorCode::Cancelled);
    // Both boundary values are valid; cancellation must stop them before discovery or WMI.
    for (const int percent : {0, 100})
        ExpectError(provider.SetBrightness(percent, source.Token()), ErrorCode::Cancelled);
}

AETHER_TEST("media_controls fresh session selection is local and rejects an unknown key") {
    MediaBrightness provider;
    ExpectError(provider.SelectSession(0), ErrorCode::NotFound);
    ExpectError(provider.SelectSession(1), ErrorCode::NotFound);
    ExpectError(provider.SelectSession(std::numeric_limits<std::uint64_t>::max()), ErrorCode::NotFound);
}
