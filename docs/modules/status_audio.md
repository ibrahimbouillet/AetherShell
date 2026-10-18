# status_audio — default speaker and microphone status and control (`src/status/audio`, target `aether_status_audio`)

## Purpose

`aether_status_audio` implements two Top Status Bar providers of `status/StatusTypes.h` with Windows
Core Audio (`mmdeviceapi.h`, `endpointvolume.h`, `functiondiscoverykeys_devpkey.h`). It also provides
the control calls behind the bar's audio menu.

| Contract | Implementation | Reports |
| --- | --- | --- |
| `IStatusProvider<AudioSnapshot>` | `AudioProvider` | default render endpoint: `deviceAvailable`, master volume scalar, mute, friendly name |
| `IStatusProvider<MicrophoneSnapshot>` | `MicrophoneProvider` | default capture endpoint: `deviceAvailable`, endpoint mute, endpoint input volume scalar (gain), friendly name; `inUseDetection = Unsupported` |

Dependencies: `aether_core`, `aether_diagnostics`, `aether_platform`, `ole32`, and `windowsapp` for
C++/WinRT (`winrt::implements` for the classic COM callback interfaces, and `winrt::com_ptr`). Only
the header `status/StatusTypes.h` is used; `aether_status` is deliberately not linked.

### Endpoint roles

* **Output: `eRender` / `eConsole`.** Windows' own UI (Sound settings, the Sound control panel's "Set
  Default", the taskbar volume flyout) sets the console and multimedia defaults together. They only
  differ when a third-party tool sets them separately. `eConsole` is the role used by Microsoft's
  documented endpoint-volume samples. The "Default Communication Device" (`eCommunications`) is not
  followed.
* **Microphone: `eCapture` / `eConsole`.** This is the "Default Device" of Sound settings > Input,
  which recording applications use unless they pick a device themselves. A separate communications
  headset (`eCommunications`) is not followed.

## Public API

| Header | Contents |
| --- | --- |
| `AudioProvider.h` | `AudioProvider`: `Start(Listener)`, `StartWithOrigin(OriginListener)`, `Stop()`, `Current()`, `SetVolume(float)`, `SetMuted(bool)`, `ToggleMute()` (each with an optional timeout, default `kControlTimeout` = 2 s), `SetVolumeAsync`, `SetMutedAsync`, `ToggleMuteAsync` |
| `MicrophoneProvider.h` | `MicrophoneProvider`: the same lifecycle, plus `SetMuted`, `ToggleMute`, `SetMutedAsync`, `ToggleMuteAsync` (the input gain is read-only) |
| `AudioControl.h` | `ChangeOrigin {External, ThisProcess}`, `ControlCompletion`, `kControlTimeout`, `AudioEventContext()`, `OriginFromEventContext()`, `CompletionOnce` |
| `EndpointReading.h` | Pure, COM-free helpers: `EndpointBinding`, `EndpointReading`, `SanitizeVolume`, `NormalizeRequestedVolume`, `MakeAudioSnapshot`, `MakeMicrophoneSnapshot`, `SameSnapshot`, `ClassifyFailure`, `RequiresRebind` |
| `EndpointMonitor.h` | Internal engine shared by both providers: one default endpoint (flow + role), its callbacks and its control operations |
| `EndpointProviderCore.h`, `SnapshotPublisher.h` | Internal: Start/Stop lifecycle, session handling and snapshot deduplication |
| `MtaWorker.h` | Internal: a dedicated MTA thread with FIFO tasks, delayed tasks, a final task and a `Poster` handle that COM callbacks can keep |

```cpp
// UI thread. Snapshots arrive on the provider's COM thread; marshal them with IUiDispatcher.
aether::status::audio::AudioProvider audio;
audio.StartWithOrigin([&](const aether::status::AudioSnapshot& s, aether::status::audio::ChangeOrigin origin) {
    ui.Post([s, origin] { /* ignore ThisProcess echoes while the slider is being dragged */ });
});

// From the volume slider: never blocks the UI thread.
audio.SetVolumeAsync(0.4f, [](aether::Result<void> result) { /* COM thread; report result.error() */ });
audio.ToggleMuteAsync({});

aether::status::audio::MicrophoneProvider mic;
mic.Start([&](const aether::status::MicrophoneSnapshot& s) { ui.Post([s] { /* ... */ }); });
```

