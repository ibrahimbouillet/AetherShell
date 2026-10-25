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

### S-018 — V2 package build and privacy audit (Claude, 2026-10-18, about 17:05–17:15 CEST)
- **Revision:** clean tree at local commit `7f5f463` (0.2.0-alpha.1 candidate). Nothing was published.
- **Command:** `./scripts/package.ps1`, the default fresh Release /WX build in `build/package`, run with
  `AETHER_RELEASE_SIGNING_KEY_FILE` cleared.
- **Audit (`scripts/release/audit_package.py`):** "Package privacy audit passed (setup and uncompressed
  embedded payload bytes)".
  - It found no source-root, home-folder or `\Users\` paths, no absolute source paths, no machine
    name, no private-key material and no CodeView paths with folders.
  - The freshness check also passed: no source input is newer than the package.
- **Output in `dist/`** (git-ignored, local only):
  - `AetherShell-0.2.0-alpha.1-win-x64-setup.exe`, 4,617,216 bytes, SHA-256
    `96deda1068037856cc305dac1d5b064fb3edb9dcc490ff94862a404afcaa2887`;
  - an **unsigned** manifest (channel prerelease, one asset), which must stay local;
  - the checksums file.
- **Independent second scan (Claude, categories and counts only):**
  - No Gmail address, no owner account e-mail, no e-mail-shaped strings at all.
  - No user-profile, Documents or temp paths, and none of the private terms supplied through the
    environment (account name, machine name).
  - 4 PE images: setup, app, launcher and updater.
  - 4 CodeView records, all bare file names (`AetherShellSetup.pdb`, `AetherShellApp.pdb`,
    `AetherShell.pdb`, `AetherShellUpdater.pdb`).
- **Not done:**
  - installing this package;
  - signing the manifest, which needs the owner's key;
  - publishing anything.

### S-019 — push and Windows CI on `9a2b1bb` (Claude, 2026-10-18, 17:14–17:30 CEST)
- **Push (owner-approved; no tag, no release):** the five local commits `1180c96..9a2b1bb` were pushed
  to `origin/main` as a fast-forward from `0893501`.
  - Before pushing: a fetch, noreply author and committer on every commit, and a privacy scan of the
    pushed changes.
- **CI run 39139547133:** **failed** (6 min 52 s). The Release /WX build succeeded; 16 of 17 unit
  executables passed.
  - `aether_applications_tests`: 66 passed, 2 failed, both in V2 `TaskbarPinsTests`:
    "local shortcuts survive…" and "catalog reconciliation reuses identity…".
- **Root cause:** the GitHub runner's temp folder is reported in 8.3 short form (`RUNNER~1`), while the
  shell resolves shortcut targets to long names.
  - Tests compared raw path strings, so the expectations did not match.
  - The product's `CatalogMatch` also compared executable paths as plain strings, so a pin lost its
    catalog identity when the two forms differed. That is a real, if rare, defect.
  - Reproduced on the development PC by pointing `TMP`/`TEMP` at a short-form folder: the same two
    cases failed.
- **Fix:**
  - `SameExecutable` in `src/applications/ShellApplicationCatalog.cpp` compares
    `MakeIdForExecutable` ids, which expand 8.3 names, when the raw strings differ.
  - The two tests compare executable identities instead of raw strings.
- **Re-verification:** `aether_applications_tests` 68/68 with both the normal and the short-form temp
  folder. The full Release /WX build (0 warnings) passed 17/17 unit executables with the short-form
  temp folder.
- **State:** the fix was pushed afterwards and CI passed; see S-020.

### S-020 — push and Windows CI on `e29e83e` (Claude, 2026-10-18, 20:50–21:00 CEST)
- **Push (owner-approved; no tag, no release):** the CI fix commit `e29e83e` was pushed to
  `origin/main` as a fast-forward from `9a2b1bb`. Local `main` and `origin/main` are in sync.
- **CI run [39156124337](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39156124337):
  passed** (8 min 7 s) on `e29e83e3b3bac9ee7dcbb3f2b9602418ba792ed5`.
  - Release build and 17/17 unit executables passed ("100% tests passed out of 17"), including
    `aether_applications_tests`, which failed on the runner in S-019.
  - The workflow uploaded its usual review artifacts (`unit-test-results` and the
    `AetherShell-win-x64-alpha` manual-install package). These are CI artifacts, not a release.
- **Not covered by CI:** integration suites, the real desktop, Windows 10 and Explorer restart.

### S-021 — real-desktop replacement checks: no auto-hide and Explorer restart (Claude, 2026-10-18, 21:21–21:23 CEST)
- **Authorization:** the owner allowed the Explorer restart test in the session ("tu peux le faire") and
  had turned taskbar auto-hide off so that case could be tested. The owner also dropped Windows 10
  qualification: no Windows 10 machine is available.
- **Setup:** the same method as S-016.
  - Release /WX `AetherShellApp.exe` and `AetherShell.exe` from `build/claude-verify` (source `e29e83e`,
    unchanged since) were copied into one temporary folder.
  - A temporary `--data-dir` held only `replaceWindowsTaskbar=true` and `taskbarPinsImported=true`.
  - The installed `0.1.0-alpha.1` was closed through the instance channel and restarted afterwards.
  - State was read through Windows APIs only; no screenshots.
- **A first attempt stopped early because of a test-script error.** A PowerShell function returned its
  log line together with the process. The tool's process cleanup then ended the app.
  - Even so, Windows showed the taskbar again on its own. Auto-hide was back off and the work area was
    restored; the baseline had already been removed.
  - The script was fixed and rerun from a fresh temporary folder.
- **Phase A, taskbar without auto-hide:**
  - **Before:** auto-hide off, taskbar visible, work area height 672 (logical pixels of the script
    process).
  - **Running:** about 4 s after start, the taskbar was hidden, the helper ran, the baseline was saved,
    and the Top Bar and Dock reserved their areas (work area 0,42–1280,606).
    - While replacement is active the taskbar's auto-hide flag reads on: replacement uses it to hide
      the taskbar.
  - **After `--quit`:** the app and helper exited and the baseline was removed. Auto-hide was off again,
    the taskbar visible, and the work area restored exactly.
- **Phase B, Explorer restart during replacement:**
  - `explorer.exe` was force-stopped; Windows restarted it by itself within 1 s.
  - The new Explorer showed its taskbar for about 5–7 s; the app and helper stayed alive. The log shows
    "Explorer taskbar was (re)created", then "Reconnecting taskbar replacement...".
  - About 8 s after the restart, the taskbar was hidden again and the reservation re-applied (work area
    0,42–1280,606). It stayed that way until 30 s.
  - **After `--quit`:** everything was restored as in phase A, and the safety `--restore-taskbar` step
    was not needed.
- **Limits:**
  - Windows 11 only, one monitor.
  - The Explorer restart leaves the native taskbar visible for several seconds before it is re-hidden.
  - File Explorer windows that were open closed with the Explorer process (expected for an Explorer
    restart).

### S-022 — interrupted V2 integration recovery (Codex, 2026-10-20–21)
- Source baseline: unchanged public `d5ccbd6`. Candidate: pending merge of `5cf6e72`, `1ef9d72`
  and `4b7c8a8`, plus preserved visual working files and new integration/review fixes. The original
  branch worktrees and public `main` remain unchanged.
- Fresh baseline command: `./scripts/build.ps1 -Config release -BuildDir build/wf-codex-baseline
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestExclude 'network|integration' -Jobs 4`.
  Full Release /W4 /WX build passed. Unit result: **17/18 executables passed; 526 cases passed,
  one failed**. Search's 2,000-application p95 was 4.781 ms (budget <2 ms), while concurrent
  compilation was running; 5,000-application p95 was 4.026 ms. This failure is retained, not a pass.
  Once all compilers stopped, a quiet targeted rerun passed **7/7 Search cases**: 2,000-app p95
  **0.227 ms**, 5,000-app p95 **0.491 ms** (0.36 s CTest). The performance budget was not relaxed.
  The initial failure remains recorded above; this is a scoped rerun, not a new whole-baseline run.
- Candidate applications: fresh focused Release /W4 /WX build and **120/120 unit cases passed**
  (`build/wf-codex-parity`, 6.04 s). Includes unknown explicit profile launch rejection, 8.3 identity,
  worker-only pin planning and ancestor-junction icon rejection with isolated temporary fixtures.
- Candidate graphics/UI: fresh focused Release /W4 /WX build and **64 graphics + 19 UI unit cases
  passed** (`build/wf-codex-visuals`, 2/2 executables, 1.08 s). Initial UI and Search UI native runs
  crashed in CompositionScopedBatch `auto_revoke` registration: the object did not provide the weak
  reference required by that path. An explicit strong batch/event-token subscription plus lifetime,
  cancellation and callback guards fixes the reproduced crash. Fresh **12/12 UI native cases pass**,
  including completion/reset/detach/destruction/throwing-callback regression and guarded RGB probes
  (dark 17.872, light 18.613, high contrast 0). Search UI initially passed 9/10: a fixed 700 ms wait
  checked spring idle prematurely. The final **10/10 pass** (7.52 s) waits for explicit layout
  completion under a 2 s overall deadline, retaining the original <=2 idle-frame assertion.
  Final UI run: 12/12, 13.03 s. Final headless repeat: graphics64/UI19, 0.74 s. Root independently
  opened all five synthetic PNGs (Search dark/light and glass dark/light/high contrast); only own
  fixture/backdrop pixels are visible. These do not establish full-app desktop or actual avatar use.
- Final frozen whole-candidate **Release /W4 /WX build passed**, including app, launcher, updater,
  setup and all test executables. Earlier intermediate links lacked newly added/changed Top Bar
  symbols while source was still being written; reconfiguration and the final frozen rebuild resolve
  them. No warnings remained.
- Combined unit gate: `ctest --test-dir build/wf-codex-integrated -L unit --output-on-failure
  --output-junit evidence/unit.xml -j 1`: **18/18 executables, 669/669 cases passed**, 15.18 s.
  Includes applications121, Dock31, Top Bar20 and app34. With compilers idle, 2,000-app p95 was
  **0.231 ms**, 5,000-app p95 **0.435 ms**. Original timing budget unchanged. Raw unit evidence is
  retained under the ignored candidate build directory before the native gate overwrites LastTest.
- Combined native gate: `ctest --test-dir build/wf-codex-integrated -L integration -LE network
  --output-on-failure --output-junit <build>/evidence/integration.xml -j 1`:
  **14/14 executables, 122/122 cases passed**, 79.94 s. Scoped counts: net28, accessibility3,
  UI12, status10 + runtime shutdown1, audio5, applications20, install7, Dock3, Top Bar4, Search UI10,
  Settings UI10, app2, setup7. Own-window/fake/read-only isolation was preserved; no live Internet
  suite or actual shell/audio/startup/dictation action was invoked. Unit and native raw logs/JUnit
  remain in the ignored build evidence directory.
- Six public screenshots were refreshed from the newly executed, independently opened synthetic
  captures. The frozen setup was relinked after those asset changes; product source/payload behavior
  did not change. Package command: `./scripts/package.ps1 -BuildDir build/wf-codex-integrated
  -OutputDir dist -SkipBuild`, signing variable cleared in that process. Final binary/payload privacy
  and freshness audit passed. Setup: **4,957,184 bytes**, SHA-256
  `4e95a10a749792d90457c020029b321fbc7f4b8fefd970abb0a3e1e0fb985f6c`.
  Setup/checksums are local review assets; the unsigned manifest remains local. No V2 tag, install
  or release publication is implied.
- Release scripts: **32/32 Python unittest cases passed**, 23.779 s. Initial current-source privacy
  scan covered 536 files; five pattern hits were inspected as synthetic URL/path rejection fixtures,
  not private data. Environment-specific path/machine scan found zero. All 17 unpublished commit
  identities use GitHub noreply addresses; 165 unpublished history blobs/commit messages scanned
  with zero environment-specific/secret-pattern findings. Repeat the final diff/assets audit before
  push; these checks do not validate unfinished source or a stale package.
- No real taskbar/work-area/audio/keyboard/startup setting was changed, no browser profile was read,
  no Explorer restart/install/publication was performed in this continuation. Native fixtures use
  only their own windows/fakes; captures require an opaque test backdrop and foreign-window guards.

- Delivery: integration merge **b87be0d68961acecc3777f07a0198c80b28f2912** was fast-forwarded into
  clean public `main` and pushed as `8e11a68..b87be0d` (2026-10-21). Original Claude worktrees remain
  untouched; no pending merge remains. Final pre-push staged audit covered 538 files, zero findings,
  with configured public noreply identity.
- After commit, a fresh Release /WX reconfigure/rebuild embeds `b87be0d68961`; product source is the
  same tested candidate. Focused app/setup confirmation passes **3/3 executables**: app34 unit,
  app2 native and setup7 native (43 cases, 9.41 s). Final setup above was repackaged/audited from
  that revision; the earlier precommit local asset is superseded. Unsigned manifest remains local.
- Windows CI [39505236903](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39505236903)
  passed on b87be0d (10m33s), including Release/unit gate and review package upload. No V2 release is claimed.

### S-023 — owner-authorized desktop gate and fullscreen correction (Codex, 2026-10-21)
- Explicit current-session owner authorization: check native taskbar replacement/restoration and
  return to the initial state. No prior Aether instance existed. Private temporary binaries/data,
  with no personal settings mutation; bounded cleanup supervisor and normal CLI quit.
- Initial/fully restored: native taskbar visible, auto-hide true, physical work area [0,0,1920,1080].
  Candidate b87be0d active: native taskbar hidden, physical work area [0,93,1920,969]. Both Aether
  surfaces could be hidden over a normal framed foreground window covering the monitor. A second
  explicit Normal launch reproduced the issue, removing the first Hidden startup confound. Both
  runs exited, removed their recovery baseline and restored the exact initial native state.
- No screenshot was captured by the test; the owner's supplied real-desktop screenshot stays private.
  Restricted fixture deliberately disabled running apps/pin import/Explorer default, leaving three
  fixed Dock tiles. The owner requested an explicit normal Explorer/running-app check next.
- Cross-reviewed uncommitted correction: style-aware platform predicate; Top Bar ABN reevaluation;
  Dock host-owned suppression; bool-only transition log. Three pure platform cases and one native
  Top Bar case added; Dock assertions expanded. Full Release /W4 /WX rebuild passes with zero warnings.
- Full serial unit gate: **18/18 executables, 672/672 cases pass** (30.16 s).
  Serial non-network integration gate: **14/14 executables, 123/123 cases pass** (127.00 s).
  Commands: `ctest --test-dir build/wf-codex-integrated -L unit --output-on-failure -j1` and
  `ctest --test-dir build/wf-codex-integrated -L integration -LE network --output-on-failure -j1`,
  each with a distinct `--output-junit` file. Full logs/JUnit remain in ignored
  `build/wf-codex-integrated/evidence/fullscreen-{unit,integration}.{log,xml}`. Platform34 unit,
  Dock31 unit/3 native, Top Bar20 unit/5 native. No actual taskbar/work-area/input/audio mutation.
- A subsequent Dock lifecycle conformance change forwards registered activation/position messages,
  preserving default processing and guarding recursive shell notifications. Release /WX passes;
  targeted Dock31 unit and app2 native pass. Dock native initially failed its light fixture's UIA
  root-type assertion at stage 3 (search Invoke consequently never ran), then the unchanged-binary
  serial rerun passed all three cases (8.28 s). Root cause is unproven. Both
  `appbar-targeted-failed.log` and `appbar-dock-recheck.log` remain ignored local evidence. No waits,
  retries or assertion relaxations were added to the fixture; privacy-safe numeric diagnostics follow.
- Bounded corrected desktop test (App SHA-256
  `524afc253c84cb28e1523e15b531850f2a80ebf6defa5166748bf2ece221d0bb`, uncommitted tree):
  Normal launch, no earlier Aether; initial pin import/Explorer initialization and running apps enabled
  in private synthetic settings, no owner settings mutation. Native hidden, both own surfaces visible,
  work area [0,93,1920,969]. An owned standard framed HWND maximized before activation received two
  `WM_SETTINGCHANGE` messages and reflowed its visible DWM frame exactly to the work area; client
  bounds [0,127,1920,969] fit inside it. Normal CLI quit restored native visible/auto-hide true/work
  area [0,0,1920,1080], helper exit, removed baseline and own-window reflow to full monitor.
- Own Settings accessibility and private-data boolean/count checks confirm five pins including
  Explorer. Actual running-icon pixels and Chrome profile completeness remain unverified. Settings
  was readable but UI clicking failed without screenshot geometry; no bypass or desktop capture was
  used. Temporary reveal/resume, crash/Explorer restart and physical compatibility remain separate.

### S-024 — owner-requested Dock icon balance (Codex, 2026-10-21)
- Owner requested balanced Start/Search/Task View and application icon dimensions. The Dock now uses
  the full existing animated icon slot for fixed glyphs, removing duplicate 18-percent side insets;
  shared glyph geometry already has optical margins. Layout, hit targets and Top Bar are unchanged.
- Added numeric UIA root-type/HRESULT/own-HWND-match diagnostics after S-023's intermittent assertion;
  no assertion weakened, no retry/sleep added. Full incremental Release /W4 /WX rebuild passes with
  zero warnings. Targeted Dock31 unit and app2 native cases pass; Dock native is **2 pass / 1 fail**
  (14.31 s aggregate three-executable command). Light root-type check again reports Pane **50033**,
  type/native-handle HRESULT **0**, and matching requested own HWND. Full stdout/JUnit remain in
  ignored `evidence/optical-targeted.{log,xml}`. No green native gate is claimed for these later edits.
- Opened and inspected both generated own-window PNGs: dark rest and light hover show the enlarged
  Search symbol balanced against synthetic application artwork. The guarded images contain only the
  test Dock/private pattern and were copied to the two existing `resources/screenshots/dock-*` assets.
  Start/Task View use the same full animated box; independent source review confirms all their shared
  paths/strokes stay inside it. No owner screenshot was copied or captured.
- Independent source review found a shared accessibility teardown hazard: synchronous `WM_CLOSE`
  reaches `UiaDisconnectProvider` while a send is active, and existing code ignores its HRESULT.
  [Microsoft forbids that call context](https://learn.microsoft.com/en-us/windows/win32/api/uiautomationcoreapi/nf-uiautomationcoreapi-uiadisconnectprovider).
  Actual teardown HRESULTs and its causal relationship to the Pane fallback have not been established.
  Do not weaken root-type/count/action/stale-provider assertions. A bounded own-provider experiment
  will evaluate early reply versus deferred cleanup before choosing a production correction.
- The earlier manual binary hash does not cover these drawing/diagnostic edits. D25 now holds all
  push/tag/release operations; local commits remain allowed. No new V2 release or installation.

### S-025 — incomplete UIA metadata/disconnect investigation (2026-10-21)
- Local-only checkpoints: `6f63ef3` records D25/evidence, `9bec731` commits the S-023/S-024
  fullscreen/AppBar/optical work and strict diagnostics. Both were privacy-reviewed and fast-forwarded
  into local `main`; `origin/main` remains b87be0d. No push/tag/release occurred after D25.
- Frozen **uncommitted** integration files: `src/ui/accessibility/AccessibleSurface.cpp` and
  `tests/integration/accessibility/AccessibleSurfaceTests.cpp`. Immutable provider capability metadata
  now returns `S_OK` after closure; actual properties/actions still require a connected owner-thread
  model. New regression checks owner/MTA metadata, null output, no model access and strict stale calls.
- Focused Release `/W4 /WX` build passes. Final native command, after removing the experimental TU
  from the normal source glob:
  `ctest --test-dir build/wf-codex-integrated -R '^aether_accessibility_integration_tests$'
  --timeout 30 --output-on-failure -j1` (with `--output-junit evidence/accessibility-metadata-final.xml`).
  **3 pass / 1 fail**, 0.52 s CTest elapsed: new metadata, invalid-model and removed-reference cases
  pass; the existing MTA client fails before invoke/toggle/focus events. Fresh filtered execution
  excluding the experiment also failed. This is not a verified correction or proof of Pane causation.
- Experimental raw-node comparison initially failed; metadata-only rerun still failed. In an active
  synchronous send the repeated disconnect returned `0x8001010D`; after early reply it returned
  `0x80040201`, as did later recovery. Because wrapper invalidation precedes the measured repeated
  call, this cannot establish first-call cleanup success or justify a production early-reply change.
  Assertions were not relaxed. The own never-shown HWND had bounded send/process cleanup and no
  screenshot/input/settings mutation; the sender is joined before its receiver can be destroyed.
- The inconclusive TU is preserved in ignored `build/manual/AccessibleDisconnectTests.experimental.cpp`,
  outside the default suite. Full negative logs remain in ignored `evidence/disconnect-experiment.log`,
  `accessibility-metadata.log`, `accessibility-metadata-without-experiment.log` and
  `accessibility-metadata-final.{log,xml}`. The uncommitted metadata patch is also copied under ignored
  `build/manual/`. No experimental source or raw log was committed.
- Next: measure the first cleanup HRESULT/send flags with reentrant provider exposure withheld, and
  the first failing MTA/client/provider stage using numeric/boolean-only diagnostics. Do not infer
  a causal fix from constant metadata or a passing visual capture. Rebuild the chosen source/revision
  before packaging; current cached investigation binaries and the historical b87 setup are not Alpha 2.

### S-026 — grouped controls/neutral glass work in progress (2026-10-21)
- D26 scope is approved; D25 publication hold unchanged. A copied prior qualified local preview is
  active by explicit owner request. It is paused only for tests and restored afterward; no startup.
- New system_controls and media_controls libraries: Release /W4 /WX build succeeds. Initial module
  filter with comma failed configuration; correct semicolon-separated filter then passed. No actual
  media/brightness/radio discovery or mutation run for these backend checks.
- Graphics: focused Release /WX build and headless68/68 pass. Initial symmetry fixture failed on
  rounded-corner raster coverage (4–6/255), then samples were corrected to central straight material/
  shadow bands with unchanged2/255 tolerance and a directional Popup positive control. Logs retained
  in ignored build/wf-control-visuals. No native blur or new surface visual approval implied.
- App integration, new native controls/RangeValue and side work-area leases remain under development.
  S-025 MTA UIA discovery failure remains unresolved; no broad passing suite or release qualification claimed.

- New safe backend unit gate: private Release /WX build and 2/2 suites pass (system_controls10,
  media_controls5). Accessibility native executable compiled only. Cases use pure mappings, invalid
  arguments/deadlines and pre-cancelled early exits; no valid discovery/mutation path was exercised.
- First full app link failed because WorkAreaInsets.cpp was created after the configure/source glob.
  Reconfigure includes it and app links successfully; full/incremental final build is still running.
  New controlcenter native fixture compiles Release /WX (4 fake-only cases); execution pending.
- Independent review fixed radio split UIA hit bounds, monitor selection/DPI closure, keyboard range
  accumulation/focus events, close/reopen action cancellation/generation fencing, radio consent
  deferral/permission scoping and shutdown admission. Real controls remain unexercised.

- Frozen aggregate unit gate:20/20 executables pass (14.75s). Their exact test registrations total
  698 cases, independently enumerated with each executable's --list because CTest truncates passed
  JUnit output. New safe provider suites are included. No fabricated count from truncated XML.
- Native fake/read-only gate: accessibility6/6 and controlcenter4/4 pass (1.72s combined); subsequent
  scoped surface gate Dock3/TopBar5/app2 passes (17.67s). One passing accessibility run does not
  establish a causal fix for the earlier intermittent S-025 failure. Tests preserve strict UIA checks.
- Copied product candidate ran in a bounded real desktop check using the owner's private preview
  data, without device-control actions. At150-percent DPI, work area became [18,93,1902,969]; own
  pre-maximized test HWND's DWM frame followed exactly without forced resizing. Normal candidate
  quit restored work area [0,0,1920,1080], native taskbar visible/auto-hide true and no own bar HWNDs.
  The prior preview was restored in finally. No screenshots, owner-window manipulation or Explorer
  restart was performed. Logs stay ignored under build/manual.
- Own-backdrop panel dark/light PNGs and native Dock/TopBar material captures were inspected.
  A translucent radial overlay above the opaque slider track appears at different positions across
  full/isolated render runs. No matching radial painter is in the call graph; native feedback is a
  hypothesis, not a proven cause. Do not claim this artifact resolved or broad visual approval.
- A reviewer caught the Top Bar discarding all Shell-approved horizontal exclusions. That override
  was removed entirely to preserve foreign AppBars; current build passes /WX and a final bounded
  side-inset check is being repeated. Primary-monitor baseline behavior is distinct from untested
  foreign AppBar configurations and physical multi-monitor/DPI changes.

- Final Top Bar correction rebuilt warning-free;22 unit/5 native cases pass (7.67s). Repeated bounded
  own-window desktop gate still aligns [18,93,1902,969] and fully restores [0,0,1920,1080] plus visible
  native taskbar/auto-hide true. Current copied controls preview is then activated by owner request,
  with the existing private data and no autorun. Two runtime binaries pass environment/privacy audit.
  Independent final source review reports no additional blocker after the Shell-allocation correction.
- Source audit:50 changed public files;0 new private/credential findings. Two legacy documentation
  files contain14 generic path-pattern lines unchanged from HEAD (not introduced private data).
 15 Markdown files/80 relative links/442 requirement references checked;0 missing/undefined targets.
  No push/tag/release, new package/install, actual device-control test or broad visual approval claimed.

- Source milestone d7e17c4 is committed locally and fast-forwarded into clean local main. Active
  preview source matches this checkpoint but embeds parent revision2dabf2700610 (compiled before
  commit); it is a private local review binary, not a current release package. Rebuild committed
  source and requalify packaging before eventual owner-approved delivery. Publication remains held.
- Final staged byte audit flagged14 unchanged legacy lines: relative ./scripts commands and literal
  keyboard-navigation descriptions. Each was inspected as a generic-pattern false positive;0 private or
  newly introduced findings remain. App/launcher binaries separately pass the unmodified strict audit.

### S-027 — D27/D28 build and focused surface qualification (Codex, 2026-10-22)

The tested source tree was based on `wip/d27-d28` at `73900bc`, including the preceding D27/D28
implementation commits; the S-027 source/test checkpoint was committed locally as `4b8d000`, with a
documentation-only repository-state follow-up afterward. This turn added a stale-consent generation
fence to `ShellApplication`, made the Weather settings disclosure and module references match the
actual permission/cache behavior, and corrected two integration expectations that still described
the removed taskbar arrow and pre-D27 Dock drag timing. No product requirement or visual direction
changed.

Full Release build of all configured modules/targets passed with `/W4 /WX` and zero warnings:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-codex-d27d28 -Modules all -Jobs 6 `
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON'
```

The final complete unit gate passed **21/21 CTest executables** (26.21 s total):

```powershell
ctest --test-dir build/wf-codex-d27d28 -L unit --output-on-failure
```

The focused native integration gate passed **6/6 executables** (65.16 s total), covering application
discovery, captured Dock drag/neighbour settling, Top Bar menus/status, Settings UI, Control Center,
and app attention/lifecycle (68.26 s total):

```powershell
ctest --test-dir build/wf-codex-d27d28 -R `
  'aether_(dock|topbar|controlcenter|settings_ui|applications|app)_integration_tests' `
  --output-on-failure
