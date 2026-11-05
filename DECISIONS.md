# Decision log

Significant decisions have a full record in `docs/decisions/`. Status: **final** (do not reopen
without new evidence) or **provisional** (a qualification caveat, not automatic rejection of the
chosen design). Product requirements and their IDs: [PROJECT_SPEC.md](PROJECT_SPEC.md).
Approval and implementation verification are separate: ADRs 0002, 0003 and 0005 are accepted choices;
D3/D4/D6 retain their existing provisional qualification status until platform/behavior tests run.

**Current delivery:** **Aether Shell Alpha 3, `0.3.0-alpha.3`**, is the latest public release
(D48–D50, 2026-10-27): source pushed to `main`, tag `v0.3.0-alpha.3`, and a published GitHub Release
marked Latest that carries only the audited x64 setup and its checksums. The owner installed it over
Alpha 2 the same day (preferences and startup preserved; TEST_STATUS S-059). The previous release is
**0.2.0-alpha.2** (D29:
reviewed `main`, tag `v0.2.0-alpha.2` and its GitHub Release, 2026-10-22). Do not infer approval for
unrelated repository or account settings. Check HANDOFF for the exact pushed commits and receipts.

**Approval source.** The owner's brief delegates technical choices to the engineering lead ("choose …
and document the decision"). D1–D14, D16 and D17 were taken by assistants under that delegation:
"final" means settled by evidence, not individually reviewed by the owner, and such a decision is
reopened only through a new entry with new evidence. D15, D18–D21 and D23–D25 record explicit owner
instructions. D15, D20 and D21 were given to Claude in the conversation that produced them; D18, D19,
D23–D25 and D30–D33 were recorded by Codex from its own conversation with the owner. The conversations are not
published in this repository; approval records and executed verification remain distinct.
A proposal from any assistant stays "proposed" until the owner approves it.

