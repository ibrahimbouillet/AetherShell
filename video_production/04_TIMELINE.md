# Showcase timeline — current deterministic plan

This is the exact owner-approved D33 runner scenario (2026-10-23), not the library's legacy ending
defaults or an accepted film. Times after launch depend on asynchronous loaded-workbench readiness
and observed window closure. A first blank native window is not readiness. Rejected takes are not
timing approval; the version-two numeric recipe retains ready/close events for offline rendering.

| Shot | Planned time | Action and new information | Motion / pacing | Current evidence |
|---|---:|---|---|---|
| S01 — Establish | 00:00.000–00:02.000 | Empty scenic desktop, inset Top Bar, centered Dock, genuine pins and visible real pointer | Stable hold; let the layout read | C5FF64A3 opening inspected; genuine cursor visible |
| S02 — Approach | 00:02.000–00:03.100 | Pointer takes a curved detour toward the Dock's left edge | Quintic approach; keep the whole path visible | Contacts inspected; normal-speed motion review open |
| S03 — Dock reveal | 00:03.100–00:04.500 | Camera resolves into a proportional Dock close-up | Rest-to-rest dive; cap raster magnification | Contacts inspected; normal-speed motion review open |
| S04 — Traverse | 00:04.500–00:06.900 | Travel across every genuine Dock entry toward Code using actual visible control bounds | Continuous camera follow; no fake divider or logo | Every-frame contact review; source cadence measured separately |
| S05 — Focus | 00:06.900–00:07.900 | Hover the Code icon for one full second | Deliberate pause and restrained cue | Full-resolution focus/footer/cursor inspected; audio sync open |
| S06 — Launch/wait | From 00:07.900 until ready event R | Click the actual pin once and await enabled onscreen English workbench markers | Asynchronous UIA worker; no blocking capture wait | R = 10.7441257 s; genuine loaded workbench observed |
| S07 — Reveal | R to R +1.600 s | Return from close-up to the genuine loaded app; pointer returns to its neutral starting location | Quintic reveal within the three-second ready interval | Contacts/full-resolution key review; normal-speed review open |
| S08 — Stable app | R +1.600 to R +3.000 s | Let the loaded app read for 1.4 stable seconds | Total ready-to-close interval is three seconds, reveal included | No toast/private content observed; genuine background change near 12.37 s |
| S09 — Close/settle | Request at R +3.000 s; wait for window absence and settled backdrop E | Send that scenario HWND `WM_CLOSE`; require two consecutive captured body/backdrop frames matching the original clean scene | Exclude DWM closing fade from the empty interval | E = 14.1604877 s; raw scene matches after closing fade |
| S10 — Empty ending | E to E +3.000 s | Genuine visually empty desktop, Dock and Top Bar; pointer already neutral | Stable three-second ending before capture stops | Independent central-scene measurement: 3.0238984 s clear |
| Cleanup/export | After E +3.000 s | Restore old apps, then flush bounded lossless tile RAM and render | Cleanup/encoding is outside the recorded scene | Relaunch attempts report success; normal preview restored; full acceptance open |

Total capture duration is E +3.000 seconds. The close request occurs R +3.000, including the reveal;
do not add another three seconds after the reveal. E is measured only after the window is absent and
two captured body/backdrop frames match the original clean scene, accounting for DWM's closing fade.
Closing latency remains observable. Rendering
rejects unsupported event order, timing outside captured coverage and durations above 120 seconds.
Recorded recipes support the default quintic profile; custom easing must never silently change.

Three real takes failed: black wallpaper at approximately 17 samples per second, then black wallpaper
at 12.44 samples per second with a 5.95-second gap and unrelated private content in the final frame.
Another restored-background take reached roughly 34.6 samples per second but omitted the pointer and
exposed MP4 inversion/GIF channel defects. Those takes cannot establish acceptance or provide public
style frames. The owner confirmed the wallpaper restoration. Bounded buffered acquisition, explicit
real cursor composition, async workbench readiness and an MP4 orientation regression are now in
source. Native NV12 and end-to-end WIC resize checks pass in the current 66-case Release `/W4 /WX`
suite (2.17 seconds). A 964-frame intermediate still had a setup warning toast, about 2.85 visually
empty ending seconds and hover behavior to refine. A fresh empty extensions directory removes the
warning-producing launch flag, and the new scene-settling gate protects the full three-second ending.
The latest C5FF64A3 film corrects those defects and decodes all 1,030 frames with uniform 60-FPS PTS.
Its planned duration is 17.1604877 s, rounded up to 1,030 delivery frames (17.1667 s); the GIF has
344 50-ms frames (17.2 s). Exact whole-film acceptance remains open. Inspect all joins,
pointer/camera peaks, readable holds, audio markers and the final loop in normal-speed playback. Do not
shorten shots or disguise missing source updates to obtain a nominal duration or frame-rate label.