### Control API semantics

* Control requests run on the provider's COM thread, in order with notifications. They report the
  real Core Audio outcome: `InvalidArgument` (NaN or infinite level, checked before anything else),
  `InvalidState` (provider not running or stopping), `Unavailable` (no usable device, or the device
  or service vanished; a rebind is queued), `Timeout` (synchronous form only; the request may still
  complete later), `Platform` (other Core Audio failures, with the HRESULT in `platformCode`), and
  `Cancelled` (async form only, when the provider stopped before the request ran).
* Finite levels are clamped into [0, 1].
* The synchronous forms post the request and wait at most `timeout`. Called from the listener
  (that is, on the COM thread) they run inline instead of waiting on themselves. UI code that must
  never wait uses the `*Async` forms. Their completion runs exactly once: on the COM thread
  normally, synchronously on the caller when the request is rejected up front, or on the thread
  calling `Stop()` when the request is discarded.
* `ToggleMute` reads the endpoint's current mute state on the COM thread, rather than the last
  snapshot, which can lag behind a queued notification.
* **Self-initiated changes:** every `SetMute` / `SetMasterVolumeLevelScalar` call passes a
  per-process event-context GUID (`AudioEventContext()`, generated once with `CoCreateGuid`).
  `IAudioEndpointVolumeCallback::OnNotify` compares `guidEventContext` with it, and the snapshot
  arrives with `ChangeOrigin::ThisProcess`. Everything else (the user, other applications,
  Windows, device changes, the initial reading) is `External`.

## Threading model

* Each provider owns one `MtaWorker` per Start/Stop cycle: a dedicated thread in the multithreaded
  apartment. `IMMDeviceEnumerator`, `IMMDevice`, `IAudioEndpointVolume`, the property store, and
  both callback objects are created, used, unregistered and released only on that thread.
* `IMMNotificationClient` and `IAudioEndpointVolumeCallback` are implemented with
  `winrt::implements`, which provides correct COM ref counting. Their methods arrive on arbitrary
  system threads. They only copy their arguments and post a task through an `MtaWorker::Poster`,
  which shares the queue and fails harmlessly once the worker has stopped. Each callback carries a
  generation or client id, so a late event from a released callback object is ignored.
* `Start()` never waits for Core Audio. It starts the thread and queues the first binding, and the
  first snapshot follows from the COM thread.
* The listener runs on the COM thread. It is never called concurrently with itself, never after
  `Stop()` returns, and outside every lock. Snapshots are deduplicated field by field
  (`SameSnapshot`): the first snapshot of a session is always delivered, later ones only when
  something changed. Listener and completion exceptions are caught and logged.
* `Stop()` (idempotent, `noexcept`) closes the publisher, discards queued work, and runs a final
  task on the COM thread. That task unregisters the volume callback and the notification client
  before releasing the objects they are registered with. `Stop()` then joins the thread. It may be
  called from the listener: it then stops deliveries immediately, and the join happens on the next
  `Start()`, `Stop()` or the destructor on another thread. `Start()` from the listener, and
  destroying a provider from its own listener, are not supported.
* A stopped provider can be started again. The new session starts from an `Initializing` snapshot.

## Binding, retries and failure behaviour

| Situation | Snapshot |
| --- | --- |
| Default endpoint bound | `Available`, `deviceAvailable = true`, values that could be read (an unreadable value stays `nullopt`, and `detail` says why) |
| No active default endpoint (`E_NOTFOUND`, device invalidated or unplugged) | `Unavailable`, `deviceAvailable = false`, `detail = "no active default audio output/input device"` |
| Audio service unreachable (`AUDCLNT_E_SERVICE_NOT_RUNNING`, RPC disconnections, class not registered...) | `Unavailable` with the system message and HRESULT in `detail` |
| Any other failure | `Error` with the system message and HRESULT in `detail` |

* Values are only published while bound. An unbound snapshot never shows a stale volume, mute
  state or name.
