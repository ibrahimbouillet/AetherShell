# Project audit — Aether Shell showcase

Audit basis: current repository source, the root continuation's Release `/W4 /WX` receipts and its
sanitized real-take findings, including S-033's exact C5FF64A3 export audit. These are distinct
evidence classes: implemented modules and passing synthetic tests do not establish an accepted film.

## Product and audience

- Product: Aether Shell, a native Windows desktop interface with a bottom Dock and a Top Status Bar.
- Intended audience: Windows users considering a more app-centered desktop shell.
- Core message for this film: Aether brings the user's chosen apps and useful status into one calm,
  coherent desktop composition.
- Project constraints: Windows 10 (2004+) and Windows 11; C++20/Win32, Direct2D/DirectWrite and
  Windows composition; public-facing documentation and source remain English.

## T36 evidence inventory

| Capability | Evidence currently available | Status for filming |
|---|---|---|
| Time-based cinematic camera and eight-sample shutter plan | `CinematicSequence.*`; deterministic synthetic tests | Implemented; final motion review pending |
| Clean-session close/relaunch | `CleanSession.*`, `Win32CleanSessionBackend.*`; fake lifecycle tests and read-only controller checks | Native backend implemented; restoration remains best effort |
| Real acquisition and frame spool | `DxgiDesktopCapture.*`, `CaptureBuffer.*`, `FrameArchive.*`; validated read-only reopen | Bounded tile RAM used in latest real take; 985 samples, 236 desktop presents, 316 acquisitions; variable cadence |
| Pixel composition | `FrameCompositor.*` CPU reference; `GpuFrameCompositor.*` Direct3D/WARP path | Eight timestamped camera samples; immutable frame-ID cache; synthetic pixel tests |
| Genuine Dock and status surfaces | Live `src/dock` and `src/topbar` product pixels and native pinned logos | Captured as raster source; no high-resolution offscreen redraw |
| Real Code activation | UI Automation pin lookup, real input and asynchronous enabled workbench markers | Latest take uses isolated empty profile; loaded reveal/close visible in inspected frames |
| MP4/audio and GIF exporters | `Mp4Writer.*`, `Soundtrack.*`, `GifWriter.*`; synthetic tests and exact-export decode | 1,030-frame 1080p60 MP4 plus 344-frame GIF decode; complete playback/listening remains unverified |
| Capture guard and offline recipe | Per-frame visible-surface checks; bounded numeric recipe and source coverage validation | Safeguards implemented; races/content still require frame-level review |

## Visual and asset facts

The approved composition uses a scenic desktop, an inset Top Status Bar, and a centered bottom Dock
with the genuine product controls and pinned application logos. Live capture must use Aether's actual
surfaces and actual desktop pixels. Existing raster app icons remain raster; zooming or resampling does
not restore source detail. The owner-provided screenshots are references only and are not repository
assets.

Audio is original procedural stereo from `Soundtrack.*`; it records no private application audio and
uses no third-party music. Exact-export loudness/peak/clipping measurements pass; listening and sync
review remain unverified. The [asset register](03_ASSET_REGISTER.md) distinguishes the third-party
wallpaper/app marks from original source. The owner's composition reference alone is not a rights
inventory.

## Rejected take evidence

The first take had a black wallpaper and approximately 17 captured samples per second. The second
had a black wallpaper, 12.44 samples per second, a 5.95-second gap and private unrelated application
content in its last frame. Both are rejected and stay outside public material. Sampling cadence is
not the same measurement as actual DXGI display updates or the nominal 60-FPS output timeline.

The wallpaper's original source was unavailable. A local cached wallpaper was restored, and the
owner confirmed seeing it again. The latest inspected opening shows the restored scene. Frame-ID
reuse reduces redundant reads/uploads and `CaptureBuffer` removes spool-file writes from acquisition.
Neither implies constant-60-FPS native UI capture. S-033 separates source intervals, DXGI presents,
offline render cost and encoded PTS. Four named rejected raw spools were removed locally; their QC
receipts remain, and those missing sources cannot be rerendered.

## Capture, privacy, and known limits

- The user-started capture is authorized to request normal closure of visible user applications and,
  if needed, terminate remaining eligible user apps; it must keep Windows shell/security/critical
  processes, Aether, and the capture worker alive.
- Automated tests use fake frame/process providers and never close real applications or capture the
  owner's desktop.
- Relaunching applications is best effort. Unsaved content, tabs, documents, profile state, and
  window placement cannot be universally restored.
- Keep footage, capture metadata, and process descriptors local until a frame-by-frame privacy review.
  Public material may contain only the approved clean desktop composition, Aether surfaces, pinned
  app logos, and the scenario application.
- Before/after-capture guards abort on unexpected visible windows and permit the genuine scenario
  app only after launch. They cannot prove allowed app content is safe or eliminate window-change
  races; every frame must still receive privacy review.
- Offline recipes are local, bounded to 4096 bytes and reject unsupported/trailing fields. Rendering
  checks source/event coverage and a 120-second maximum before allocation; offline edits do not
  require closing applications again. Default quintic easing is the supported recorded profile.
- C5FF64A3 has complete decode, measured cadence/audio and scoped contact-sheet privacy review.
  Normal-speed playback/listening and the complete scorecard remain open. Do not describe the
  candidate or rejected footage as a 100/100 accepted cinematic result.
