# Test status

Only executed results are listed as passed or failed. Requirements: [PROJECT_SPEC](../../PROJECT_SPEC.md);
backlog: [TASKS](../../TASKS.md). Historical runs below retain their recorded scope; they were not all
independently rerun in this session. Original historical environment: Windows 11 build 26300 x64,
MSVC 14.51, Debug, Intel UHD, 150% scaling, interactive desktop.

## Executed — passed (2026-10-17, commit 158ffe1)
| Executable | Cases | Notes |
|---|---|---|
| `aether_core_tests` | 23 | JSON (incl. malformed/limits), Result, strings, cancellation, worker pool, signals, atomic files |
| `aether_diagnostics_tests` | 1 | filtering, sanitization, rotation |
| `aether_platform_tests` | 8 | dispatcher, message loop, single instance, instance channel, monitors, paths |
| `aether_graphics_tests` | 6 | easing, tween retargeting, spring frame-rate independence, motion policy, tint floor |
| `aether_ui_integration_tests` | 2 | glass probe: blur (dark) and no-blur (light) composed on screen; captures checked visually |

## Executed — passed (2026-10-18, commit af0e20f)
| Executable | Cases | Notes |
|---|---|---|
| `aether_net_tests` / `_integration_tests` / `_network_tests` | 55 total after review | loopback HTTP server (redirects, limits, timeouts, cancellation); real TLS to api.github.com; badssl.com certificate failures rejected |

## Executed — passed (2026-10-18, lead re-verification, fresh build `build/lead-verify`)
| Executable | Result | Notes |
|---|---|---|
| `aether_updates_tests` | passed (117 cases per reviewer) | GitHub/HTTP via fakes; signature fixtures from the release scripts |
| `aether_applications_tests` / `_integration_tests` | passed (72 cases per reviewer) | real discovery, icons and window tracking on this desktop; no launches |

## This session — executed 2026-10-18

Base revision `a9e22cb`, branch `main`, dirty tree with pre-existing untracked service implementations
and this session's focused fixes/tests/docs; no commit was created. Current OS build 26300 / 64-bit
was read directly; MSVC 19.51.36257.0 from configure, Debug, /W4 and /WX ON. These runs cover selected
module tests, not a full project build or full requirement acceptance. Initial private directories
were checked absent; no existing build directory was deleted. CMakeCache and LastTest.log summaries
were inspected by the lead after agent executions; that inspection is not an additional test run.

### S-001 — status (FR-005–FR-008; NFR-005/NFR-008)

Fresh final configuration/build, warning-free; **45 unit + 8 read-only integration cases**, 2/2 CTest
executables passed (13.43 s CTest). Command actually executed:

```powershell
./scripts/build.ps1 -BuildDir build/wf-codex-status-20261018-final -Modules status `
  -Target @('aether_status_tests','aether_status_integration_tests') -Jobs 2 `
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex '^aether_status'
```

Initial private build `build/wf-codex-status-20261018` passed 43 unit + 8 integration cases; after
timeout/debounce validation and test-lifetime/race-assumption fixes, the incremental run passed
45 + 8 before the fresh final run. Root executed all these commands. Real clock, battery/fallback,
keyboard, network and stop/factory checks ran read-only. No SSID/profile-name read, real settings,
registry, audio/work-area change, screen capture or application launch.

Independent source review corrected unsafe test state destruction, unknown battery presence and
cross-sample foreground assertions. **GAP-003 remains:** overlapping Start/Stop deadlock, unsafe
callback-side destruction and process-lifetime MTA ownership; these are not covered by passing
cases and full NFR-005 acceptance is not claimed. See [status reference](../modules/status.md).

### S-002 — audio (FR-009; NFR-004/NFR-005/NFR-008)

Agent-executed private initial /WX build and subsequent reviewed-source rebuilds passed; final
**23 unit + 5 read-only integration cases**, 2/2 CTest executables passed. Lead inspected final
LastTest.log (23/5, zero failures) and cached /WX ON. Initial command:

```powershell
./scripts/build.ps1 -BuildDir build/wf-codex-audio-20261018 -Modules status_audio -Jobs 2 `
  -Test -TestRegex status_audio -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON'
