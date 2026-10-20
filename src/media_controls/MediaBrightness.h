// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
#pragma once

#include "core/Cancellation.h"
#include "core/Result.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace aether::controlcenter {

enum class MediaAction { TogglePlayPause, Next, Previous };
enum class MediaPlayback { Unknown, Closed, Opened, Changing, Stopped, Playing, Paused };

struct MediaSession {
    std::uint64_t key = 0; // opaque, provider-local; valid only while this session remains listed
    std::wstring title;
    std::wstring artist;
    MediaPlayback playback = MediaPlayback::Unknown;
    bool canTogglePlayPause = false;
    bool canNext = false;
    bool canPrevious = false;
};

struct MediaSnapshot {
    std::vector<MediaSession> sessions;
    std::optional<std::uint64_t> selectedSession;
};

struct BrightnessSnapshot {
    std::optional<int> percent; // only a value read from the matched active primary internal display
    bool canSet = false;
};

// Worker-only synchronous adapter. Run every method on an MTA worker, never the UI thread.
// Serialize calls; cancel and join the owning worker before destroying this object. Constructors
// perform no Windows discovery. No title/artist/device identity is logged or persisted.
// Read operations never change playback/brightness. Controls are called only after user input.
class MediaBrightness {
public:
    MediaBrightness();
    ~MediaBrightness();
    MediaBrightness(const MediaBrightness&) = delete;
    MediaBrightness& operator=(const MediaBrightness&) = delete;

    [[nodiscard]] Result<MediaSnapshot> ReadMedia(CancellationToken cancel = {},
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1800));
    // Chooses the local control target; it does not change Windows' current SMTC session.
    [[nodiscard]] Result<void> SelectSession(std::uint64_t key);
    [[nodiscard]] Result<void> ControlMedia(std::uint64_t key, MediaAction action, CancellationToken cancel = {},
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1800));
    [[nodiscard]] Result<BrightnessSnapshot> ReadBrightness(CancellationToken cancel = {},
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2500));
    // Success means the WMI method accepted the command and this is a fresh observed reading;
    // it does not assert that the device has reached the requested/supported level. UI must show
    // the observed value, not an unconfirmed "applied" claim. Cancellation cannot undo a sent write.
    [[nodiscard]] Result<BrightnessSnapshot> SetBrightness(int percent, CancellationToken cancel = {},
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2500));

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace aether::controlcenter