* **Rebinding:** on `OnDefaultDeviceChanged` for the provider's flow and role; on
  `OnDeviceStateChanged` or `OnDeviceRemoved` for the bound endpoint; on any device
  added/removed/state event while nothing usable is bound; and when a call fails with a
  device-gone or service-gone HRESULT. A burst of notifications queues a single rebind.
* **Bounded retry:** a binding that failed because of the service or an unexpected error is
  retried every 30 s (at most one pending timer, never a tight loop), in addition to the device
  notifications. A missing device relies on the notification client alone. When the service was
  the problem, the enumerator and its notification registration are rebuilt on the next attempt.
* **Health check:** while bound, volume and mute are re-read every 30 s. Core Audio documents no
  notification for a restart of the Windows Audio service. The check notices a dead connection
  (and repairs a lost volume notification) within one interval. Deduplication drops the usual
  no-op.
* `OnPropertyValueChanged` is only acted on for `PKEY_Device_FriendlyName` of the bound endpoint
  (a rename). Every other property event is ignored without posting work.
* **Logging:** binding transitions and failures go to `status.audio` (Info/Warning). Friendly names
  can contain personal data and are never logged. Exception text from listeners, completions and
  worker tasks is also omitted. There is no logging per volume notification.

## Limitations

* **Microphone "in use" is not reported** (`inUseDetection = Support::Unsupported`). Windows has no
  documented, reliable API across the supported versions to tell whether an application is
  recording. The taskbar's privacy indicator relies on internal capability-access state that is not
  a documented API. Neither the existence of an endpoint nor a peak meter proves recording, so
  nothing is inferred from them.
* `inputVolume` is the endpoint's recording gain (the level slider in Sound settings), not a live
  signal level.
* Per-application volume and per-application output-device overrides (Volume mixer) are not
  reflected. Only the default endpoint's master volume and mute are reported.
* Only the `eConsole` role is followed (see "Endpoint roles"). A different `eMultimedia` or
  `eCommunications` default set by third-party tools is not shown.
* The behaviour while the Windows Audio service restarts was reasoned from the documentation, not
  reproduced on this machine: stopping the service in a test would disturb the user's audio. The
  classification of service HRESULTs is unit tested. The health check bounds detection to about
  30 s if no call fails earlier.
* The synchronous control calls can block the caller for up to their timeout (default 2 s) if the
  audio service hangs. UI code should use the `*Async` forms.

## Tests

* `tests/unit/status_audio/`: volume clamping (reported and requested), snapshot construction for
  every binding state (no stale values), field-by-field deduplication, the publisher's sessions and
  origin forwarding, HRESULT classification, the event-context GUID, exactly-once completions,
  control calls on stopped providers (including argument validation before state), and the
  `MtaWorker` (MTA membership, FIFO order, delayed tasks, Stop discarding work, stop from its own
  task, concurrent Stop joining, `Poster` outliving the worker). Failed level reads clear stale
  fields and recover independently. A synthetic-marker log test verifies exception privacy.
  No audio device is touched.
* `tests/integration/status_audio/`: starts both real providers read-only (individually and side by
  side), prints scalar/state diagnostics without friendly names or platform detail strings, and
  checks no listener calls after `Stop()`, concurrent `Stop()` waiting for an active listener, stop
  from the listener and restart. No integration test changes mute, output volume or input gain.
  Real control success and notification echo are unverified; stopped-provider failures and
  event-context classification are covered without changing the user's audio state.

Verification observed on 2026-10-18: **23 unit cases + 5 read-only integration cases passed**, in two
CTest executables. Both default endpoints reported `Available`; friendly names were not printed.
The private build directory was checked absent before the first configuration:

```powershell
./scripts/build.ps1 -BuildDir build/wf-codex-audio-20261018 -Modules status_audio -Jobs 2 -Test -TestRegex status_audio -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON'
```

After review fixes, the same command without `-CMakeArgs` rebuilt changed dependencies and passed
again; `AETHER_WARNINGS_AS_ERRORS:BOOL=ON` remained set. Independent source/log review found and
confirmed fixes for exception-text leakage and stale level values after a failed health check.
No actual mute/volume setters, device unplugging, audio-service restart, Windows 10, or UI wiring
were exercised. The interrupted implementation's original mutating integration tests were not run.