```

The first aggregate native run had two expected failures in stale checks: the Dock fixture demanded
that a single synthetic pointer message expose the crossed slot immediately after release, while the
separate captured-input test already verifies continuous drag, neighbor reaction and settling; the
Top Bar fixture clicked the removed `NotificationArea` arrow instead of testing recovery through the
logo menu. Those fixtures now check settled Dock geometry and arrow-free menu recovery. The focused
Dock/Top Bar rerun passed 2/2, then the full six-executable gate passed 6/6. These fixture
expectations were updated; the dedicated captured-input Dock case still checks live movement and
settling, and the new Top Bar check confirms recovery remains available from the menu.

Two earlier whole-unit reruns intermittently failed only the Search microbenchmark's single-sample
`times[33] < 2.0 ms` check; an isolated Search rerun passed, and the final complete 21-executable
unit run passed without changing its threshold. Keep this timing flake visible if it recurs; do not
reinterpret it as a product Search defect without repeated measured evidence.

The installed `0.2.0-alpha.1` process was paused for bounded tests and restored using its original
binary and data directory; its executable path/arguments were checked after relaunch. After tests,
the Release candidate was started directly from the worktree and verified visible with an isolated
local profile; its copy has weather disabled and Windows location consent false. The installed files
and profile were not overwritten. No location permission prompt/read, live forecast request,
audio/radio/brightness/media action, Explorer restart, real settings mutation or owner-window capture
occurred.
Physical DPI/multi-monitor, actual shell attention/fullscreen behavior, Windows location consent,
live weather, reduced-motion visual quality, and taskbar/Explorer recovery still need owner/device
acceptance. This run is test evidence, not release acceptance; D25 still forbids package, push, tag,
and GitHub Release.

### S-029 — cinematic showcase camera and clean-session coordinator (Codex, 2026-10-22)

Focused Windows Release build with warnings as errors and the showcase unit suite:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-showcase-codex -Modules showcase -Jobs 4 `
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex '^aether_showcase_tests$'
```

The build passed `/W4 /WX`; CTest passed the focused executable **1/1**, and its `--list` output
confirmed **13 cases** (7 deterministic camera tests, 6 fake clean-session tests). Coverage includes
the stable composition hold, cursor path, Dock framing and tracking, VS Code launch/reveal gates,
resolution scaling, invalid input, filtering/deduplication of app candidates, all-app restart
preflight before closure, orderly close with authorized termination fallback, rollback after a close
failure, sanitized relaunch errors and destructor restoration. The first run found one mistaken test
fixture with a valid process creation time; the fixture was corrected and the same gate passed.

Follow-up on the same date, against HEAD `322594b1b3486f319c9bf7144bd683d7890e44b5` with a dirty
worktree (the D29 Alpha 2 candidate is staged; T36 is local and uncommitted): the default camera
profile was changed to quintic smootherstep, which settles position, velocity and acceleration at
each endpoint. The same Release `/WX` build command passed again after adding finite-difference speed
and acceleration checks at choreography boundaries and the observed-window reveal, a custom cubic
art-direction override with zero-velocity endpoint validation, and bounded 8-sample shutter timestamp
planning. One intermediate test run rejected the custom-curve endpoint check at a 1-ms finite-difference
step; tightening the measurement interval to 0.1 ms confirmed the endpoints are at rest, and the final
rerun passed. CTest passed **1/1**, and the executable listed **18 cases** (**12 camera/timeline, 6 fake
clean-session**). No real application process or desktop was touched. `git diff --check` also passed.
This still does not verify rendered motion, native capture, output cadence, motion blur, MP4/GIF export
or the desktop.

This is a focused static library and fake-backend suite, not a runnable showcase application. No real
process/window backend, screen capture, native high-resolution Dock renderer, MP4/GIF encoder or
preview exists. No user process was closed and no desktop capture was made. The complete Alpha 2
build/release gate and the real desktop showcase are not covered by this result.

### S-030 — reference frame compositor and whole-film motion acceptance (Codex, 2026-10-22)

The user supplied a motion-design standard, which was read in full, and raised the target from the
generic 92-point recommendation to 100/100 for the complete final film. D32 records that stricter
owner decision. The public project-specific rubric adapts all 60 binary checks and ten blocking gates
in `video_production/06_SCORECARD.md`; no score may be claimed from source tests or a still frame.

The T36 CPU reference compositor now consumes frames from an injectable time-indexed source, evaluates
the camera at each shutter timestamp, resamples RGBA pixels with Catmull-Rom cubic reconstruction,
blends temporal samples in premultiplied-alpha space, and reports sample count plus measured CPU time.
The desktop base pose now scales the full source into the output composition. Four fake-source tests
cover exact identity pixels and destination padding, eight-sample temporal averaging, desktop fit at a
different output size, and malformed source/output rejection. No test touches real apps or the desktop.

Focused verification used the S-029 Release `/W4 /WX` command and build directory. The suite passed
CTest **1/1**; the executable lists **22 cases** (12 camera/timeline, 6 fake clean-session, 4 fake
compositor). Two intermediate attempts exposed an unnecessary identity blur in the first resampler
choice and a mistaken out-of-bounds test index; both were corrected, and the final rerun passed. This
is correctness evidence only: no benchmark, real capture provider, GPU path, MP4/GIF writer, visual
preview, or measured 60-FPS output exists. The whole-film score remains unscored, with all checks and
gates pending because there is no export.

### S-031 — H.264 writer smoke test and compositor checkpoint (Codex, 2026-10-22)

The existing T36 timeline, fake clean-session coordinator, and CPU reference compositor remain
unchanged in intent. A Windows Media Foundation H.264 sink writer was added to the showcase module.
It validates dimensions/rate/bitrate and RGBA frame memory, refuses an existing final path, encodes to
a unique temporary sibling, and publishes the completed output without replacement. Temporary output
is removed on failure or empty finalization. The new runtime test writes three synthetic 64×64 color
frames, verifies the resulting file is nonempty and that Media Foundation's Source Reader opens its
H.264 stream and observes three samples, then deletes the temporary file. A collision control confirms
existing sentinel bytes remain unchanged. This is container/stream smoke evidence only; it does not
decode and visually inspect the frames or represent Aether product footage.

Focused verification used the S-029 Release `/W4 /WX` workflow and build directory:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-showcase-codex -Modules showcase -Jobs 4 `
  -CMakeArgs '-DAETHER_WARNINGS_AS_ERRORS=ON' -Test -TestRegex '^aether_showcase_tests$'
