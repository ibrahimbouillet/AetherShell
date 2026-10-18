# System status providers — in progress

Contract: `src/status/StatusTypes.h` — `IStatusProvider<Snapshot>` (`Start(listener)`, `Stop()`,
`Current()`), snapshots with explicit `ProviderState`; unknown values are `std::nullopt`, never zero.

| Provider | Source | Windows API |
|---|---|---|
| Clock (+ `ClockFormatter`) | `src/status` | `GetLocalTime`, time-zone info, boundary-aligned waitable timer |
| Battery | `src/status` | `Windows.System.Power.PowerManager` + `GetSystemPowerStatus` |
| Keyboard | `src/status` | foreground thread `GetKeyboardLayout`, foreground WinEvent + 1 s HKL check |
| Network | `src/status` | `Windows.Networking.Connectivity` (no SSID) |
| Audio, Microphone | `src/status/audio` | Core Audio endpoints + callbacks; mic in-use = unsupported |
| Weather | `src/weather` | Open-Meteo over `IHttpClient`, cache, explicit states; off by default |

Threading: each provider owns its thread/pool; listeners fire off the UI thread and never after `Stop()`.
Per-module details: `docs/modules/status.md`, `status_audio.md`, `weather.md` (written with the code).