| # | Decision | Reason | Alternatives | Consequences | Status |
|---|---|---|---|---|---|
| D1 | Native C++20/Win32 + Direct2D on Windows.UI.Composition (ADR 0001) | small footprint, fast start, zero idle CPU, real translucency | Electron, WinUI 3, Qt | more code for UI; full control of rendering | final |
| D2 | Blur via HostBackdropBrush on Windows 11 only; no undocumented accent API (ADR 0001) | documented rendering API | `SetWindowCompositionAttribute` | Windows 10 uses a translucent tint fallback; high contrast/transparency-off stay opaque | final |
| D3 | Minimum Windows 10 2004 (19041), x64 (ADR 0002) | composition interop + modern APIs | older builds | Windows 10 still untested | provisional (until tested) |
| D4 | Top bar = optional top AppBar; Dock floating by default and a mandatory bottom AppBar in D19 replacement mode; hide for full-screen apps; configurable hotkeys (ADR 0003, qualified by ADR 0009) | keep maximized applications clear of both bars | both overlays | needs Explorer-restart, allocation and full-screen qualification | provisional (until desktop-tested) |
| D5 | Update trust: ECDSA P-256 signed manifest + per-asset SHA-256; Authenticode optional until a certificate exists (ADR 0004) | checksum alone does not prove publisher | checksum only; Authenticode only | owner must protect the private key; CI secret required | final |
| D6 | Custom per-user native installer, versioned folders + launcher, journaled update transaction with health check and rollback (ADR 0005) | shared code path for install/update/rollback; no elevation | WiX/MSI, NSIS, MSIX | no MSI enterprise features | provisional (until implemented and tested) |
| D7 | In-house test macros + CTest labels unit/integration/network (ADR 0006) | no downloads, offline reproducible | GoogleTest, Catch2, doctest | fewer conveniences | final |
| D8 | Static CRT (`/MT`) | users need no redistributable | dynamic CRT + VC++ redist | slightly larger binaries | final |
| D9 | Modules that only need another module's contract header do not link its library (status_audio, weather, search) | lets modules be built in parallel | full linking | contracts must stay header-only | final |
| D10 | Weather provider Open-Meteo (no key, CC BY 4.0), disabled by default | no secret in the app, privacy | key-based providers | attribution must be displayed | final |
| D11 | Wi-Fi SSID never displayed; "microphone in use" reported unsupported | location-privacy rules on Windows 11 24H2+; no documented cross-version API | registry consent-store heuristics | less information in the bar | final |
| D12 | Development tools (Graphify, Obsidian) are never product dependencies; their outputs stay out of Git | public repo hygiene | committing generated graphs | each developer regenerates locally | final |
| D13 | Root continuity files are canonical by responsibility; PROJECT_SPEC owns objectives/acceptance, AGENTS rules, TASKS backlog, DECISIONS choices, PROJECT_CONTEXT map, HANDOFF latest session; `docs/knowledge/` keeps evidence/index/log | one source of truth for any assistant | duplicating state in the vault | the Obsidian vault (`docs/`) does not contain the root files; D15 adds requirements traceability | final |
| D14 | Parallel agent work runs in batches of three with implement → independent review, private build dirs | usage limits and file-ownership safety | all modules at once | slower wall-clock, less lost work | provisional |
| D15 | PROJECT_SPEC is authoritative for intended requirements; existing source/tests establish observed behavior; stable FR/NFR IDs link tasks and evidence; independent verification on takeover | explicit owner request, 2026-10-18 | another duplicate architecture/status document | no automatic promotion of previous AI claims; significant protected-scope changes require owner approval | final, owner-requested |
| D16 | UI-owned settings model with serialized worker persistence and UI completions (ADR 0007) | resolve GAP-007 without UI disk blocking | synchronous UI writes or waiting on a worker | Update acceptance and durable completion are separate; revisions retain dirty-state correctness; configurable chord fields preserve ADR 0003 | accepted; service32unitcases passed /WX, UI qualification in progress |
| D17 | Status sessions/impl lifetime shared across worker callbacks; Start/Stop never joins while holding lifecycle locks; owned process WinRT runtime with explicit terminal cleanup (ADR 0008) | resolve GAP-003 concurrency and self-destruction hazards while cached WinRT factories need MTA lifetime | unsafe per-provider final MTA teardown; leaked cookie | provider COM scopes balance individually; FinalizeStatusRuntime after providers/graphics stop releases the owned cookie, refuses live scopes and rejects later starts | reviewed; fresh49unit+10readonly+1shutdown cases pass /WX |
| D18 | Publish an independently initialized clean Git checkout; retain the development checkout/history locally | owner explicitly requires private/public separation; historical author metadata is not anonymized | rewriting the existing history (forbidden), pushing private metadata | only reviewed publishable files and public GitHub noreply identity enter the official repository; no remote is added to the private history. Initial path-pattern hits were triaged as example URL/key-name false positives, not private paths | accepted publication safeguard; export audit required before push |
| D19 | Reversible visual taskbar replacement with a recovery helper and mandatory Dock reservation (ADR 0009) | owner explicitly requested V2 on 2026-10-18 after observing two overlapping bars | retain coexistence only; replace Explorer; claim native Windows Settings takeover | originally default off with Desktop Apply consent; D23 supersedes that default while preserving explicit opt-out and Apply for later changes; native visibility uses a disclosed shell-class compatibility seam; no administrator rights, Winlogon changes or new library | owner-approved scope change; implementation and full qualification are separate |
| D20 | Every assistant session starts from the same owner-pasted prompt, `docs/SESSION_START_PROMPT.md`, which states the objective in brief and points to the repository files instead of repeating changing facts | owner request to Claude, 2026-10-18: each assistant must know the objective, read the handoff, verify the previous work, continue and document | ad-hoc prompts per session | the prompt and the reading order in `AGENTS.md` change together; facts stay in the canonical files | owner-requested |
| D21 | The taskbar replacement (D19) must be recoverable without Aether Shell running. Before any hiding, the captured taskbar baseline is persisted; `--restore-taskbar` restores the native taskbar from it even when no instance runs, and setup repair does the same. `0.2.0-alpha.1` is not published until this is implemented and verified | owner decision to Claude, 2026-10-18, answering GAP-011; matches brief §18 ("recoverable without Aether Shell running") | accept that the taskbar stays hidden until Explorer restarts when both the app and its helper fail (rejected by the owner) | amends ADR 0009's recovery limitations; restore must be idempotent, safe without a baseline and covered by fake-backend tests; real restoration needs an owner-authorized desktop check (T21) | owner-approved; implemented, unit-tested (S-015) and checked once on the real desktop (S-016) |
| D22 | Restoring the native taskbar shows every Explorer taskbar window and never hides one | real-desktop check (S-016): the baseline captured at start recorded the taskbar as not visible because the capture coincided with Explorer's re-layout; exact restoration would have hidden it | restore the exact saved visibility (Codex's original lease behaviour) | a taskbar window that was hidden before Aether Shell started is shown on restore; Explorer re-applies its own layout afterwards | adopted by Claude under the brief's delegated engineering authority (safety rule "prefer system safety"); owner may review |
| D23 | V2 visually replaces the native taskbar on launch by default; a saved explicit opt-out persists | owner explicitly requested that launching V2 remove the Windows taskbar and leave only Aether's Dock, then reiterated that only one Dock should be visible; recorded in this continuation on 2026-10-20 | require a separate first-launch Desktop Apply; permanently disable Windows taskbar; discard a saved opt-out | missing/non-boolean replacement values use true; Apply changes the Desktop choice; ResetToDefaults preserves that choice; `--restore-taskbar` persists false even after missing/corrupt settings, without rewriting a newer schema; setup discloses replacement and offers an optional restore shortcut; start at sign-in remains opt-in under the historical D23 policy (D34 supersedes the fresh interactive installer default with a disclosed checked choice and opt-out, preserving existing saved choices and Task Manager disables; settings/portable defaults stay false); D19/D21/D22 recovery and reservation safeguards remain mandatory | owner-approved launch behavior; batch-1 implementation is under integration verification, not a release or real-desktop acceptance receipt |
| D24 | Microphone menu requests Windows voice typing in the previous application's active field, alongside mute/privacy settings | owner explicitly chose "Windows dictation + settings/mute" in this Codex session, 2026-10-20 | mute/settings only; add an Aether recording service or always-on capture | explicit menu action sends Win+H only after verifying/restoring foreground focus; Windows handles voice support/permissions/processing; no speech capture/storage by Aether; fake automated tests never open real dictation | owner-approved interaction scope; implementation and manual voice/focus qualification remain separate |
| D25 | Hold all pushes, new tags and GitHub Releases; local commits remain permitted | owner expected additional changes before the next delivery, explicitly instructed 2026-10-21 | publish the current candidate immediately; treat older push authorization as still active | keep work local and documented; the next public delivery is Alpha 2; align final version/package naming before later delivery; require explicit owner go-ahead to lift the hold | owner-approved hold; lifted for the single Alpha 2 delivery by D29 |
| D26 | Grouped quick controls, neutral bar glass, aligned maximized-window margins and active local preview | owner request and clarification, 2026-10-21 | fake status/toggles; move manually placed windows; stop preview throughout development | real provider state and supported controls; honest Settings routes; uniform bar border without directional sheen; side work-area insets only for maximized windows; keep preview active except bounded tests and restore afterward; no autorun | owner-approved scope; implementation/native qualification pending; D25 unchanged |
| D27 | Continuous spring drag, minimal neutral surfaces, round status glyphs and genuine attention cues | owner instruction 2026-10-21 | instant swaps, hover tiles/separator/trailing native-bar arrow, inferred attention from arbitrary window opens, forced foreground | pointer-follow held icon, stable slot crossing with spring neighbors/drop; preserve magnification and keyboard focus, reduced motion; round proportional glyphs, green full/red critical battery; remove visible native-bar arrow while preserving recovery; real OS attention permits a bounded 6-second Dock-only fullscreen peek without focus theft | implemented and automated gates passed in S-027; owner/device visual and live-shell qualification remains; included in D29 release |
| D28 | General automatic weather location through Windows | owner explicitly requests OS-derived location for every installation, 2026-10-21 | hardcoded owner city; invented country/weather; undocumented scraping of Windows weather, SSIDs or raw IP; bypass OS/user denial | Windows location permission and bounded worker location reads; coarse coordinates to disclosed forecast provider; no weather when unavailable/denied; efficient refresh; preserve existing opt-outs/manual configurations; first-use choice/OS consent, no private data in public source | implemented and automated gates passed in S-027; live Windows consent/forecast qualification remains; included in D29 release |
| D29 | Publish the second public release as Aether Shell `0.2.0-alpha.2` | owner explicitly confirms Alpha 2 and authorizes commit, push, tag and GitHub Release on the official repository, 2026-10-22 | keep the D25 hold active; publish a different version or repository | push the reviewed public `main`, create `v0.2.0-alpha.2`, and publish the matching x64 installer/checksum after local build and privacy gates; leave unsigned update manifests unpublished | owner-approved, limited to this release |
| D30 | User-started clean-desktop cinematic showcase follows the supplied full-screen composition and closes visible user applications, then attempts to reopen them | owner visual reference and explicit process-closure instruction, 2026-10-22; supersedes the conflicting “do not close pre-existing applications” clauses in the supplied showcase brief §§7 and 11 | minimize/leave user windows visible for the marketing capture; claim perfect restoration of documents/tabs; store a process inventory or command line in public files/logs | show scenic desktop, compact inset Top Status Bar, centered bottom Dock and real pinned logos; close visible user-session applications on explicit showcase start (orderly close first; termination permitted for remaining user apps), keep Windows/system/own capture processes running, and attempt relaunch on ordinary cleanup paths. Forced termination/power loss bypasses in-process recovery; exact app/document/tab state is not universally restorable. Never automate real process closure in tests. Keep capture output local until frame-by-frame privacy review; no auto-commit/publish | native lifecycle and buffered runner tested; C5FF64A3 has scoped frame-contact privacy review; four rejected raw spools removed locally with QC retained; complete failure/device qualification remains |
| D31 | The showcase must meet a polished motion-design bar: authored, fluid movement and deliberate editing rather than simple state changes | owner motion-design instruction, 2026-10-22 | raw UI-state recording; abrupt cuts or unmeasured claims of production quality; adding an Adobe runtime dependency | use deterministic time-based keyframes, purposeful holds, continuous cursor/camera movement and eased joins; default quintic motion settles position, velocity and acceleration; custom cubic curves must have zero-velocity endpoints; target measured 60-FPS output and inspect motion in preview/export; optional blur derives from genuine temporal samples and preserves text readability. The reference to After Effects is a quality bar, not a required tool | native pipeline tested; exact C5FF64A3 export has complete decode and measured cadence/audio (S-033); normal-speed motion/listening acceptance remains unverified |
| D32 | The complete showcase video must achieve 100/100 on the supplied motion-design rubric | owner explicitly raises the whole-film target to 100/100, 2026-10-22 | treating the generic 92-point target as sufficient; scoring from a still frame, storyboard, or uninspected export; averaging a weak shot against a strong one | require every one of the 60 adapted checks to pass on the exact final export and all ten blocking gates to pass; audit the whole film, review motion at full resolution and its real playback, retain evidence/timecodes, then rescore after every change. The score remains an internal acceptance checklist, not a universal artistic certification | approved acceptance threshold; C5FF64A3 technical and scoped frame-contact checks complete (S-033); normal-speed playback/listening and full score remain unverified; no 100/100 claimed |
| D33 | Exact showcase order, loaded-app interval and empty ending precede restoration | owner's sequence clarification, 2026-10-23 | show only a first blank Code window; start the app hold after an extra reveal interval; restore old apps before the ending; use an invented pointer or omit Dock entries | two-second empty scenic desktop with real visible pointer; curved left approach, zoom and full genuine Dock traversal; one-second Code hover then real click; asynchronous English workbench readiness; three seconds total after readiness, including 1.6-second reveal and 1.4-second stable hold; return the pointer to its neutral start during reveal; request that scenario window close and observe window absence plus two consecutive captured body/backdrop samples matching the original clean scene; then three visually empty seconds, excluding DWM's closing fade, stop capture, restore old apps, then flush lossless buffered frames and render. Use actual visible control bounds, preserve authentic pixels, and fail on foreign surfaces without treating guards as complete privacy proof | owner-approved exact sequence; 66 unit/two own-window cases pass; C5FF64A3 ready/close/ending measured and visually inspected at contact/key scope; complete final-film acceptance remains open |