```

The final build passed and CTest passed **1/1** in 2.72 seconds. The test executable lists **26 cases**:
12 timeline/camera, six fake-session, six compositor, and two MP4-writer cases. An earlier version
failed to compile because an inline default constructor instantiated deletion of the writer's
incomplete implementation type; the constructor was moved out of line. A first Source Reader check
also requested decoded RGB32, which failed on this host; the final test instead checks the actual
native H.264 stream and sample count and makes no decoded-image claim.

No desktop capture provider, native process/window backend, capture orchestration, live Dock render,
real app launch/observation, GIF, audio, measured frame cadence, or reviewable project video exists.
No user process was closed; no user-facing video was created. All 60 checks and ten gates in the
project scorecard remain pending; no whole-film score is available.

### S-032 — native showcase implementation and rejected-export audit (Codex, 2026-10-23)

This supersedes S-029–S-031's library-only continuation boundary. The native runner now closes and
attempts to reopen owner-authorized user applications, protects controller ancestry and Windows
shell/security processes, isolates Code's profile, captures authentic DXGI pixels/cursor, buffers
lossless changed tiles in bounded RAM, and saves a validated numeric recipe for offline rendering.
The GPU compositor evaluates eight shutter-time camera poses and reuses immutable source frames.
Media Foundation writes interleaved H.264/AAC; WIC produces the looping GIF. Automated checks use
synthetic images/fake lifecycle operations or read-only process checks, never real app closure.

Canonical focused command:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-showcase-runner -Modules showcase -Jobs 4 `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_BUILD_SHOWCASE_RUNNER=ON') `
  -Test -TestRegex '^aether_showcase_tests$'
