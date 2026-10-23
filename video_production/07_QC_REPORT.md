# Showcase QC report — local candidate C5FF64A3

Date: 2026-10-23. Target: the owner's complete-film 100/100 requirement, using the
[60 checks and ten blocking gates](06_SCORECARD.md). This report records an exported and measured
candidate; it does not certify final acceptance or authorize publication.

## Exact artifact

| Property | Measured value |
|---|---|
| MP4 | `<local-export>/Aether-Shell-Showcase-C5FF64A3E03848EFA7FCD18A0F236221.mp4` |
| SHA-256 | `6839bf39c247896265d31cc37278e3eb16fa5a06c2eb6e6a8e55cb189c2cf7a2` |
| File size | 51,139,749 bytes |
| Video | H.264; 1920×1080; progressive; SDR BT.709; `yuv420p` limited range; rotation 0 |
| Delivery cadence | 60 FPS; 1,030 decoded frames; first PTS 0.000 s; final PTS 17.150 s; frame-count duration 17.166667 s |
| Audio | AAC-LC; 48,000 Hz; stereo; 192 kb/s; original procedural score |
| Source | 985 timestamped 1920×1080 samples from take `D6B936B041634529BFF9B521281DDF85` |
| Source snapshot | Local `codex/t36-showcase-checkpoint` at `bf853e23d60df4ac190c01f4e0ac6d565e92d3ad`; not a public release |

Lineage and attribution are in the [asset register](03_ASSET_REGISTER.md). Private source paths,
process inventory, footage, recipes, and detailed QC remain outside the public checkout. The native
receipt's 17.1605 s is the configured sequence duration; the encoded frame-count duration is
17.166667 s. Rounded container reports may display 17.17 s.

## Executed checks

Evidence set: `<local-qc>/final-C5FF64A3-5b69c697/`, containing `numeric-quality.json`,
`decode-timing.log`, `audio-ebur128.log`, `source-empty-hold.json`, and the image review files.
Measurements were produced with FFmpeg 7.1 and the native capture receipt. The final numeric report
was read directly; visual observations below are the scene-review agent's recorded inspection.

| Check | Result and interpretation |
|---|---|
| Complete decode | Exit 0, 1,030 frames, no recorded decode errors. This proves decode, not normal-speed viewing. |
| Output PTS | Strictly monotonic; intervals 16.666–16.667 ms; no interval outside the 60-FPS target with 10 µs tolerance. |
| Dark frames | Zero decoded frames with >99% black pixels. The scenic background is present in reviewed opening and ending keys. |
| Exact duplicates | Two adjacent exact duplicates at approximately 0.650 and 0.667 s, within the deliberate static opening hold; not evidence of a frozen moving shot. Perceptual motion review remains pending. |
| Luminance steps | Maximum adjacent mean-luma change 0.0408914; P99 0.00564886; no step >0.05. A whole-frame average is not a temporal flash-risk assessment. |
| Orientation | MP4 error against upright raw opening: MAE 3.6843; against vertically flipped raw: 39.8131. Selected decoded keys inspected upright. |
| Audio decode | PCM and loudness analysis exit 0; 824,320 stereo sample frames; approximately 17.173333 s decoded audio; zero nonfinite and zero clipped samples. |
| Audio level | Integrated -22.6 LUFS; loudness range 1.3 LU. FFmpeg's true-peak analysis prints -10.5 dBFS; decoded sample absolute peak 0.300039. These measurements do not prove listening comfort, sync, or a channel-specific loudness standard. |

### Actual capture and offline render cadence

The native receipt reports 57.3721 sampled FPS, P95 source interval 29.973 ms, and 10.7724% of sample
intervals above 25 ms. The independent source-index calculation gives P95 29.91659 ms; the methods
and populations are recorded separately rather than silently treated as identical. Native DXGI
counters are 236 desktop presents and 316 acquisitions for 985 sampled frames. Static frames can be
reused, so none of these counts means 985 distinct desktop presentations or a 60-Hz source stream.

