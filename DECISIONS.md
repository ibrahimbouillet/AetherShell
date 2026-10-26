# Decision log

Significant decisions have a full record in `docs/decisions/`. Status: **final** (do not reopen
without new evidence) or **provisional** (a qualification caveat, not automatic rejection of the
chosen design). Product requirements and their IDs: [PROJECT_SPEC.md](PROJECT_SPEC.md).
Approval and implementation verification are separate: ADRs 0002, 0003 and 0005 are accepted choices;
D3/D4/D6 retain their existing provisional qualification status until platform/behavior tests run.

**Current delivery:** D25's temporary hold was explicitly lifted by the owner on 2026-10-22 for the
second public release, **Aether Shell 0.2.0-alpha.2** (D29). That authorization covers the reviewed
`main` changes, tag `v0.2.0-alpha.2`, and its GitHub Release only; do not infer approval for unrelated
repository or account settings. Check HANDOFF for the exact pushed commit and release receipts.

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