```

The WIC correction checkpoint passed **64 cases**, **1/1 CTest executable**, in **2.34 seconds**,
without warnings under Release `/W4 /WX`. Tests include lossless tile round trips/capacity failures,
archive validation/reopening, frame-ID cache reuse, real native H.264/AAC decoding, an NV12 top/bottom
orientation and padded-row regression, palette/delay/loop/color checks, and an end-to-end RGBA resize
to GIF decode. Later cursor/framing and visual-close refinements require the next exact receipt.

Executed real takes exposed defects which unit/container checks alone had missed:

| Take | Observed result | Disposition / correction |
|---|---|---|
| Initial GDI take | Approximately 17.94 capture samples/s; black scenic backdrop | Rejected; no public use |
| Early DXGI take | 12.44 samples/s; 5.95-second gap; unrelated private content in its final source frame | Rejected locally; before/after frame scene guards added; no public use |
| Buffered take | Backdrop restored; 34.63 samples/s, no >100-ms gaps; cursor missing, MP4 inverted, GIF channels swapped | Rejected; explicit actual Windows cursor composition, native RGB32 bottom-up encoding and explicit WIC RGBA conversion added |
| First exact-sequence take | Real pointer and genuine empty Code workbench; technically correct master, but test-setup toast, clipped close-up footer, persistent hover ending and about 2.85 visually empty seconds | Rejected for polish; fresh empty extensions directory without a disabling flag, padded framing, cursor return, click-position preservation and photographed-backdrop closure check added |

The last audited intermediate MP4 was
`Aether-Shell-Showcase-AABA77A7EC324CF18729CAAD9414E0D1.mp4`, SHA-256
`9c8410a30883a30773d3d1244d1134ff5ce7caf19e180b89d676f6d4513acd2f`.
All **964** H.264 frames decoded; delivery was **1920×1080, 60 FPS, 16.0667 seconds**, progressive
SDR BT.709, with AAC-LC stereo 48 kHz. All PTS intervals matched 1/60 second within 10 microseconds.
Its **931** source samples averaged **57.95/s**; the archive's 95th-percentile interval was **31.35
ms**, maximum **118.20 ms**. During Dock traversal the 95th percentile was **27.36 ms**, maximum
**45.88 ms**. These are sampling intervals, not proof that every sample contains a new desktop image:
the separate DXGI receipt counted **243** desktop presents and **309** acquisitions, with cached
images retained for genuine static holds. The lossless capture buffer used about **144 MB**.

Audio measurements on that exact intermediate were **−22.6 LUFS integrated**, **−10.5 dBFS true
peak**, zero full-scale/non-finite decoded samples. The corrected GIF had **322 frames**, **960×540**,
50-ms delays and an infinite loop. Independent frame reviews covered all raw and exported frames
through contact sheets plus full-resolution critical images; only the permitted desktop/Aether and
fresh empty Code workbench were observed in that inspection scope. This is not a normal-speed
playback/listening claim or a guarantee against every possible privacy issue.

D33 now requires two seconds on the empty scenic desktop with a visible real pointer, curved left
approach, full Dock traversal/zoom, one-second Code focus, real click and loaded-workbench readiness,
three seconds total after readiness, actual close, then three **visually** empty seconds. Restoration
must follow capture completion. The closing event now waits for the Code window to be absent and
two consecutive captured backdrop matches, accounting for the DWM fade. Every replacement export
must be audited again; intermediate measurements cannot be transferred to a new hash.

The owner reported a black desktop during work. The referenced wallpaper source was unavailable;
the existing cached wallpaper was reapplied, and the owner confirmed restoration. No color/security
setting was intentionally changed. Raw footage, process state and local QC media were not committed
or published. A final accepted export, full playback/listening, all 60 checks and ten gates remain
separate from these source and technical receipts. No 100/100 score is claimed.

### S-033 — complete genuine export, technical QC and scoped image review (Codex, 2026-10-23)

This advances S-032's rejected/intermediate boundary. The latest native run completed capture,
reported successful relaunch attempts, flushed its lossless buffer and exported MP4/GIF. Capture
is no longer running. Source snapshot: local `codex/t36-showcase-checkpoint`, commit
`bf853e23d60df4ac190c01f4e0ac6d565e92d3ad`. The checked-out `main` remains
`322594b1b3486f319c9bf7144bd683d7890e44b5`; the original mixed index was preserved byte-for-byte
through a separate-index recovery commit. Later documentation-only checkpoint commits may advance
that local branch. No push, tag, release, installed payload replacement or autorun was performed.

**Executed source gates:**

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-showcase-runner -Modules showcase -Jobs 4 `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_BUILD_SHOWCASE_RUNNER=ON') `
  -Test -TestRegex '^aether_showcase_tests$'
./build/wf-showcase-runner/modules/showcase/aether_showcase_integration_tests.exe
```

Final focused gate: **66 passed, zero failed**, 2,113 ms inside the test harness, 2.15 s CTest
executable time and 2.17 s aggregate CTest time, with zero build warnings under Release `/W4 /WX`.
The own-window integration executable passed **two cases**, zero failed, in **105 ms**: opaque
RGBA capture of its requested private test-pattern window and invalid-bound rejection. These tests
did not close user apps, launch a scenario app or capture the owner's desktop. The separate real
showcase run was explicitly owner-authorized, not an integration-test action.

**Exact local artifacts:**

| Artifact | Receipt |
|---|---|
| MP4 | `Aether-Shell-Showcase-C5FF64A3E03848EFA7FCD18A0F236221.mp4`; 51,139,749 bytes; SHA-256 `6839bf39c247896265d31cc37278e3eb16fa5a06c2eb6e6a8e55cb189c2cf7a2` |
| Local delivery copy | `Aether-Shell-Showcase-1080p60.mp4`; byte-identical SHA-256 checked after copying; outside public Git |
| GIF | Same candidate stem; 98,379,380 bytes; SHA-256 `7e90507c33f1d266b987e8fc3449fff315a488276bd1d65c5eab9a46162dd143` |
| Source/recipe | Take `D6B936B041634529BFF9B521281DDF85`; 985 samples; numeric `AETHER_RECIPE_2`; raw and metadata remain local |

All **1,030 MP4 frames** decode without errors. Delivery is **1920×1080**, H.264 progressive
limited-range SDR BT.709, **60 FPS**; all PTS intervals are 16.666–16.667 ms, within 10 µs of the
target cadence. Last picture PTS is 17.150 s; frame-count duration is 17.166667 s. No >99%-black
frame was detected. Two exact adjacent duplicates at 0.650/0.667 s occur in the deliberate opening
hold. Maximum adjacent global mean-luma change is 0.0408914; none exceeds 0.05. This aggregate
measurement is not a complete temporal photosensitivity/comfort assessment.

The **985 source samples** average **57.3721/s**. Independent interval analysis gives P95
**29.91659 ms**, **106** intervals above 25 ms, **10** above 50 ms and **one** above 100 ms,
maximum **108.0744 ms**. The native percentile implementation reports 29.973 ms; both results
are retained with their methods. Native counters are **236 desktop presents**, **316 DXGI
acquisitions**, 147,157,168 committed / 147,153,152 stored buffer bytes. These are distinct
populations: cached static images are genuine but are not new UI updates. Offline shutter-time
camera composition produces regular output PTS, not constant-60-FPS source capture. Offline render
cost was 19.7638-ms mean, 31.903-ms P95, 61.9417% above 16.667 ms on the hardware path; this
wall-clock cost is not playback jitter or a real-time composition pass.

Audio is original procedural AAC-LC, 48-kHz stereo, 192 kb/s. Complete PCM and loudness decode
passed: **−22.6 LUFS integrated**, **1.3 LU loudness range**, **−10.5 dBFS true peak** as printed
by FFmpeg's true-peak analysis, **zero clipped/non-finite samples**. No private application or
microphone sound was recorded. Audio has been measured, not listened to or accepted for sync/comfort.
The GIF decodes as **344 frames**, **960×540**, **50 ms** per frame, infinite loop, **17.2 s**.
Selected decoded GIF keys have correct orientation/channel ordering. Its 98.38-MB weight needs
optimization before README embedding.

**Image/privacy scope:** two independent reviewers covered all 985 raw and all 1,030 exported
frames through contact sheets, plus full-resolution critical images/cursor alerts. Root also
inspected the 7-s focus/footer, 13-s clean Code and final empty frame at full resolution. Only the
permitted scenic wallpaper, genuine Aether surfaces/pins, actual system pointer and isolated empty
Code workbench were observed. No unrelated window, personal path/account/document, native taskbar,
extensions toast or premature restoration appeared within that scope. The footer fits, the click
has no recenter jump and the pointer returns to its neutral start with no ending hover/tooltip.
Contact coverage is not every-pixel full-resolution inspection or an absolute privacy guarantee.
The genuine Code background settles to a darker shade near 12.37 s and remains stable; controls,
logo and pointer stay intact. Review found no blocking export fault from that native appearance change.

**D33 events:** loaded workbench at **10.7441257 s**; close requested three seconds later;
visually clear event at **14.1604877 s**; configured ending at **17.1604877 s**. Independent
source analysis finds **3.0238984 s** continuously matching the final empty central region
(`x=300:1600, y=100:900`, every fourth pixel), including the final sample at 17.1687406 s.
A second check using the scene's opening body/backdrop samples finds 3.0082529 s clear after the
recorded event. Neither is equality of all full-frame pixels. Restoration occurs after capture,
before archive flush/render; successful relaunch attempts do not prove recovery of unsaved work.

The normal usable Alpha 2 preview was restored with a fresh private clone of the prior normal
profile: original Code launch route, replacement enabled, startup off. Original installed/prior
preview settings hashes are unchanged. Read-only native probes confirm a live preview, visible
preview windows, hidden native taskbar and zero capture workers. Four specifically rejected raw
spools were removed locally, reclaiming 14,681,102,304 bytes; their local QC receipts remain. Current
source, recipes and final media are retained privately. No captured media was committed/published.

**Not yet verified:** normal-speed complete playback and listening, moving-detail/comfort/audio-sync
acceptance, all 60 scorecard checks and ten gates, optimized web GIF, owner media selection and any
Alpha 2 package/CI/publication gates. Therefore **no 100/100 score or complete cinematic acceptance
is claimed**. See the [correction log](../../video_production/05_CORRECTIONS.md),
[scorecard](../../video_production/06_SCORECARD.md), [QC report](../../video_production/07_QC_REPORT.md)
and [asset register](../../video_production/03_ASSET_REGISTER.md). Every changed export needs its
own exact-file review; the above receipts cannot be transferred to new bytes.

### S-034 — installed lifecycle and fresh versioned gallery continuation (Codex, 2026-10-23)

Scope: FR-016/FR-017/FR-020/FR-024/FR-029/FR-030; D29/D34/D35; T37/T38. The owner renewed
Alpha 2 installation, sign-in startup and publication authorization, prepared the desktop/pins,
and requested new full-screen media. Historical T36 film acceptance remains separate and unverified.

**Implemented and focused-tested:**

- Lifecycle policy, owned startup removal, stable launcher supervision, cancellation and bounded
  maintenance ordering: Release `/W4 /WX`, settings54 + install16 cases. Independent rerun of
  `ctest --test-dir build/wf-alpha2-lifecycle --output-on-failure --no-tests=error -R '^aether_(install|settings)_tests$'`
  passes 2/2 executables, 70 cases, 0 failed, 2.36 seconds. Fake registry/child policy and isolated
  kernel objects only; no real startup, app crash or installation was exercised by those tests.
- Release tooling: 33 Python tests pass. A stale same-version manifest signature is removed before
  replacing manifest bytes, so unsigned/failing-sign operations cannot retain the old signature.
- Reusable capture scenarios: 80 focused cases pass in `build/wf-alpha2-media`, Release `/W4 /WX`,
  CTest 7.22 seconds. Full gallery is 24.5 seconds: controls pages, fixed Explorer Search/context,
  Top Bar menu and eight Settings pages. Acquisition is independent of synchronous UIA calls;
  incomplete actions or insufficient settled still intervals reject the take.
- Independent review identified and corrected media-presence ambiguity, early volume-cleanup
  disarming, queued writes retargeting an audio endpoint, dynamic anchor PID adoption and planned
  rather than observed still timestamps. Volume writes now bind to the retained endpoint and
  observe the real UI slider; restoration requires stable precise-scalar readback. The session
  control explicitly exposes active/no-media state regardless of disabled transport capabilities.

**Fresh full gate in progress:** `build/alpha2-release`, Release `/W4 /WX`, all modules. An initial
build failed on a UIA `SetValue` string-literal/BSTR type mismatch while the scenario extension was
being completed; the source was corrected and the build restarted. A started/restarted build is
not a passing receipt. Final all-module, native integration, installed lifecycle, capture/privacy,
exact-source package/CI and publication results must be appended below as they are executed.

At this checkpoint no new real startup mutation, installation, capture or Alpha 2 publication has
been performed. Current settings were privately backed up without modifying the active profile.
No media-quality score, reboot/sign-in timing or complete platform qualification is claimed.

**Fresh full source gate:** the restarted all-module Release `/W4 /WX` build completed with exit0.
After the final scenario changes, the incremental canonical command below rebuilt current inputs
and passed **22/22 unit executables, 812 cases, 0 failed, 47.94 seconds**. Full raw case counts were
read from the retained local CTest log; JUnit truncates long test output and is not a case-count source.

```powershell
./scripts/build.ps1 -Config release -BuildDir build/alpha2-release -Jobs 2 `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestExclude 'network|integration' -JUnitFile build/alpha2-release/unit-results.xml
```

