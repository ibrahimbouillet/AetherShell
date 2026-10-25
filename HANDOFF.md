# Handoff

> **PUBLIC REPOSITORY — READ THIS FIRST.** Never commit credentials, private keys or their locations,
> personal addresses/accounts/paths, machine names, or unrelated app content. Automated captures are
> limited to isolated project test windows. Narrow D30 exception: owner-approved showcase media may
> show only the clean desktop, Aether surfaces, pinned app logos and the app opened by the scenario;
> keep raw output local until every frame passes privacy review. See AGENTS.md and
> [showcase capture notes](docs/showcase-capture.md).

## Active continuation — Codex review of T42/T43, 2026-10-25 (Europe/Paris)

Last updated: 2026-10-25, by Codex. Claude has stopped writing; Codex owns the current edits.

**Independent review checkpoint (S-042):** official `origin`, `main`, one worktree and the existing
local history were checked. The two launcher files had unexpectedly moved to `installer/launcher/`;
their blob hashes exactly matched `HEAD:launcher/`. A checked plain move back restored the tracked
layout without content edits. The cause of the move is unknown. A fresh build folder avoids the
failed old CMake cache. Private archive recovery remains Claude's S-040 receipt, not a new Codex
restore verification.

Three own-window regressions reproduced unsafe preview input: tracker reorder could redirect a
pressed X or a keyboard close to a sibling, and capture loss/cancel mode could leave a close armed.
The review patch cancels stale pointer gestures, follows keyboard identity through reorder and
disarms a removed selection until navigation. Release `/W4 /WX` including the app and guard passes;
graphics 74, Dock unit 61, Dock native 10 executed plus one opt-in recorder skipped (3/3 CTest
executables, 49.17 s). No owner app was closed by these tests. Remaining: card anchoring during Dock
reflow, thumbnail crop/aspect after a source-only resize, media controls, physical DPI/external-app
acceptance and the other V3 tasks. See S-042 and the Dock module note.

**Local preview authorization:** the owner directly requested that the reviewed prototype be launched
on their computer and clearly announced so they can evaluate it. This covers the bounded shell
instance swap; D36 still requires a fresh ready reply for recording or hiding/closing their apps.
Prepare a private settings clone and an adjacent recovery helper; preserve the installed Alpha 2
files and startup registration. The runtime receipt will identify which binary is actually running.
No V3 installation, push, tag or release is authorized by this review.

### Current continuation — V3 accepted for development (D38)