| D34 | Installed Alpha 2 starts at sign-in and remains available with reversible taskbar replacement | owner explicitly renews Alpha 2 publication and requests persistent installed startup, 2026-10-23 | leave only a temporary no-startup preview; hidden autoruns, services/elevation, Winlogon replacement, OS-critical/realtime priority or promises of indestructibility/first-boot ordering | fresh interactive setup visibly selects sign-in startup with an opt-out; existing saved choices and Task Manager disable remain respected; silent startup needs explicit flag; portable/settings defaults remain off until consent. Stable launcher supervises bounded abnormal Windows-exception crashes with backoff, revalidates active installation and exits on intentional Quit/update/uninstall and ordinary errors. Keep independent taskbar recovery, disable/uninstall paths and normal process scheduling. Windows controls startup order/delay; no promise that its taskbar never appears before Aether is ready or during failure recovery. Owner authorizes actual installation/startup on this machine | approved; implementation and fresh release/desktop gates in progress, T37 |
| D35 | General reusable capture and versioned public product gallery | owner requests desktop, quick-controls, settings and volume demonstrations for Alpha 2/Alpha 3 comparisons, 2026-10-23 | one hardcoded promotional film only; unrelated windows; missing pointer; publishing unreviewed personal content; replacing real product pixels with mockups | reusable bounded scenarios/actions drive genuine Aether UI; full-desktop PNG and MP4 with real cursor; close visible user apps including browsers on user-started capture, orderly first then termination if needed, protect infrastructure and attempt relaunch after capture. Explicitly authorized volume interaction restores observed initial state on ordinary cleanup; tests use fakes and never mutate devices/close apps. Keep raw media/process metadata local; review every frame and all stills, preserve approved images/video lineage by release, link gallery from README. Alpha 3 focuses on later design/motion optimization; do not label current material 100/100 without D32 proof | approved; reusable scenario and gallery work in progress, T38; owner proceeds to Alpha 2 publication without converting pending D32 checks into passes |
| D36 | Coordinate every assistant-operated desktop recording with the owner | owner asks for a readiness question before each recording so the computer remains usable during preparation, 2026-10-23 | surprise takeover while the owner works; treating prior blanket capture permission as the current readiness signal | finish preparation first, ask whether the owner is ready, wait for an explicit ready reply, then start that one capture attempt. Ask again before another attempt. Read-only preflight/code/build work can continue without recording or closing apps. The user-started CLI itself still needs no second in-program confirmation | approved; supersedes earlier no-warning permission for assistant-operated takes; ongoing coordination requirement |
| D37 | Publish Alpha 2 before optional presentation work | owner prioritizes the new release before the remaining usage quota, 2026-10-23 | delaying the release for repeated capture attempts or an optional video; publishing unreviewed media | complete the verified Alpha 2 source/package and publish the authorized release first; fresh screenshots/video may be added afterward. Preserve D36 readiness and every-frame privacy review for any later take. Do not expand testing beyond the changed behavior and required delivery checks | approved; supersedes the fresh-gallery-before-publication ordering, not privacy or package requirements |

### D38 — Alpha 2 accepted; begin the broader V3 design and interaction track

Owner-approved, 2026-10-23. Prioritize documenting the full request for alternating assistants,
given the remaining usage limit, and start the empty-margin input correction. Requirements are
FR-031–FR-036; detailed scope is [the V3 work order](docs/specs/v3-design-and-interaction.md).
The first batch does not complete V3. Preserve the usable installed Alpha 2 and its release/tag.
No V3 release number, tag or publication is approved yet. Native Quick Settings is preferred when
supported, with honest fallback; external-window animation/corner feasibility must be investigated
without injection, unsafe shell changes or unsupported universal promises. D36 capture readiness
and all public/private boundaries remain binding. T38 media work remains open but is no longer the
next priority ahead of this explicit V3 request.

Owner refinement, 2026-10-25: Claude implements the next focused T42/T43 batch; Codex independently
reviews it. Prioritize true hover previews with selected-window normal-close X, pointer continuity,
equal-width hover/foreground pills and slower continuous marker motion. Other V3 streams and
publication remain outside this batch. See [the implementation prompt](docs/CLAUDE_V3_HOVER_PROMPT.md).

Owner runtime feedback, 2026-10-25: the reviewed hover prototype works very well, but its card has
excessive empty height and width. Fit the panel around the existing miniature, reducing gutters
without reducing the thumbnail itself or the usable close button. This accepts the basic interaction,
not the final card spacing, all of T43 or a V3 release.

### D41 — Unify the appearance of all Aether-owned interfaces

Owner-requested direction, 2026-10-25 (FR-037/T47): the logo menu, Settings and the application's
supporting interfaces must share the existing neutral frosted-grain glass identity, restrained
light rim, refined glyphs/typography and deliberate fluid motion. A palette-only pass over the
current stock white Settings form or opaque owner-drawn menu is insufficient. Audit every owned
surface/state, implement shared presentation components and verify actual composed appearance.
Use the [Claude implementation brief](docs/CLAUDE_V3_VISUAL_UNIFICATION_PROMPT.md).

Owner follow-up the same day explicitly includes the complete installer/maintenance presentation:
welcome, licence, options/consent, progress/cancellation, finish/errors, upgrade, repair and uninstall.
It is a required visual redesign, not an optional branding-only pass. Preserve the existing
deployment engine, trust/rollback/data/startup policies and clean-machine independence; validate
the real setup target and isolated test wizard without modifying the owner's installed Alpha 2.

The owner assigns implementation to Claude and subsequent independent review to Codex. This is
the next V3 development priority, not completion/publication of V3. Preserve the usable private
prototype, installed Alpha 2, taskbar/material recovery, pin identity, compact previews and exact
input bounds. NFR-007's standard-control/native input and accessibility requirements remain:
styling does not authorize replacing text editing/IME/keyboard/UIA with painted placeholders.
Record and qualify any necessary compatible custom-control strategy before substituting it.
Windows-owned and pre-renderer emergency UI retain honest documented native exceptions. No
directional sheen, clipped square shadows, backend rewrite, new dependency or publication follows
from this visual direction. Implementation and owner visual acceptance are still pending.

### D62 — Alpha 4: Liquid Glass, then fluidity, then a trusted setup; first, the welcome bubble

Owner direction, 2026-11-05, after publishing 3.5. The next major version is **Alpha 4**, in parts, in this order:

1. **Design: Liquid Glass.** Aether Shell is a bubble of water and everything it shows belongs to it, divides from
   it and behaves physically. A new feature is also announced, not yet named.
2. **Fluidity:** recompute the application's motion and rendering for the least memory and graphics work, and make
   it rebuild its graphics reliably.
3. **A setup Windows trusts:** SmartScreen treats every new setup as suspect; the last step is a code-signing
   identity (Authenticode), which needs a certificate the owner obtains.