This worktree still has base revision `322594b1b3486f319c9bf7144bd683d7890e44b5`; candidate commit
and exact-HEAD package/CI receipts are pending. Sixteen isolated/read-only native integration
executables are running separately. No passing integration receipt is implied until they finish.

**First native full gate:** `ctest --test-dir build/alpha2-release --output-on-failure --no-tests=error -L integration -LE network --output-junit integration-results.xml`
completed in 157.02 seconds: **15/16 executables passed; Dock failed**. The captured-drag/attention
case's final unchanged-attention assertion observed `RenderedFrames`185 versus182 after2700ms;
the other three Dock cases passed. All remaining native executables passed, including the new
single-disabled-media-session presence regression. The failure is retained locally in
`build/alpha2-release/native-first-ctest.log`; investigation is pending. Do not describe the full
native gate or Alpha 2 publication qualification as passing until resolved or explicitly bounded.

**Dock follow-up:** the unchanged binary passed the isolated attention case, then4/4 full Dock
cases in16.444s. Source inspection shows unchanged presentation snapshots return before invalidation;
the original three extra frames remain unattributed. The test now requires two consecutive150ms
idle intervals within a bounded1.2s settling loop after its existing2700ms wait, then retains its
strict180ms unchanged-snapshot no-new-frames assertion. Continuous rendering still fails the idle
precondition. No Dock production source changed. Root rebuilt the target under the same Release
`/W4 /WX` cache and ran the corrected executable: **4/4 cases pass, CTest18.10s**. Together with the
15 unchanged passing native executables this covers136 native cases; it is not a retroactive green
result for the first full invocation (135 passed,1 failed).

**Independent public audit:**614 current UTF-8 text files plus156 changed historical text blobs and
nine outgoing commit messages:0 real private-data findings. All37 reachable commits use the public
owner identity/noreply; fixture matches are deliberate synthetic values. Existing binary assets and
new media need separate inspection. Raw setup+embedded payload byte privacy/freshness audit passes:

```powershell
python ./scripts/release/audit_package.py build/alpha2-release/modules/installer/AetherShellSetup.exe `
  --source-root . --check-freshness --version 0.2.0-alpha.2
```

Final capture runner rebuilt with frozen original-process/anchor checks:80/80 focused cases pass,
CTest9.27s; SHA-256 `46ffb1785cfb3cde3dc22d4be5044c8c580d619f22b32f7e4e29e44e531932ec`.
No real capture or new installed startup mutation has occurred at this point.

**Source continuity and real preparation:** checkpoint
`b9b3f3de9657776127e73faec6c66336c220f06b` is committed and pushed to the official public `main`.
[Windows CI 39964752759](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39964752759)
passed for that exact revision. No Alpha 2 tag or release exists yet.

The old preview was stopped through its Quit command. A fresh private capture profile preserves
the five genuine pins; background pin synchronization is now explicitly disabled only in the clone.
Executed preparation checks confirm source settings hash/pin JSON unchanged, startup false,
replacement true and five pins. A loose build-directory preview initially lacked its colocated
recovery launcher; adding the matching built helper restores the intended installed-layout contract.
Short-lived local command hosts did not retain the GUI child after returning. A retained preview
host is now live; a subsequent read-only preflight passes with its native taskbar hidden.
This is not installed sign-in or crash-supervision proof.

One real attempt was interrupted by the owner; later attempts were rejected by the initial
scene guard before acquisition. **No usable new gallery media exists.** Local attribute-only
preflight identifies `UIHubMouseHiderWindowClass`, owned by the Windows Explorer shell:5×5px,
successful `LWA_ALPHA` query, alpha1/255. Region query returns ERROR, which does not establish an
empty region. This is nonzero opacity. An independently reviewed narrow compatibility rule is
being implemented, preserving all other scene guards; no image editing or Explorer mutation.
The owner now requires D36 readiness coordination before every further assistant-operated real
capture. Do not infer readiness from earlier blanket approval, the elapsed time or read-only probes.

**Narrow helper compatibility and capture follow-up:** six pure snapshot tests cover retained shell
identity, bounded positive geometry with overflow protection, DWM-frame containment, known exact
styles, no class drop shadow, alpha-only opacity at most 1/255, and a one-helper limit. Release
`/W4 /WX` passes **86/86 cases**, CTest 3.99 seconds (total 4.04 seconds). Diagnostic runner SHA-256:
`9577804a7914618fc222314eea6af292664c225f612ee671f2692a3aee90a375`.
Root's read-only preflight passed; fresh local attributes match the policy, including known class
style `0x3`. This is a measured undocumented compatibility allowance, not an invisibility claim.

After the owner explicitly answered ready, one real take passed the initial clean-scene guard but
stopped immediately at the acquisition guard (`an unexpected visible surface interrupted presentation
capture`). No usable export was produced. The current guard error combines native-taskbar visibility
and scene checks; fail-only local diagnostics are being added to distinguish the next observation.
The preview remains live. No further app closure or recording is authorized before a new D36 reply.

**Offline review helper:** `tools/showcase/Review-Presentation.py` passes 12 synthetic checks,
including complete-frame contacts/PTS/hash/luma evidence, variable frame spacing, selected full-size
stills, rejected public output paths and preservation of existing output directories. Python
compilation and whitespace checks pass; synthetic contact sheets were inspected. This does not
establish review of actual owner media, source acquisition cadence, normal-speed playback or audio.

**D37 source follow-up:** the full canonical Release `/W4 /WX` build passes. All 22 unit
executables pass in 32.72 seconds. The app integration target was rebuilt after the final test-only
cleanup and passes in 3.74 seconds; its three new own-window audio-observation cases use synthetic
snapshots and real own-window UIA without any actual device changes. Independent source review found
no major audio-bridge/lifetime defect. The review helper now passes 16 synthetic checks, including
default/explicit junction containment and forced local MOV/MP4 decoding with a protocol whitelist.

The latest owner-ready capture passed the scene check but failed volume acknowledgement. Source
inspection identified the missing live audio-to-panel bridge; it is now corrected and covered above.
That failed take had no numeric restoration receipt, so exact scalar restoration is not retroactively
proved. New best-effort private receipts separately record acknowledgement and stable restoration.
They never publish device identifiers, and invalid read samples are represented as unknown.
The owner explicitly prioritizes Alpha 2 publication before optional new media (D37). No successful
new gallery, normal-speed video acceptance or 100/100 score is claimed.

**Alpha 2 delivery — executed:** source `ad77019efbaed5aa59f96494da9e086e9006a4c2` is pushed;
[Windows CI 39971203823](https://github.com/ibrahimbouillet/AetherShell/actions/runs/39971203823)
passes for that exact revision. A metadata-only Release `/W4 /WX` setup rebuild embeds
`ad77019efbae` / `0.2.0-alpha.2`. Packaging succeeds with:

```powershell
./scripts/package.ps1 -BuildDir build/alpha2-release -OutputDir dist/alpha2-ad77019 -SkipBuild
```

The signing-key environment variable was removed only from the temporary packaging process; no
signing key was read. Setup/embedded-payload privacy and source freshness pass. Only these two assets
were uploaded; GitHub's reported digests match the local SHA-256 values:

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| AetherShell-0.2.0-alpha.2-win-x64-setup.exe | 5268992 | `bdaa27f67f3e3caa215f05cb47dceffe95275255a157376670f21e490bbc82ba` |
| AetherShell-0.2.0-alpha.2-checksums.txt | 110 | `5ea049bfa70551b177d248118ffc6ebf57a74a56c171b5785bc847ffb3e70a26` |

Actual setup ran with `--silent --start-with-windows --no-launch`, exit 0. The install journal
committed the upgrade from `0.2.0-alpha.1` to `0.2.0-alpha.2`. Current settings/pins were privately
backed up and migrated; startup/replacement are true. HKCU Run matches the stable installed launcher
with `--autostart`; no StartupApproved disable value was present or overwritten. The native app
launcher reported no targetable window because the shell uses tool surfaces; process/path/hash and
hidden-taskbar preflight independently confirm the installed binary is running. Its bytes match
the exact build. This is not reboot/sign-in timing proof.

One retained installed child was deliberately terminated with `0xC0000005`; a new installed child
was observed after 1143 ms. This is process creation latency, not visual-ready latency or proof of
every crash path. Stable-launcher `--quit` then stayed stopped and a read-only native-taskbar check
observed Windows' taskbar visible. The installed shell was relaunched independently and the scene
preflight passed. Local evidence stays in ignored installed-alpha2/recovery/quit receipts.

The subsequent owner-ready no-volume gallery failed with `a real Aether presentation control is
unavailable`, HRESULT `0x80070005`. Its exact action is unproven; no usable fresh media or volume
mutation occurred in that scenario. Further takes need another D36 ready reply. T38 remains open.

[Alpha 2 prerelease](https://github.com/ibrahimbouillet/AetherShell/releases/tag/v0.2.0-alpha.2)
was published at **2026-10-23 14:47:05 UTC**. Read-only release/tag checks confirm draft false,
prerelease true, the exact two assets above, and tag `v0.2.0-alpha.2` at `ad77019efbaed5aa59f96494da9e086e9006a4c2`.
No unsigned manifest, raw capture, private receipt, or later version was published.

### S-035 — V3 requirements and initial native input-region correction (Codex, 2026-10-23)

Scope: owner acceptance of Alpha 2 and D38's next development track, FR-031–FR-036 / T39–T44.
The full owner request is captured in the English V3 work order, authoritative requirements, tasks
and continuation prompt. No V3 version/tag/release has been approved or produced.

**Inspected:** Dock and Top Bar supplied shadow-expanded native window regions while returning
HTTRANSPARENT outside their narrower input rectangles. Microsoft's same-thread forwarding contract
does not guarantee delivery to another process. The initial source correction clears these visual
overrides, leaving panel/current animated-icon rectangles as the native HWND region. This deliberately
clips outer shadows/entrance decoration. It does not change work-area reservation, default size,
saved owner settings or the installed Alpha 2.

**Executed focused gate:**

```powershell
./scripts/build.ps1 -Config release -BuildDir build/alpha2-release -Jobs 2 `
  -Target @('aether_dock_integration_tests','aether_topbar_integration_tests') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^(aether_dock|aether_topbar)_integration_tests$' -TestExclude '' `
  -JUnitFile build/alpha2-release/v3-input-region-results.xml
