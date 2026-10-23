# Cinematic desktop showcase

Status: **native pipeline implemented/tested; a genuine local 1080p60 MP4/GIF is rendered and
technically audited (S-033). Whole-film acceptance remains open.** This is the portable handoff for
FR-029/NFR-012 and D30–D33. The worker has finished and the normal preview is restored. Read the
[exact-artifact QC report](../video_production/07_QC_REPORT.md) before starting another run. No
100/100 score is claimed without normal-speed playback/listening and every required gate.

The later owner-approved Alpha 2 presentation baseline also uses reusable desktop/controls/settings
scenarios described below. Those source-faithful recordings are separate from the D33 authored film;
they do not inherit a whole-film quality score. A successful synthetic test is not a real gallery run.

The owner requires the complete final film to score **100/100** under the project-specific 60-check
rubric, with every blocking gate passed. See the [production brief](../video_production/README.md),
[creative direction](../video_production/02_CREATIVE_DIRECTION.md), and
[scorecard](../video_production/06_SCORECARD.md). The score is not a universal certification and may
only be assigned to the exact inspected export.

## Visual target

The owner-provided reference sets the opening composition: the real scenic desktop fills the frame;
the compact Top Status Bar sits near the top edge with equal side margins; the Dock is centered at
the bottom with the real Start, Search, Task View and pinned application logos. Keep the desktop
visible, quiet and free of unrelated open windows. Do not add promotional text or invented controls.
Hold the full scene for two seconds before the cursor moves. The user-provided reference image is a
design source only and is not copied into this repository. At the reference's 1920×1080 size, use a
top-bar edge inset near 1% of screen width, a Dock around one-third of the screen width and about
10% of its height, centered roughly 1% above the bottom edge; treat these as composition guides and
scale from live layout/DPI rather than hard-coded output pixels.

## Current source architecture

- Product code is C++20/Win32 with Direct2D/DirectWrite and Windows.UI.Composition; no third-party
  rendering dependency is introduced.
- `src/dock/DockWindow.cpp` paints the live Dock surface through the shared graphics context. Its
  application marks use both native vector drawing and Shell-provided bitmap icons. A bitmap icon
  must be treated as raster input even if it is transformed or supersampled.
- `src/topbar` uses the same native graphics/composition foundations for the live status surface.
- The showcase timeline is in `src/showcase/CinematicSequence.*`. It deterministically plans a stable
  full-desktop hold, curved left approach, fitted Dock zoom, full-Dock horizontal track, one-second
  Code hover, launch, loaded-workbench reveal, observed close and empty-desktop ending. The runner's
  D33 configuration holds the app for three seconds total after readiness, including 1.6 seconds of
  reveal and 1.4 stable seconds. The pointer returns to its neutral start during reveal. After the
  window is absent, two consecutive captured body/backdrop samples must match the original clean
  scene before the three-second empty ending starts, so DWM's closing fade is not counted. Its quintic
  profile settles position, velocity and acceleration at each move boundary; an explicitly selected
  custom cubic profile is available for art direction only when its endpoints have zero velocity. It
  waits for observed workbench readiness, not the first blank native Code window.
- The timeline is a pure planning module; the separate runner consumes its plan for real pointer
  movement, Dock activation, capture, rendering and encoding.
- `CinematicSequence::BuildMotionBlurSamples` plans up to eight evenly spaced timestamps across a
  frame exposure (180-degree shutter at 60 FPS by default).
- `FrameCompositor.*` is a CPU reference renderer. It samples the provider at those timestamps,
  evaluates the actual camera pose for each subframe, resamples captured pixels with Catmull-Rom
  filtering, and blends in premultiplied-alpha space. It reports actual sample count and elapsed CPU
  time. It remains a correctness reference rather than the runner's rendering path.
  Unit tests use synthetic buffers and are correctness checks only, not frame-rate or
  visual-quality evidence.
