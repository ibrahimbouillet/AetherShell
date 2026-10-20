<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Graphics and the design language

`src/graphics`, target `aether_graphics`, owns the Direct3D/Direct2D/DirectWrite devices, the
Windows.UI.Composition surfaces, the glass material, the design tokens, text, bitmaps, frame pacing
and the animation primitives. Search, tooltips and owner-drawn menus consume the new role groups.
Dock, Top Bar and Settings adoption is tracked separately in their module documents. Graphics tests
do not establish that all consumers have completed the surface-integration batch.

## Design language ("Aether Fluent")

Windows 11 Fluent roles and metrics on a 4-DIP grid, neutral materials, the Windows accent color,
two-tone keyboard focus and the Segoe UI Variable type ramp, with macOS-like Dock polish (springs,
lift, a gravity launch bounce, pill indicators).

`graphics::DesignTokens` (`Theme.h`) is the single source. Role groups:

| Group | Contents |
|---|---|
| `glass` | neutral tint (dark `#1E1E1E`, light `#F3F3F3`), tint alpha with and without blur, `uniformBorder` for the desktop surfaces; gradient border, top-weighted sheen (`highlightFraction` 0.30) and 1-DIP inner `rim` for flyouts; `backdropOpacity` 1.0, `noiseAlpha` 0.02 |
| `surface` | opaque layers and control fills usable by GDI: `background`, `layer`, `card`, `cardStroke`, `subtleFill`, `controlFill*`, `controlStroke*`, `strongStroke`, `divider`, `selectedFill`, `selectedText`, `popupBackground` |
| `accentRoles` | `fill`, `fillHover`, `fillPressed`, `fillDisabled`, `text`, `textOnAccent` (black or white, whichever contrasts more) |
| status | `success`, `caution`, `critical`, `info`; `warning` stays an alias of `caution` |
| `focus` | two-tone ring: `outer` 2 DIP in the text tone, `inner` 1 DIP in the opposite tone, 1-DIP gap |
| `radii` | control 4, card 6, overlay 8, popup 12, bar 10, tooltip 8, item 10; Dock about 0.3 x the panel height clamped to 14..22 (`DockCornerRadius`) |
| `elevation` | two layers (key and ambient: blur, offset, alpha) for tooltip, bar, dock, popup and lift; bar and dock offsets are zero so their soft separation has no directional cast |
| `type` | caption 12, body 14, body strong 14 semibold, subtitle 20, title 28; `fontFamily` Segoe UI Variable Text, `displayFontFamily` Segoe UI Variable Display, `smallFamily` Segoe UI Variable Small; every family falls back to Segoe UI when missing |
| `curves` | standard (0.2,0,0,1), decelerate (0,0,0,1), accelerate (0.3,0,1,1) |
| `glyph` | 16-DIP grid, 1.5-DIP stroke, `halo` knockout color and width for overlapping marks |