```

Warning-free build passes; **2/2 executables pass, total 23.07 seconds** (Dock 15.07 s,
Top Bar 7.66 s). Added own-window GetWindowRgn/PtInRegion checks exclude points in former shadow
padding while including panel interior. Existing hover/drag/attention and Top Bar native cases run
in these executables. No input is injected by the added assertions; no real device/startup/work-area
mutation or owner-window control occurs. Own-pattern test captures remain local.

**Still unverified:** actual separate-process input receipt, transparent rounded-corner pixels,
physical visual quality of clipped shadows/entrance, and a complete DPI matrix. No installed V3
preview or owner acceptance is claimed. Compact defaults, AppBar policy, native Quick Settings,
signal-only refresh, icons, indicators, hover previews/media actions and external-window motion/
corners remain requested/unimplemented. The existing Windows signal reading and route source were
inspected; neither is a new real-device qualification. No current screenshot/video was recorded.

### S-036 — compact Dock and native Quick Settings request (Codex, 2026-10-23)

Scope: D38, FR-031/FR-035/FR-036, T40/T41/T44; source base `54c825f` plus the following patch.
The owner explicitly requested continued implementation and coherent commits before interruption.

**Implemented:** Dock top interior padding is 9 instead of 12 DIPs. The default visible panel height
is 67 instead of 70 DIPs: 3 physical pixels less at 96 DPI, about 4–5 at 144 DPI, 6 at 192 DPI. Baseline,
bottom gap, icon/hit geometry, saved sizes and magnification are retained; no settings migration.

The production grouped status click now requests Windows 11 Quick Settings with one Win+A chord
when native access and the saved flyout preference permit it. Unknown/older Windows, opt-out and
isolated tests keep Aether controls. The owned custom panel closes first. A request is not an
acknowledgement; no uncertain retry or second popup follows. The Top Bar settings page now exposes
the existing preference as **Use Windows Quick Settings (Windows 11)**.

**Executed Release `/W4 /WX` checks:**

```powershell
./scripts/build.ps1 -Config release -BuildDir build/alpha2-release -Jobs 2 `
  -Target @('aether_dock_tests','aether_applications_tests','aether_app_integration_tests','AetherShellApp','AetherShell') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^(aether_dock_tests|aether_applications_tests|aether_app_integration_tests)$' `
  -TestExclude '' -JUnitFile build/alpha2-release/v3-compact-native-controls-results.xml

./scripts/build.ps1 -Config release -BuildDir build/alpha2-release -Jobs 2 `
  -Target @('aether_settings_ui_integration_tests','AetherShellApp') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^aether_settings_ui_integration_tests$' -TestExclude '' `
  -JUnitFile build/alpha2-release/v3-quick-preference-results.xml
```

Both warning-free builds pass. First gate: **3/3 executables pass, 5.99 s** (applications 1.73 s,
Dock 0.09 s, app native 3.86 s). Second gate: **1/1 passes, 5.83 s** (settings native 5.71 s).
Added tests cover 96/144/192-DPI/custom-size geometry, unknown/old-OS rejection before input,
single Win+A ordering, partial-input key-up recovery, and the real own-window checkbox's saved opt-out
through a private temporary settings file. All keyboard injection is fake; no real Windows panel,
audio/radio/device, startup entry or owner application is modified. Independent source review found
no major request-gating/lifetime issue.

**Corners investigated, not fixed:** WorkAreaInsets reserves primary-monitor side bands; it does
not alter third-party frames or maximized state. Microsoft excludes native maximized/snapped
windows from automatic rounding. The [owned-window prototype plan](../specs/window-corner-investigation.md)
rejects a one-line DWM hint or silently restoring foreign windows as a universal fix. No corner
attribute, region or placement of another application was changed.

**Still open:** physical compact-Dock/shadow acceptance; native Quick Settings opening/position with
the taskbar hidden and UIPI filtering; complete signal freshness/icon redesign; rounded maximize
prototype and external compatibility. The installed/published Alpha 2 is unchanged and remains usable.
These are unreleased V3 source changes; the local executable still carries the existing Alpha 2
base version, not a newly approved V3 release. No real capture or V3 publication occurred.

### S-037 — continuous running-marker width (Codex, 2026-10-25)

Scope: D38 / FR-033 / T42, source base `d9095f5` plus the Dock patch. The owner's existing demo
commit is preserved; this gate neither produced nor accepted any presentation media.

**Implemented:** stable case-normalized application IDs retain a native width spring. An observed
running background marker starts at 5 DIPs, targets 12 while hovered or genuinely foreground, and
returns to 5 on leave when background. Hover never changes activation/active color. Retargeting
preserves width and velocity; the center follows existing animated icon/reorder geometry. Closed
pins have no marker; closure removes one immediately. Reduced motion/high contrast snap, and
settled widths stop frame demand. Overflow/removal discards state; reappearance starts a new dot.

**Executed focused Release `/W4 /WX` gate:**

```powershell
./scripts/build.ps1 -Config release -BuildDir build/alpha2-release -Jobs 2 `
  -Target @('aether_dock_tests','aether_dock_integration_tests','AetherShellApp') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^aether_dock(_integration)?_tests$' -TestExclude '' `
  -JUnitFile build/alpha2-release/v3-indicator-results.xml
```

Warning-free build passes; **2/2 executables pass, 15.32 s** (unit 0.09 s; native 14.84 s).
**43 unit + 4 native cases** pass. Five new deterministic cases cover closed/inconsistent state,
hover/foreground/leave targets, interrupted reversal momentum and observed closure, instant policy
changes, repeated observations, finite settlement and equal elapsed time at different step cadences.
Existing own-window native Dock cases run, including idle/drag checks. Independent source review
found no major defect; the new tests do not independently measure rendered native marker pixels.

The unwired corner prototype was present during the app compilation; its final source and dedicated
checks are a separate T44 gate. No owner window, real keyboard/device/startup/work-area setting or
installed Alpha 2 is changed by the marker tests. Own-window synthetic captures remain local.
Actual frame cadence, physical motion/visual acceptance, full layout-origin continuity, live previews
and external-window choreography remain open. No V3 release, installation or real recording occurred.

### S-038 — unwired maximized-corner prototype (Codex, 2026-10-25)

Scope: D38 / FR-036 / T44, continued from `d9095f5` with the tested T42 patch. The final source
includes the reentrancy correction described below. No SettingsWindow production caller is added.

**Implemented experimental helper:** physical native/DWM-visible/work-area intersection and
window-local DPI-scaled radius, rejecting invalid/overflow/empty bounds. Attach requires own PID,
owning UI thread and a top-level standard left-to-right resizable HWND. Apply requires genuine
maximized, non-minimized state. A private property retains window-lifetime identity; original
HRGN content is intersected and only private copies retained. USER32 gets ownership after successful
SetWindowRgn. Unchanged regions are skipped; conditional restoration preserves a later replacement
or lost identity. Root review identified synchronous SetWindowRgn message reentry: a busy guard
and own-WndProc regression were added before the final build. Destructor cleanup contains errors.

**Executed focused Release `/W4 /WX` gate:**

```powershell
./scripts/build.ps1 -Config release -BuildDir build/alpha2-release -Jobs 2 `
  -Target @('aether_settings_ui_integration_tests','AetherShellApp') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^aether_settings_ui_integration_tests$' -TestExclude '' `
  -JUnitFile build/alpha2-release/v3-maximized-corner-prototype-results.xml
```

Warning-free build passes; **1/1 executable, 19 cases pass, 6.44 s** (native executable 6.34 s).
Seven added cases cover 96/144/192-DPI/negative-origin/small/overflow geometry, hidden genuine
maximize/placement preservation, rounded-region point membership, original nonrectangular region
intersection/restoration, replacement-region preservation, synchronous Apply/Restore reentry and
normal/wrong-thread/destroyed-identity rejection. Independent source review found no further major
defect. Windows are owned and never shown or activated; frame/work-area observations are synthetic.
No real work-area/device/startup setting, owner window or installed Alpha 2 is changed.

**Not established:** actual DWM frame observation/wiring, antialiased appearance, visible/input pixel
quality, caption buttons/content, snap/restore transitions, foreign compatibility or icon-anchored
window animation. The helper is default-off by absence of any production caller; it is not a shipped
global corner service. Continue with an opt-in owned Settings observer and visible qualification.
No new real capture, installation or V3 publication occurred; the local app retains Alpha 2's base
version. Owner-added media remains untouched, with no new media acceptance claim.

### S-039 — focused Claude implementation handoff (Codex, 2026-10-25)

Documentation-only owner refinement of D38 / FR-033/FR-034 / T42/T43. Source HEAD `35746c8` and a
clean worktree were inspected before writing the focused implementation prompt. The installed
Alpha 2 was not inspected, changed or claimed to display the local V3 work. The owner requests
slower visible marker motion, actual aspect-preserving hover previews with selected-window normal
close, and pointer continuity; Claude implements and Codex subsequently reviews.

No product source, tests, settings, runtime windows or presentation assets were modified. No tests
were rerun for this documentation-only change; S-037/S-038 retain their exact historical boundaries.
The prompt requires focused build/native checks and actual visual inspection for the later code.

### S-040 — single shared working folder consolidation (Claude, 2026-10-25)

Scope: D39 / T45. Organization only; no product source changed. Starting state inspected first:
canonical public checkout clean at `57427f0` (main ahead of `origin/main` by 4), ten linked
worktrees on that lineage, plus the separate private-history checkout and one hidden Codex
worktree of it. Every linked-worktree HEAD was verified to be an ancestor of `main`; one retired
workflow tree had uncommitted edits (3 modified, 2 untracked files), preserved as files and as a
patch whose reverse-apply check passed on that tree. Branch `codex/t36-showcase-checkpoint` is not
in `main` and remains a local branch.

**Executed checks (private paths omitted):**
- `git bundle create --all` for both histories; `git bundle verify` reports complete histories;
  mirror clones of both bundles pass `git fsck --full` with no errors; their refs are byte-identical
  to the source repositories (`for-each-ref` diff empty).
- Retired worktrees moved into the recovery archive, then `git worktree prune -v` removed exactly
  the ten stale administrative entries; `git worktree list` shows only the shared folder.
- The private-history checkout was moved (no deletion); afterwards `git fsck --full` passes
  (two pre-existing dangling trees only), HEAD/status are unchanged and refs equal the bundle.
- Canonical repository after the move: `git status` clean, `origin` unchanged, `main...origin/main
  [ahead 4]`, `git fsck` clean, local refs identical to its pre-move bundle. Restored local tool
  files are hash-identical to their sources; `.codex/` is now git-ignored.
- Focused gate from the final folder (fresh build directory; the old one was retired because its
  CMake cache named the previous path):

```powershell
./scripts/build.ps1 -Config release -BuildDir build/v3-release -Jobs 6 `
  -Target @('aether_dock_tests','aether_dock_integration_tests') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^aether_dock_tests$' -TestExclude ''
ctest -R '^aether_dock_integration_tests$'   # from build/v3-release
```