- `DxgiDesktopCapture.*` is the runner's current hardware desktop-duplication path, with SDR and
  unrotated-display checks, cursor handling, acquisition counters and present timestamps.
  `GdiDesktopCapture.*` remains available as a separate capture implementation. Neither path invents
  product pixels. Windows Graphics Capture was researched but is not the current runner backend.
- `FrameArchive.*` stores timestamped RGBA captures locally and reopens finalized archives read-only
  for offline edits. Reopen checks header, exact file size, geometry, bounded frame count and strictly
  increasing finite timestamps before allocating a bounded index; pixel loading is lazy.
- `GpuFrameCompositor.*` evaluates the camera at eight elapsed-time shutter timestamps per output
  frame, fetches genuine nearby captured frames, transforms them and blends in linear-light sRGB.
  Direct3D hardware is preferred with WARP fallback. Subframe camera evaluation does not create
  additional captured UI states between samples. Immutable provider-scoped frame IDs allow cached
  pixel reads/texture uploads when shutter samples reuse one source frame; unknown IDs forbid reuse.
  Cache correctness tests do not establish a real-device speed improvement.
- `Win32CleanSessionBackend.*` classifies visible processes by user/session and creation identity,
  protects Windows shell/security/critical processes, Aether, the worker and its controller ancestry,
  and supports explicit controller exclusions. Unclassifiable visible processes fail preflight.
- `tools/showcase/ShowcaseRunner.cpp` puts retained controller windows and desktop icons aside,
  closes user apps and Explorer windows, checks the opening scene/taskbar state, then re-queries
  the genuine Code pin after changing Dock entries settle. Input and observed Code launch are real.
  Before and after each capture, a visible-surface guard allows the Dock/Top Bar, genuine own-process
  tooltips, desktop shell and, after launch, a process with the exact `Code.exe` basename. Unexpected
  surfaces abort before finalization/rendering. This guard does not eliminate window-change races or
  inspect the contents of an allowed application; every captured frame still requires privacy review.
- `CodeReadinessObserver.*` polls enabled onscreen English workbench UIA markers on a separate COM
  worker with configured connection/transaction timeouts. It retains the verified HWND and process
  creation identity for the scenario close. Capture-thread polling makes no UIA provider calls.
  This observes workbench controls, not completion of arbitrary documents/extensions/network work;
  provider timeout settings are not an independent hard-deadline sandbox.
- `Mp4Writer.*` encodes RGBA frames through the Windows Media Foundation H.264 Sink Writer. It checks
  frame geometry and H.264 limits, writes to a unique temporary sibling, and moves the completed file
  into place without replacing an existing output. A focused runtime test writes a short MP4 from
  synthetic color frames, confirms Media Foundation can read its H.264 stream and sample count, and
  deletes the file. The writer also supports AAC-LC stereo at 48 kHz. `Soundtrack.*` generates original
  procedural audio with authored fades and motion/focus cues; it records no application audio and
  uses no third-party music. A native decoded NV12 orientation regression covers the corrected MP4
  inversion. Synthetic encoder tests do not constitute project footage or audio QC.
- `GifWriter.*` uses WIC, a shared palette and an infinite loop. The runner resizes every third
  rendered 60-FPS frame to at most 960 pixels wide, targeting 20 FPS with 5-centisecond delays.
  GIF timing/palette tests are separate from real-footage quality acceptance; native end-to-end WIC
  resized-frame channel checks now cover the corrected color conversion.
- `CaptureBuffer.*` stores one full first image and subsequent changed tiles in a bounded RAM arena
  (512 MiB by default). Appending performs no disk writes; explicit capacity/failure paths do not
  silently discard frames. After the observed close/three-second empty ending stops capture and old
  apps are restored, `FlushTo` reconstructs losslessly into `FrameArchive` before offline rendering.
  Its source/synthetic tests do not establish the final take's acquisition cadence.

## Motion-design quality bar