**First step, the welcome element.** After the greeting writes "Salut", the arrival plays, 3 to 5 s: a round drop
of water appears with the Aether mark, light above and its reflection below, semi-transparent with a grain layer;
it floats and deforms as water does in weightlessness, swells, divides in two; one drop strikes the top of the
screen and one the bottom, they bounce, and spread into the Top Bar (thinner) and the Dock (taller) at their
current sizes, which stay customizable. The same arrival will follow Close in the setup when it launches Aether.
Later, in Settings: faster, off, and detail. **The bubble has no colour of its own, never blue: water shows only the
colours of what is behind it** (owner, while reviewing the first frames).

The owner judges it first as a prototype outside the product, without reinstalling: `tools/liquid-glass/welcome-bubble.html`,
a web page used as a design tool only, written to be ported (motion as pure functions of time, material as one
shader). Nothing enters the shell or the setup before the owner validates it.

### D61 — Test 3.5: the welcome's name in setup, its picture always, a lighter pen; a README gallery

Owner instruction, 2026-11-05, before deciding whether to publish what D60 prepared. **Correction first:** D60's publication never
happened. The 3.4 session built and installed its setup on the owner's machine and wrote the documents as if published, then stopped
before any commit: there is no tag, release or S-067 receipt for 3.4. This work is test 3.5 (D57) and contains all of 3.4; whether
and under which number it is published is the owner's call after trying it.

- **The name in setup:** the welcome said only "Salut" until the name was typed in Settings, which few people find. Setup's Options page now
  has an optional field for it (FR-041's local name), showing the saved name on an upgrade or repair, and saving it only when it changed,
  only into the chosen data folder, trimmed and within the settings' limits. Uninstall does not ask. Silent setup has no option for it.
- **The picture behind the welcome:** black on the owner's machine. Measured there: the desktop picture's file had been deleted from the
  Downloads folder after it was set; Windows then reports no picture for the monitor (`IDesktopWallpaper::GetWallpaper` is empty) although
  the desktop-wide setting still names the file and Windows still shows the picture from its own copy (`TranscodedWallpaper`, readable).
  Aether read an empty answer as "plain colour". Now the desktop-wide name decides between picture and colour, and Windows' copy is tried
  when the file cannot be read. The log says which way it went, never the path.
- **A lighter pen:** every frame blurred the whole greeting's ink again for its soft dark edge, most of the screen. The edge is now kept in
  its own bitmap and blurred again only around the ink just added, with Direct2D's exact blur so that pieces meet without a seam:
  identical to 1/255 in an offscreen comparison, about 5 % of the blur work per frame. Each greeting logs its frames, rate and longest
  gap, so that the next report of a jerky pen comes with a measure.
- **README:** the owner's three new desktop captures join the two from D60. Shown as an animated slideshow (WebP, five desktops, about
  three seconds each with a short cross-fade) and a strip of thumbnails that open each picture at full size, since GitHub runs no script.
  The download links point at the latest release, so that they never lead to a missing page, and the stale "screenshots will be
  added" section is gone. The pictures show the owner's desktop and third-party wallpapers; the README says these are not covered by the
  project's license.
- **Validated (owner, 2026-11-05):** after trying test 3.5, "everything pleases me, I see no bug for now"; commit and push to `main`
  approved. Not fixed and left for later investigation: the blank page (AS4-01) and a newly reported disappearing Claude window. The next
  batch is announced as version 4 ("very advanced"), not 3.6; its content is still to be named.
- **Published (owner, 2026-11-05: "Yes, publish 3.5"):** a normal GitHub Release marked Latest, tag `v0.3.5-alpha.3`, setup and
  checksums only. The release's setup is rebuilt from the tagged commit, because the validated test setup was built before its commit
  and embeds the previous revision (`8af10b2`); the source of both is the same, the Markdown aside.

### D60 — Version 3.4: the top pixel only, a quarter of a second, a flowing pen, panels that close, a complete search

Owner instruction, 2026-11-01, after using 3.3. To be published as version 3.4 of Alpha 3, `0.3.4-alpha.3`, a normal GitHub Release marked
Latest, right after commit and CI, without a private trial, at the owner's word. The owner first named 3.3, then 3.4; 3.3 keeps its
tag and stays in the list. **Not done (D61):** the session stopped after building and installing the setup; nothing was committed or published.

- **Top Bar, supersedes D58:** the wait starts only on the very top row of pixels of the primary monitor, with no margin, lasts
  0.25 s, and one pixel down cancels it. D58 had extended the zone to where the bar appears, for a hand stopping there; that put the
  bar over browsers' tabs and close buttons, the very thing Auto-Hide exists to leave free. A resting hand's wobble no longer counts.
- **Writing, supersedes D59's 1.5 s:** 2.2 s. The owner found 1.5 s stressed and without its refinement, and still wants it quick. The
  time alone was not the fault: squeezed into so little, a hand's letter-by-letter changes of pace became a flicker of speed, so these
  now change slowly and less, over one gesture that sets off and lands gently. The newest ink starts fine and settles to its width just
  behind the tip, so the line is seen being drawn. The letters and their widths are unchanged: "La police est très bien." With the reading
  time and the fade the whole greeting takes about 3 s.
- **Panels close when anything else is used, and on Escape:** activation was not enough, because a panel without the foreground never
  hears of a click on the application that has it. See `src/app/PanelDismissal.h`: raw mouse presses, touch contacts, the foreground
  moving to another process, and an Escape hotkey that exists only while a panel is open. Escape closes at once, also from a detail view
  and with text typed in Search. Glass menus already had this and Windows owns its own flyouts.
- **Search finds Windows' own tools:** consoles (Services, Event Viewer, Computer Management, Task Scheduler...) were in the Apps folder
  and rejected as documents; Device Manager exists only among the Control Panel's items, which were not read at all; Disk Management and a
  few others are only files in System32 (a closed list, the only such names that open anything). Every match can be reached, not the best
  eight, and only rows on screen ask for an icon. What was typed goes when the panel does.
- **Settings no longer shows a page twice over:** the owner saw it on two machines after a check for updates. To scroll without redrawing everything, a
  form keeps what it has drawn. A check adds a progress row and takes it away, which moves what is below it, and only what is on screen is told to redraw, so
  the kept pixels still held the old rows, and the next scroll showed both. A label that changed out of view also came back with its old words. Any change of
  arrangement now redraws what is kept, and a change of text forgets a label's kept image. Reproduced on 0.3.0's source and on this one before the fix.
- **A newer release without a signed manifest is announced, not a failure:** every release so far has none, because no unsigned manifest is published, so every installed copy
  showed "The update could not be completed. Failure: Validation" with nothing to do about it, while a newer version was waiting. Settings now names that
  version and says it installs by hand, through Release details; nothing is offered or downloaded, so the policy is unchanged. Other failures are said in words.
- **README pictures:** `resources/preview/` had never been committed, so GitHub showed broken images. Committed at the owner's request, after
  looking at both: no name, address or path shows. They do show the owner's desktop, its pinned applications, a small Chrome profile picture, and
  wallpapers not made for this project (a photograph and One Piece art), which the MIT license of this repository does not cover. The owner decides whether to keep them.

### D59 — Version 3.3 is published as the Latest release; half a second, two seconds, and the desktop

Owner instruction, 2026-11-01. The source goes public as **3.3**, version `0.3.3-alpha.3`, a normal GitHub Release marked
Latest, in place of Alpha 3 for new downloads. It is not called Alpha 4 and is not presented as the end of the next version.
The owner first asked for a pre-release, then settled on this in the same exchange. Work continues on top of it.

- **Top Bar:** half a second in its place (D58). The owner also said "le prochain raccordement, tu mets à 0.25", whose
  object is not certain; 0.5 s is what the owner said of the bar itself. A smaller number brings the bar over a browser's tabs sooner.
- **Back on the desktop, both bars return,** even with windows behind it: nothing counts as covering a bar while the desktop
  itself is in front (Show desktop, a click on it). The covering check returns to four times a second, because halving it
  did not lower the idle cost and it delays the bars' return.