Every field that existed before the design language keeps its name and meaning and is derived from
the role groups (`dockCornerRadius = radii.dockMax`, `barCornerRadius`, `popupCornerRadius`,
`focusRing = focus.outer`, `focusRingWidth`, `fontSizeSmall`, `fontSizeBody`, the legacy single
`glass.shadow*` mirror the Dock elevation's key layer). The legacy durations stay
0.12/0.22/0.36 s; `durationMicro` is 0.083 s and `tooltipWarmWindow` 0.6 s.

High contrast replaces every color with system colors (`COLOR_WINDOW`, `COLOR_WINDOWTEXT`,
`COLOR_HIGHLIGHT`, `COLOR_HIGHLIGHTTEXT`, `COLOR_BTNFACE`, `COLOR_BTNTEXT`, `COLOR_HOTLIGHT`,
`COLOR_GRAYTEXT`, `COLOR_MENU`), makes the glass opaque, zeroes every elevation, the sheen and the
grain, widens focus to 3 DIP and maps the status roles to the text color, so state never depends on
color alone.

### Theme mode and accent

- `DetectThemeMode()` / `DetectAppThemeMode(preference)`: the app mode (`AppsUseLightTheme`), for
  app windows such as Settings.
- `DetectShellThemeMode()` / `DetectShellThemeMode(preference)`: the Windows mode
  (`SystemUsesLightTheme`, read through `platform::SystemPrefersDarkShell()`), the mode Windows uses
  for its own taskbar; it falls back to the app mode when the value is absent. Shell surfaces use it.
- `ResolveThemeMode(preference, highContrast, systemDark)` is the pure rule: high contrast wins, then
  a Light or Dark preference, then the system value.
- `QuerySystemPalette()` (`SystemPalette.h`) reads the accent palette (Accent, AccentLight1-3,
  AccentDark1-3) and the text scale through the documented WinRT `UISettings` API. It never throws;
  on failure `available` is false and the brand palette applies. Call it once per theme or setting
  change, never per frame.
- `MakeDesignTokens(mode, palette, options)`: with `AccentSource::Windows` the accent comes from the
  palette (dark mode prefers AccentLight2, light mode AccentDark1) and is stepped through the other
  variants until it keeps 3:1 against the card; the best variant is used when none does.
  `AccentSource::Aether`, or an unavailable palette, keeps the brand indigo. `systemAccent` tells
  which one applied. The brand gradient (`brandPrimary`/`brandSecondary`) is kept for the logo.
- `MakeDesignTokens(mode)` uses a fixed brand accent without reading the accent palette. It still
  reads the Windows transparency preference and, in high contrast, the system colors. Material tests
  explicitly set transparency when they need a fixed translucent or opaque configuration.
- `MakeShellDesignTokens(options)` and `MakeAppDesignTokens(options)` combine detection, palette and
  options for the app.
- `ContrastRatio`, `RelativeLuminance` (WCAG 2), `CompositeOver` and `Color::ToColorRef` are pure.

`graphics::ThemePreference` and `graphics::AccentSource` mirror `settings::ThemePreference` and
`settings::AccentSource`; graphics does not depend on settings, so the app maps one onto the other.

Unit coverage (`DesignTokenTests`) checks with synthetic palettes: text and accent contrast (7:1,
4.5:1, 3:1) in both modes, the variant choice and stepping, the brand fallback, every legacy field,
the high-contrast system colors, resolution and the text-scale clamp.

## Motion

One motion table, `graphics::Motion()` (`Animation.h`), shared by the Dock and the Top Bar:

- magnify spring 420/38 (unchanged since 0.1);
- press k900 zeta 0.75, lift k600 zeta 0.8, slide k380 zeta 0.85 (`SpringFromDampingRatio`);
- long press 350 ms, lift 8 DIP, lift scale 1.08, press scale 0.92;
- launch bounce: a 0.36 s gravity hop, restitution 0.45, one rebound; `MaxBounceHeight(iconSize)` =
  min(0.3 x icon size, 24 DIP), used for drawing, the window extent and the optional reservation.

`CubicBezier`/`EvaluateCubicBezier` evaluate CSS-style curves; `Easing::Standard`, `Decelerate` and
`Accelerate` are the token curves, and `Tween::AnimateTo` also accepts a `CubicBezier`.
`MotionPolicy` scales durations and springs by the animation speed (`durationScale`, clamped
0.5..1.5, `ScaleSpring`); `reduced` makes durations 0 and `AllowsMovement()` false (no slides,
bounces or scale; state changes are instant).

### Window transitions

`Transition` (`Transition.h`) describes opacity, offset and scale (around a center) keyframes in
DIPs with a duration and a curve. `MakeEnterTransition(from, tokens, motion)` fades in from a shape
over `durationNormal` with the decelerate curve; `MakeExitTransition(to, tokens, motion)` fades out
towards a shape over `durationFast` with the accelerate curve. Durations follow the animation speed;
reduced motion and high contrast make them 0 (`IsInstant()`).

`CompositionSurface::Animate(transition, pxPerDip, completed)` plays it on the root visual inside a
`CompositionScopedBatch`. `completed` runs on the UI thread when the batch completes (delivered
through the thread's DispatcherQueue), or synchronously for an instant transition. A new transition,
`ResetTransition()` or `Detach()` drops a pending completion. `AnimateEntrance`/`AnimateExit` are now
wrappers with the token curves. No UI frame loop runs for these transitions.

The batch source and its event token are held explicitly: the tested composition runtime does not
support the `IWeakReferenceSource` path assumed by C++/WinRT `auto_revoke` here. The callback is moved
to a stack local before removing its subscription, and callback exceptions stay inside the module.

## Glass material

`PaintGlassPanel(context, rect, tokens, style)` paints the exterior-only shadow, neutral tint and
grain. `Elevation::Dock` and `Elevation::Bar` use sanded glass: their shadow is centered, their edge
is uniformly subtle on every side, and they never paint the top sheen, inner rim or gradient edge.
This keeps the real blurred backdrop visible without painting a light source onto either desktop
surface. Popup and tooltip elevations keep their flyout treatment for separation and readability;
their top sheen and rim still follow `GlassStyle::highlight`.

`GlassStyle` retains `elevation` (None, Tooltip, Bar, Dock, Popup, Lift), `noise` and `highlight`;
`shadow = false` still means no shadow. Tint strength, compositor blur, grain and the opaque
high-contrast/transparency-off policies are unchanged. `GlassTintAlpha` keeps its semantics.

- Shadows: `PaintElevation` bakes each (device, bake size, radius, layers, DPI, color) once into a
  bitmap on a private device context of the same Direct2D device (so the caller's clips, layers and
  transform never affect it) and draws it as nine slices. Any panel longer than
  2 x extent + 2 x radius + 2 DIP shares one bake, so the Dock's per-frame width changes cost nine
  bitmap draws. The silhouette is excluded, so the shadow never darkens a translucent panel. Up to
  16 bakes are kept per thread (least recently used first out); `ReleaseGlassResources()` drops them
  and runs from `GraphicsDevice::Shutdown` and before a lost device is recreated.
- Grain: a deterministic 128 x 128 premultiplied tile (xorshift32, mean alpha `noiseAlpha`) drawn with
  a wrapping bitmap brush. It is skipped in high contrast and when transparency effects are off.
- Backdrop: `CompositionSurface::SetBackdrop` takes the frosted layer's opacity
  (`glass.backdropOpacity`, default 1.0; it was a literal 0.72 that let the sharp desktop ghost
  through the blur). `SurfaceWindow::SetBackdropPanel` forwards it.

Other painters: `PaintFocusRing` (two-tone; an `ID2D1RenderTarget*` overload exists for DC render
targets), `PaintRunningIndicator` (fully rounded pill; Running in `runningIndicator`, Active in the
accent, Attention in `caution`, with an opacity for fades; the Dock owns the geometry),
`PaintLiftShadow` (shadow under a dragged icon, faded by the lift amount, baked per 8-DIP size bucket),
`PaintItemHighlight` (hover and press amounts; high contrast adds an outline), `PaintSelectionPill`,
`PaintAccentIndicator` and `DrawIconBitmap` (high-quality cubic filtering when downscaled by more
than 1.2x, `IconInterpolation`). Pure helpers for tests: `ElevationFor`, `ShadowExtent`,
`ShadowBakeLength`, `NineSliceRects`, `MakeNoiseTile`.

## Glyphs

`Glyphs.h` holds original vector marks drawn in code on the 16-DIP grid with round caps and joins;
no icon font, image asset or third-party artwork:

- `PaintBrandMark`: the logo's two ribbons, the same geometry and gradient as the Top Bar logo
  (an 18-DIP box reproduces it exactly); solid text color in high contrast.
- `PaintStartMark`: four rounded squares in a 2x2 arrangement, an original mark. `StartMarkColor`
  is the Windows accent when the tokens carry it (`AccentSource::Windows` and the palette was read),
  otherwise Windows blue (`#0078D4` light, `#4CC2FF` dark); text color in high contrast.
- `PaintGlyph(target, glyph, box, brush, stroke)`: Search, Task View, the Settings navigation marks
  (Dock, Top Bar, Desktop, Weather, Updates, General, About), chevrons, check, close and arrows.
- `PaintWiFiGlyph(target, box, brush, activeWaves, stroke)`: cached Wi-Fi geometry with zero to
  three active arcs. The Top Bar model maps its real Windows signal snapshot to that count;
  graphics never reads or invents network status. Inactive arcs use 28% of the incoming brush
  opacity. Known zero has a hollow dot; unavailable or invalid counts add an explicit question
  mark rather than drawing full signal or a disconnected cross. The painter restores brush
  opacity and the target transform. `BuildWiFiGlyphGeometry` exposes its geometry for headless
  bounds checks.
- `PaintStatusBadge`: InfoBar severity circles with a contrasting mark.
- `RoundStrokeStyle(factory)` for the Top Bar status glyphs, which live with the Top Bar.

Geometries and the stroke style are cached per factory on the calling thread
(`ReleaseGlyphResources()`).

## Text

`TextStyle` gains `opticalSmall` (Segoe UI Variable Small for 12 DIPs or less, when the text family
is Segoe UI Variable and the Small family is installed) and `tabularFigures` (the `tnum` feature
through a text layout, so changing digits such as the clock do not shift).

## Tests

Unit (`tests/unit/graphics`, no window): design tokens, motion and transitions, glass geometry,
glyph geometry, text formats, and painters rendered headless on a WARP device into an offscreen
bitmap (`D2DCanvas.h`): exterior-only shadow, shared nine-slice profile, grain, high contrast,
indicators, highlights, the two-tone focus ring, the lift shadow and icon drawing. Integration
(`tests/integration/ui`) owns the backdrop RGB-delta gate; executed native receipts are dated below
and in [shared UI](ui.md).

2026-10-20 focused verification: Release with warnings as errors builds successfully; all 64 graphics
unit cases pass. The first native run exposed an access violation while registering the batch's
`auto_revoke` completion handler. Explicit source/token ownership fixes that crash; the final shared
UI suite passes all 12 cases, including normal completion, cancellation, detach, destruction and
callback exceptions. These are scoped checks on the current Windows 11 runtime, not Windows 10 or
whole-product qualification. See [shared UI](ui.md) for the native material results.

2026-10-21 focused verification: the graphics target builds in Release with `/W4 /WX`; all 68
headless graphics cases pass. Added checks cover neutral Dock/Bar lighting in both palettes,
transparency-disabled opacity, and Wi-Fi geometry, distinct unknown/zero states, progressive signal
coverage, bounds and painter state restoration. The initial new lighting check failed at rounded
antialias corners (4 to 6 channel levels). Its final measurement uses the central straight-edge,
interior and exterior-shadow band, excluding corner rasterization while retaining the same
2-level tolerance and a directional Popup positive control. Initial, diagnostic and final receipts
remain local in ignored `build/wf-control-visuals/headless-graphics-{initial,diagnostic,final}.log`.
This run creates no windows or captures and changes no system controls. Desktop/compositor blur and
interactive menu behavior were not requalified for this material change.

## Not done here

- Frame pacing aligned to the DWM vblank (optional and measurement-gated in the plan) is not
  implemented; `FrameScheduler` keeps its relative interval.
- Acrylic saturation ("vibrancy") through a composition effect brush is deferred; the grain and tint
  stand in for it.
- Windows 10 has no host blur (unchanged fallback) and is untested for 0.2.0-alpha.1.