```

After fixes the same build/test command without `-CMakeArgs` rebuilt changed files; /WX remained ON.
Concurrent Stop joining, failed-read field reset/recovery and synthetic exception-privacy regressions
passed. Independent reviewer inspected fixes and logs; no separate second execution is implied.
Read-only real endpoint lifecycle ran; mutating legacy integration tests were removed **before**
any execution. No real mute/volume/gain setters were exercised. Real controls/notification echo,
device unplug/audio-service restart, Windows 10 and UI wiring remain unverified.
See [audio reference](../modules/status_audio.md).

### S-003 — weather (FR-010; NFR-004/NFR-005)

Agent-executed fresh final /WX build, warning-free; **54 unit cases**, 1/1 CTest executable passed
(3.23 s CTest). Lead inspected final test log and /WX cache. Command:

```powershell
./scripts/build.ps1 -BuildDir build/wf-codex-weather-20261018-final -Modules weather -Jobs 2 `
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex weather
```

Initial private baseline `build/wf-codex-weather-20261018` passed 50 cases. Four added regressions
cover freshness at refresh expiry, strict unit metadata, geocoding cancellation precedence and
exception-message privacy. Official API documentation was consulted; **no live forecast/geocoding
request** was issued. HTTP/clock/cache behavior uses fakes/temporary roots.

Independent review found **GAP-006** still open: an already-published cached snapshot can outlive the
6 h display limit while synchronous HTTP blocks scheduling. That missing test/fix prevents complete
FR-010 acceptance. Live transport, location consent/attribution UI and Windows 10/resume remain
unverified. See [weather reference](../modules/weather.md).

### S-004 — specification and continuity (NFR-010, T-DOC-1)

Source/registry/workflow existence, Git branch/revision/no remote and canonical document roles were
inspected. Independent specification review caught and corrected unsupported search UI additions,
font-use wording, historic-evidence semantics and missing weather expiry/geocoding distinctions.
Final documentation checks passed: **23 FR + 11 NFR**, unique definitions and complete task/trace
coverage; **22 documents** checked for local Markdown file targets and whitespace. A targeted
privacy-pattern scan over those documents plus **72 module source/test files** found no personal
absolute paths, e-mail patterns, private-key blocks or common credential shapes. The ignored local
receipt `build/verification-20261018/evidence.json` records scope and source SHA-256 hashes. This is documentation consistency validation,
not a whole-tree privacy/security certification or product acceptance.

Graphify launcher discovery failed with `uv trampoline failed to canonicalize script path`;
`uv tool list` was empty, active Python lacked its module, and the old graph was left stale.
Obsidian 1.14.4 read/search/link checks succeeded for vault `docs` while its app ran. No app settings,
vault configuration or generated export was changed.

## Earlier continuation: failed attempts / findings (superseded below)
- Historical: two FileSystem cases failed then were fixed before their commit.
- This session: one patch write to NetworkProvider.cpp failed during the build (cause not established);
  the two already-applied battery edits were checked, and the remaining patch was applied afterward.
- Weather's first regression build failed because a test ReadFile call omitted its required size
  limit; corrected, then all 54 cases passed in incremental and fresh final builds.
- Final executed suites have zero failing cases. GAP-003 and GAP-006 remain review findings despite
  green scoped suites. Graphify availability failure is a tooling limit, not a product test failure.

## Current continuation evidence (2026-10-18)

The owner resumed product implementation after S-004. All results below refer to the preserved
working tree on base a9e22cb with uncommitted changes, not an already published/released revision.
Source and executed checks take precedence over earlier "not started" notes.

### S-005 — status lifecycle and owned runtime (T5c/GAP-003)
Fresh `build/wf-status-lifecycle2-final`, `/W4 /WX`: **49 unit + 10 read-only integration + 1 isolated
runtime-shutdown cases**, 3/3 CTest pass (18.55 s). Sessions/implementation lifetime supports callback
ownership and concurrent Stop without joining under the host lifecycle lock. Owned MTA runtime
cleanup refuses active scopes and is terminal. Independent root review; generic listener-log privacy
regression. An attempted per-provider MTA teardown crashed during readonly integration and was
rejected; the final owned-runtime design passed its shutdown process test. See ADR 0008/status docs.

### S-006 — independent weather expiry (T5e/GAP-006)
`build/wf-weather-scheduler2`, `/W4 /WX`: **56 unit cases**, 1/1 CTest pass. Separate HTTP worker
keeps six-hour cache expiry publication live while HTTP blocks. A blocked fake proves expiry before
the request is released. Independent review found no actionable issue. No live API request.

### S-007 — settings/search/accessibility/branding (T5f–T5i)
- Settings `build/wf-settings2`: **32 unit cases pass /WX**, serialized worker persistence,
  schema preservation, migration/ranges, failure retry ordering and injected startup registry.
