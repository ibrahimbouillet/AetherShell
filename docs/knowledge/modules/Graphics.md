# Graphics and UI base — verified

Source: `src/graphics`, `src/ui`. Tests: `tests/unit/graphics`, `tests/integration/ui/GlassProbeTests.cpp`.

| Type | File | Role |
|---|---|---|
| `GraphicsDevice` | `graphics/GraphicsDevice.h` | DispatcherQueue, compositor, D3D11 (WARP fallback), D2D, DWrite, WIC; `HandleDeviceLost()`, `DeviceReplaced` |
| `CompositionSurface` | `graphics/CompositionSurface.h` | per-HWND visual tree; `SetBackdrop(HostBlur/None)`, `Draw(dpi, fn)` in DIPs |
| `FrameScheduler` / `IFrameClient` | `graphics/FrameScheduler.h` | high-resolution waitable timer at refresh rate, only while requested |
| `Tween`, `Spring`, `Ease`, `MotionPolicy` | `graphics/Animation.h` | interruptible, frame-rate independent; reduced motion → instant |
| `DesignTokens`, `MakeDesignTokens` | `graphics/Theme.h` | colours, glass, radii, spacing, typography, durations; dark/light/high contrast |
| `PaintGlassPanel`, `PaintFocusRing`, `PaintFallbackIcon` | `graphics/GlassPainter.h` | the glass material |
| `TextCache`, `BitmapCache` | `graphics/TextCache.h`, `BitmapCache.h` | DWrite formats; device-generation-tagged D2D bitmaps (pixels, files, RCDATA) |
| `SurfaceWindow` | `ui/SurfaceWindow.h` | base popup: DPI, input regions, pointer/keyboard virtuals, `Invalidate`, `RequestAnimationFrame`, `OnAnimate` |
| `TooltipWindow` | `ui/TooltipWindow.h` | hit-transparent glass tooltip |

Gotchas
- Subclasses of `SurfaceWindow` must call `Destroy()` in their own destructor.
- Call `SetBackdropPanel()` whenever the panel geometry changes (it is in DIPs).
- Composition visual units are physical pixels; drawing units are DIPs.
- Needs `dxguid` for effect CLSIDs; links `windowsapp`, `CoreMessaging`.
