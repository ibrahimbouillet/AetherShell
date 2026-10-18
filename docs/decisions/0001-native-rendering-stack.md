# ADR 0001 — Native rendering stack

- Status: accepted (2026-10-17)
- Deciders: Ibrahim Bouillet (owner), implementation by the engineering session

## Context

Aether Shell draws two always-present desktop surfaces (Dock, Status Bar) plus a few transient
surfaces (search, tooltips). They must be translucent with rounded geometry, animate smoothly,
start fast, stay idle at zero CPU and use little memory. Electron or a large UI framework would
dominate memory and startup time for two small bars.

## Decision

- **C++20 + Win32** for windows, input and system integration.
- **Windows.UI.Composition** (desktop interop, documented since Windows 10 1803) hosts each window's
  visual tree. Every surface is a `WS_EX_NOREDIRECTIONBITMAP` popup with a `DesktopWindowTarget`.
- **Direct2D 1.1 + DirectWrite** render the content into a `CompositionDrawingSurface`
  (premultiplied alpha) created from a `CompositionGraphicsDevice` bound to a Direct3D 11 device
  (hardware, WARP fallback).
- **Blur**: a `HostBackdropBrush` sprite clipped to the panel's rounded rectangle, enabled with
  `DWMWA_USE_HOSTBACKDROPBRUSH`. This is documented for Win32 windows on **Windows 11 only**; on
  Windows 10 the same tree runs without the backdrop and the tint becomes more opaque.
  The undocumented `SetWindowCompositionAttribute` accent API is deliberately **not** used.
- **Frame pacing**: event-driven. A high-resolution waitable timer armed for the next refresh
  interval drives frames only while something is invalidated or animating; idle surfaces cost no
  CPU. Animations use QPC time (frame-rate independent), tweens retarget from their current value
  and springs keep their velocity, so they are interruptible.
- **Supporting dialogs** (Settings, update details, installer pages) use standard Win32 controls
  for accessibility, keyboard navigation and IME support, with branded header art.

## Consequences

- No runtime dependencies beyond Windows itself; the static CRT means no redistributable.
- Device loss is handled by recreating the D3D/D2D devices and rebinding the composition graphics
  device (`GraphicsDevice::HandleDeviceLost`), after which every window redraws.
- Hit-testing for transparent margins is restricted with window regions (`SetWindowRgn`) so shadow
  and magnification headroom never block clicks to other applications.
- Verified on this development machine (Windows 11, Intel UHD, 150% scale) with the visual probe in
  `tests/integration/ui/GlassProbeTests.cpp`. Windows 10 has not been tested yet.