- **The welcome takes about two seconds:** 1.5 s of writing whatever the length, 0.5 s to read, then a 0.3 s fade. The hard
  deadline falls to 6 s. Its font and look stay as they are, at the owner's word.

The owner also announced new features to come; none was named, so none is in 3.3.

### D58 — The Top Bar comes out one second after the pointer reaches its place

Owner instruction, 2026-11-01: one second, not two ("au bout de la seconde, le top bar s'affiche"). Test build 3.1 already
waited one second, but only from the top 6 DIPs. A recording of the pointer's height on the owner's machine showed why it
still felt long: on one reveal the hand rested 2.6 s at 52 pixels (150 %), the middle of where the bar appears, and the bar
came out 4.9 s after the hand entered that place. On the top row itself it took about a second every time (998-1172 ms, 7 of 7).

The second now starts anywhere in the place the bar appears, its margin and height from the top of the screen (38 DIPs by
default), and a wobble within 10 DIPs below it does not restart it. Leaving cancels it, so passing through does nothing.

**The price, and the owner's to judge:** D52 asked for a wait so that the bar would not come over controls near the top of
a maximized application. A browser's tabs are in this place, so a pointer resting on them for a second brings the bar over
them. If that is a nuisance, the alternatives are a smaller place or a longer wait; both are one constant.

### D57 — Every finished piece of work ends in a commit and a test build

Owner instruction, 2026-11-01. When work is finished: commit it and hand over a setup executable. That
executable is a **test build**, not the release everyone gets. The numbering follows the alpha: Alpha N is
version N, and N.M is its test M, so `0.3.1-alpha.3` is "Alpha 3, test 3.1" (the patch number counts test
builds, starting from the published `0.3.0-alpha.3`). A test build goes to the owner from `dist/` and is not
published. Putting one on GitHub as a pre-release is rare and the owner's call each time, as is a full release.
The instruction covers commit and the executable: not push, tag, release or installation over the owner's copy.

### D56 — Second round of the owner's trial: bar timing, corners, a real pen, Wi-Fi networks

Owner instruction, 2026-11-01, after trying D54 and D55:

- **The Top Bar waits one second, and the wait starts sooner.** The owner still felt five seconds. The
  log the last round added shows the old wait ran exactly as written (eleven reveals, the timed ones
  2000-2016 ms), so the difference lay before the timer: it only started on the last rows of pixels. It now starts
  in the top 6 DIPs and holds through the top 16, and the wait is 1000 ms. This changes D52's number, the owner's
  own, because the owner's complaint is the wait as it feels. One constant, if it needs changing again.
- **Rounded corners belong between the bars.** A maximized window that covers the whole monitor, which is what
  both bars hidden gives, takes the screen and is left square. The AS4-06 correction of S-061 had this the wrong
  way round and is reverted: what the audit called a defect was what the owner wants.
- **Chromium keeps its corners between the bars.** A browser re-asserts a plain rectangular region or its
  material, which ended the rounding for that whole maximize, or after eight re-assertions. A plain rectangle or no
  region is now a re-assertion and not a shape of its own, and the retry budget limits a burst, refilling after a
  quiet minute. An application's real shape still wins until the next maximize. The cause is established from
  Chromium's documented behavior and this source; the owner's Chrome has not been observed through it.
- **The welcome has the screen to itself.** The Dock and Top Bar stay off the screen while the cover or the greeting
  is up, ready underneath, and come out when it ends.
- **A calligrapher's pen.** The owner called the writing childish. The authored paths are unchanged, but the pen now
  leans forward, long letters reach well above small ones, down strokes swell while up strokes are hairlines, strokes
  enter and leave on a fine point and a dot is a dab. Writing fills the room given (88 % of the width or 82 % of the
  height), on one line or two, whichever is larger. A doubled point at a letter join, which knotted the spline, is gone.
- **The Wi-Fi view lists the networks around, and joins them.** Names and signal strength, the connected one first; a
  scrolling list by finger and wheel. Choosing one connects; a network that needs it asks for its password in a masked
  native field. Windows keeps the password in its own profile; Aether keeps nothing and logs neither names nor
  passwords. An enterprise network goes to Windows' own dialog. **This changes FR-008 at the owner's request:** network
  names are read and shown only here, only once this view is opened; the Top Bar still shows none.

Also done from the handoff: AS4-18 (Settings says what Windows will really do at sign-in, and Apply repairs a lost
registration while a Task Manager turn-off stays Windows'); AS4-21 (a failed save puts the registration back); AS4-05 (a
new guard takes over the exact clip of a guard that died, and removes it on release); and startup diagnostics for AS4-02.
Walking the windows less often did not visibly lower the idle cost (S-064), so that remains open. AS4-23 stays open: its update path is dormant while signed updates are unavailable.

Unchanged and recorded: Aether's own Settings window keeps its rounded frame when maximized, whatever the bars do.
Evidence and limits: TEST_STATUS S-064.

### D55 — Quick Settings scrolls under the finger, and sliders go where the hand goes

Owner report, 2026-11-01, from the first trial: the second Quick Settings page (Project, All settings) could
not be reached by touch, and some sliders are slow or return to where they started.

- **The pages move with the finger, up and down.** They are stacked vertically, but T51 listened only for a
  sideways swipe, so a vertical pan was ignored: an implementation error, and D53's "pages turn on release"
  no longer holds. A pan moves them continuously; on release they settle on the nearest page, or the next one
  after a quarter of a page or a flick. Past the first or last page they give a little and return, without
  wrapping around. The sideways swipe is gone. Buttons, dots, keys and the wheel are unchanged.
- **A form slider goes where the hand goes.** The native trackbar drags only from its small thumb and answers
  any other press with one page step (reproduced: a press at 80 % moved it from 20 to 40 and no further).
  Mouse presses on a slider are now handled by the form surface: on the thumb it takes hold without jumping,
  anywhere else it goes there at once, and either way the drag continues. The control keeps its value, range,
  keyboard and accessibility. By touch, direction decides: sideways moves the slider, up or down scrolls the
  page, a tap goes to the finger.
- **A held slider only stages its value.** Applying re-themes and lays out every surface on the UI thread, and it
  used to run whenever the hand paused for 350 ms. It now runs when the slider is let go.
- **A requested value holds until Windows reports a change.** A display's brightness reading can lag. After
  1.5 s the panel returned to the stale reading, and nothing read it again until it reopened, so the thumb went
  back and stayed there. This is the source-established cause found for "returns to where it started". The panel
  now holds a request for up to 4 s, settles when a reading has really moved to about that value, and the
  application re-reads brightness meanwhile. Brightness also follows the thumb during a drag, at a slower pace
  than volume. A native return was tested for the mouse and not found: a thumb drag with the old control stayed.

Evidence and limits: TEST_STATUS S-063. Synthetic contacts only; **nothing here has run under a real finger,
and the real brightness path has only compiled.**

### D54 — After the first trial: a Top Bar wait a hand can hold, and a welcome on the desktop picture

Owner instruction, 2026-11-01, after trying a private build on the real machine. The owner approved the
moving-stroke writing itself and rejected the rest of the greeting's presentation:

- **The Top Bar took "almost five seconds", not two.** The decision logic does wait exactly two seconds,
  but only while the pointer stays on one exact pixel row, so one pixel of movement of a resting hand
  restarted it. Now the wait starts within the top 2 DIPs and continues within the top 12; a real move away
  still cancels it. This refines D52's "exactly the top pixel row"; a monitor above still does not count.
  The cause is established from source, not measured: a recording made during this work caught no visit to
  the edge. Each reveal now logs its wait, so the next trial measures it.