Warning-free `/W4 /WX` build, 84/84 steps; `aether_dock_tests` passed (0.14 s);
`aether_dock_integration_tests` passed (15.06 s, own windows only). The installed Alpha 2 process,
program folder, settings, pins and startup entry were not touched. Not established: Codex's
independent review of the consolidation.

### S-041 — hover emphasis, genuine window previews and calmer marker motion (Claude, 2026-10-25)

Scope: D38 / FR-033 / FR-034 / T42 / T43, local source on top of `671faad` (shared checkout, D39).
Running binary at the start: the installed **Alpha 2** (`0.2.0-alpha.2`, built 2026-10-23); the V3
source was never installed over it and was not installed by this batch.

**Implemented:** dedicated `graphics::Motion().marker` spring (stiffness 100, damping ratio 0.92;
simulated 0.16 s to 50 %, 0.35 s to 90 %, 0.52 s to within 0.1 DIP, overshoot 0.003 DIP; the former
magnify-driven marker reached 90 % in 0.17 s). Pure `DockHoverSession`: immediate emphasis of a
running icon, 0.6 s dwell, 0.12 s warm switch, 0.35 s icon↔card crossing grace, 0.8 s activation
hold, suppression until leave, keyboard mode. `DockPreviewWindow`: landscape neutral-glass card
(host-backdrop blur, Dock opacity preference, HWND exactly the card), one DWM thumbnail per live
window (aspect fit in 240×150 DIPs, no upscaling, invisible-border crop, honest placeholders),
header title and close button, UIA list, keyboard navigation, compositor fade with DWM opacity
following it. Identity-validated tile activation through `IApplicationLauncher::Activate` and
`WM_SYSCOMMAND/SC_CLOSE` close requests with a pending/timeout state; no termination.

**Executed focused Release `/W4 /WX` gate (final source):**

```powershell
./scripts/build.ps1 -Config release -BuildDir build/v3-release -Jobs 6 `
  -Target @('aether_dock_tests','aether_dock_integration_tests','aether_graphics_tests','AetherShellApp') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^aether_(dock|graphics)_tests$|^aether_dock_integration_tests$' -TestExclude '' `
  -JUnitFile build/v3-release/t42-t43-results.xml
```

Warning-free build including the integrated `AetherShellApp`; **3/3 executables pass, 38.24 s**:
`aether_graphics_tests` 74 cases (0.49 s), `aether_dock_tests` **61 cases** (0.10 s; 18 new: ten
hover-session, six layout/identity, two marker timing/continuity), `aether_dock_integration_tests`
**8 cases** (37.51 s; three new own-window cases plus one opt-in frame recorder that is skipped
unless `AETHER_DOCK_PREVIEW_FRAMES` is set). The earlier four Dock native cases still pass.

New own-window native evidence (test windows created by the test; sources stay behind a private
topmost backdrop so only their DWM thumbnails can appear): hover never activates and leaves
`GetForegroundWindow` unchanged; B's marker is mid-growth after 110 ms while A stays 12 DIPs; after
settling both are exactly 12 DIPs; the card lists both windows in tracker order with two live
thumbnails; thumbnail aspect matches each window's extended frame within 0.03; captured thumbnail
pixels are >50 % each source's own colour and <5 % the other's; the card HWND has no extra region,
sits above the icon row and contains every close button; Dock and card both reach zero frame
demand; icon→card crossing and close-button hover keep the card and emphasis beyond the grace;
leaving both dismisses, releases every thumbnail and contracts B to 5 DIPs; ten rapid observed
foreground alternations stay within 5–12 DIPs with <3 DIPs per 45 ms sample. Icon activation calls
the launcher once and the observed transfer keeps B at exactly 12 DIPs for every sample while A
decreases; a tile click activates only that window; X posts a close to the selected window only
(the accepting window is destroyed, its sibling untouched, nothing activated); a refusing window
stays open, its X shows pending, a second click sends nothing, and the button recovers after the
timeout; a destroyed-but-still-listed window gets no request and its tile is dropped; keyboard Up /
Enter / Escape and the UIA list (List root, `Synthetic window 1` + `Close Synthetic window 1`,
Invoke pattern) work; destruction during the dwell shows no card; reduced motion and high contrast
snap the marker and show/hide the card without a fade; a minimized source showed DWM's retained
content at its restored aspect without activation; suppression releases the card at once.

**Visually inspected (own windows only):** a 115-frame normal-speed own-window sequence and
zoomed marker strips confirm smooth continuous growth, contraction after the grace, concurrent
A-contraction/B-held transfer, card fade-in/out and the translucent card material. One temporary,
reverted experiment painted glass over the thumbnail rectangles: thumbnails stayed visible, which
established DWM's above-content stacking on this Windows 11 build. Frames stay local.

**Not established:** behaviour with the owner's real applications (for example ChatGPT/Chrome) on
the real desktop at normal speed, physical multi-monitor DPI changes while a card is shown,
Windows 10 stacking, protected-content appearance, elevated-window UIPI refusal and owner
acceptance. No real desktop capture, installation, startup change, push, tag or release occurred.

### S-042 — independent hover-preview review and input lifetime corrections (Codex, 2026-10-25)

Scope: D38 / D39 / FR-033 / FR-034 / T42 / T43 / T45, source base `0ca846e` plus the review patch.
Claude confirmed it had stopped writing. Official `origin`, `main`, a single worktree and local
lineage were checked. An unexpected folder move left `launcher/` deleted and `installer/launcher/`
untracked. Both files matched the tracked Git blob hashes exactly; a checked plain move back
restored them without content changes. Its cause is unknown. No reset, checkout replacement or
history rewrite occurred. Private archive recovery is supported by Claude's S-040 receipt, not a
new Codex archive restoration.

**Defects reproduced before correction:** three new own-window regression cases failed on the
original product source. Reordering two tracked windows between X-down and X-up closed the sibling;
keyboard Delete after reorder targeted the new slot rather than the chosen window; capture loss
and cancel mode left a close armed. Each test uses disposable synthetic windows that count and
refuse close requests, fake tracking/launching, temporary settings and the private test backdrop.
Their sources now have RAII destruction so failed assertions cannot leave a window using expired
fixture state. No owner application, registry, work area or device setting was changed.

**Correction:** retain keyboard window identity and body/X part through refresh; if it disappears,
consume action keys until explicit navigation. Cancel pointer gestures when the original slot's
HWND/process/thread changes, or capture/cancel mode invalidates the gesture. Notify accessibility
of retained focus or the root when selection disappears. No new dependency or per-frame polling.

**Executed final focused Release `/W4 /WX`:**

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-codex-hover-review -Jobs 4 `
  -Target @('aether_dock_tests','aether_dock_integration_tests','aether_graphics_tests','AetherShellApp','AetherShell') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^aether_(dock|graphics)_tests$|^aether_dock_integration_tests$' -TestExclude '' `
  -JUnitFile build/wf-codex-hover-review/review-final-results.xml
```

Fresh build folder, warning-free app and recovery helper. **3/3 CTest executables pass, 49.17 s**:
graphics **74 cases** (0.93 s), Dock unit **61 cases** (0.36 s), Dock native **11 reported cases**
(47.51 s: 10 executed, opt-in own-window recorder skipped). New regressions cover cancelled
pointer gestures after reorder, retained keyboard selection followed by removal and explicit
navigation to rearm, and both capture-loss/cancel paths. The existing native thumbnail pixels,
association, normal/refused close, hover transfer, keyboard/UIA, idle and reduced-motion checks pass.
This rerun creates no new motion recording; it does not establish owner visual acceptance.

**Remaining review boundaries:** an open card keeps its old anchor during Dock model reflow;
source-only resize does not refresh cached crop/aspect until a later tracker refresh or re-hover.
HWND/process/thread checks are not a universal window-generation identifier. Physical DPI changes,
external/protected/elevated applications, media actions and normal-speed owner acceptance remain.
The owner directly requested a reviewed local prototype launch; its runtime receipt is separate
from these automated checks. No push, tag or release is part of this review.

**Separate authorized runtime trial, 11:32 CET:** rebuilt app/guard from the committed source
`f692b9fc8f1e12f7c00f7cf486d2af2bee3f22d3`. Normal installed Quit completed; all old installed
processes exited and their baseline file was removed before the private candidate started. A second
process/path/hash check confirmed the candidate app and its adjacent guard, with no installed
process left. Logs acknowledged both desktop surfaces and active taskbar replacement. The private
settings clone retains five genuine pins while disabling clone-only startup, pin reimport and
automatic update checks. Installed app/settings hashes and startup registry value match the
pre-swap observations. No unrelated app closure, desktop recording, installation or startup write.
The candidate is left running for owner review; actual hover appearance/acceptance is still pending.
The private waiting host attempts return to installed Alpha 2 after candidate Quit; that restoration
branch has not been exercised in this trial. Runtime metadata stays ignored in `.local/hover-preview/`.

### S-043 — content-fitted preview cards without shrinking the image (Codex, 2026-10-25)

Scope: D38 / FR-034 / T43, source base `07f28f4` plus the compact geometry patch. The owner confirmed
the running S-042 prototype works very well but supplied a private screenshot showing excessive
card whitespace. It was used only as visual feedback and was not copied to the repository.

The existing thumbnail fit formula and 240×150-DIP limits are unchanged. A two-pass layout sizes
each tile to its fitted image (160-DIP minimum for header readability), with the tallest fitted
image setting the shared row height. Outer/inner padding changes 6/8→4/4 DIPs; header gap 6→4.
The 28-DIP X, 16-DIP icon, thumbnail pixels, aspect and no-upscaling policy remain unchanged.
For one 16:9 source, card 268×212→256×183 DIPs, image still 240×135. A 20:9 source keeps 240×108
pixels in DIP geometry and removes its unused vertical letterbox. Portrait cards may be taller
than wide rather than forcing extra landscape space; headers remain readable. Mixed-window rows
still share height; unknown thumbnails retain a full-size honest placeholder. Capacity remains
conservative even when a custom minimum header width exceeds the thumbnail limit.

Final Release `/W4 /WX` command:

```powershell
./scripts/build.ps1 -Config release -BuildDir build/wf-codex-hover-review -Jobs 4 `
  -Target @('aether_dock_tests','aether_dock_integration_tests','AetherShellApp','AetherShell') `
  -CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2') `
  -Test -TestRegex '^aether_dock_(tests|integration_tests)$' -TestExclude '' `
  -JUnitFile build/wf-codex-hover-review/compact-preview-final-results.xml
```