- Search `build/wf-search2`: initial **5 cases pass /WX**, Debug5000-app p50 6.238/p95 8.747 ms.
  Earlier150ms/32ms implementations failed the20ms engineering check and were optimized. Added
  cancellation/closed-dispatcher/2000-app Release cases await the settled full gate.
- Accessibility `build/wf-accessibility2`: **3 unit + 3 native own-window integration cases pass /WX**;
  separate MTA client actions/navigation/focus/disconnect. Independent cross-review completed.
- Branding: original SVG/PNG/wordmarks/banner/installer art; renderer checks **12 raster sizes and
  8 ICO entries**, resources <5 MiB. Contact sheet and banner opened; small-size optical correction
  regenerated and inspected again. Final owner visual selection remains Q-002.

### S-008 — desktop surfaces and transient UI (T6–T8)
- Dock `build/wf-dock2`: initial **14 unit + 2 integration cases pass /WX**, dark/light rest/hover
  captures viewed. Later independent UIA/AppBar fixes, drag interpolation and hit-test assertions
  are in the final rerun. Separate MTA focus test records Windows E_ACCESSDENIED instead of
  pretending OS background focus policy was bypassed.
- TopBar `build/wf-topbar2`: **7 unit + 2 integration cases pass /WX**. Synthetic own-window dark/light
  PNGs opened, material/unchanged-work-area/idle checks passed; real providers sampled read-only.
  Independent review closed wheel accumulation, clock width and AppBar notification issues.
- Search UI `build/wf-search-ui2`: **4 integration cases pass /WX** after debounce/error/scroll/input/
  focus fixes. Both guarded synthetic PNGs opened. Standard EDIT Value/name/focusable properties
  checked from MTA; actual IME composition remains unverified. GPU transitions added afterward
  await full regression. A foreground-policy-dependent fixture failed, then was made deterministic
  without activating a foreign application.
- Settings UI: initial2cases pass /WX in `build/wf-settings-ui2`; delayed startup/reset consent,
  merge/preservation,15modifier choices, live debounce, full MIT and constrained viewport cases
  added. A viewport test caught offscreen-child scrolling; corrected, final5case rerun pending.

Captures stay private in temp until individually inspected and copied to approved project screenshot
assets. A foreign overlap was refused by guards; an early TopBar capture exposed occlusion and was
removed before acceptance. No such image is publishable. The old glass fixture is being hardened
with before/after guards and a blur margin before any serial whole-suite integration run.

### S-009 — installation/application and full matrix (T9–T11)
Fresh `build/wf-install2-final`: **10 unit + 7 integration cases pass /WX**, 2/2 CTest (8.95 s),
launcher and updater compile. Temporary roots/resource-only DLLs/fake processes cover staging,
recovery, health success/rollback, signature refusal before shutdown, root containment/reparse/case
aliases, cancellation and injected registration keys only. Real signed official update remains
unverified. A test linked stale objects during an overlapping edit/build and failed; settled rerun
passed. Production has no unsigned verification bypass.

Setup initial `build/wf-app2`: **3 integration cases pass /WX** (11.73 s), silent install/real payload
hashes/repair/uninstall keep or remove data/malformed CLI. Installed maintenance relocation and own
200% viewport tests are added; final run pending. Test-only wrapper forces no registration/startup/
shortcuts/app launch and injects no-op process operations. A temp maintenance executable may remain,
as allowed by ADR 0005; original exit0 means accepted handoff, child/log owns final outcome.

App target and full wiring compile; own-window lifecycle/health/channel/shutdown test added with
isolated instance name, temp data, no reservation/hotkeys/updates/weather/running-app display.
No real external apps are launched. Execution awaits final build.

Root complete Release `/WX` build in `build/wf-release2` is in progress. Earlier global builds were
intentionally stopped to release source locks during final corrections. The first app link failed
because install target did not yet exist in its CMake cache; reconfiguration resolved that dependency.
18 Python release-script cases pass, including a malformed-key diagnostic-location privacy regression.

### S-010 — publication privacy and delivery (T12–T15)
D18 retains the original development Git history locally. Public export must initialize independent
history with the public GitHub noreply identity; no old author metadata/history is pushed.
Filename/content review currently covers447candidate files: no forbidden private/tool/build/key/log
filenames. Text-pattern hits were synthetic URL userinfo tests or Arrow/Home/End, not personal data.
Final export needs a refreshed audit after source closure.