The bounded buffer committed 147,157,168 bytes and stored 147,153,152 bytes before archive flush.
Offline GPU composition mean was 19.7638 ms/frame, P95 31.903 ms, with 61.9417% above 16.667 ms;
hardware acceleration was reported. This is an offline render, not measured real-time 60-FPS
composition. The final file has a regular 60-FPS timeline regardless of acquisition/render wall time.

### Full-sequence image and privacy review

The scene-review agent inspected all 1,030 decoded frames through **21 contact sheets**, plus selected
full-resolution critical keys at frames 420, 660, 750, 810, and 1029. The retained key set also covers
the opening, approach, launch, reveal, close, and ending. It observed only the scenic wallpaper,
genuine Aether surfaces and pinned logos, real pointer, and a fresh empty Code window. No unrelated
app content, personal document, private path, account name, or disabled-extensions toast was observed
within that review scope. Footer clipping was not observed in the inspected close-up/reveal keys.
The final key has neutral pointer placement and cleared tooltip/magnification.

A second independent review covered all 985 raw frames and all 1,030 exported frames through a
separate contact set and full-resolution cursor/critical checks, with no blocking visual fault found.
It observed a genuine stable Code background shade change near 12.37 s; the menus, logo and pointer
remain intact and the shade does not flash back. This app appearance change is retained rather than
fabricated away. Root inspected the 7-s close-up, 13-s workbench and final image at full resolution.

**This is all-frame contact-sheet coverage plus selected full-resolution inspection. It is not
every-pixel full-resolution inspection of every frame, normal-speed playback, or an absolute privacy
guarantee.** Runtime guards cannot remove allowed-content or window-transition races. Keep the
reviewed media local until the owner selects material for public use.

### Observed event timing and empty ending

| Source/recipe event | Time |
|---|---:|
| Deliberate desktop opening | 0.000–2.000 s |
| Curved approach, zoom, full-Dock traverse, and one-second Code hover | 2.000–7.900 s |
| Actual Code click | Approximately 7.900 s |
| Loaded English workbench readiness | 10.7441257 s |
| Three-second loaded-state interval, including camera reveal | 10.7441257–13.7441257 s |
| Recipe's visually clear ending start | 14.1604877 s |
| Configured end before frame quantization | 17.1604877 s |

The source empty-hold analysis compares every fourth pixel in the large central region
`x=300:1600, y=100:900` against the final empty source frame. That region matches continuously from
source index 751 at 14.1448422 s to the last sample at 17.1687406 s: **3.0238984 s**. This supports the
requested empty ending; it is a central-region sample test, not equality of every full-frame pixel.
The runner waits for window absence and two matching captured backdrop frames before starting its
three-second hold, stops capture, then restores prior apps before flushing/rendering. Relaunch
attempts were reported successful; restoration of unsaved work or app-specific state is not proven.

## GIF derivative

| Property | Result |
|---|---|
| File | Same candidate stem with `.gif`; 98,379,380 bytes |
| SHA-256 | `7e90507c33f1d266b987e8fc3449fff315a488276bd1d65c5eab9a46162dd143` |
| Frames and timing | 344 frames; 960×540; 50 ms per frame; 17.2 s; loop enabled |
| Channel check | Opening MAE 6.0925 against true-color source versus 13.8508 with red/blue exchanged |
| Review | Selected opening, motion, app, and ending GIF keys inspected; not a complete normal-speed GIF playback |

The GIF is approximately 98.38 MB and is unsuitable as an unexamined default README download. A
smaller derivative needs its own measurements and review. The MP4 is the primary local deliverable.

## Verdict and remaining work

**Exported and technically measured; whole-film acceptance pending.** Full-sequence contact review
and selected full-resolution keys passed the stated privacy/composition scope. No normal-speed
start-to-end playback or audio listening has been performed. Motion quality, audio synchronization,
comfort/accessibility, final asset distribution review, and the remaining scorecard checks stay open.
No 100/100 score, external quality certification, or publication readiness is claimed.

The [correction log](05_CORRECTIONS.md) records rejected takes and implemented fixes. Rejected raw
archives were selectively removed (approximately 14.68 GB reclaimed); local QC remains available.
Any re-export invalidates this exact-file verdict until its hash and affected checks are refreshed.
