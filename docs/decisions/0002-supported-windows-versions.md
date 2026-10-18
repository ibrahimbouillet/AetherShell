# ADR 0002 — Supported Windows versions

- Status: accepted (2026-10-17)

## Decision

- **Minimum: Windows 10, version 2004 (build 19041), 64-bit (x64).** ARM64 builds are possible with
  the MSVC ARM64 toolset but are not produced or tested yet.
- The application manifest declares Windows 10/11 compatibility, Per-Monitor-V2 DPI awareness,
  long-path awareness, the UTF-8 code page and `asInvoker` (no elevation).

## Windows 10 vs Windows 11 differences

| Area | Windows 11 (22000+) | Windows 10 (19041–19045) |
|---|---|---|
| Backdrop blur behind glass | `HostBackdropBrush` + `DWMWA_USE_HOSTBACKDROPBRUSH` | Not available to Win32 windows → more opaque tint |
| DWM rounded corners / borders on popups | Disabled explicitly (we draw our own geometry) | Not applicable |
| Segoe UI Variable font | Present | Falls back to Segoe UI |
| Wi-Fi SSID | Treated as location data on 24H2+ → never shown | Not shown either (consistent privacy) |

## Not claimed

Compatibility is only claimed for configurations that have been tested. As of this ADR only
Windows 11 (build 26300, x64) has been exercised; Windows 10 testing is an open release
qualification task (see `KNOWN_LIMITATIONS.md`).