Binary review found actual private source/PDB paths in older Debug app/setup. `/PDBALTPATH` now embeds
only the filename, and `WINRT_NO_SOURCE_LOCATION` removes C++/WinRT source capture for all targets.
Final Release app/setup/embedded payload must be rebuilt and rescanned. Old binaries must not be
published. No remote/public repository/CI/release has been created yet; those receipts remain pending.

## Still unqualified
Windows10/ARM64, physical monitor/DPI changes, real AppBar/full-screen/Explorer restart, real external
app launch/UWP frame identity, real audio/startup actions, actual IME/screen-reader workflow and live
owner-signed GitHub update/recovery. Global release performance budgets remain Q-001. Module fake/
readonly tests do not establish those product claims.

## Requires an interactive Windows desktop
`integration` label: glass probe, provider integration tests, window tracker, UI Automation tests.
CI runs `unit` only (`ci` test preset excludes `integration` and `network`).


### S-011 — settled native alpha, privacy and delivery (authoritative current continuation)

2026-10-18, Windows 11 x64, preserved development tree; public export starts independent history
under D18. Complete **Release /W4 /WX build passed**, all **426 unit cases in 17 executables passed**.
All **91 integration cases in 14 executables have passing scoped runs** on the settled product
binaries. Aggregate desktop runs were **not uniformly green**: foreign topmost/full-screen activity
triggered safe capture refusals, Search lost activation, and two Top Bar idle fixtures initially
sampled pending render work. The latter now establish bounded observed quiescence; perpetual
rendering still fails. The app fixture accepts suppressed bar visibility only when an actual
external full-screen foreground exists, while requiring both own HWNDs and health/channel/cleanup.
No foreign application was activated and no unsafe crop was saved.

| Module | Unit cases | Integration cases |
|---|---:|---:|
| core / diagnostics / platform | 23 / 1 / 9 | — |
| net | 24 | 28 loopback |
| graphics / accessibility / UI | 6 / 3 / — | — / 3 / 2 |
| status | 49 | 10 read-only + 1 isolated terminal shutdown |
| status_audio | 23 | 5 read-only |
| weather | 56 | — |
| applications | 57 | 15 read-only/fake |
| search / settings / updates | 7 / 33 / 101 | — |
| install | 10 | 7 temporary-root/fake |
| Dock / Top Bar | 14 / 7 | 2 / 2 |
| Search UI / Settings UI | — | 5 / 5 |
| app / setup | 3 / — | 1 / 5 |

Commands executed (full aggregate and then only affected/scoped desktop fixtures):

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-release2 -Jobs 6 `
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestExclude network
ctest --test-dir build/wf-release2 -R '^aether_(ui|dock|search_ui)_integration_tests$' --output-on-failure -j 1
python -m unittest discover -s scripts/release -p 'test_*.py' -q
./scripts/package.ps1 -BuildDir build/wf-release2 -OutputDir dist/review -SkipBuild
```

The first isolated capture rerun passed Glass 2 and Search UI 5; Dock privacy checks refused overlap.
The Dock-only fixture correction expands/repaints a full opaque 128px guard band, checks containment
(including a negative uncovered-band assertion), raises only its own backdrop/Dock without activation
and calls DwmFlush. Its final scoped two cases passed (6.81 s CTest); foreign pre/post guards remain.
A subsequent batch again refused Glass dark and lost Search activation. These environmental failures
remain recorded; earlier passing scopes are not represented as one clean aggregate execution.
Eight redacted metadata-only samples found a real visible, non-iconic, non-cloaked foreign topmost
full-screen window, but were not simultaneous with each refusal and do not prove its precise cause.

Search Release: 5000 synthetic apps p50 **1.409 ms**, p95 **1.962 ms**; 2000 apps p50 **0.512 ms**,
p95 **0.601 ms**. Seven cases include cancellation, failed dispatcher and malformed empty-index query.
Settings 33 includes the independently reviewed settle → freeze → flush shutdown admission fix.
Existing saves/Flush remain durable; late Update/Reset reject, and cancelled quit reenables writes.
App health-before-discovery is ordering review plus normal smoke, not a deliberately blocked-discovery
timing test. App-to-updater duplicate/observer/error paths have source review, not live execution.

