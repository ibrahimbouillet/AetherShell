// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ibrahim Bouillet
//
// Watches the default Core Audio endpoint of one data-flow direction and role
// (for example render/console) and controls its endpoint volume. Shared engine
// of AudioProvider and MicrophoneProvider; not meant to be used directly by UI
// code.
//
// Threading: every COM object (IMMDeviceEnumerator, IMMDevice,
// IAudioEndpointVolume and the two callback objects) is created, used,
// unregistered and released on a private MTA thread (MtaWorker). The Core Audio
// callbacks (IMMNotificationClient, IAudioEndpointVolumeCallback) arrive on
// arbitrary system threads and only post a task to that thread.
//
// Binding: the default endpoint is (re)bound at start, when Windows reports a
// default-device change, when the bound device changes state or is removed, and
// when a device appears while nothing is bound. A failed binding caused by the
// audio service or an unexpected error is retried every `retryInterval` (a single
// pending retry, never a tight loop). While bound, a light health check re-reads
// volume and mute every `healthCheckInterval`: Core Audio documents no
// notification for a restart of the Windows Audio service, so a dead connection
// is otherwise only noticed by the next failing call. The check notices it (and a
// lost volume notification) within one interval.
#pragma once

#include "core/Result.h"
#include "status/audio/AudioControl.h"
#include "status/audio/EndpointReading.h"

#include <windows.h>
#include <mmdeviceapi.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace aether::status::audio {

namespace detail {
class MonitorCore;
}

struct EndpointMonitorOptions {
    EDataFlow flow = eRender;
    ERole role = eConsole;
    // Short noun used in logs and snapshot details, e.g. "output" or "microphone".
    std::string label = "output";
    std::wstring threadName = L"Aether audio";
    std::chrono::milliseconds retryInterval{std::chrono::seconds{30}};
    std::chrono::milliseconds healthCheckInterval{std::chrono::seconds{30}};
};

class EndpointMonitor {
public:
    // Receives every new reading on the monitor's COM thread, never concurrently with itself and never
    // after Stop() has returned. It must not block for long: it delays every other audio event.
    using ReadingSink = std::function<void(const EndpointReading&, ChangeOrigin)>;

    explicit EndpointMonitor(EndpointMonitorOptions options);
    ~EndpointMonitor();  // Stop()
    EndpointMonitor(const EndpointMonitor&) = delete;
    EndpointMonitor& operator=(const EndpointMonitor&) = delete;

    // Starts the COM thread and queues the first binding; never blocks on Core Audio. Can be called
    // once per monitor.
    [[nodiscard]] Result<void> Start(ReadingSink sink);

    // Unregisters both callbacks and releases every COM object on the COM thread, then joins it. When
    // called from the sink (on the COM thread) it cannot join: it stops further deliveries, the
    // cleanup runs right after the current task, and a later Stop() or the destructor joins.
    void Stop() noexcept;

    [[nodiscard]] bool IsMonitorThread() const noexcept;

    // Control requests run on the COM thread, in order with notifications. `completion` receives the
    // real outcome exactly once (see ControlCompletion). Levels are clamped into [0, 1]; NaN and
    // infinities are rejected before anything is queued.
    void SetVolumeAsync(float level, ControlCompletion completion);
    void SetMutedAsync(bool muted, ControlCompletion completion);
    void ToggleMuteAsync(ControlCompletion completion);

    // Same requests, waiting at most `timeout` for the outcome (ErrorCode::Timeout otherwise; the
    // request may still complete later). Called from the sink they run inline instead of waiting.
    [[nodiscard]] Result<void> SetVolume(float level, std::chrono::milliseconds timeout = kControlTimeout);
    [[nodiscard]] Result<void> SetMuted(bool muted, std::chrono::milliseconds timeout = kControlTimeout);
    [[nodiscard]] Result<void> ToggleMute(std::chrono::milliseconds timeout = kControlTimeout);

private:
    // Shared with the final cleanup task, so a Stop() issued from the sink cannot free the core while
    // its COM thread still needs it.
    std::shared_ptr<detail::MonitorCore> core_;
};

}  // namespace aether::status::audio
