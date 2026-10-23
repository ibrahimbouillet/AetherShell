<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Ibrahim Bouillet -->
# Showcase tools

`Review-Presentation.py` is an offline evidence generator for an explicit local MP4. It never
captures the desktop, changes a device, closes an application, publishes media, or approves a
privacy/motion score. It uses the already installed Pillow and FFmpeg tools; it installs nothing.
FFmpeg is found through an installed `imageio_ffmpeg` package or `PATH`, or an explicit `--ffmpeg`.

```powershell
python tools/showcase/Review-Presentation.py '<private-input>.mp4' --stills '2s,4.5s,8s'
python tools/showcase/Review-Presentation.py '<private-input>.mp4' `
  --ffmpeg '<installed-ffmpeg-executable>' --output '<new-private-directory>' --stills '12,2.5s'
```

The output directory must not exist. The default is a fresh token under the Git-ignored
`.local/showcase-review/`. Both default and explicit paths are resolved before the containment
check, so a `.local/` junction cannot redirect output into an unignored repository directory.
Explicit repository output must also be under `.local/` or `build/`.
External output directories remain private and must never be copied into public source by default.
Existing directories and their contents are never overwritten. Failed runs retain a categorical
`failure.json` and any partial evidence in their newly created private directory.

Both decode passes explicitly select the MP4/MOV demuxer and allow only local `file`/`pipe`
protocols. A filename ending in `.mp4` cannot enable automatic playlist/network demuxing.
The tool decodes every video frame twice: one pass records exact presentation timestamps, per-frame
hashes and 8-bit luma statistics; the second saves complete numbered contact sheets with frame/time
labels. First and last frames are saved at full resolution. Use `--stills` for additional zero-based
frame indices or timestamps in seconds relative to the first frame, especially before/after every
known UI change. Timestamps select the nearest decoded frame; changes are not guessed automatically.
Contact layout defaults to six columns, eight rows and 320-pixel thumbnails. `--columns`, `--rows`
and `--thumbnail-width` customize it without omitting frames.

`review.json` records source SHA-256, size, decoded dimensions/count, duration, timestamp monotonicity,
uniformity, observed presentation cadence, every contact range and full-resolution still, and luma
statistics. It omits source/output paths, media titles, application/process metadata and raw FFmpeg
errors. `framehash.txt` and `signalstats.txt` preserve numeric decoder evidence. The default near-black
threshold is 18 on FFmpeg's converted 8-bit digital luma; the report distinguishes a dark mean from
an entire frame below that threshold. This does not establish that a frame is blank or safe.

Complete contact coverage is reduced-resolution evidence. A human still needs to inspect every
sheet, critical full-resolution images, and normal-speed playback with audio. Encoded presentation
cadence does not prove source acquisition/display cadence. The report leaves privacy, motion,
audio and publication acceptance pending; only separately executed receipts may establish them.