**2/2 CTest executables pass, 48.95 s:** Dock **62 unit cases** (0.27 s), native **11 reported**
(48.32 s: 10 executed, opt-in recorder skipped). New regression asserts the same 240×108 miniature
at 96/144/192 DPI, no unused single-image vertical box, edge gutters at most 8 DIPs, header gap at
most 4 and X at least 28. Existing aspect, no-upscale, multiple-window, source-pixel association,
close/cancellation, keyboard/UIA and idle cases pass. The old blanket landscape assertion was
replaced by header readability to honor the compact-content request. No owner app was closed by
the tests. Runtime relaunch and owner acceptance of the tighter spacing remain separate checks.

**Separate runtime, 11:47 CET:** after its local commit, app/guard were rebuilt from
`fdc2691133aa76fe3ad3580de38e433f5cf737ae` (no further product changes). Normal Quit of S-042's
candidate exercised the private host's restoration branch: old candidate/guard exited, installed
Alpha 2 supervisor/app/guard returned. The new trial then quit the installed session normally,
waited for shutdown/baseline clearance and launched the compact app with fresh cloned settings.
Surface/taskbar readiness and a second process/path/hash check passed with one adjacent guard and
five genuine pins. Installed app/settings hashes and startup value stayed unchanged. The compact
prototype remains active for owner spacing acceptance; no new desktop capture was made. Private
runtime receipts remain ignored. No installation, push, tag or release.

### S-044 — unique desktop pin association (Codex, 2026-10-25)

Scope: FR-002 / FR-003 / FR-004 / FR-034 / T46, source base `637ce80` plus the Dock patch. The owner
reported one editor window showing separately from its pin. Private boolean-only checks confirm
one standard editor AUMID pin and a matching installed executable process path; no private path,
window title or screenshot is copied into source or documentation.

The shared applications identity helper already permits missing-window-AUMID executable matching;
the Dock had an additional stricter filter that rejected it. A bounded one-way fallback now joins
an argument-free, nonpackaged AUMID pin with an executable-only running group only on validated
full-path equality and unique pin ownership. Conflicting catalog/running AUMIDs, profile arguments,
multiple candidate pins and different installations reject fallback. Explicit profile matching and
the generic-pin/explicit-profile separation remain intact. The pinned ID and launch route survive
late arrival of the running AUMID.

Executed Release `/W4 /WX` Dock unit target through `scripts/build.ps1`, same review build folder:
`aether_dock_tests` **65 cases pass** (CTest 1/1, 0.60 s). Three new unit cases cover joining,
normalization, late identity, conflicting/profile/packaged/path/catalog cases and ambiguous pins.
The separately rebuilt own-window native executable filtered by `dock preview identity:` passes
**1 case, 4.16 s**: one pin only, matching genuine source thumbnail, activation through the runtime
identity/fake launcher with zero launches, late AUMID refresh preserving the card, normal selected
close, and pin/marker/card state after its running group disappears. No owner app is touched.

Corner work is separate and not qualified by these results. Its first app compilation passed but
native-fixture compilation failed; no full app gate or real corner deployment is claimed here.
No push/tag/release or installation change.

### S-045 — guarded genuine-maximize corner policy (Codex, 2026-10-25)

Scope: D40 / FR-036 / T44, source base `568277b` plus the app guard/wiring/tests. The requested
external corner outcome uses a child process because DWM's preference cannot override maximized
rounding policy. The main app queues lifecycle requests and never calls foreign `SetWindowRgn`.
The child uses actual native/DWM/work-area observations, lifetime/property and current-region
checks, conservative eligibility, a 12-DIP radius and an explicit parent/ready/stop handle list.
No maximize/placement/style substitution, injection, app termination or startup write.

Independent review found a real recovery defect: an observation/read/write failure could discard
the only original-region snapshot. Corrected by retaining owned failed leases and retrying at
250 ms, including conditional shutdown restoration. Temporary observation failures no longer clear
ownership; application replacements and positively lost identity are preserved. Nine new fake/unit
cases cover geometry/DPI, unchanged writes, normal restore, custom/replaced regions, lifetime/PID
loss, failures, destructor cleanup, malformed dispatch and isolated-session refusal.

Focused Release `/W4 /WX` build includes `AetherShellApp`, launcher, Dock/app unit/native targets
and the test-only parent fixture. Executed through `scripts/build.ps1`,
`build/wf-codex-hover-review`, targets:
`aether_dock_tests`, `aether_dock_integration_tests`, `aether_app_tests`,
`aether_app_integration_tests`, `AetherShellApp`, `AetherShell`;
`-CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2')`.

Results across the scoped runs (not a single claimed all-green invocation):

- Dock unit **65 pass**, 0.33 s; Dock native **12 reported**, 51.46 s (11 executed, recorder skipped).
- App unit **49 pass**, 4.50 s, including all nine new corner cases.
- Final app native **8 pass**, 6.41 s (CTest 1/1, 6.76 s), using
  `-Target aether_app_integration_tests -Test -TestRegex '^aether_app_integration_tests$' -TestExclude ''`
  and `-JUnitFile build/wf-codex-hover-review/corners-native-final-results.xml`.

Earlier native fixtures had compile issues and showed normally instead of maximizing; those runs
were failures. The final fixture uses genuine `SW_SHOWMAXIMIZED` with `WS_EX_NOACTIVATE`, asserts
`IsZoomed`, maximized placement and unchanged foreground, then observes the actual DWM bounds and
rounded-region corner membership. It verifies exact prior region restoration on stop, preservation
of an app's replacement region, and restoration after a real owned fixture process exits without
signalling stop. Every target is the test's own PID plus explicit HWND whitelist; no owner window
is modified. The small lifetime helper exits on its event or a bounded timeout, is test-only and
is excluded by the install payload's explicit file list.

The production inherited child bootstrap is separate from the in-process native guard fixture;
its ready log/process identity must be checked during the authorized runtime trial. Raster-region
antialiasing, excluded/shell/protected/elevated/full-monitor/custom windows, physical DPI changes
and broader compatibility remain unqualified. A target hanging inside synchronous `SetWindowRgn`
can delay recovery; killing the recovery child loses its in-memory state. No universal recovery or
all-window rounding guarantee. No real desktop capture, installation, push, tag or release.

**Separate authorized runtime, 12:39 CET:** source `eae2684027b03ab7ac3ec0b6a0e9cf3f98f22e04` rebuilt
after its local commit and launched in a fresh private profile. The prior compact trial quit normally,
its host returned installed Alpha 2, then the new bounded swap quit that installed session and
started the candidate. Independent process/path/hash checks confirm its primary app, taskbar helper
and corner child, with the correct parent/internal CLI mode. Private logs acknowledge surfaces,
active taskbar replacement and `window corner guard started`; this exercises the production child
dispatch/readiness path omitted by the native fixture. Default settings/installed binary hashes,
startup value and five genuine pins are preserved. The live guard may now apply requested corners
to eligible owner windows; automated tests never did. The candidate remains active for owner
verification of the duplicate-icon fix and visible rounding. No owner app was closed/moved, desktop
recording, installation or publication occurred. Private receipts remain ignored.

### S-046 — transparent cutouts instead of rectangular DWM material (Codex, 2026-10-25)

Scope: D40 / FR-036 / T44. The owner explicitly withdrew the apparent acceptance of the pin/corner
trial and rejected the dark rectangular remainder around the rounded edge. Neither current fix
is owner-accepted on the basis of that reply. The supplied crop stays private.

The original S-045 tests established region membership/cleanup only. A new own-window pixel fixture
reproduced the missing case: custom non-client frame, `WS_EX_NOREDIRECTIONBITMAP` and explicit
`DWMSBT_MAINWINDOW` failed every corner in both changing-backdrop phases (eight RGB mismatches),
while the standard and custom GDI cases passed. This was a genuine failed baseline, not a visual
acceptance claim. Native tests read only small guarded crops fully covered by an opaque own-pattern
window and the explicit own target; pre/post coverage checks reject unexpected overlays. No capture
is saved and no owner application is captured or modified by tests.

Correction: independently snapshot the exact supported system material, temporarily select NONE
for explicit full-window materials, read it back, acquire fresh geometry and apply the region.
Material and region replacements are conditional and independent; app choices win. Retain a first
partial application in the observer and schedule a bounded retry rather than losing recovery
snapshots; abandon application-replaced lifetimes, including during initial acquisition. No style,
placement or non-client-rendering change. Removing the system material can change transparent
interior/title-bar areas too; a genuine opaque CompositionSurface body verifies the app content
survives while the excluded pixels reveal the changing real backdrop.

Nine new unit cases cover exact material restoration, unchanged AUTO/NONE/unavailable policies,
independent app replacements, first-application recovery, native read/write/restore failures,
synchronous material reassertion and later application opt-in. The first scoped Release `/W4 /WX`
app unit run passes **58 cases**, 3.86 s (CTest 1/1, 4.27 s). Subsequent source review added initial
replacement abandonment; the final gate will rerun this suite.

Intermediate native run: **11 pass, 1 fail**. The final custom-composition/Mica cutout and material
replacement cases pass; the standard fixture encounters an empty excluded-pixel mask and is under
diagnosis. Earlier post-fix fixtures also incorrectly used GDI content with NOREDIRECTIONBITMAP
and assumed DWM never installs its own normal region after a material change. Those fixture
failures are recorded, not counted as a fully passing gate. No updated runtime, owner acceptance,
installation, push, tag or release is claimed at this checkpoint.

**Final scoped gate:** the standard fixture now settles its own initial native show/topmost frame
before guard acquisition, then requires an actual complex region and nonempty excluded-pixel masks.
No region is forced and RGB assertions are unchanged. Final Release `/W4 /WX` passes **58 app unit
cases** (4.20 s) and **12 app native cases** (12.42 s), **2/2 CTest executables**, total 17.02 s.
No skipped backdrop case on this run. All four excluded corner areas exactly match the genuine
private backdrop in both phases for standard/custom GDI and custom composition/Mica fixtures;
interior app pixels remain visible. Explicit stop restores MAINWINDOW and application material/
region replacements survive cleanup. Existing parent-death region recovery remains covered.

Command: `scripts/build.ps1 -Config release -BuildDir build/wf-codex-hover-review`
with targets `aether_app_tests,aether_app_integration_tests,AetherShellApp,AetherShell`,
`-CMakeArgs @('-DAETHER_WARNINGS_AS_ERRORS=ON','-DAETHER_VERSION_PRERELEASE=alpha.2')`,
`-Test -TestRegex '^aether_app_(integration_)?tests$' -TestExclude ''`
and `-JUnitFile build/wf-codex-hover-review/corner-backdrop-final-results.xml`.
Raw build/failure evidence remains ignored in `build/corner-*.log`. No universal OS/DPI or
antialiasing claim. Owner visual acceptance remains pending; no publication or installation.