The owner wants a composed motion-design piece with the refinement associated with After Effects,
not a raw recording where UI states simply change. Treat that as a quality target, not a dependency
on Adobe software. The sequence must feel deliberately edited: an unhurried establishing hold,
continuous cursor and camera travel, a readable full-Dock close-up, a one-second focus pause and a
reveal only after the real workbench is loaded. Three seconds total after readiness includes reveal
and stable viewing; return the pointer during reveal, observe close, require two captured body/backdrop
matches with the original scene, and then record three visually empty seconds before restoring old apps.
Each transition is evaluated from
elapsed time at arbitrary subframe timestamps. The default quintic curve also makes acceleration
settle at the joins; automated tests measure near-zero motion around the choreography cuts and reveal.
The GPU renderer evaluates and blends genuine nearby captured frames at the eight shutter timestamps.
Source acquisition cadence still limits actual UI motion detail; camera samples cannot restore
missing captured states.

The renderer targets a measured, stable 60-frame-per-second MP4 master on capable
hardware. If motion blur is used, derive it from real captured temporal samples and measure the cost;
keep status text and app marks readable. The CPU compositor is a correctness reference and must not be
treated as a real-time performance claim. GPU tests check synthetic geometry and linear-light
temporal blending. GIF timing and palette optimization must be checked
separately. Add no false UI, synthetic logos, decorative glow, or promotional overlays. A smooth curve
or compositor test is not proof of a finished animation: inspect a rendered preview and the final
export, verify its cadence, and pass every row in the 100/100 scorecard.

## Capture and rendering strategy

The implemented nonblocking DXGI path acquires the real scene into bounded lossless RAM before the
post-restoration disk flush and offline GPU/encoding pass. The system pointer is composed explicitly
from its actual `GetCursorInfo` shape/hotspot/position. The live
desktop is raster input: camera zoom cannot restore detail absent from the captured pixels. The
runner has no native high-resolution offscreen Dock redraw. It preserves genuine product pixels,
Shell icon bitmaps, hover behavior and the observed VS Code window-visible event.

The implementation must measure output dimensions, frame rate, frame drops, memory and per-frame
render time before describing a quality level. `--output-size source` preserves physical monitor
dimensions by default; `--output-size 1920x1080` and `--output-size 3840x2160` configure delivery
dimensions. Smaller sources are resampled; larger output does not restore icon/text detail. 8K
remains aspirational, not an implemented runner option. Keep the high-quality MP4 master; derive the
looping GIF from the same rendered frames
or master, never from a lower-quality GIF conversion. Preserve existing outputs by choosing a new
name or requiring a clear overwrite policy.

Official API references consulted:

- [Windows Graphics Capture](https://learn.microsoft.com/en-us/windows/apps/develop/media-authoring-processing/screen-capture)
- [GDI BitBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt)
- [DXGI desktop duplication](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/desktop-dup-api)
- [Capture a window with `IGraphicsCaptureItemInterop`](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow)
- [Direct2D render targets](https://learn.microsoft.com/en-us/windows/win32/api/d2d1_1/nf-d2d1_1-id2d1devicecontext-settarget)
- [Windows Imaging Component GIF format](https://learn.microsoft.com/en-us/windows/win32/wic/gif-format-overview)
- [Media Foundation Sink Writer](https://learn.microsoft.com/en-us/windows/win32/medfound/using-the-sink-writer)
- [Restart Manager application guidance](https://learn.microsoft.com/en-us/windows/win32/rstmgr/guidelines-for-applications)
- [`RmRestart` only restarts applications registered for restart](https://learn.microsoft.com/en-us/windows/win32/rstmgr/functions)
- [VS Code command-line isolation](https://code.visualstudio.com/docs/configure/command-line)
- [VS Code portable-mode override](https://code.visualstudio.com/docs/setup/portable)
- [Start-Process waiting behavior](https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.management/start-process)

## Clean desktop session and restoration boundary

D30 is the owner's explicit 2026-10-22 instruction and supersedes the opposite clauses in the supplied
showcase brief §§7 and 11. The showcase is user-started; on start it closes visible application
windows in the active user session, requesting normal closure first and allowing termination of
remaining user applications, so the captured desktop is blank except for the wallpaper and Aether
surfaces. Keep Windows shell/security/critical processes, Aether and the capture worker alive. Never
run this destructive workflow from unit or integration tests.

Keep the process/window restart inventory in memory only. Do not log or persist command lines,
document names, browser URLs, tokens or process listings. On success, cancellation or failure, attempt
to relaunch every application the tool closed and report any failed relaunch without exposing paths.
Windows does not provide a universal way for one program to restore another program's unsaved work,
tabs, documents, profile state or original window arrangement. The documented Restart Manager restart
path applies to applications that register for restart; it cannot reconstruct arbitrary application
state. Some applications may not restart at all. A capture must not be described as a perfect
restoration; the owner's specific instruction authorizes closing apps, not claiming that lost app
state can be reconstructed. Controller window placements, desktop icon visibility and the pointer
are restored on normal cleanup; Explorer windows receive separate window-level close/reopen handling.
A completed capture can still export with a reported relaunch warning. D33 requires capture to finish
the three-second empty ending before restoring old apps, and restoration before disk flush/render.

RAII cleanup attempts relaunch after ordinary failures and capture-loop ESC cancellation. **Forced
runner termination, process-host loss or power failure bypass in-process cleanup.** No independent
recovery guardian exists, so restoration cannot be promised for those exits.

Generated media and private runtime metadata stay under a local per-user output directory by
default, outside the repository. Only manually selected, frame-by-frame reviewed showcase media may
be copied into public README assets. Public media may show only the selected wallpaper, Aether's two
surfaces, pinned application logos and the app opened by the scenario; no unrelated app content,
personal documents, private paths, machine identifiers or credentials.

Raw archives and scene data stay under `%LOCALAPPDATA%\AetherShell\Showcase`; MP4/GIF output uses the
Windows Videos known folder's `Aether Shell` directory. Local progress is
`%LOCALAPPDATA%\AetherShell\Showcase\last-run-status.txt`; an output's `.qc.txt` receipt records
cadence/composition measurements. No process inventory belongs in these receipts. The opening and
per-frame scene checks are safeguards, not a privacy guarantee: a surface may change between checks,
and allowed application content may itself be private. Inspect every frame before sharing.

## Prepare and run a private scene

`tools/showcase/Prepare-ShowcaseScene.ps1` reads current Aether settings into a fresh private scene
below `%LOCALAPPDATA%\AetherShell\Showcase\scenes`. Genuine pin identities, names, executable icons
and ordering are preserved. Only the cloned Code launch route changes to a scene-owned shortcut with
an empty user-data directory, fresh empty extensions directory, no restored windows
and no startup editor. The user's Code profile/workspace is never copied. Portable Code with an
adjacent `data` directory that overrides isolation is rejected. No owner-installed extensions are
copied; built-in extensions keep normal behavior. The launch omits `--disable-extensions` because its
warning toast would intrude into the film. This changes no app or operating-system security setting.

Preparation launches, closes, hides and captures nothing; original settings/profiles are not edited.
The returned scene paths are private runtime values: retain them in memory, never public receipts.
Start the built Aether candidate with that scene's Aether data directory for capture, then restore
the owner's usable preview/profile afterward. Do not change installation files or register startup.

Build the runner and focused synthetic/read-only tests through the canonical Release warning gate:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/showcase -Modules showcase `
    -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON', '-DAETHER_BUILD_SHOWCASE_RUNNER=ON') `
    -Test -TestRegex '^aether_showcase_tests$'
```

This focused command builds showcase dependencies, not the complete product application. The current
Alpha 2 application must already run with the prepared scene and taskbar replacement enabled.

1. Run `AetherShowcaseRunner.exe --preflight --quiet`. It checks Aether surfaces, the Code pin,
   taskbar state and restart descriptors without closing, hiding or capturing applications.
2. Pass each retained capture-controller process as `--keep-pid <pid>` to preflight and the real run.
   Runner ancestors are protected automatically. Explicit exclusions cover other controller processes;
   they are capture infrastructure, not a bypass for unrelated visible user content.
3. Explicitly start `AetherShowcaseRunner.exe --quiet`. Starting the tool is the owner's authorization;
   there is no second confirmation prompt. During capture, ESC requests cancellation and cleanup.
   The real pointer is occupied by the sequence.
4. Wait for the runner process itself, inspect its exit code/local status, then inspect the outputs.
   In PowerShell use `Start-Process -PassThru -WindowStyle Hidden` and the returned process object's
   `.WaitForExit()`. Avoid `Start-Process -Wait`: on Windows it can wait for the process tree,
   including applications deliberately reopened after capture.

For a reviewed source capture, `--render-only <absolute-local-recipe>` reopens the finalized
archive and performs offline camera/export work without closing applications or capturing again.
The numeric recipe is limited to 4096 bytes, rejects network paths and trailing tokens, and version
one supports default quintic easing only; version two additionally stores the observed close event
and empty-ending timing. Before allocating output, rendering checks finite timing,
at most 120 seconds, a launch-visible event within the source interval, and captured coverage through
the ending. A valid recipe is not evidence that its footage is private or visually acceptable.

## Reusable presentation scenarios

The same explicitly started native runner supports baseline documentation for Alpha 2 and later
versions. These scenarios reuse protected app closure/restoration, DXGI capture with the real pointer,
the bounded lossless tile buffer, local archive and H.264 encoder. They record full-desktop product
behavior without camera effects, invented pixels, overlays or application audio. The default delivery
rate is 60 FPS; `--fps 30` explicitly selects a smaller 30-FPS documentation recording. Output rate
does not establish the source's acquisition or display-update cadence.

```powershell
# Read-only preflight; does not open controls, capture pixels or change devices.
AetherShowcaseRunner.exe --scenario gallery --preflight --quiet --keep-pid <controller-pid>

# One user-started clean session: desktop, Quick Controls and all eight Settings pages.
AetherShowcaseRunner.exe --scenario gallery --quiet --keep-pid <controller-pid>

# Also demonstrate the real output-volume slider. Use only with owner device-change authorization.
AetherShowcaseRunner.exe --scenario gallery-volume --allow-device-changes --quiet --keep-pid <controller-pid>
```

Starting a capture authorizes the documented clean-session app closure. It does not make subsequent
automatic tests eligible to close applications or change devices. `--allow-device-changes` is an
explicit run option for the owner-authorized volume demonstration, not another confirmation dialog.
No radio, network, microphone, brightness, startup or saved Settings value is changed by these plans.
Capture profiles continue to have startup disabled, independently of installed-application policy.
For these non-launching scenarios, `Prepare-ShowcaseScene.ps1 -DesktopOnly` clones the actual pins
without requiring Code or creating another application profile. It returns null Code-related paths.

Named plans are `desktop`, `quick-controls`, `settings`, `volume`, `gallery` and `gallery-volume`.
Desktop/controls/settings last five seconds; volume lasts seven. Gallery lasts seventeen seconds:
Quick Controls at 0.5 s; optional eased 20-percentage-point downward volume excursion from 1.5–3.5 s;
original volume restoration at 4 s; panel close at 4.2 s; Settings at 4.6 s; then Dock, Top Bar,
Weather, Updates, General, About, Desktop and Appearance at 5.8 s plus 1.3 s per page. The first
Settings view is Appearance. Lookup uses real UI Automation controls rooted in verified Aether HWNDs
and the same process; unavailable controls fail instead of using guessed coordinates. The existing
cinematic no-scenario path still requires the genuine Code pin; presentation scenarios do not.

The authorized volume demonstration writes only to the retained default-console audio endpoint;
it never posts an application request that could later redirect to another endpoint. Before any
change, the runner retains its precise scalar and identity in memory. Each distinct rounded target
must be observed on both that endpoint and the real Aether UIA RangeValue before another write;
duplicate targets are skipped. Endpoint switches fail the take. Cleanup restores the original scalar
and requires repeated stable readback before disarming recovery; failure leaves recovery armed.
Action failures remain failures even when best-effort restoration succeeds. Ordinary cleanup closes only
panels opened by the scenario. A changed/removed endpoint, provider failure, forced termination
or power loss can prevent restoration and must not be described as guaranteed recovery. Existing
open Aether editors/panels are rejected so pending owner changes are not edited by a capture.

The local Videos output directory receives a uniquely named silent MP4, primary PNG, `-desktop.png`
and numbered `-shot-N.png` stills selected 0.8 s after each observed open/page completion. Gallery shot 0 is Quick
Controls, shot 1 is the initial Appearance view, and shots 2–9 follow the eight-page order above.
All stills come from genuine captured frames, use lossless WIC PNG and contain no injected metadata.
Existing outputs are never replaced. The presentation QC receipt records planned output rate, actual
source count/mean cadence, P95/maximum interval, intervals over 25 ms, buffer usage, restoration result
and the requirement for privacy review. Raw archives
and receipts remain private local material. Rendering and PNG/MP4 disk work occur after restoration.

At 1920×1080, a 17-second 60-FPS gallery requires roughly 8.47 GB for the uncompressed on-disk archive,
plus outputs, headers and build space; 30 FPS roughly halves archive size. At a 4K source the archive
requires four times as much. The runner checks the conservative full-frame archive requirement
before closing applications. The RAM spool is separately capped at 512 MiB and fails explicitly
instead of silently dropping changed frames. Do not delete retained source/QC material to bypass
insufficient-space errors without authorization.

Acquisition runs on a dedicated MTA `ConcurrentRecorder` worker. Its callback performs only surface
guards, DXGI acquisition and RAM-buffer append. The coordinating thread serializes UIA actions and
privacy queries; slow providers cannot block acquisition. Nonblocking polling reports worker errors.
Success, cancellation and failure join acquisition before panel/device/app cleanup or archive flush;
destruction order also preserves that boundary. Fake tests cover a blocked coordinator, acquisition
failure/exception, stop/join, completion coverage and deferred action-failure restoration. These are
thread-boundary checks, not proof of real-device frame rate or provider reliability.

For broader release documentation, `gallery-full` and `gallery-full-volume` preserve the seventeen-
second plans unchanged and add a separate 24.5-second sequence. It records Quick Controls page one,
page two (Night light, Mobile hotspot, Nearby sharing, Cast and Project), fixed `explor` application
search, the real pinned Explorer context menu, the Top Bar logo menu and all eight Settings pages.
It never launches Explorer or opens operating-system settings/flyouts. The extra context menu is a
real right-click using `SendInput` at current UIA bounds; no hard-coded icon coordinates are used.

```powershell
AetherShowcaseRunner.exe --preflight --scenario gallery-full-volume --allow-device-changes `
    --scene-settings <absolute-prepared-scene-settings-file> --fps 60 --quiet --keep-pid <controller-pid>
```

Remove `--preflight` only for the owner-authorized real capture. `--scene-settings` is required for
the Explorer context action: it reads at most 1 MiB from an absolute local non-UNC settings file,
verifies one saved pin against the genuine Windows Explorer executable or official AppUserModelID,
then uses its actual localized name to require one visible pinned UIA control. Paths, pin selectors
and settings contents are not logged. Do not pass a stale settings source from another preview.

Full-gallery numbered shots are 0 Quick Controls page one, 1 page two, 2 Search, 3 Explorer context,
4 Top Bar menu, 5 initial Appearance and 6–13 the eight Settings pages. It requires roughly 12.2 GB
of raw 1080p60 storage plus the output/headroom budget. Action start/completion times are measured;
every action must complete before capture ends and every selected still must have a clear 0.8-second
settled interval before the next scene change. A missed schedule fails rather than selecting a
mislabelled page. Search displays only real app names and generic kind labels; the fixed query's
result names must match known Explorer/Explorateur names or the verified saved Explorer pin. Other
results fail closed. These checks do not replace every-frame/full-resolution privacy review.

For later versions, `--action-plan <absolute-local-file>` accepts a bounded numeric plan instead of
`--scenario`. It permits 1–30 seconds and at most 128 ordered actions. Unknown actions, nonfinite or
out-of-range numbers, oversized files, UNC paths and trailing tokens fail validation. The format is:

```text
AETHER_PRESENTATION_1
6 2 2
0.5 1 0
4 5 0
```

The second line is duration, primary-still time and action count. Each action line is elapsed
seconds, numeric kind and value. Kinds are 1 open Quick Controls, 2 open Settings/Appearance,
3 volume offset (−30 to +30 percentage points from the saved value), 4 restore volume, 5 close
scenario panels, 6 select Settings page (integer 0–7 in the order above), 7 next Quick Controls page,
8 fixed Explorer search, 9 verified Explorer context menu and 10 Top Bar logo menu. Only kind 3 requires
`--allow-device-changes`; it cannot launch arbitrary applications or run commands. These plans
contain no arbitrary UI selector, process inventory, device identity or executable path.

Privacy remains a separate gate. The visible-surface guard admits only known Aether-owned panels
and menus. A model-backed UIA assertion must explicitly report `No media session`, with disabled
transport/session controls as an additional check; a single capability-free session still fails.
Those
guards cannot inspect every rendered string or eliminate UI races. The Control Center can display
an audio-device label; Settings may display local location/pin values. Review all video frames and
each full-resolution still for SSIDs, profile/account names, addresses, paths, media or unrelated
content before selecting public assets. Never replace real data with invented product information.

## Test checkpoint and remaining acceptance

The root continuation reports a passing aggregate Release `/W4 /WX` suite after the encoder/archive
fixes; exact commands/counts and any later regression belong in
[TEST_STATUS](knowledge/TEST_STATUS.md). This is implementation evidence. Coverage uses synthetic
camera/compositor/archive/encoder data, fake lifecycle operations and read-only controller inspection;
automated tests never close real apps.

Three real takes were rejected: the first had a black wallpaper and approximately 17 captured samples
per second; the second again had a black wallpaper, 12.44 samples per second, a 5.95-second gap and
unrelated private application content in its final frame. A subsequent restored-background take
measured roughly 34.6 samples per second but lacked the pointer and exposed inverted MP4 and
swapped-channel resized GIF output. None may be reused as public media.
These sampling figures are not display-update or output-encoding rates. The wallpaper source was
unavailable; a cached wallpaper was restored and the owner confirmed that it was visible again.
A fresh capture must verify that opening composition, rather than rely on the confirmation alone.

The native buffer, explicit cursor path, async workbench observer, MP4 orientation and GIF channel
corrections are implemented. The latest gate passes 66 focused Release `/W4 /WX` cases in 2.17 s,
plus two own-window integration cases in 105 ms. A 964-frame intermediate still had a setup toast,
about 2.85 visually empty ending seconds and hover behavior requiring correction. These are corrected
in the C5FF64A3 candidate: 1,030 decoded frames, 1920×1080 progressive BT.709, uniform 60-FPS PTS,
and measured source central-backdrop clearance of 3.0238984 s. Its 985 source samples average
57.3721/s (P95 interval 29.92 ms; maximum 108.07 ms); the separate DXGI receipt counts 236 desktop
presents and 316 acquisitions. The buffer stored 147,153,152 bytes. Static image reuse and offline
subframe camera rendering do not establish constant-60-FPS UI capture.

All decoded frames were reviewed through 21 contact sheets plus critical full-resolution images;
only the permitted genuine scene was observed in that scope. This is not full-resolution every-pixel
inspection. No unrelated app, private path/account content, toast or native taskbar was observed.
Audio decodes with no clipped/non-finite samples (−22.6 LUFS, −10.5 dBFS true peak); it has not
been listened to. The 98.38-MB GIF is not yet optimized for README embedding. The complete exact
export still needs normal-speed playback/listening and every scorecard gate. Review S-033 and the
production QC report before assigning any score. A successful build, synthetic test or attractive
still cannot establish whole-film acceptance. The large raw archive and all metadata remain local;
four explicitly identified rejected raw spools were removed while retaining their local QC receipts.