**D39/T45 executed by Claude, 2026-10-25 — ONE shared working folder.** Claude and Codex now both
work in the single checkout named `AetherShell` (official `origin`, branch `main`; on the owner's
machine `%USERPROFILE%\Documents\AetherShell`). `git worktree list` shows only that folder. Do not
create another clone or assistant worktree. The canonical public Git lineage was moved there
unchanged (main `57427f0` and every local branch, tag and checkpoint ref preserved; verified
identical). The old private-history checkout, ten retired linked worktrees (Codex integration and
nine old Claude workflow trees, all HEADs already contained in `main`) and one hidden Codex worktree
of the private history were moved — not deleted — into a verified local recovery archive outside the
project, with Git bundles test-restored (fsck clean, refs identical). One retired workflow tree held
old uncommitted visual-core edits; they are preserved there as files and as a verified patch, not
applied (newer versions exist in `main`). Local tool state (`.claude/`, `.codex/`, `docs/.obsidian/`,
the owner's canvas) was restored into the shared folder and stays git-ignored. The previous build
tree was retired because its CMake caches named the old path: use fresh `build/<name>` folders.
Branches `codex/t36-showcase-checkpoint` (not in `main`), `wip/d27-d28` and `wf/b1-*` remain local
and unpushed. Receipt: S-040. Codex reviews this consolidation; the recovery location/inventory is
private and deliberately not recorded here. Installed Alpha 2 was not touched.

**T42/T43 implemented by Claude, 2026-10-25 — ready for Codex review (S-041).** Brief:
[docs/CLAUDE_V3_HOVER_PROMPT.md](docs/CLAUDE_V3_HOVER_PROMPT.md). Local commits only; the running
shell is still the installed Alpha 2 (`0.2.0-alpha.2`), which does **not** contain this work.

- *Implemented:* hovering a running background app expands its dot to exactly the foreground
  marker's width (`kDockMarkerPillWidth`, 12 DIPs) with no activation and no active colour; the
  dedicated `Motion().marker` spring is about twice as calm (≈0.35 s to 90 %, 0.52 s to 0.1 DIP).
  After a 0.6 s dwell a landscape neutral-glass card (`DockPreviewWindow`) replaces the name tooltip
  with genuine DWM thumbnails, one tile per live window, each with a title and a close button at
  its upper right. Icon↔card travel keeps card and emphasis (0.35 s grace); leaving both dismisses
  the card and contracts the marker. Clicking the icon or a tile activates through the existing
  launcher; a short emphasis hold lets the observed transfer keep B wide while A contracts. X posts
  `SC_CLOSE` to that identity-validated window only; refusals/cancelled prompts leave it open.
  Keyboard: focused Dock → Up opens the card, arrows/Tab, Enter/Space, Delete, Escape/Down. UIA list.
- *Tested:* focused Release `/W4 /WX` incl. `AetherShellApp`; dock unit 61 cases, graphics 74, dock
  native 8 (three new own-window preview cases; one opt-in recorder). Details in S-041.
- *Observed at runtime:* only own-window fixtures (synthetic windows, private backdrop): a 115-frame
  normal-speed sequence was inspected; DWM draws thumbnails above the card's visuals on this
  Windows 11 build (temporary reverted experiment). Frames are local and not committed.
- *Not done/unverified:* real applications on the owner's desktop, multi-monitor DPI change while
  shown, Windows 10, protected/elevated windows, media play/pause (later T43 step), owner acceptance.
- *Implementation choices to review (reversible):* timing constants in `DockHoverTiming`; 240×150-DIP
  thumbnail box; X in the tile header (DWM covers anything drawn over a thumbnail); a window already
  in front is left alone on tile click instead of being minimized; the card uses the Dock opacity
  preference with host-backdrop blur; marker colour switches instantly on the observed transfer
  while the width animates.

The owner accepted the published Alpha 2 and requested a broader V3 design/interaction overhaul.
The remaining-usage instruction prioritizes an English, traceable handoff, plus an initial input
correction. Read [the complete V3 work order](docs/specs/v3-design-and-interaction.md), requirements
FR-031–FR-036 and tasks T39–T44. [English prompt for the next assistant](docs/V3_CONTINUATION_PROMPT.md).
This first batch does not finish V3. No V3 release/tag or new semantic version has been approved.

Initial source work removes Dock/Top Bar shadow-expanded native window regions: actual input panels
and currently animated icons now define the HWND region. This avoids relying on same-thread-only
`HTTRANSPARENT` for another process. Outer shadows/entrance decoration are deliberately clipped;
rounded-corner input, full cross-process interaction and visual refinement remain. Focused native
regressions pass: focused Release `/W4 /WX`, 2/2 executables, 23.07 seconds (S-035).
AppBar reservation remains separate. T40 now reduces top interior padding from 12 to 9 DIPs:
the default panel is 67 instead of 70 DIPs, about 4–5 physical pixels shorter at 150% DPI. Icons,
saved sizes, baseline and visible bottom gap are unchanged. The installed Alpha 2 remains intact.

T41 now has a guarded Windows 11 Win+A route for the grouped status action when the existing
flyout preference is enabled, plus a Top Bar checkbox for the Aether fallback. Isolated sessions,
older Windows and opt-out retain the custom panel. Submission is not a native-panel acknowledgement;
do not retry an uncertain chord or open a competing panel. S-036 records passing scoped build/fake
tests and own-window preference persistence. Actual hidden-taskbar/native-panel behavior is unverified.

T42 now implements a per-application native spring: an observed running dot expands from 5 to
12 DIPs on hover or real foreground activation, retaining width/velocity through interruption.
Hover does not activate a window or change its active color, and closed pins never acquire a marker.
Reduced motion/high contrast snap; stable visible identities survive reorder/model refresh.
Release `/W4 /WX` passes 43 Dock unit cases and four native cases, 2/2 executables (S-037).
Physical motion acceptance, measured cadence and complete layout-origin continuity remain open.

The rest is requested/unimplemented: symmetric free edges; trustworthy signal freshness;
rounded icon redesign;
real hover previews with selected-window close and supported media controls; icon-anchored window
motion and rounded corners. The owner repeated the maximized-square-corner problem; read the
[corner investigation](docs/specs/window-corner-investigation.md). Native maximized rounding is
excluded by Windows policy; a simple DWM hint is not a fix. T44 now has an unwired owned-window
region prototype with true maximize/placement preservation, DPI geometry, identity, conditional
cleanup and synchronous-message reentry checks. S-038 passes seven new prototype cases inside the
19-case settings native suite. Fixtures are hidden with synthetic frame/work observations; no actual
DWM appearance or caption quality is established. Next: opt-in Settings-window observer/visible
qualification, without silently changing maximize semantics or manipulating foreign windows.
Owner-added demo commit `d9095f5` is preserved unchanged. Its media was not captured or reviewed
in this V3 continuation; do not infer T38 acceptance from the commit alone.
T38's gallery failure remains open and is deferred behind the new V3 priority. Never publish the
owner's supplied reference screenshot, which contains unrelated application content.

### Delivered — Alpha 2, 2026-10-23

**Published:** [0.2.0-alpha.2](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2)
at 14:47:05 UTC. Tag `v0.2.0-alpha.2` points exactly to source
`ad77019efbaed5aa59f96494da9e086e9006a4c2`, pushed to the official repository.
[Windows CI 39971203823](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39971203823)
passed for that exact revision. Subsequent documentation receipts do not change the release tag.

- The full Release `/W4 /WX` build and 22/22 unit executables pass (32.72 s). The final rebuilt
  app own-window integration executable passes (3.74 s), including three new live-audio regressions.
- Exact-revision packaging passes setup/embedded-payload privacy and freshness audit. Published
  assets are only the x64 setup (5,268,992 bytes; SHA-256
  `bdaa27f67f3e3caa215f05cb47dceffe95275255a157376670f21e490bbc82ba`) and checksums
  (110 bytes; SHA-256 `5ea049bfa70551b177d248118ffc6ebf57a74a56c171b5785bc847ffb3e70a26`).
  GitHub asset digests match the local files. No unsigned manifest or raw media was uploaded.
- Actual per-user upgrade from `0.2.0-alpha.1` committed successfully. Current preferences/pins were
  privately backed up and migrated to the default profile. Owner-authorized startup and taskbar
  replacement are enabled; HKCU Run targets the stable installed launcher with `--autostart`.
  No Task Manager disable was present or overwritten. Automatic pin import remains as previously
  configured. The installed Alpha 2 is running independently of a temporary command host.
- A controlled own-child exit with `0xC0000005` was followed by a new installed child after 1,143 ms.
  This measures process relaunch, not full visual readiness. Intentional Quit then stayed stopped
  and the native Windows taskbar was observed visible. The installed shell was relaunched and its
  hidden-taskbar preflight passed. No reboot/sign-in timing or universal recovery guarantee is claimed.
- **Media remains unfinished (T38).** D37 explicitly allowed publication first. The latest owner-ready
  no-volume gallery attempt also stopped with `a real Aether presentation control is unavailable`
  (`0x80070005`); the exact failing action is not yet identified. No usable fresh gallery/export was
  produced or published. Do not reuse rejected old media. D36 requires a fresh ready reply before
  every later real attempt. The earlier T36 film still has no complete 100/100 acceptance.
- Local-only evidence is in the newest `Showcase/alpha2-delivery-<id>/` receipts and default app logs.
  `installed-alpha2.local.json`, `installed-recovery.local.json` and `installed-quit.local.json` are
  current; older preview receipts identify stopped processes. Never commit those files or their paths.

**Previous next step (superseded by D38):** preserve the running installation. Continue T38 by diagnosing the precise native action
failure, then capture/review fresh stills and optionally MP4 for the versioned gallery. Do not repeat
publication, move the tag, publish an unsigned manifest, or release Alpha 3 without new authorization.

> **OWNER AUTHORIZATION — 2026-10-22 (D29).** The owner lifted the D25 delivery hold for the second
> public release only: commit and push the reviewed public `main`, tag `v0.2.0-alpha.2`, and publish
> the matching GitHub Release. This does not authorize unrelated repository/account changes or a
> later release. Recheck the current receipts below before continuing.

Long-term objective: a native C++20 Windows Dock and Top Status Bar alongside Explorer, with real
status, accessible interaction, durable settings, safe per-user installation and authenticated
updates. Earlier continuation sessions recovered Claude's interrupted `0.2.0-alpha.1` batches and
completed the S-026 local controls preview. This turn qualified the Release build, unit suite and
focused native integration gates for D27/D28; manual owner/device acceptance remains. The published
`0.1.0-alpha.1` does not include the local V2 work.

### Alpha 2 publication baseline (2026-10-22)

- The owner confirmed the second public version as `0.2.0-alpha.2` and authorized its commit, push,
  tag and release on `https://github.com/ibrahimbouillet/AetherShell`. The verified source branch
  `wip/d27-d28` was fast-forwarded into the clean public `main` checkout. The Alpha 2 version and
  end-user docs are being prepared on that branch; exact build/package/privacy/CI/tag/release receipts
  must be added here before another assistant treats publication as complete.
- The owner rejected the existing synthetic/older component-test images for README presentation and
  requested fresh captures of the current Dock with the machine's pinned application logos plus a
  short animated recording. At this initial publication checkpoint the native capture capability
  was unavailable; the later T36 continuation below supersedes that boundary with real media.
  Do not present old fixtures as current product imagery or fabricate a recording. The README still
  omits rejected imagery; complete the exact-export acceptance and media selection before adding
  the new local captures to the public repository.
- In the meantime, the reviewed README text identifies the public alpha and taskbar behavior. The
  release remains a manual-install preview unless a valid signed manifest is produced with the
  owner's signing setup; never publish an unsigned manifest or expose key material/location.

### Historical preparation — D34–D37 (superseded by the delivered receipt above)

- **Latest owner priority, D37:** publish the verified Alpha 2 installer now; fresh screenshots and
  optional video may follow. The failed gallery is no longer a publication blocker. Keep all raw
  media private and preserve D36 readiness for every later attempt. Do not claim a finished gallery.
- **Current owner coordination, D36:** ask whether the owner is ready immediately before every real
  assistant-operated capture attempt and wait for an explicit ready reply. Earlier blanket
  no-warning permission is superseded. Code/build and read-only preflight work can continue while
  the owner uses the computer. Do not close/hide apps for another take before that reply.
- Source checkpoint `b9b3f3de9657776127e73faec6c66336c220f06b` is committed locally on `main`;
  it is now pushed to the official public `main`; [Windows CI 39964752759](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39964752759)
  passed for that exact revision. No tag/release yet. Original mixed index was privately backed up before the reviewed worktree
  was staged. The active capture-preview receipt is under the newest ignored local
  `Showcase/alpha2-delivery-<id>/gallery-preview.local.json`, beside settings backups and scene/run
  receipts. The older normal-preview receipt identifies a stopped process; do not assume it is live.
  The current preview is held by a long-running local execution session; short-lived tool command
  hosts did not keep its child alive. Verify processes before changing anything. Final installed
  lifecycle/independent sign-in launch are still pending.
- Real recording has not succeeded: an owner-interrupted attempt and subsequent initial scene
  rejection produced no usable media. The blocker is `UIHubMouseHiderWindowClass`; keep guards strict
  while inspecting its effective visibility with local diagnostics. Read-only inspection observes
  an Explorer-owned 5 × 5 px layered helper at alpha 1/255, not a zero-pixel window. A narrow,
  independently tested compatibility policy now passes 86 focused Release `/W4 /WX` cases. A fresh
  read-only preflight passed; the observed class style is `0x3`, without `CS_DROPSHADOW`. Do not
  blanket-allow class names or low-opacity apps. After an explicit D36 ready reply, the next take
  passed the initial scene check but stopped immediately at the acquisition guard, with no usable
  export. Its error did not yet distinguish taskbar visibility from a scene change; fail-only local
  diagnostics are being added before another owner-coordinated take.
  The rebuilt fail-only diagnostic runner then passed a subsequent owner-ready initial scene and
  reached volume interaction, but stopped with `the real volume change was not acknowledged`.
  Source review found that external endpoint observations reached Top Bar immediately while Quick
  Controls could wait for its 1.5-second refresh, beyond the 600-ms acknowledgement deadline. The
  live observation bridge and own-window regression are now being built. No precise restoration
  receipt existed for that failed take; do not retroactively claim its scalar was proved restored.
  New private numeric acknowledgement/restoration receipts distinguish original, observed and
  restored values without names or identifiers. A current private runtime copy keeps Aether usable
  while the root build is unlocked for compilation; consult the newest ignored preview receipt.
  Old media remains rejected for
  the new README. Private scene preparation now freezes genuine pins by disabling background import
  only in the clone. A loose build app needs the matching recovery launcher alongside its executable.
- T37 source is implemented and independently reviewed: visible fresh-wizard startup default,
  explicit opt-out/silent flags, prior-choice/Task Manager preservation, stable-launcher startup,
  three abnormal-exception retries (1/2/4 seconds), installation-scoped cancellation and safe
  maintenance ordering. Settings disable now removes only this installation's owned startup entry.
  A hung pre-IPC child causes bounded maintenance refusal; no force-kill or realtime priority.
- Focused Release `/W4 /WX` passes settings54 + install16 cases, 2/2 executables; independent rerun
  passes in 2.36 s. Release scripts pass 33 Python tests after rejecting stale signature carryover.
  These are source/fake/isolated-kernel checks, not actual sign-in or installed recovery proof.
- T38's reusable scenarios pass 80 cases (Release `/W4 /WX`): concurrent MTA acquisition
  remains independent of blocking UIA, joins before every restore/flush, and volume actions verify
  endpoint acknowledgement and restore the precise scalar. Full-gallery scenes include Search,
  Dock context, Top Bar menu and the second quick-controls page. Fresh owner-coordinated capture
  and complete privacy review are still pending. The diagnostic runner that passed read-only
  preflight was SHA-256 `9577804a7914618fc222314eea6af292664c225f612ee671f2692a3aee90a375`;
  recheck the next rebuilt runner's exact hash and local receipt before another take.
- The fresh all-module Release `/W4 /WX` build passed in `build/alpha2-release`. The final incremental
  canonical unit gate passes 22/22 executables, 812 cases, 0 failed, 47.94 s (S-034). Sixteen isolated/
  read-only native integration gate finished 15/16 in 157.02 s: Dock's final unchanged-attention frame
  count failed 185 versus 182; all others, including the new media-presence regression, passed.
  The unchanged Dock binary subsequently passed 4/4; exact residual-frame cause remains unproven.
  A test-only bounded observed-idle precondition preserves the strict unchanged-snapshot assertion;
  root rebuilt it and 4/4 pass, CTest 18.10 s. No Dock product code changed. Retain the first failure and
  scoped rerun distinctly; combined native coverage is 136 cases, not a retroactive first-run pass.
  The retained current capture preview remains usable. No real install, startup mutation, usable
  gallery export, tag or release has occurred in this D34/D35 continuation.
- The owner prepared the current desktop/pins but now wants to use the machine between takes (D36).
  Read the current ignored delivery/gallery-preview receipt for its active settings path, not the
  stopped old normal-preview or stale installed/profile copy. Current wallpaper and
  five pin identities were checked locally. Preserve their current configuration with private
  backups when upgrading the installed instance. Do not reuse older media for this new gallery.
- Remaining publication gates: finish full Release build/tests, package freshness/privacy and
  native installer/recovery checks; real installed preview/startup registration; new full-resolution
  stills and every-frame video privacy review; versioned gallery/README; coherent public commit,
  exact-revision Windows CI, audited installer/checksums, then `v0.2.0-alpha.2` prerelease. No unsigned
  manifest or raw capture/process metadata may be published. Actual reboot timing is unverified.

Latest source follow-up passes a full Release `/W4 /WX` build and 22/22 unit executables in
32.72 seconds. The final own-window app integration executable passes in 3.74 seconds, including
three audio-observation regressions. Independent source review found no major bridge/lifetime
defect. The media review helper passes 16 synthetic checks, including resolved-output junction
containment and forced local MP4 decoding. Exact source commit, package, installation, CI and
release receipts remain to be appended during the D37 delivery.

### Earlier T36 cinematic checkpoint

**Superseding owner priority, 2026-10-23 (D34/D35):** the owner accepted proceeding to Alpha 2
delivery and explicitly renewed permission to commit/push/publish `0.2.0-alpha.2`. Implement and
verify disclosed installed sign-in startup, bounded abnormal-crash relaunch and reliable reversible
taskbar hiding; do not claim Windows guarantees launch order, immortality or OS-critical priority.
Keep explicit Quit, opt-out, Task Manager disable and recovery working. The owner authorizes actual
installation/startup on this machine. Extend the capture tool to reusable clean-desktop/control/
settings/volume scenarios and preserve reviewed media by release for Alpha 3 comparisons. Real
user-app closure/termination followed by best-effort relaunch is authorized again, including browser
windows. Do not exercise closure or device mutations in automated tests. The earlier film's D32
score remains unverified; proceeding does not turn it into a 100/100 claim. Publication still needs
fresh source/build/package/privacy/CI receipts and reviewed public media.

- **Previous T36 boundary (priority superseded by D34/D35):** D32 acceptance of the local film
  remains open; the owner now proceeds to Alpha 2 publication and a documentary gallery.
  A reviewable export exists: `Aether-Shell-Showcase-C5FF64A3E03848EFA7FCD18A0F236221.mp4`,
  SHA-256 `6839bf39c247896265d31cc37278e3eb16fa5a06c2eb6e6a8e55cb189c2cf7a2`.
  All 1,030 frames decode, with uniform 60-FPS PTS, 1920×1080 progressive SDR BT.709 and AAC stereo.
  Frame-contact review and critical full-resolution images found only the permitted scene; this is
  not full-resolution every-pixel review. Normal-speed playback/listening and the complete 60-check,
  ten-gate score are still unverified. No 100/100 score is claimed. See S-033 and production QC.
- **Owner's exact sequence (D33):** show the empty scenic desktop and visible pointer for two seconds;
  take a curved detour toward the left of the Dock, zoom and traverse the entire genuine Dock, hover
  Code for one second, click, and wait for its loaded English workbench. Hold the app for **three
  seconds total after readiness**, including the 1.6-second camera reveal and 1.4-second stable hold.
  During reveal, return the pointer to its neutral starting location. Send the scenario window
  `WM_CLOSE`; require the window to be absent and two consecutive captured body/backdrop samples to
  match the original clean scene before starting the three-second empty interval. This accounts for
  DWM's closing fade. Only after capture stops may old applications be restored; flush the lossless buffer and
  render afterward. A first blank Code window is not readiness.
- Implemented native modules include `Win32CleanSessionBackend`, fakeable `CleanSession`, DXGI desktop
  duplication, `CaptureBuffer`, validated/reopenable `FrameArchive`, CPU/GPU compositors, Media
  Foundation H.264/AAC, WIC GIF and an original procedural soundtrack. The runner lives in
  `tools/showcase/ShowcaseRunner.cpp`; `Prepare-ShowcaseScene.ps1` preserves actual pins in a private
  cloned Aether profile and opens Code through a separate empty profile without editing the originals.
- `CaptureBuffer` stores a full first image and changed tiles in bounded RAM (512 MiB by default).
  Acquisition performs no spool-file writes; after capture and restoration, it reconstructs exactly
  into the archive. Capacity/error paths fail explicitly instead of silently dropping frames. DXGI
  acquisition is nonblocking and the actual Windows cursor is composed explicitly from
  `GetCursorInfo`. Immutable frame IDs avoid repeated archive reads/texture uploads during the
  eight-sample shutter composition. These mechanisms require measured real-take evidence, not a
  presumed 60-FPS source claim.
- `CodeReadinessObserver` checks enabled, onscreen English workbench markers on a separate COM/UIA
  worker, retaining the verified HWND and process creation identity. UIA connection/transaction
  timeouts are configured; no provider calls run on the capture thread. This observes workbench
  readiness, not document/network completion or a universal hard deadline for faulty providers.
- Native controller ancestry and explicit `--keep-pid` infrastructure are protected; retained control
  windows are put aside temporarily. Per-frame before/after capture checks abort on unexpected visible
  surfaces before finalization/rendering. Allowed app content and between-check races can still be
  private: every frame must be reviewed. Ordinary failure/ESC cleanup attempts relaunch; forced worker
  termination or power loss bypasses in-process cleanup. Unsaved work, tabs and arrangement cannot
  be promised restored. Automated tests never close real apps or capture the owner's desktop.
- Root's latest focused Release `/W4 /WX` suite passed 66 cases in 2.17 seconds; the own-window
  integration executable then passed two cases in 105 ms. Exact commands and results are in
  `docs/knowledge/TEST_STATUS.md` S-033. Native NV12 orientation and
  end-to-end WIC resized-frame channel checks now pass. Source tests remain distinct from full-film,
  privacy and sound review.
- Three prior takes are rejected. Earlier black-wallpaper attempts had roughly 17 and 12.44 capture
  samples/second; the latter had a 5.95-second gap and unrelated private final-frame content. A later
  restored-background take measured about 34.6 samples/second but lacked the pointer and produced
  inverted MP4/swapped-channel GIF output. Do not reuse these as public assets. The wallpaper's
  referenced source was missing; a cached wallpaper was restored and the owner confirmed seeing it.
  The latest take shows the restored scenic composition; its motion remains subject to D32 review.
- A later 964-frame candidate corrected the earlier technical defects but remains unaccepted: a
  setup warning toast intruded, visually clear ending coverage was only about 2.85 seconds, and hover
  behavior needed refinement. The isolated scene now uses a fresh empty extensions directory without
  the warning-producing `--disable-extensions` flag; no owner extensions or security settings change.
  The empty-ending gate now requires captured scene return, and the cursor returns during reveal.
- The latest take has 985 source samples averaging 57.3721/s; the independent interval P95 is
  29.92 ms and maximum 108.07 ms. There were 236 DXGI desktop presents and 316 acquisitions, with
  cached static images reused. These are not 985 distinct UI updates or a native constant-60-FPS
  capture claim. The 60-FPS camera export is authored offline. Its measured composition cost
  (19.76-ms mean, 31.90-ms P95) is offline throughput, not delivery PTS jitter.
  Ready was observed at 10.7441257 s and the settled closing event at 14.1604877 s; the source
  central-backdrop check found 3.0238984 continuously clear seconds after the closing fade.
  The real pointer is visible, the close-up footer fits, the extensions toast is absent, and the
  ending has no tooltip or magnified Dock. A small stable Code background change near 12.37 s is
  genuine app behavior, retained in the export; it is not a fabricated UI or black frame.
- The MP4 is 51,139,749 bytes; its matching GIF has 344 frames, 960×540, 50-ms delays and is
  98,379,380 bytes. The GIF needs size optimization before README embedding. Audio measurements
  are −22.6 LUFS integrated, −10.5 dBFS true peak and zero clipped/non-finite decoded samples;
  these are measurements, not listening approval. All media, raw archives and private QC remain
  outside public Git. Four specifically identified rejected raw spools were removed (14.68 GB);
  their review receipts/previews remain local. Do not try to rerender those removed sources.
- The capture worker is finished. The normal usable Alpha 2 preview is active again from the
  existing Release app build, using a fresh private clone of the prior normal preview settings.
  The ordinary Code pin route is restored; taskbar replacement is requested and startup is off.
  Original installed settings and the prior preview settings were hash-checked unchanged. The
  ignored local launch receipt is under the per-user showcase data root. No installed payload,
  original profile or autorun was replaced.
- Inspect the actual live process/status and local artifacts before starting another run; an observation timeout alone
  does not mean it stopped. Prefer offline rendering of a valid capture recipe to closing apps again.
  Recipes are numeric, local, bounded and validated against source coverage; keep them and all media
  private until review. See `docs/showcase-capture.md` and `video_production/04_TIMELINE.md`.
- Git rechecked: official `origin` is `https://github.com/ibrahimbouillet/AetherShell.git`; `main` HEAD
  remains `322594b1b3486f319c9bf7144bd683d7890e44b5`. Existing mixed staged/unstaged Alpha 2 and T36 work
  is preserved. A separate-index recovery snapshot was committed on local
  `codex/t36-showcase-checkpoint` at `bf853e23d60df4ac190c01f4e0ac6d565e92d3ad`; later documentation
  follow-ups may advance that local branch. The checked-out `main` and its original mixed index were
  not changed. The snapshot also includes the already-prepared Alpha 2 working-tree docs; inspect
  its tree before any integration and do not blindly cherry-pick or reset `main` to it. No push, tag
  or release was made. Recheck D29 receipts and current instructions before Alpha 2 delivery;
  no later release is authorized.

### D27/D28 implementation checkpoint (2026-10-22)

- Owner requests continuous captured Dock drag with reactive spring neighbors/drop, no hover tiles,
  separator or trailing native-taskbar arrow; neutral material also on hover/tooltips; round status
  icons, larger clear native type, green100-percent/red<=5-percent or OS low-critical battery.
- Genuine background shell attention should show a finite red Dock cue. A real new alert may briefly
  reveal only the Dock during fullscreen for6 seconds, without taking focus/raising the application.
- Owner explicitly clarified automatic Windows weather location for general installations, no fixed
  owner city. Use OS permission/coarse position, honest no-weather on denial/unavailability, preserve
  existing opt-outs/manual choices. Never scrape SSIDs/raw IP or pretend to read Windows weather.
- The prior source snapshot is committed as `8595f14`, with a test/schema follow-up at `73900bc`.
  S-027 continued in the official-origin worktree on `wip/d27-d28`, based on `73900bc`; local `main`
  has not been advanced. The tested source/test checkpoint is local commit `4b8d000`; this handoff
  records the later repository-state update. This session added consent-generation fencing, accurate
  weather disclosure/reference text, and integration expectations for the D27 arrow-free menu and
  animated Dock settling. The worktree is clean after the local S-027 commits. The separate public
  public `main` checkout was clean at `a01fa9d`, five commits ahead of `origin/main`; it has since been
  fast-forwarded to the S-027 checkpoint under D29. Earlier agents are no longer running. Check
  `git status` and `git log` before editing.
- A complete Release build of all modules and targets passed with MSVC `/W4 /WX` and zero warnings.
  The final CTest unit gate passed **21/21 executables**. Focused own-window integration tests for
  applications, Dock, Top Bar, Settings UI, Control Center and app passed **6/6 executables** after
  two obsolete assertions were updated. The exact commands, earlier Search benchmark flake, and
  limitations are recorded in S-027 in `docs/knowledge/TEST_STATUS.md`.
- Review found a stale-consent race: a Windows permission response could arrive after a manual
  location or weather mode changed. `ShellApplication.cpp` now fences permission against the full
  location choice (enabled/mode/consent/manual coordinates/name), while unit and interval edits do
  not invalidate it. Weather refresh is documented at the host's 15-minute cadence. Both changes
  are covered by the passing S-027 gates.
- The installed `0.2.0-alpha.1` application was paused for bounded tests and restored with its
  original executable/data directory. After the passing gates, the freshly built worktree candidate
  was launched and verified visible as **Aether Shell Dock** from the Release build. It runs with a
  separate local preview profile copied from current settings; weather is disabled and Windows
  location consent is false in that copy. The installed files/profile were not overwritten, and no
  installer or startup registration was changed. No real location prompt/read, forecast request,
  media/audio/radio/brightness mutation, Explorer restart or owner-window capture occurred.
- D27/D28 are tested, not fully accepted: direct visual review of Dock motion/glyphs, reduced motion,
  real shell attention/fullscreen behavior, real Windows location consent and live weather, physical
  DPI/multi-monitor behavior, and complete taskbar/Explorer recovery remain owner/device checks.
  At the time of S-027 no package/push/tag/release existed; D29 subsequently authorized the Alpha 2
  delivery. The release-specific build, package and publication receipts are recorded later in this
  handoff.

### Earlier unqualified work — D27/D28 source snapshot (2026-10-21)

Implementation details and prior session ownership are preserved in commit `8595f14`; it added the
continuous Dock drag/spring behavior, shared status glyphs, Windows shell attention propagation and
the consent-gated Windows location backend. Those files were committed as a recovery snapshot, not a
verified milestone when written. The later S-027 Release build, unit and focused own-window results
are recorded above; owner/device acceptance remains.

### Owner controls scope — 2026-10-21 (D26)

One grouped Wi-Fi/audio/battery action opens real media/system controls. Dock/Bar glass is neutral,
  with grain, blur/tint and a uniform border. Side margins apply only to maximized windows; never move
  manually placed owner windows. Keep the usable local preview active except bounded tests, then
  restore it; no autorun. D29 authorizes only the Alpha 2 publication. Current implementation/evidence follows.

### S-026 completed local checkpoint — 2026-10-21

- **The new controls preview is now active**, using copied current app/launcher binaries and the
  existing private preview data; no installation/autorun. Keep it active except bounded tests, then
  restore it. Ignored `build/manual/owner-preview.json` records the current process/root privately.
- Full Release /W4 /WX build succeeds. Aggregate units:20/20 executables,698 cases, all pass (14.75s).
  Registrations were cross-checked using --list because passed CTest XML output is truncated.
  Scoped native runs pass accessibility6/controlcenter4, then Dock3/TopBar5/app2. Final Top Bar
  correction rerun passes22 unit/5 native cases (7.67s). No real device control was automated.
- New media/system/controlcenter modules and app bridge are implemented and independently reviewed.
  Radio consent/worker actions use close-generation fencing and cancellation; shutdown clears pending
  flags safely even if the owner cancels Quit after a save error. Unknown remains unavailable;
  unsupported global toggles are clearly labelled Windows Settings actions. Artwork is not implemented.
- Final bounded desktop gate after preserving Shell-approved Top Bar horizontal exclusions passes:
  Windows11/150-percent work area and own pre-maximized DWM frame both [18,93,1902,969]. Normal quit
  returns both to [0,0,1920,1080], native taskbar visible/auto-hide true. No foreign HWND was resized,
  Explorer restarted or screenshot captured. Foreign AppBar/multi-monitor behavior remains untested.
- Dock/Top Bar and panel dark/light own-backdrop captures were inspected. A moving radial overlay
  appears above opaque panel content in captures; no matching radial painter exists in the call
  graph. Native feedback is only a hypothesis. Do not claim this artifact resolved. Neutral material
  tests pass; broader visual/hardware/screen-reader approval still requires the owner.
- Privacy review:50 changed public files,0 new private/credential findings;80 relative links and442
  requirement references valid. App/launcher binary privacy audits pass. No raw logs/private launch
  receipts/owner images, private Git history or local tool state are public material.
- All current source edits belong to this local checkpoint, including the preserved S-025 metadata
  regression plus RangeValue. One green six-case accessibility run does not prove the historical
  intermittent discovery/teardown issue causally fixed. S-025 negative evidence stays ignored locally.

### Git and delivery checkpoint — 2026-10-21

- Latest source checkpoint: **d7e17c4**, committed locally and fast-forwarded into clean local `main`.
  Documentation receipts may follow it; inspect HEAD. Integration and main contain the same source.
  Remote origin/main remains **b87be0d**. No push/tag/release occurred under D25.
- The active copied preview has this checkpoint's tested source contents, but its build metadata
  still embeds parent revision **2dabf2700610**, because it was compiled before the local commit.
  This is a local review preview, not a release package. Rebuild exact committed source and repeat
  appropriate package/freshness gates before any eventual delivery. No setup was installed here.

- Official public `origin`: `https://github.com/ibrahimbouillet/AetherShell.git`.
  Integration merge **`b87be0d68961acecc3777f07a0198c80b28f2912`** was committed, safely fast-forwarded
  into the original clean `main`, then pushed as `8e11a68..b87be0d`. Both `main` and `origin/main`
  were rechecked at that exact commit; the main checkout was clean before this delivery-note update.
- Merge parents preserve the foundation `d5ccbd6` and all three recovered streams:
  lifecycle `5cf6e72`, applications `1ef9d72`, visual core `4b7c8a8`. Five dirty/untracked visual
  files were copied into the candidate before fixes; their original Claude worktrees remain intact.
- Local `main` and the detached integration worktree contain source checkpoint **`9bec731`**, above the local-only
  documentation checkpoint `6f63ef3`. The source checkpoint commits S-023 fullscreen/AppBar fixes,
  S-024 icon balance, strict UIA diagnostics and two inspected synthetic images. Both local commits
  were privacy-reviewed and fast-forwarded without push. Later documentation-only checkpoints may
  follow; inspect HEAD. Remote `origin/main` remains b87be0d.
- Historical pre-D26 state: the integration worktree contained **two uncommitted S-025 files**. They
  are now included with the reviewed RangeValue work in this local checkpoint; preserve the evidence.
  Their earlier investigation was not part of the 9bec731 product checkpoint.
  Use `git worktree list` to locate that worktree; there is no pending merge. Build/log/experimental
  outputs are ignored local evidence. The ordinary `main` checkout has no pending source edits.
- Windows CI [39505236903](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39505236903)
  for `b87be0d` **passed**, 10m33s, including Release build/unit gate and review package upload. This does not qualify the later fullscreen correction.
- The final local package was rebuilt with embedded source revision **`b87be0d68961`**, then passed
  privacy/freshness audit and focused app/setup gates. No V2 tag, release or installation was made.
  The owner explicitly authorized the bounded real-desktop hide/restore check. It found a fullscreen
  defect; all native state was restored before coding the correction (checkpoint below).
- The archive has no remote and private history. Never push/develop there. No private archive history,
  owner canvas, private capture, browser profile database or local assistant state was copied.

### Implemented and reviewed

- D23: taskbar replacement defaults on, explicit opt-out/reset semantics preserved, setup disclosure
  and restore shortcut. Canonical default-on docs and native Settings consent fixtures agree.
- Applications: trusted profile launch/pin routes, best-effort Taskband pin reconciliation, one-time
  Explorer default, worker-only 8.3 planning, ancestor-junction icon rejection, late profile avatar
  presentation without changing launch routes. Unknown profiles can activate existing windows but
  cannot silently launch the default browser profile. Actual Chrome behavior still needs manual checks.
- Dock: shared roles/glyphs/glass/menus, configurable Start/Search/Task View, saved pin order and
  session-only running-item drag, dismissal persistence, source-sensitive icon refresh, compact
  shadow reservation. Reviews fixed invisible empty regions, modal-menu lifetime/stale selection,
  shutdown/entrance admission, DPI cache retention and queued/async tooltip resurrection.
- Top Bar: configured height/weather side/keyboard format/battery preference, shared material/glyphs,
  anchored menus, keyboard/network/weather/clock/power/sound/privacy actions and temporary native
  taskbar reveal/manual resume. Dock remains visible until native restoration completes; a monotonic
  60-second timer then resumes replacement. Stale timer messages cannot shorten a newer reveal.
- Owner clarified **Chrome profile avatars**, **Dock-icon context menus**, and **Windows dictation
  plus settings/mute** (D24). Do not ask those scope questions again. Voice typing requests Win+H
  only after verified previous-window focus; Aether does not capture/store speech. No real dictation
  or audio control was exercised by automation.
- App/settings: Appearance page, shared theme/motion application, worker pin sync with settings
  revision/retry/opt-out protection and bounded newest dismissal retention. Native taskbar access
  remains restricted to the production instance; isolated full-app tests cannot hide it.
- Graphics/UI/Search: foreign menu-pointer rejection, correct shadow/entrance regions, actual drawn
  row hit/UIA geometry, entrance pointer guard and accessibility-motion cancellation. A reproduced
  CompositionScopedBatch crash was fixed with explicit strong batch/event-token ownership and
  lifetime/cancel/detach/destruction/throwing-callback regressions.
- Continuity: reusable English `docs/SESSION_START_PROMPT.md` works for Claude/Codex. Added required
  `ARCHITECTURE.md` and `TESTING.md` (T19/GAP-009). Refreshed six public screenshots from independently
  inspected own-window synthetic fixtures; no owner desktop screenshot is public material.

### Executed verification — exact scope in TEST_STATUS S-022

- Frozen full Release /W4 /WX build: app/launcher/updater/setup and all tests, zero warnings.
- Combined unit gate: **18/18 executables, 669/669 cases pass** (15.18 s).
- Combined serial non-network integration gate: **14/14 executables, 122/122 cases pass** (79.94 s).
  Includes Dock3, Top Bar4, Settings UI10, app2, UI12 and Search UI10. These are own-window/fake or
  read-only checks; no actual taskbar/work-area/audio/startup/keyboard/dictation mutation or external
  app launch. Raw logs/JUnit stay in ignored `build/wf-codex-integrated/evidence`.
- Release-tool Python tests: **32/32 pass**. Baseline Search initially failed timing under concurrent
  compilation; its quiet rerun passes, and candidate p95 is 0.231 ms (2,000 apps), 0.435 ms (5,000),
  with unchanged budgets. Original native crashes and premature 700-ms idle assertion remain recorded;
  corrected tests retain a bounded two-second settlement deadline and <=2 idle-frame requirement.
- Local package privacy/freshness audit passes, including setup and uncompressed embedded binaries.
  `dist/AetherShell-0.2.0-alpha.1-win-x64-setup.exe`: **4,957,184 bytes**, SHA-256
  `4e95a10a749792d90457c020029b321fbc7f4b8fefd970abb0a3e1e0fb985f6c`.
  Setup and checksums are review assets only. **Unsigned manifest stays local.**
- Initial source/history privacy review found no private material; synthetic rejection-test strings
  were inspected separately. All 17 unpublished commit identities use GitHub noreply addresses;
  165 unpublished history blobs/commit messages had no environment-specific/secret-pattern finding.
  Final staged audit covered 538 files with zero environment-specific/secret-pattern findings; the configured author identity was GitHub noreply before commit/push.

### Authorized desktop gate and follow-up correction — 2026-10-21 (S-023)

- The owner explicitly approved closing any old Aether instance, checking the candidate's native-bar
  hide/restore and returning to the initial state. No Aether instance was running initially.
- Candidate b87be0d ran from copied project binaries with `--data-dir` pointing to a private temporary
  synthetic configuration. Personal settings stayed untouched; weather/network updates/hotkeys,
  running-app display, pin import and Explorer initialization were disabled for this ownership test.
  The owner asked why Explorer's icon was absent: **expected in this restricted fixture**, which had
  only Start/Search/Task View. A new normal-running/Explorer-default check is now explicitly required.
- Initial Windows 11 state: native taskbar visible, auto-hide true, work area [0,0,1920,1080].
  Active candidate: native taskbar hidden, work area [0,93,1920,969], but both Aether surfaces could
  disappear when a normal framed foreground window covered the monitor. Explicit Normal launch
  reproduced it after removing the initial Hidden-launch confound. The owner saw the bars return
  when ordinary Explorer became foreground; their screenshot is private and was not copied here.
- Both bounded candidate runs exited, the saved baseline was removed, the native taskbar was visible,
  auto-hide true and work area exactly [0,0,1920,1080]. Read-only Win32 measurements only; no desktop
  screenshots, Explorer restart, real audio/startup/voice operation or personal settings change.
- Uncommitted correction in the integration worktree: platform fullscreen classification now rejects
  framed/child windows while preserving borderless full-monitor/maximized/frame-stripped fullscreen;
  Top Bar treats raw AppBar fullscreen notifications as reevaluation requests; Dock follows the host's
  combined suppression authority so raw notifications cannot override temporary native-bar reveal.
  Added three pure platform cases, one native Top Bar case and Dock regression assertions; cross-review
  found no blocker. Bool-only suppression logging helps diagnosis without app identity/title leakage.
- Full Release /W4 /WX rebuild passes with zero warnings. New combined gates pass: **18/18 unit
  executables, 672/672 cases** (30.16 s), and **14/14 serial non-network integration executables,
  123/123 cases** (127.00 s). Logs/JUnit are ignored local evidence under
  `build/wf-codex-integrated/evidence/fullscreen-*`. These results cover the fullscreen correction.
- A subsequent bounded Dock conformance fix forwards registered activation/position messages to
  the shell, preserving default message handling and the recursion guard. Its Release /WX rebuild
  passes; Dock31 unit and app2 native pass. The first targeted Dock native run failed its light
  fixture's UIA root-type assertion (stage 3), then the unchanged-binary serial rerun passed all
  three cases (8.28 s). Both raw receipts are retained. Root cause is unproven; no assertion was
   relaxed. Numeric/boolean-only diagnostics were added for any recurrence.
- Corrected candidate App SHA-256 `524afc253c84cb28e1523e15b531850f2a80ebf6defa5166748bf2ece221d0bb`
  repeated the authorized **Normal** desktop launch with running apps and initial pin import enabled.
  Both Aether HWNDs stayed visible, native taskbar hidden, work area [0,93,1920,969]. A synthetic
  framed window maximized before launch received two work-area notifications and its visible DWM
  frame reflowed exactly to that area. On normal CLI quit it returned to [0,0,1920,1080]; native
  taskbar visibility/auto-hide and work area were restored exactly, helper exited and baseline removed.
- Own Settings accessibility and private synthetic-data checks confirm five pins including Explorer.
  This does not prove actual running-icon pixels or Chrome profile completeness. The UI tool could
  read Settings but could not click without screenshot geometry, so temporary reveal/resume remains
  unverified. No desktop screenshot, unrelated-app control, Explorer restart, audio, startup or voice
  operation occurred. The synthetic probe closed itself after its bounded lifetime.
- The owner then requested equal visual weight for Start/Search/Task View and application icons.
  The Dock removes an extra 18-percent inset on each side: the shared glyphs already contain internal
  padding. Layout, hit targets, magnification and Top Bar glyph sizes are unchanged. This later visual
  edit and UIA diagnostics pass Release /WX and visual inspection (S-024); the targeted Dock gate
  again fails the light UIA fixture (Pane 50033, successful HRESULTs, correct own HWND). Dock unit and
  app native pass. The preceding manual binary hash does not cover these later edits. No assertion
  was relaxed; accessibility qualification is unresolved. Remote `origin/main` remains b87be0d;
  local commits are held under D25. No source or documentation was pushed after that instruction.

### Accessibility investigation checkpoint — S-025, incomplete

- Frozen uncommitted files in the integration worktree: `src/ui/accessibility/AccessibleSurface.cpp`
  and `tests/integration/accessibility/AccessibleSurfaceTests.cpp`. The source permits the immutable
  `get_ProviderOptions` capability getter after closure; model/property/action guards remain strict.
  A fourth native case checks owner/MTA metadata before/after closure and stale-element rejection.
- Focused Release `/WX` compilation passes. The new metadata case passes, but the existing MTA
  client fails before any action, including a fresh process that excludes the experiment. This is
  **not a verified Pane-fallback fix**. Final native gate after rebuilding without the experimental TU:
  **3 pass / 1 fail**, 0.52 s; exact command/logs in S-025. No changes to this investigation were committed.
- A bounded own-provider experiment confirmed synchronous-send rejection on a repeated disconnect,
  but its repeated-call success expectation was inconclusive after wrapper invalidation. Early reply
  did not establish a successful first cleanup. It was moved out of the default suite to ignored
  `build/manual/AccessibleDisconnectTests.experimental.cpp`; negative logs remain in
  `build/wf-codex-integrated/evidence/disconnect-experiment.log` and `accessibility-metadata*`.
  A copy of the uncommitted patch is preserved in ignored `build/manual/accessibility-metadata-unverified.patch`.
- Do not add sleeps/retries or relax Toolbar/count/invoke/stale-provider assertions. Next bounded
  diagnostic: record numeric first-call teardown HRESULT/send flags before wrapper invalidation,
  withholding reentrant provider exposure, and the first MTA failing stage plus own-instance provider
  callback/thread results. Synchronous teardown and Pane fallback may be different defects. No broad
  dispatcher/lifetime refactor or generic early-reply behavior has been implemented.
- The build cache has compiled this investigation; rebuild the chosen source and exact Git revision
  before preparing a package. The old b87be0d setup is historical and must not be presented as Alpha 2.

## NEXT SESSION START HERE

0. **Codex: independently review two Claude batches, 2026-10-25.** (a) D39/T45 consolidation
   (`d833239`, `671faad`, S-040): confirm `git worktree list` shows only this folder, `origin`
   is official, and all branches/tags are present; the owner holds the private recovery location.
   (b) T42/T43 (S-041): review `src/dock/DockHover.*`, `DockPreview.*`, `DockPreviewWindow.*`,
   `DockMotion.*`, the `DockWindow.cpp` wiring and `Motion().marker`, then rerun the S-041 command
   from this folder. Optional own-window motion frames (synthetic windows only, local output):
   `$env:AETHER_DOCK_PREVIEW_FRAMES='<local folder>'; build/v3-release/modules/dock/aether_dock_integration_tests.exe frames`.
   The owner has directly requested a local prototype launch after review (see authorization above).
   Recording still requires a fresh D36 ready reply. For a bounded instance swap, quit the installed
   shell (its own Quit command, or
   `%LOCALAPPDATA%\Programs\AetherShell\versions\0.2.0-alpha.2\AetherShellApp.exe --quit`), run `build/v3-release/modules/app/AetherShellApp.exe --data-dir <private temp folder>` (no
   startup registration happens unless chosen in its Settings), hover a background app with a
   foreground app present, dwell, cross into the card, leave, re-hover, click, then close a
   disposable window through X. Never capture the owner's apps without consent; when ending the trial,
   quit the candidate (its own binary with `--quit --data-dir <private temp folder>`) and relaunch the installed
   Alpha 2 from its Start-menu shortcut, confirming the native-taskbar state returns to what it was.
1. Read this public rule, the current D38 V3 continuation and delivered Alpha 2 receipt above,
   `AGENTS.md`, `PROJECT_SPEC.md` FR-031–FR-036, `TASKS.md` T39–T44, D38 in `DECISIONS.md`, and
   `docs/specs/v3-design-and-interaction.md`. Verify official `origin`, branch/HEAD,
   worktrees, tags and working-tree state. D39: work only in the one shared `AetherShell` checkout
   (no new clones/worktrees); the private history is recovery data, never a working folder.
2. Alpha 2 is published and owner-accepted. Continue T39–T44; do not repeat completed release or installation work. Inspect current processes/status before
   launching anything. Preserve the owner's current pins and preferences; private capture profiles
   never register startup. The installed startup/recovery gate has the scoped passing receipt above;
   reboot timing remains unverified. Check S-035's source/test boundary, finish input/geometry
   qualification and continue the documented V3 sequence. Diagnose T38's native action failure
   before any later gallery capture. An earlier build failure or a started
   command is not a pass. Check the latest S-034 entry when available for exact receipts.
   For the historical T36 film, read S-033 and the production QC docs; full normal-speed playback,
   listening and the 60-check/ten-gate score remain unverified. D35 permits proceeding to Alpha 2
   without turning those pending checks into passes. Preserve D33 and do not claim 100/100.
   User-started closure remains authorized, including termination after orderly close fails; attempt
   relaunch on normal cleanup and disclose forced-termination/unsaved-state limitations.
3. After reaching a coherent tested checkpoint, check D29/D34–D37 receipts before any Alpha 2 push,
   tag or release work. D29 authorizes only `0.2.0-alpha.2`; do not duplicate completed publication
   actions and never publish an unsigned update manifest.
4. The owner prepared the current desktop and explicitly rejected old imagery. Capture a fresh
   versioned gallery and MP4 using the owner-approved desktop composition: wallpaper, Aether's top and bottom
   surfaces, pinned icons and only the app launched by the scenario. Keep raw media local and inspect
   every frame for private content before public use. Include the grouped controls, settings,
   Search and Dock context where safely supported. Restore exact observed volume after an authorized
   volume demonstration; reject uncertain restoration or missing/late scenes. Do not use old
   captures or fabricated UI. Keep approved Alpha 2 media by version for later Alpha 3 comparison.
5. The owner still wants direct review of Dock movement and current pinned icons. Preserve separate
   evidence for real Windows location permission/forecast, shell attention/fullscreen behavior,
   reduced-motion quality, DPI/multi-monitor behavior, accessibility and taskbar/Explorer recovery.
   Do not claim these were proven by unit tests or a GIF.
6. Keep the preview/recovery instructions current. Update `HANDOFF.md`, `TASKS.md`, and
   `docs/knowledge/TEST_STATUS.md` with exact revisions, commands, results and remaining gaps. Never
   include private preview receipts, keys/locations, user paths, test logs, local config or unrelated
   captures in public Git.

## Historical receipts — previous sessions, superseded as continuation instructions

### Claude, 2026-10-18, about 15:55–16:40 CEST
Task: the owner asked for one authoritative specification, checked against the original brief, and for
a standard prompt to start every session. Claude changed no product source and made no commit in this
checkout. Nothing was pushed or released, and no repository setting was changed.
- **Independent checks (TEST_STATUS S-014):**
  - A fresh Release /WX build of the uncommitted V2 tree has 0 warnings; all 17 unit executables
    (468 unit cases) pass.
  - Read-only GitHub checks confirm the public repository, the green CI run 39123368917, and the two
    published prerelease assets with the recorded size and SHA-256.
  - A privacy scan of tracked and untracked files found nothing.
- **PROJECT_SPEC.md:**
  - The sources of requirements and their authority.
  - The problem statement and use cases.
  - The owner's Version 1 definition of done as the release gate (section G).
  - Milestones M0–M5 and the long-term direction from the brief (section B).
  - New FR-025 (logging and diagnostics).
  - Acceptance criteria completed from the brief for FR-004, FR-010, FR-015, FR-018–FR-020, FR-022,
    FR-023, NFR-001 and NFR-002.
  - The owner's decision priorities and prohibitions (section F).
  - GAP-009–GAP-012 and an updated GAP-004 (section H).
- **New:** `docs/SESSION_START_PROMPT.md` (D20).
- **Updated:** DECISIONS (approval-source note, D20), TASKS (T-DOC-2, T18–T20, T5a count), AGENTS and
  CLAUDE (session prompt, archive copy, Graphify status), TEST_STATUS S-014.
- **Archive copy:** a "do not develop here" notice was committed there locally (`99ec016`); nothing
  else changed there.
- **Not checkable or not run:**
  - Owner approvals recorded by Codex (D18, D19, T17 wording) come from its own conversation.
  - V2 integration/native suites and real desktop behaviour were not run in this session.
- **Owner decision after the review (D21):** the owner rejected the double-failure risk (GAP-011).
  The taskbar must be recoverable without Aether Shell running, through `--restore-taskbar` and setup
  repair using a persisted baseline, before `0.2.0-alpha.1` is published. Task T21; not implemented yet.
- **Owner decisions still pending (T18):** GAP-010 (repository description), Q-001 (performance
  targets), Q-002 (branding).
- **T21 implemented (same session, about 16:30–17:00), TEST_STATUS S-015:**
  - The taskbar baseline is saved atomically to `<config>\taskbar-baseline.json` before the helper can
    hide anything. A baseline left by a crash is restored first.
  - `--restore-taskbar` with no instance running restores from that file (or shows the taskbar),
    turns replacement off and reports the result.
  - Setup repair and uninstall restore a saved baseline when no instance runs.
  - Results: 480 unit cases and 3 silent setup cases pass, with 0 warnings.
  - The first test run briefly called the real taskbar once to make it visible (it already was) and
    waited on a message box. Claude stopped that test process and fixed the code (see S-015).
  - **Real-desktop check (owner-authorized, S-016):**
    - It first exposed a V2 bug: the Dock's first AppBar reservation was always rejected, so
      replacement never activated. Fixed in `src/dock/DockWindow.cpp`.
    - After the fix, the taskbar was hidden, both processes were killed and `--restore-taskbar`
      reported success. The saved state was removed and the mode was turned off.
    - Explorer had already shown the taskbar again by itself.
    - Because the saved state recorded the taskbar as not visible (an Explorer re-layout at capture
      time), restoration now always shows the taskbar (D22).
    - The owner's installed 0.1 alpha was closed for the check and restarted; the taskbar is back to
      its pre-test state.

## Historical continuation list (use the current list above)
1. Follow `docs/SESSION_START_PROMPT.md`. Check that `git remote -v` shows the official repository: this
   checkout is the only one for development. The original checkout is a local history archive (GAP-012).
2. The V2 work (Codex) and Claude's T21 fixes and documentation are committed as one commit, "Add
   reversible taskbar replacement with recovery without Aether Shell running", followed by gate,
   audit and CI-fix commits.
   - Before the commit: a privacy check of every changed or new file and the four changed screenshots.
   - **Pushed with the owner's approval** (S-019, S-020): `origin/main` contains all of it. No tag and
     no release were created; `0.2.0-alpha.1` is not published.
3. V2 gate (S-017):
   - **Done:**
     - V2 steps 1 and 2: the integration suites and the `ShellApplication.cpp` review (no blocking
       defect).
     - All 14 integration suites (95 cases) have passing runs. A Top Bar fixture fix moved its guard
       band off the screen corner.
     - No combined run has all 14 green yet; desktop activity interrupts the GUI suites.
   - **Package privacy audit (S-018): passed.**
     - The Release setup `AetherShell-0.2.0-alpha.1-win-x64-setup.exe` (4,617,216 bytes) is in the
       local `dist/`, unpublished.
     - Its manifest is unsigned and must stay local.
   - **Pushed (S-019, S-020).** The owner approved both pushes; no tag, no release.
     - CI run 39139547133 on `9a2b1bb` **failed**: 2 pin-import unit cases broke on the runner's 8.3
       short temp path.
     - The fix (path identity in catalog matching, plus the tests) is commit `e29e83e`. Its CI run
       39156124337 **passed**: Release build and 17/17 unit executables.
   - **Remaining:**
     - Windows 10 and Explorer restart during replacement;
     - the owner's manifest signature if automatic updates should offer it.
4. T21 is implemented, unit-tested and checked once on the real desktop (S-015, S-016), including the
   Dock reservation fix and D22. Review it with the rest of V2. Still open before `0.2.0-alpha.1` is
   published:
   - Windows 10;
   - Explorer restart during replacement;
   - a desktop whose taskbar does not use auto-hide (the owner's does);
   - the rest of the V2 gate.

   The remaining owner answers are in T18.
5. Then T17 (the owner's newest requests: drag reorder, Chrome profiles, File Explorer entry,
   interactive status controls), T19 (`ARCHITECTURE.md`, `TESTING.md`) and T20 (diagnostics export check).
6. Real desktop checks (taskbar hide and restore, crash recovery) need the owner's explicit authorization
   in the current session; follow V2 step 3.
7. Update TASKS, TEST_STATUS, DECISIONS and this file at each checkpoint, not only at the end.

## V2 continuation — IN PROGRESS, 2026-10-18

**Read this section before the older alpha receipt below.** The public checkout contains uncommitted
V2 work. Version in CMake is `0.2.0-alpha.1`; it is not yet installed, pushed or released. The running
installed application is still the old `0.1.0-alpha.1`. Never discard these edits or restart from the
old completion claims. Work only in the public clean checkout; the original development history
remains local (D18). User-supplied desktop screenshots remain private and must never enter Git.

The owner approved reversible visual taskbar replacement, superseding the original no-hide rule
only within D19 / ADR0009 / FR-024. Explorer keeps running. Native Windows Settings cannot become
Aether's taskbar settings page through a supported API; the Desktop page owns this mode. Settings
default false and require Apply. The owner also authorized enabling this mode for their own use.

Implemented, with focused checks:
- Dock placement and forced AppBar reservation while replacement is requested; lower visible margin,
  animated footprint protection, import/start context actions and pin argument preservation.
  Debug /WX: 19 unit + 2 synthetic native cases pass; real shell geometry remains unqualified.
- Read-only best-effort Windows `.lnk` pin import, catalog identity reconciliation, complete arguments.
  Release /WX: 66 applications unit cases (9 new) pass. Windows 11 pin completeness/order not promised.
- Correct Wi-Fi arcs/dot, distinct Ethernet/cellular, visible glass, exterior-only shadow, opaque
  high contrast/transparency-off. Release /WX: 14 graphics/Top Bar unit + 5 native cases pass.
  Synthetic paired-background RGB deltas: 26.827 dark, 18.968 light, 0.000 high contrast.
- Desktop consent/import/status page: 7 Debug /WX native settings cases pass (15.14 s); a separate
  Release /WX settings build passes 36 unit cases (3 new). Temporary settings/fake callbacks only;
  no actual taskbar/work-area mutations. The earlier agent's Dock/Settings Release wording was
  corrected by inspecting CMAKE_BUILD_TYPE in each focused cache; source/test counts stand.
- New platform taskbar lease, versioned-launcher recovery helper and parent fallback: 15 platform
  unit cases pass (6 new). Guard native process checks/explicit inherited handles/restoration ack
  reviewed. Controller worker owns blocking calls; 10 injected-session cases pass /WX. None of
  those tests hides or changes the real native taskbar.
- App wiring, first-import orchestration, recovery observer, `--quit` / `--restore-taskbar`, reserved
  space preflight/error fallback and shutdown AppBar-before-guard join are implemented. **The normal
  app build/tests and real native recovery are still pending.** New CLI cases bring app unit count
  to 15 when the complete new glob is configured (5 command cases + 10 controller cases).
- Release Python tooling: 32 cases pass. Four updated Dock/Top Bar PNGs in resources/screenshots
  were independently inspected; only isolated synthetic windows/patterns, never the owner's desktop.

Immediate next steps:
1. Finish the current full Release /WX build in `build/wf-v2-release`. Rerun it afterward so
   CONFIGURE_DEPENDS includes any source/test files created during its first configure. Then run
   all unit executables and relevant native suites serially; do not run GUI suites concurrently.
   Current native checks pass in focused build trees; those do not establish a complete app build.
2. Finish the independent review of `src/app/ShellApplication.cpp`: callback must preserve preflight
   reservation while desired=true, never re-enable a failed reservation, and keep observing pending
   restoration. Check import persistence completion and case-insensitive identity deduplication.
3. Perform separately authorized actual desktop checks only after compilation/tests: orderly quit,
   mode disable, own application crash/helper crash restoration, work-area protection and one bottom
   Dock. Do not kill Explorer for an automated test. Never capture unrelated desktop content. If
   native ownership/reservation fails, restore Windows and report the failure rather than hiding it.
4. Address the owner's newest urgent requests in T17 (recorded below/TASKS): drag reorder, Chrome
   profile identity, Explorer access and interactive status controls. Distinguish existing actions
   from missing behavior; the installed old alpha does not include the current V2 fixes.
5. Final source/binary privacy audit, coherent source commit, public push, Windows CI, audited setup
   and owner-authorized delivery/release remain pending. Never publish Debug/PDB/log/private history
   or unsigned update manifests. Only verified source belongs in a milestone commit.
6. Update this section, TASKS and TEST_STATUS as checks finish. Keep remaining platform qualification
   explicit: no claim of complete native Settings replacement, all Windows pins, Windows 10 or all
   Explorer/monitor/full-screen recovery. Preserve the owner's canvas and local tool state.

Newest owner priorities for the next assistant (2026-10-18): holding and dragging an icon left/right
should move it into the other icon's place and persist; user reports this broken in the installed
alpha. They want recognizable Chrome account/profile logos for open browser instances, Explorer
available, exactly one visible bottom Dock, and a Top Bar that can change keyboard language, manage
Internet and provide useful microphone interaction. The owner clarified that avatars mean **Chrome
profiles**, and right-click means **a Dock icon's menu**. Exact desired menu actions remain unspecified;
do not modify application-page context menus or silently record audio.

## Previous delivered alpha state — 2026-10-18
The continuation implemented the native Dock, Top Status Bar, Search, Settings, application lifecycle,
install library, launcher, updater and setup. A per-user **0.1.0-alpha.1** is installed: production setup
exit 0, Installed apps entry and launcher present, startup disabled, no automatic application launch.
This is a delivery observation, separate from the fake/temporary-root automated test policy.

The official public repository now exists at https://github.com/ibrahimbouillet/AetherShell.
The clean source is pushed and [Windows Release CI passed](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39123368917) on
`4eed02879fb79aa60376e9b50c698b295bae163e`. The first [manual-install prerelease is public](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.1.0-alpha.1).
Its only assets are setup and checksums; no unsigned manifest, PDB or raw log is published.
Setup: 4,451,840 bytes; SHA-256 `edeafb8c89ca7c833b4b79dfbaee160267fd7278e3e0e91980f31df67fbb481f`.
The installed maintenance executable matches this exact CI package after a successful repair;
startup remains disabled and no application was automatically launched. The following documentation
receipt changes no product source and therefore does not rerun the same CI build. See S-012.
No live signed-update execution or complete Version 1 acceptance is claimed.

## Standing handover rules (from the 0.1.0-alpha.1 session; still valid)
1. Read AGENTS, PROJECT_SPEC, TASKS and TEST_STATUS S-011. Code and executed evidence win over old
   work orders. Do not restart already implemented modules from historical "not started" notes.
2. Inspect Git status and remotes. **D18: original development history stays local.** Since D39,
   all development uses the one shared `AetherShell` checkout with the independent public history and
   public GitHub noreply identity; the old private history is only a local recovery archive. Never push the original history or copy its .git, build/logs/PDBs,
   .local/.claude/.obsidian/graphs or owner `docs/Untitled.canvas`. The canvas remains untouched.
3. Inspect the published alpha and S-012 receipts, then continue outstanding qualification or
   owner-requested features. The first alpha is manual-install only. Future release publication
   follows AGENTS and current owner instructions; the production signature verifier has no bypass.
4. Continue the remaining manual/platform qualification in KNOWN_LIMITATIONS and TASKS. The complete
   Release /W4 /WX build and all 17 unit executables pass. Every integration executable has a passing
   scoped run; aggregate desktop runs also recorded capture-guard refusals and activation interference.
   A quiet interactive desktop is required. Never weaken capture guards to obtain a green result.
5. Preserve test isolation: temporary roots, fake launch/audio/startup operations, no real work-area
   changes. Captures need opaque guard bands, containment and foreign-overlap checks before/after.
   Only individually inspected synthetic-window assets in resources/screenshots are publishable.
6. Before packaging, run scripts/package.ps1 on current Release source. It rejects Debug/stale input
   and scans setup plus embedded payload bytes for private source paths, personal directories, machine names and CodeView paths.
   Old Debug artifacts contain private build paths and must never be exported.
7. Update TASKS for progress, DECISIONS/ADRs for decisions, TEST_STATUS for actual checks and this
   handoff when ending a session. Preserve requirement definitions and scoped-versus-full acceptance.

## Implemented corrections and evidence
- Status lifecycle: shared sessions, joins outside lifecycle locks, explicit terminal owned WinRT
  runtime cleanup (ADR0008), 60 cases. Weather expiry runs independently of blocked HTTP, 56 cases.
- Settings: UI-owned model/FIFO worker persistence (ADR0007), 33 unit + 5 native editor cases.
  Quit settles editor/startup work, freezes new mutations, then flushes. Cancelled quit restores input.
- Search: 7 unit + 5 native UI cases; Release 2000-app p95 0.601 ms, 5000-app p95 1.962 ms in the
  recorded run. Native EDIT supplies selection/clipboard semantics; actual IME remains unqualified.
- Dock: 14 unit + 2 native cases; Top Bar: 7 + 2. UIA, keyboard, finite/reduced-motion transitions,
  update badge, glyphs, drag/hit testing and safe dark/light synthetic assets exist.
- Install library: 10 unit + 7 isolated integration cases. Setup: 5 native cases, including maintenance
  relocation and constrained DPI viewport. App: 3 CLI + 1 isolated lifecycle/health/channel case.
- Updates: 101 unit cases, including persisted skipped-version clearing. App updater handoff has a
  duplicate guard and observer established before launch, with shared completion surviving failed
  dispatcher delivery. This handoff is source-reviewed; its live signed-release cycle is unqualified.
- Release tooling: 32 Python cases pass. Public candidate review: 457 files, source attribution and
  Markdown link/requirement consistency checked. Final Release binaries/payload pass privacy audit.

## Remaining qualification
Windows 10/ARM64, physical monitor/DPI/removal, real AppBar/full-screen/Explorer restart, external
application launch/UWP frame identity, audio controls/startup policy, IME/screen-reader workflows,
owner-signed update/recovery and global performance budgets (Q-001). Final branding direction remains
Q-002. The original branding is an alpha proposal. See KNOWN_LIMITATIONS and PROJECT_SPEC.

Graphify launcher is unavailable/stale in this session; source/Markdown are current. Obsidian CLI
works while its app runs; docs/ is the vault. Local configuration, exports and owner canvas are not public.