- **The greeting arrived late, after the desktop.** The background now goes up when the session locks,
  behind Windows' lock screen, so an unlock reveals it instead of the desktop, and the writing starts at the
  unlock. Waking without a lock greets too, but the desktop shows first there. At sign-in it can appear
  no earlier than Windows starts Aether; a different startup mechanism would need its own decision.
- **The black background is gone.** The background is the person's own desktop picture, opaque and softly
  blurred, under a veil only as dark as the ink needs (4.5:1 on the picture's average). A plain-colour desktop
  uses that colour. High contrast keeps its system colours. The picture is read locally, only while a
  greeting is due, and is never logged, stored or sent.
- **Much larger, and longer.** The writing takes up to 86 % of the screen's width and 3.6-6.2 s, then the
  finished words stay 1.4 s and the screen eases back to the desktop in 0.5 s. The hard deadline is 12 s.

Engineering choices within this, open to the owner's view: the blur's strength (1.2 % of the picture's width),
the veil, the pale ink with a soft shadow, and the primary monitor only. Windows' lock screen is itself a
full-screen window, so the greeting now judges full screen from the foreground window, ignoring that class,
rather than from the state the bars keep. A cover is removed if Windows reports the session unlocked, or
cannot say, even without a notification, and any click or touch removes it.

Evidence and limits: TEST_STATUS S-062; ADR 0013. **The cover has not run at a real lock.**

### D53 — Engineering choices for T51/T52, open to the owner's review

Claude, 2026-11-01, implementing D51 where the work order left the choice open. None of these was
decided by the owner, and each is easy to change:

- **The welcome is on by default**, with no name until one is entered. The owner asked for the feature;
  it is brief and skippable, and Settings → Welcome turns it off. Default off is the alternative.
- **First run opens Settings → Welcome** instead of Weather, to offer the optional name with its
  explanation. The setup wizard itself gained no name field.
- **Hidden bars are reached by a swipe in from the screen edge, read passively from raw digitizer
  input.** A visible handle would be simpler but would intercept other windows' input at the edge,
  which FR-039 forbids. Windows' own edge gestures still receive the same touch.
- **In forms, a tap acts on release, with no pressed state before it,** so a pan can never click.
  Native editors, lists, sliders and scroll bars keep their own touch handling.
- **Quick Settings pages turn on release** rather than following the finger; sliders do follow it.
  Superseded by D55 after the owner's trial.

Evidence and limits: TEST_STATUS S-061; ADR 0013.

### D52 — The Top Bar's Auto-Hide edge reveal waits two seconds

Owner instruction, 2026-11-01: an accidental visit to the top edge revealed the Top Bar at once and
could put it over a control being used near the top of a maximized application. The pointer must now
stay on the top edge for two seconds; leaving earlier cancels the wait and the next visit starts it
again. This refines FR-039 for the Top Bar only: the Dock keeps its 40 ms dwell because the owner
reported, and scoped the change to, the top edge. The wait guards only a hidden bar, so a bar already
shown is not hidden while the pointer rests on its edge. Use, panels, keyboard, an uncovered desktop
and turning the setting off still show the bar at once. The edge is now exactly the primary monitor's
top pixel row, so working on a monitor above no longer counts as resting on it. The Dock's bottom
check keeps the equivalent behavior for a monitor below; that is recorded, not changed here. Evidence:
pure decision tests; the owner's check with real applications is pending.

### D51 — Audit Alpha 3, stabilize the next batch, add complete touch and a transient welcome

Owner instruction, 2026-10-28, given to Codex: prepare an efficient technical English prompt for
Claude, and independently search the current source for defects beyond the reported blank/grey
maximized application windows after Auto-Hide changes, startup failures and poor fluidity. Codex
audits/prepares; Claude implements the fixes and continues discovery. T50 records stabilization;
FR-040/T51 adds complete touch pan/fling/elastic return and input arbitration. FR-041/T52 adds
original handwritten FR/EN greetings with an optional, explained local display name after sign-in
and unlock, while essential Aether surfaces prepare. The secure Windows authentication desktop is
untouched; this is transient supporting UI (ADR 0013), with immediate skip and a fail-safe deadline.

The owner calls the next development batch version 4 and expects substantially stronger release
quality and fewer dangerous/reachable defects. No specific version string, new tag/publication,
installation, changed startup mechanism or Windows/macOS port is approved by this preparation.
Future macOS adaptation is a direction, not current implementation. D50's Latest publication does
not waive missing qualification; this report explicitly reopens the reported reliability issues.
The audit distinguishes owner reports, source-established defects, risks and missing features;
neither exhaustive bug freedom nor security certification is claimed. Source implementation,
physical touch, actual reboot/unlock timing and owner acceptance remain pending. Work order:
[Claude prompt](docs/CLAUDE_V4_STABILITY_TOUCH_WELCOME_PROMPT.md),
[prioritized audit](docs/specs/v4-stability-audit.md); evidence: TEST_STATUS S-060.

### D50 — The owner accepts Alpha 3 as version 3 and publishes it directly as Latest

Owner instruction, 2026-10-27, given to Claude while D49's package was building: the owner counts
the latest version as valid, as version 3 of the application, so no draft is needed; publish it
directly as the Latest release. This supersedes D49's draft-only scope. Following the Alpha 2
precedent (a published, non-prerelease release marked Latest; GitHub never marks a prerelease as
Latest), the release is published as a normal release with exactly the setup and checksums, and the
README download links move to it. The tag stays on the CI-verified revision; later documentation
receipts do not move it. The acceptance covers the Alpha 3 build as a whole; checks that were never
executed (for example Windows 10, multi-monitor/DPI and other window-rendering families) stay listed
as limitations rather than becoming passes. Afterwards the owner asked for Alpha 3 to replace the
installed Alpha 2 while keeping preferences and the startup choice, and ran the published setup
themself; the assistant verified the result read-only. Evidence: TEST_STATUS S-059.

### D49 — Tag `v0.3.0-alpha.3` and prepare a draft GitHub prerelease for owner review

Owner instruction, 2026-10-27, given to Claude: tag `v0.3.0-alpha.3` and prepare a draft GitHub
prerelease with the audited setup and checksums for the owner's review. Following RELEASE_PROCESS and
the Alpha 2 precedent: the tag is a plain tag on the exact pushed source revision whose Windows CI
passed; the setup comes from a fresh Release `/W4 /WX` build of that revision in a new build directory
(a reused cache can keep an older prerelease identifier) and passes the package privacy/freshness
audit; the draft is marked as a prerelease and carries exactly
`AetherShell-0.3.0-alpha.3-win-x64-setup.exe` and `AetherShell-0.3.0-alpha.3-checksums.txt`. Without
the trusted offline signing key the manifest stays local and unsigned; no manifest, signature, PDB or
media is uploaded. The release text comes from `docs/releases/0.3.0-alpha.3.md`.

Not authorized: publishing the draft (making it public), moving the tag after review, switching the
README download links, or installing Alpha 3 over the owner's Alpha 2. Evidence: TEST_STATUS S-059.

### D48 — The current V3 source is Aether Shell Alpha 3 (`0.3.0-alpha.3`); push it to `main`

Owner instruction, 2026-10-27, given to Claude: commit and push the project as **AetherShell Alpha 3
(v3)**, including all new designs; this is version 3 of the application. This supersedes D38's "no
V3 version number" and the D45 brief's "no push" for this source only. Following the existing
`0.1.0-alpha.1` / `0.2.0-alpha.2` pattern, the version is `0.3.0-alpha.3` (CMake project version
`0.3.0`, prerelease `alpha.3`). "All new designs" means the implemented V3 designs in source and
documentation: the unified glass Settings, menus and installer, owned Quick Settings, Dock hover
previews and markers, notifications, hidden icons, rounded maximized windows and Auto-Hide. The
private reference images and runtime receipts stay out of Git as before.

Not authorized by this instruction: tag `v0.3.0-alpha.3`, a GitHub Release or uploaded assets,
replacing the installed Alpha 2, or changing repository settings. RELEASE_PROCESS still governs any
later release (CI on the pushed revision, audited package, reviewed notes, then the owner's release
approval). Owner acceptance of the S-054–S-057 behavior remains pending and is not implied by the
version name. Evidence: TEST_STATUS S-058.

### D47 — Auto-Hide for the Dock and the Top Bar; follow-ups to D45/D46

Owner direction, 2026-10-27, given to Claude in the implementing session (new FR-039/T49): the Dock
and the Top Bar each get an independent, persisted Auto-Hide setting for a true full-screen
experience. An enabled bar hides while an application window covers it and returns when the pointer
reaches its screen edge (bottom for the Dock, top for the Top Bar) with smooth transitions; it stays
while in use and hides again afterwards; on the desktop with no window over it, it stays visible
without the setting changing. Technical choices (Claude): like Windows' auto-hide taskbar, an
auto-hidden bar reserves no work area, so maximized windows keep the full screen and the revealed bar
floats over them instead of resizing every window on each reveal; with both bars auto-hiding the
replacement-mode side margins are dropped too. Edge and cover detection sample the pointer and window
bounds on a 50 ms timer that runs only while a bar has Auto-Hide on; no hook or strip window
intercepts input at the screen edges. Full-screen suppression keeps priority.

Same session, owner feedback on D45/D46: the hidden-icons chevron is a toggle (arrow up while
Windows' overflow is open; a second click closes it), the Top Bar's focus ring follows keyboard
navigation only, and the corner guard clips a window again (bounded, growing delay) when an
application merely re-applies the material it had before the clip, because Chromium's re-applied
Mica left some maximized windows square. A different material or an own region still wins.
Evidence: TEST_STATUS S-057; owner acceptance pending.

### D46 — A ring window beneath maximized windows; Settings draws its own maximized caption

Owner choice, 2026-10-26, asked by Claude while implementing D45 (FR-036/T44, FR-037/T47): the
thin perimeter around an eligible maximized foreign window is a click-through Aether ring window
directly beneath it, with the window clipped inside its rounded outline by the ring width. This is
the separate technical decision D45 required for the backend change; it adds no global overlay,
injection, subclassing or geometry change. Captioned, resizable `WS_EX_LAYERED` windows become
eligible, and an application's own region/material replacement wins until that window's next
maximize. Technical consequence for Aether Settings (no owner choice needed, recorded here): a
window region removes DWM's caption buttons on Windows 11, so a maximized Settings window draws
minimize/restore/close itself with native hit codes (Snap layouts), system commands and
title-bar accessibility rectangles, and turns opaque while maximized. Details, rejected alternatives
and limits: [ADR 0012](docs/decisions/0012-maximized-window-perimeter.md); evidence: S-054.
Owner visual acceptance and broad application compatibility remain pending.

### D45 — Notifications, real hidden icons and complete rounded maximized frames

Owner direction, 2026-10-26 (FR-038/T48, FR-011, FR-036/T44, FR-037/T47): the Top Bar date/time
must open the actual notification center/calendar; add a small dedicated chevron for the actual
hidden notification-area application icons. The latter are background app icons, not notification
messages or a second Dock. This narrowly supersedes D27's arrow prohibition for that purpose only;
existing taskbar-recovery entry points remain. Reuse supported Windows interaction and genuine state,
qualify hidden-taskbar/version/monitor behavior, and state unsupported overflow placement/access
instead of inventing icons or treating a taskbar-restoration button as completed overflow.

The owner also rejects square corners on a maximized external application and explicitly requires
a thin continuous Aether-style perimeter around the entire window, including true maximize. This
clarifies FR-036/FR-037 beyond D40's region/material lease, which draws no rim. Preserve maximize,
placement, native caption/input/UIA and real transparent cutouts. Qualify owned and foreign window
paths separately; respect protected/unsupported targets and conditional ownership/restoration.
No injection, private Explorer memory/interfaces, silent restore-and-resize, or unapproved global
overlay is authorized. Significant backend/ownership changes need a separate technical decision.

Claude implements [the focused brief](docs/CLAUDE_V3_NOTIFICATIONS_TRAY_FRAME_PROMPT.md), including
Codex's S-053 source-review fixes. Codex prepares the shared assignment and private images; actual
delivery/read in an external Claude chat is not established. Five owner references remain local
and ignored; their private contents must not enter public material. No installation, push, V3
release, device-state change or new persistent desktop surface follows from this direction.
Requirements are authorized; implementation, actual native/foreign compatibility and owner visual
acceptance remain pending. S-053 records the independent existing-unit-binary rerun and review.

### D44 — Recreate Quick Settings as a complete Aether-owned native panel

Owner direction, 2026-10-26 (FR-026/FR-035/FR-037, T41/T47): rather than depend on unestablished
placement of Windows Quick Settings, implement Aether's own native panel closely matching the
supplied private reference's layout, control families, polished recognizable icons, interactivity
and animation. It must open below the clicked Top Bar group and use Aether's neutral translucent
fine-grain material in both themes. The previous crude airplane glyph is explicitly rejected;
compare real rendered icons against the private picture. Reuse proven native providers/adapters,
but rebuild the presentation as needed. This is the primary panel for the reviewed private
candidate, not an unfinished fallback or a simulated Windows screenshot.

This supersedes the native-placement-first investigation in the earlier section 14 of the brief.
Claude implements; Codex only updates/transmits instructions and privately supplies the owner images.
See [the updated brief, section 14](docs/CLAUDE_V3_SETTINGS_FLUIDITY_PROMPT.md). The owner authorizes
the private candidate's Aether routing; record coherent defaults/migration and any retained explicit
native alternative. Do not let a stale native-routing flag bypass the requested panel or modify
installed Alpha 2 preferences/startup during this development preview.

Preserve real observed state, supported APIs, explicit consent, failures, native input/UIA and
FR-008 network-identifier privacy. Where Windows exposes no supported equivalent, report the
specific functional gap instead of inventing a toggle/state or claiming full parity. Installed
system-icon rendering is permitted under FR-022; do not redistribute fonts or extract reference
pixels as shipping assets. Preserve media capability conditionally without the default empty media
card. The panel remains transient; no extra persistent desktop surface, new dependency, installation,
push, tag or release is authorized. The supplied pictures remain local and ignored.

Routing policy as implemented (Claude, 2026-10-26, S-052): a new `topBar.windowsQuickSettings`
(default false) is the only routing choice. The Alpha 2 `openWindowsFlyouts` flag defaulted to true,
so a stored true is indistinguishable from an untouched default; it is kept, validated and written
unchanged for older builds but no longer routes. Files without the new key, including every Alpha 2
file, open the Aether panel; the Settings switch records an explicit native choice. No installed
preference is rewritten. Unsupported global switches (airplane mode, energy saver, night light,
hotspot, nearby sharing) are labelled Settings routes; see [quick controls](docs/modules/controlcenter.md).

### D43 — Settings must remain coherent during movement and share its glass with the top frame

Owner direction, 2026-10-26 (FR-037/FR-015/T47): the current private Settings presentation is not
accepted. The owner reports content breaking apart during scrolling and reconstructing on release,
insufficient actionable-icon hover response and abrupt state changes. The supplied private still
also shows a caption strip materially different from the content. Settings must match the current
Dock's quality of responsiveness, continuous interaction and restraint, without retuning the Dock.
The title/frame region is explicitly included in the coherent translucent design where supported;
preserve native window commands, accessibility and system fallback policies.

Claude implements next; Codex prepares the
[detailed implementation/acceptance brief](docs/CLAUDE_V3_SETTINGS_FLUIDITY_PROMPT.md) and reviews
afterward. The reported motion defect is not independently reproduced by this documentation turn.
Source inspection identifies separately repainted native children/backgrounds as one hypothesis,
not an established cause. S-049's passing functional tests and resting stills do not establish
continuous-frame visual quality. Require before/after reproduction, meaningful intermediate-frame
regressions, actual visual inspection and stated performance measurements. Numeric timing/cadence
targets in the brief are reference-configuration engineering proposals, not new global release
budgets or amendments to Q-001. D42's implementation may be revised with evidence and a focused ADR
update; native editing/UIA/consent/persistence requirements are preserved. This turn authorizes no
source implementation by Codex, new dependency, installation, push, tag or release.

Implementation (Claude, 2026-10-26, S-052): reproduced in own-window moving frames and corrected
with atomic container presentation, live-editor motion masking, retained layers, a spring scroll and
an integrated Acrylic title strip with native caption buttons; see the ADR 0011 amendment. Owner
acceptance of the moving interface remains pending.

### D42 — Native forms on one scene with opaque native editors; glass popups behind IMenuPresenter

Implementation decision under D41 (Claude, continued/reviewed by Codex, 2026-10-25,
[ADR 0011](docs/decisions/0011-native-form-presentation.md)); implementation review completed,
owner visual acceptance pending, not a new owner-approved objective. Application windows that keep native
child controls (Settings, setup, the updater, confirmations) use the new `ui_form` module instead of
`SurfaceWindow`: a normal redirected, activatable window with DWM's system Acrylic
(`DWMSBT_TRANSIENTWINDOW`, frame extended over the client) where Windows 11 22H2+ and transparency
allow it, an opaque fallback elsewhere, and stock system-colored painting in high contrast or without
a renderer. All client pixels come from one premultiplied scene rendered per window through buffered
paint and a Direct2D DC render target; native buttons, check boxes, combo boxes, track bars, progress
bars and list boxes keep their classes, messages and system UI Automation providers and only change
their painting. Text editors stay native EDIT/HOTKEY controls on opaque layered child surfaces inside
drawn fields, so caret, IME, selection and clipboard are untouched (NFR-007 is preserved; no control
was replaced by painted text). Popup menus use a composition `GlassMenuPresenter`
implementing the existing `IMenuPresenter` contract with TrackPopupMenu semantics (modal, same ids,
foreground rule, thread keyboard taken by its own `MessageLoop::RunNested` loop, mouse capture for
outside clicks) and fall back to the owner-drawn/native HMENU presenters in high contrast or on
failure. Rejected: compositor-only Settings with GDI children (airspace/redirection problems with
`WS_EX_NOREDIRECTIONBITMAP`), recoloring the stock controls (does not reveal the backdrop), and custom
text editing (violates NFR-007). Consequences: Windows draws an inactive window's Acrylic as its flat
fallback; combo boxes keep their native dropdown with shared field/list colors so Selection and
ExpandCollapse remain available. A glass MenuItem list without equivalent combo semantics was
rejected during review; layout is
DIP-based with fractional-scale rounding; setup now links the graphics/ui_form libraries but still
runs before Aether is installed.

### D40 — Reversible corners for eligible maximized windows

Owner-requested outcome, 2026-10-25 (FR-036/T44): retain true maximization and add rounded visible
corners, rather than restoring/resizing normal windows to imitate maximize. Implementation choice:
a separate child guard observes native geometry events, applies a provisional 12-DIP region lease
and conditionally restores it on stop or parent exit. The main UI never performs foreign region
calls. Start only during active taskbar replacement; isolated sessions cannot start the policy.

Use documented APIs, same-user/session/medium-integrity checks and HWND lifetime markers. Preserve
native placement, original simple regions and application replacements; retain snapshots and retry
recoverable errors. Skip normal/snapped/full-monitor, shell/security, protected/elevated, owned/tool,
layered/cloaked and nonstandard-region windows. This is bounded region clipping, not a universal
antialiased DWM override. A hung target can delay the child; killing the guard itself loses its
in-memory recovery state. Those limits remain explicit. No elevation, injection, application kill,
registry change or V3 publication is authorized by this implementation choice. S-045 records proof.

Owner correction / S-046, 2026-10-25: visible cutouts must be genuinely transparent. The initial
region-only gate missed a DWM full-window material behind the rounded edge. For an explicitly
selected `MAINWINDOW`, `TRANSIENTWINDOW` or `TABBEDWINDOW` system backdrop, lease `DWMSBT_NONE`
while the region is managed, retaining the exact original enum independently of the region.
Only this supported material attribute changes; non-client rendering, window styles, placement
and application content are preserved. Material removal also affects otherwise-transparent
interior/title-bar areas, so qualify those with real composition content. Read back before clipping,
restore only our current material/region, preserve app replacements, and retain partial-application
recovery snapshots on errors. Unsupported attributes remain unknown; do not invent a policy.
Pixel evidence must compare the real composed cutouts against a changing private backdrop,
not just inspect `GetWindowRgn`. Owner visual acceptance remains separate.

### D39 — One shared active checkout; retain private history as recovery data

Owner-approved organization change, 2026-10-25. Claude and Codex should alternate in one canonical
active project folder, preferably named AetherShell, without creating a new checkout per assistant
or session. This supersedes D18's multiple-folder working arrangement after a verified migration,
but preserves its independent public Git lineage and exclusion of old private history.

Inspect all trees/worktrees and preserve unique commits, staged/unstaged/untracked work and needed
ignored files before moving or retiring anything. Verify local recovery backups; they are not
additional active project folders. Never merge/push private history, assume a public branch/commit
can be private, or use Gitignore to conceal already tracked history. Untested/incomplete work stays
local between assistants. No consolidation push/tag/release is authorized by this instruction.
Record final location/entry-point changes and focused build receipts before declaring completion.
See [the Claude addendum](docs/CLAUDE_SINGLE_CHECKOUT_PROMPT.md). Migration executed by Claude on
2026-10-25 (T45, TEST_STATUS S-040), pending Codex's independent review: the shared folder is the
`AetherShell` checkout with the official origin; D18's separate-folder arrangement is superseded,
its private/public history separation is retained.

## Open questions and implementation discrepancies

These are not approved new requirements or decisions. Keep their IDs stable; disposition is tracked
in PROJECT_SPEC section H and TASKS. No decision here blocks creating the specification.

| ID | Question / discrepancy | Rationale and alternatives | Disposition / approval needed |
|---|---|---|---|
| Q-001 | Product performance budgets not established | Measure startup/CPU/memory/search first; retain search's <2 ms/2,000 apps engineering target, while Debug p95 <20 ms is an example and neither is a universal release budget | proposed measurement in T5g/T14; owner approves release thresholds later |
| Q-002 | Final logo concept/palette not selected | Original identity is required; work-order palette is an example, not proof of owner selection | T5i presents concrete assets/options for owner direction |
| GAP-003 | Status overlapping Start/Stop, callback-side destruction and deliberate process-lifetime MTA keep-alive | Existing source behavior requires lifetime/concurrency restrictions or a fix, and explicit resource-lifetime justification; neither silently changes NFR-005 | resolved in reviewed implementation; TEST_STATUS S-005, broader OS qualification remains |
| GAP-006 | Weather cache can remain displayed beyond 6 h while synchronous HTTP occupies its scheduler | Separate expiry scheduling from blocking fetch, or propose a bounded policy; do not redefine the existing 6 h requirement to fit the code | resolved by separate request worker and independent expiry scheduling;56cases pass, TEST_STATUS S-006 |
| GAP-007 | UI-thread-only settings work order includes disk I/O, contradicting responsive-UI rule | Preserve UI-owned state/notifications; record background persistence ordering and failure semantics before implementation | resolved by ADR0007 + serialized worker persistence; no UI disk exception |
| GAP-008 | Configurable hotkeys in ADR 0003 lack settings fields/editor | Add validated key-combination model/UI, or propose a documented scope change; enabled flag alone is insufficient | chord fields/validation and native editor implemented; final UI regression pending |