**32 Python cases pass, no skips** (independent agent execution and root rerun). Package tests reject
Debug/stale source, ASCII/UTF16 personal/source/machine markers and non-basename CodeView records;
negative cases redact private strings. Actual final Release app/launcher/updater/setup and raw embedded
payload bytes pass the binary privacy scan. The package was generated successfully with real sizes
and hashes. Signing was unavailable; its unsigned manifest stays local and is excluded from assets.

Public candidate audit: **457 files**, no symlinks or forbidden filenames; **313 source headers** have
MIT SPDX and public author attribution. Markdown/requirement consistency checks cover 72 Markdown
files, 123 local targets and all 23 FR + 11 NFR IDs. Only six synthetic privacy/containment/URL fixture
files triggered broad private-pattern candidates; no actionable private content was found. Six
individually inspected synthetic project-window screenshots are included; no other-app captures.

Owner-authorized production delivery was performed separately from automated tests: setup exit 0,
Installed apps entry `Aether Shell` / `0.1.0-alpha.1`, launcher present, startup disabled and no app
launch. The official public repository was created at
[ibrahimbouillet/AetherShell](https://github.com/ibrahimbouillet/AetherShell). Initial clean-history
push, actual GitHub CI and first-release asset receipts are pending at this snapshot and will be
recorded in the delivery follow-up. No original Git history, raw logs or debug artifacts are exported.


### S-012 — public source, green CI and published alpha

Official repository: [ibrahimbouillet/AetherShell](https://github.com/ibrahimbouillet/AetherShell).
Initial clean history contains only two new commits (including a trailing-blank-line fixture fix),
with the owner's public GitHub noreply metadata. Original history/remote-free checkout and owner
canvas stay local. All 457 source candidates byte-matched the reviewed export; no .git, build,
logs/PDBs, local tools/graphs/vault state, keys or owner canvas were copied.

[CI run 39123368917](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39123368917) **succeeded** on
`4eed02879fb79aa60376e9b50c698b295bae163e`: complete Windows x64 Release /WX build, **17 unit executables,
zero failures and zero skips**, package privacy audit and artifact upload. The JUnit report truncates
larger native stdout; the 426-case count in S-011 comes from the local complete logs, rather than a
fabricated CI per-case transcript. Public CI does not run interactive or Internet tests.

[Release v0.1.0-alpha.1](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.1.0-alpha.1) is **published, prerelease, not draft**, targeting that source
revision. Assets verified against the CI download, server SHA-256 metadata and an authenticated
release-download byte round-trip before publication:

| Asset | Bytes | SHA-256 |
|---|---:|---|
| AetherShell-0.1.0-alpha.1-win-x64-setup.exe | 4451840 | `edeafb8c89ca7c833b4b79dfbaee160267fd7278e3e0e91980f31df67fbb481f` |
| AetherShell-0.1.0-alpha.1-checksums.txt | 110 | `3bf07a0cb1d4ad0af86ebbc8274fa37cba40c1a53b6e179680bc16b336d02fd9` |

The actual CI setup and all raw embedded payload bytes passed the privacy audit. Only these two
assets are published: no unsigned manifest/signature placeholder, debug artifacts or raw logs.
No trusted signing environment was configured; this is a manual-install alpha and the mandatory
production updater signature checks are intact. Release notes disclose Authenticode absence and
all remaining qualification from KNOWN_LIMITATIONS.

Owner-authorized delivery was then repaired to this exact CI package: production setup exit 0,
installed maintenance setup hash equals the published CI asset, startup remains disabled, no app
launch. Installed apps entry and Start Menu shortcut were observed. This is a separate delivery
action, not permission for automated tests to mutate real user registration.

The final documentation receipt changes only Markdown and skips an unchanged native CI rebuild;
the release binary remains tied to the exact successful source run above. S-011’s aggregate desktop
failures and wider Windows 10/real-action/IME/screen-reader/signed-update gaps remain open.

### S-013 — V2 continuation, focused evidence; final app/delivery pending

All following checks were executed locally on 2026-10-18. Build cache configuration was inspected;
Debug and Release receipts are distinct. Native tests use synthetic windows/fake callbacks and do
not change the actual taskbar, work area, keyboard layout, audio, startup or native pins.

| Area | Configuration / build tree | Executed result |
|---|---|---|
| Available taskbar pin import | Release /W4 /WX, `build/wf-v2-pins` | 66 unit cases pass, including 9 synthetic `.lnk` import cases; no owner pin directory read by tests |
| Dock placement/reservation model | Debug /W4 /WX, `build/wf-v2-dock` | 19 unit + 2 native cases pass before the later running-icon drag/profile changes; final changed gates pending |
| Glass and Top Bar visuals | Release /W4 /WX, `build/wf-v2-visuals` | 6 graphics + 8 Top Bar unit and 3 glass + 2 Top Bar native cases pass before later action additions |
| Desktop settings editor | Debug /W4 /WX, `build/wf-v2-settings` | 7 native cases pass, 15.14 s; consent, concurrent fake import, retained status and unchanged work area |
| Settings persistence | Release /W4 /WX, `build/wf-v2-settings-roundtrip` | 36 unit cases pass, including 3 new safe-default/roundtrip/invalid-arguments cases |
| Platform lease | Release /W4 /WX, `build/wf-v2-platform` | 15 unit cases pass, including 6 fake lease/recovery precondition cases |
| Async controller | Release /W4 /WX, ignored focused harness `build/wf-v2-controller` | 10 injected-session cases pass; normal app target integration remains a separate gate |
| Release Python tooling | `python -m unittest discover -s scripts/release -p 'test_*.py'` | 32 cases pass, 73.122 s |

Glass paired-background mean RGB changes: dark blur 26.827, light no-blur 18.968, high contrast
0.000. Four updated Dock/Top Bar PNGs were independently opened before replacing public synthetic
assets; no owner screenshot or unrelated app content was copied.

Initial full Release build stopped at a launcher WCHAR-to-char narrowing warning under /WX.
The parser now validates decimal handle digits before explicit conversion. The incremental complete
build in `build/wf-v2-release` is running; final source changes require a final rebuild after agents
close their files, all unit checks and serial relevant native gates. Do not claim that intermediate
successful module linking or tests against an earlier source state verifies the final V2 tree.

Source privacy-pattern review covered 469 candidate files at that point. No actual source root,
personal root or machine-name matches were found. Broad path/key-marker candidates were URLs,
relative build commands, Arrow/Home/End text, synthetic URL/privacy-detector fixtures and detector
constants; these are not real private records. Repeat the final review after all edits. Binary setup
privacy/source-freshness audit, real native taskbar recovery, install, V2 push/CI and release remain
pending; S-012 still describes the only delivered alpha.

### S-014 — independent Claude verification of the uncommitted V2 tree (2026-10-18, about 16:00–16:20 CEST)
- **Revision:** `0893501` plus the uncommitted V2 working tree (60 modified and 12 new files) and Claude's
  documentation edits. Claude changed no product source.
- **Command (the CI configuration):** `./scripts/build.ps1 -Config release -BuildDir build/claude-verify -Fresh
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestExclude 'network|integration'`.
- **Environment:** Windows 11 Pro build 26300 x64, MSVC 14.51, Windows SDK 10.0.26100.
- **Result:** configure and all 260 build steps succeeded with 0 compiler warnings (/W4 /WX); CTest reported
  `100% tests passed out of 17`. Each unit executable was then run directly: core 23, diagnostics 1,
  platform 15, net 24, graphics 6, accessibility 3, status 49, status_audio 23, weather 56, applications 68,
  search 7, settings 36, updates 101, install 10, dock 22, topbar 9, app 15. That is **468 unit cases passed,
  0 failed**.
- **Differences from earlier records:** applications 68 (S-013: 66), dock 22 (19) and topbar 9 (8) come from
  later source in the same tree. The historical "updates 117 cases" (T5a) is not reproduced: the current
  executable has 101 cases.
- **Read-only GitHub checks (`gh`):**
  - The repository `ibrahimbouillet/AetherShell` is public.
  - CI run 39123368917 succeeded on `4eed028` (6 min 17 s).
  - Prerelease `v0.1.0-alpha.1` has two assets: setup 4,451,840 bytes with SHA-256
    `edeafb8c89ca7c833b4b79dfbaee160267fd7278e3e0e91980f31df67fbb481f`, and the checksums file.
  - All three public commits use the GitHub noreply identity.
- **Privacy scan:** `git grep --untracked` over tracked and untracked files for user-profile paths, e-mail
  domains, the machine name and personal names found 0 files.
- **Not run:**
  - integration and network labels (they need a quiet interactive desktop or Internet; S-013 requires GUI
    suites to run one at a time);
  - Python release-tool tests and packaging;
  - any real taskbar or desktop check.

  This run qualifies V2 at unit level only.

### S-015 — T21 taskbar recovery without Aether Shell running (Claude, 2026-10-18, about 16:30–17:00 CEST)
- **Revision:** `0893501` plus the uncommitted V2 tree, plus T21 by Claude:
  - `src/platform/windows/TaskbarReplacement.*` and `TaskbarGuardSession.cpp`;
  - `src/app/TaskbarRecovery.*`, `TaskbarController.*` and `ShellApplication.cpp`;
  - `installer/main.cpp`;
  - new tests in `tests/unit/platform/TaskbarReplacementTests.cpp` and
    `tests/unit/app/TaskbarRecoveryTests.cpp`, and an updated `tests/unit/app/CommandLineTests.cpp`.
- **Command:** `./scripts/build.ps1 -Config release -BuildDir build/claude-verify -CMakeArgs
  '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestExclude 'network|integration'` (incremental on the S-014
  tree).
- **Result:** 0 compiler warnings (/W4 /WX); CTest `100% tests passed out of 17`. Direct runs:
  - platform 24 cases (9 new) and app 18 cases (3 new);
  - every other executable unchanged from S-014;
  - **480 unit cases passed, 0 failed**.
- **New cases:**
  - baseline round trip;
  - malformed, foreign-schema and oversized baselines rejected;
  - restore from baseline, then removal and idempotence;
  - no baseline: only an explicit command shows the taskbar, and auto-hide is untouched;
  - an unreadable baseline shows the taskbar and is removed;
  - failures keep the baseline for a retry;
  - preparation persists before hiding and repairs an earlier crash first;
  - an unwritable location fails before anything is hidden;
  - a relative location is refused before any taskbar is touched;
  - app-level recovery turns replacement off, creates no settings file and leaves settings unchanged
    on failure.

  All of them use a simulated taskbar and temporary folders.
- **Setup (integration, silent cases only):** `aether_installer_integration_tests.exe "setup: isolated"
  "setup: explicit removal" "setup: malformed"` → 3 passed. These cases use temporary roots and
  `--test-no-registration`, with no baseline present. The GUI viewport and maintenance-relocation cases
  were not run.
- **Incident during the first run (fixed):** an existing test,
  `app: recovery commands without a running instance never start desktop surfaces`, ran
  `ShellApplication::Run` with `--restore-taskbar` and empty paths.
  - With the new standalone path, it called the real taskbar backend once to make the Explorer taskbar
    windows visible. They were already visible, so no state change was expected, and auto-hide was
    not touched.
  - It then waited on a result message box. Claude stopped that test process.
  - Fix: the standalone path now refuses a non-absolute configuration folder before touching any
    taskbar or showing UI, and `RecoverTaskbars`/`PrepareTaskbarBaseline` refuse relative baseline
    paths.
  - The test now expects InvalidArgument; a platform case covers the relative-path refusal.
  - No file was written outside the test folders (checked).
- **Not run:**
  - any real-desktop check of hiding and restoring the taskbar (done later, see S-016);
  - integration and network suites other than the three setup cases;
  - packaging.

### S-016 — T21 real-desktop check, owner-authorized (Claude, 2026-10-18, 16:44–16:56 CEST)
- **Authorization:** the owner asked in the session to "briefly hide my taskbar with the V2 build, stop
  it, and verify that --restore-taskbar brings it back".
- **Setup:**
  - Release /WX `AetherShellApp.exe` and `AetherShell.exe` from `build/claude-verify` were copied into one
    temporary folder, as the helper requires.
  - A temporary `--data-dir` held only `replaceWindowsTaskbar=true` and `taskbarPinsImported=true`, so
    the owner's settings and pins were not read or changed.
  - The owner's installed `0.1.0-alpha.1` was closed gracefully through the instance channel and
    restarted afterwards.
  - Taskbar state was read only through Windows APIs (auto-hide flag, `IsWindowVisible` of the Explorer
    taskbar windows, work area). No screenshots were taken.
- **First attempts found a V2 defect.** Replacement never activated; the new log line read "Dock
  reservation failed (InvalidArgument: invalid Dock placement geometry)".
  - Cause: on its first AppBar registration the Dock used its own empty AppBar rectangle as the work
    area, giving a lower edge of 0. The tests inject a synthetic work area, so they never reached this.
  - Fix in `src/dock/DockWindow.cpp`: use the AppBar rectangle only once it has been positioned.
  - Taskbar replacement state changes are now logged in `ShellApplication.cpp`.
- **After the fix (run at 16:54):**
  - **Running:** replacement became active. The taskbar was hidden, the helper process ran, the baseline
    file existed and the Dock reserved its bottom area.
  - **Simulated double failure:** the helper was killed, then the app. The baseline file remained.
    Explorer itself showed its taskbar again within 3 s, once the killed AppBars disappeared, so a
    hidden taskbar after a double failure was **not** reproduced on this machine.
  - **`AetherShellApp.exe --restore-taskbar` with no instance:**
    - It reported "The Windows taskbar was restored to its saved state. Taskbar replacement is turned
      off."
    - It removed the baseline file and set `replaceWindowsTaskbar` to false.
    - The taskbar stayed visible, and the work area returned to its pre-test value.
- **Second finding:**
  - The baseline captured at start recorded the taskbar as *not visible*. This happened in every run:
    the capture coincides with Explorer's re-layout after the Top Bar AppBar registers.
  - Restoring that baseline exactly would have hidden the taskbar. `RestoreTaskbars` now shows every
    Explorer taskbar window and never hides one (D22), and the lease test was updated accordingly.
- **Limits of this check:**
  - The owner's taskbar already used auto-hide before the test, so restoring the auto-hide state could
    not be told apart from leaving it unchanged.
  - The scripted OK click did not close the result message box: `WM_COMMAND IDOK` was posted. The
    process exited after the box was closed some other way (probably by the owner), so its exit code
    was captured only in the no-baseline run (0).
  - Windows 11 only, one monitor; Windows 10 and Explorer restart during replacement were not tested.
- **Afterwards:**
  - Full Release /WX build, 0 warnings, 17/17 unit executables. Platform cases stayed at 24, with the
    lease case updated.
  - The owner's taskbar matched the pre-test reading exactly (auto-hide, visibility, work area with
    0.1 running), and the installed 0.1.0-alpha.1 was running again.
  - The temporary folder was deleted.

### S-017 — V2 gate: integration suites and ShellApplication review (Claude, 2026-10-18, 17:00–17:10 CEST)
- **Revision:** local commit `1180c96` plus one test-fixture change, described below.
- **Setup:** the build was `build/claude-verify` (Release /WX). The owner's installed 0.1.0-alpha.1 was
  closed gracefully for a quieter desktop and restarted afterwards. The owner may have been using the
  PC during the runs.
- **Command:** `ctest --test-dir build/claude-verify -L integration --timeout 900`, which runs the
  suites one at a time.
- **Run 1:** 13 of 14 suites passed. The Top Bar capture case failed in
  `REQUIRE(PrivateCrop(backdrop, guardedArea))`, and failed again when run alone.
  - Cause: the fixture's 128 px guard band reached above the top of the screen and included its corner
    pixel. Explorer keeps four topmost 1x1 helper windows (`ThumbnailDeviceHelperWnd`) there, so the
    guard correctly refused the capture.
  - Fix (test fixture only, guard unchanged): the capture area moved 40 px down so the guard band stays
    below the screen edge. After that the Top Bar suite passed 3/3.
- **Run 2 (all 14 again):** 11 of 14 suites passed.
  - The Dock and Top Bar guards refused captures because a foreign window appeared over the guarded
    area, possibly the auto-hide taskbar sliding up at the bottom of the screen.
  - A search UI case lost keyboard focus.
  - Run individually right afterwards, Dock 2/2, Top Bar 3/3 and Search UI 5/5 passed.
- **Cases per suite** (each passed in at least one run of the current code):
  net 28, accessibility 3, ui 3, status 10, status runtime shutdown 1, status_audio 5, applications 15,
  install 7, dock 2, topbar 3, search_ui 5, settings_ui 7, app 1, installer 5. That is **95 integration
  cases**.
  - A combined run with all 14 suites green has still **not** been achieved. On an active desktop,
    foreign windows and focus changes interrupt the GUI suites; the guards were never weakened.
- **ShellApplication review (V2 step 2), reading the taskbar callback, `ApplySettings`,
  `FailTaskbarReservation`, the poll timer and `ImportTaskbarPins`:**
  - The reservation is kept while replacement is still requested or active.
  - A failed reservation is reset only when the setting is turned off and on again.
  - The poll timer keeps running until restoration is confirmed.
  - Imported pins are saved through `settings->Update` with a completion status. `taskbarPinsImported`
    is set only after a successful read, so a failed read stays retryable.
  - Pin identities are deduplicated without regard to case, including duplicates within one import.
  - **Minor finding, not fixed:** after the controller's activation attempts are exhausted, the 1 s poll
    timer keeps waking until the option is turned off. This costs a little idle wake-up time, not
    correctness.
