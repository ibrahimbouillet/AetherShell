# applications — discovery, launching, running windows, icons (`src/applications`, target `aether_applications`)

## Purpose

`aether_applications` implements the application contracts of `ApplicationTypes.h` (owned by the lead)
for the Dock, Search and Settings:

| Contract | Implementation | Backed by |
| --- | --- | --- |
| `IApplicationCatalog` | `ShellApplicationCatalog` | `shell:AppsFolder` + Start-menu `.lnk` files |
| `IApplicationLauncher` | `ShellApplicationLauncher` | `ShellExecuteEx`, `IApplicationActivationManager` |
| `IRunningApplicationTracker` | `WindowTracker` | `EnumWindows` + WinEvent hooks |
| `IIconProvider` | `ShellIconProvider` | `IShellItemImageFactory` |

Dependencies: `aether_core`, `aether_diagnostics`, `aether_platform`, `shell32 ole32 oleaut32 propsys
shlwapi user32 gdi32 dwmapi uuid`, and `windowsapp` for the C++/WinRT support code behind
`winrt::com_ptr` / `winrt::hresult_error`.

## Public API

| Header | Contents |
| --- | --- |
| `ApplicationIdentity.h` | Pure identity helpers: `MakeIdForAumid`, `MakeIdForExecutable`, `MakeIdForParsingName`, `MakeIdForWindowClass`, `GetIdScheme`, `IsPackagedAumid`, `IsExplicitAumid`, `NormalizeExecutablePath` / `NormalizeShortcutPath` / `NormalizeAbsoluteFilePath` (with `PathPolicy{allowUnc}`), `ValidateParsingName`, `MakeApplicationId`, `RefersToSameApplication` |
| `CatalogRules.h` | Pure catalog rules: `DiscoveredItem`, `ClassifyDiscoveredItem` → `CatalogDecision`, `IsUninstallerName`, `IsUninstallerExecutable`, `IsHelpViewerExecutable`, `IsUrlTarget`, `IsShellNamespaceTarget`, `MergeDuplicateApplications`, `CombineCatalog` |
| `ShellApplicationCatalog.h` | `ShellApplicationCatalog(Options{includeStartMenuShortcuts, maxShortcutDepth = 6, maxShortcutFiles = 5000})`, `DiscoverApplications(cancel)` |
| `TaskbarPins.h` | Worker-only `DiscoverTaskbarPins(cancel, TaskbarPinImportOptions{directory, maxShortcutFiles = 512, catalog})`; best-effort, read-only import of supported local taskbar shortcuts |
| `LaunchRules.h` | Pure launcher decisions: `MakeLaunchError`, `MakeLaunchErrorFromHResult`, `ShouldTryNextLaunchRoute`, `ChooseWindowToActivate` → `ActivationChoice` |
| `ShellApplicationLauncher.h` | `ShellApplicationLauncher(IUiDispatcher&)`, `LaunchAsync`, `Activate`, static `LaunchNow` |
| `WindowEligibility.h` | `WindowFacts`, `ClassifyWindow` → `WindowExclusion`, `IsEligibleAppWindow`, `IsShellWindowClass`, `CollectWindowFacts(HWND)` |
| `WindowIdentityRules.h` | Pure tracker identity rule: `WindowIdentityFacts` → `ResolveWindowIdentity` → `WindowIdentity{id, appUserModelId, executablePath, processId}`, `ShouldRememberFrameIdentity`, `IsApplicationFrameClass` |
| `WindowTracker.h` | `WindowTracker(IUiDispatcher&, Options{coalesceDelay = 100 ms})`, `Start(listener)`, `Stop()`, `Snapshot()` |
| `IconPixels.h` | `AnalyzeAlpha` → `AlphaAnalysis{format, translucentPixels, colourAboveAlphaPixels}`, `IsPremultipliedBgra`, `PremultiplyBgra`, `NormalizeToPremultipliedBgra(pixels, AmbiguousAlpha)` |
| `IconCache.h` | `IconCache(maxEntries, maxBytes)`: LRU keyed by (key, pixel size) |
| `ShellIconProvider.h` | `ShellIconProvider(IUiDispatcher&, Options{cacheEntries = 256, cacheBytes = 32 MiB})`, `RequestIcon`, `CancelAll`, static `ExtractIcon` → `ExtractedIcon{image, source}` |
| `ShellItems.h` | Internal shell helpers (RAII `PropVariant`, CoTaskMem deleters, property reads, `CreateAppsFolderItem`, `CreateApplicationItem`, `RunOnSta`) |

```cpp
// Worker thread: discovery blocks for a few seconds.
aether::apps::ShellApplicationCatalog catalog;
auto apps = catalog.DiscoverApplications(cancelToken);

// UI thread.
aether::apps::ShellApplicationLauncher launcher(uiDispatcher);
launcher.LaunchAsync(apps.value()[0], [](aether::Result<void> result) { /* UI thread */ });

aether::apps::WindowTracker tracker(uiDispatcher);
tracker.Start([](const std::vector<aether::apps::RunningApplication>& running) { /* UI thread */ });

aether::apps::ShellIconProvider icons(uiDispatcher);
icons.RequestIcon({app.id, app, 64}, [](const std::wstring& key, auto image) { /* UI thread; image may be null */ });
```

## Taskbar pin import

`DiscoverTaskbarPins` supports the owner's V2 request to keep pinned applications in the Dock even
when they have no open window (FR-002). The app owns the import action, persistence and user feedback;
this module returns ordinary `ApplicationInfo` records with identity, shortcut, arguments, executable
and AUMID metadata. It performs no launch and never modifies Windows pins or application settings.

The default root is the documented `FOLDERID_UserPinned` plus the legacy `TaskBar` subdirectory.
`SHGetKnownFolderPath` uses `KF_FLAG_DONT_VERIFY`, without folder creation. Immediate local `.lnk`
files are loaded through `IPersistFile` with `STGM_READ`; `IShellLink::Resolve` is deliberately omitted
for import, so link tracking/search, shortcut rewriting and missing-target repair do not run. No
registry pin database is read. A missing folder returns an empty success, and cancellation or an
unreadable folder returns a generic error without user names or paths. The directory must be an
ordinary local directory; reparse entries and nested directories are skipped. Enumeration is bounded
at 4096 entries and 512 shortcut files by default; exceeding a bound fails instead of returning an
unmarked partial import.

Supported entries use the catalog's existing application filters and identity rules. Optional
`options.catalog` reuses an exact identity or an unambiguous AUMID/executable match with the same
arguments. Conflicting AUMIDs, ambiguous profiles and differing arguments do not collapse. The
imported `.lnk` and its arguments remain attached to the record. Matching identities are deduplicated,
and filename ordering is deterministic so repeated imports do not randomly reorder the same source.
The caller must merge into its persisted pins and protect existing user ordering.

This is a best-effort source, not a complete Windows 11 pin enumeration API. Modern packaged pins
may have no `.lnk` in this folder; a packaged AUMID is imported when a readable shortcut exposes it.
Windows taskbar order is unavailable from these files. Internet `.url` files, document links and
unsupported shell targets are skipped consistently with the current launcher contract. No binary
`Taskband`/`CloudStore` format is parsed and no Windows Settings integration is implied. Users can
pin any catalog application directly in Aether and reorder imported pins afterwards.

Primary references checked for this implementation:

- [Microsoft KNOWNFOLDERID reference](https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid)
  documents `FOLDERID_UserPinned`; its `TaskBar` child is treated as a best-effort legacy convention.
- [Microsoft IShellLink::Resolve reference](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-resolve)
  describes the resolution/search/update behavior omitted by the importer.
- [Microsoft TaskbarManager reference](https://learn.microsoft.com/en-us/uwp/api/windows.ui.shell.taskbarmanager)
  lists pin requests and pin-status checks, without a method that returns every user's pin in order.

## Identity

| Form | Used for |
| --- | --- |
| `aumid:<lower-case AUMID>` | packaged apps (`<PackageFamilyName>!<AppId>`) and explicit AUMIDs (`Chrome`, `Microsoft.Windows.Explorer`) |
| `exe:<lower-case normalized path>` | desktop apps (full path, `/` → `\`, `.`/`..` collapsed, 8.3 names expanded best effort with `GetLongPathNameW` on drive paths only: never on UNC paths, so computing an id never touches the network) |
| `shell:<lower-case parsing name or .lnk path>` | Apps-folder items without exe/AUMID, and shortcuts that pass arguments |
| `window:<lower-case class>` | tracker only: windows whose process cannot be opened |

The Apps folder synthesizes AUMIDs for shortcuts that declare none (path-like `{GUID}\7-Zip\7zFM.exe`,
`Microsoft.AutoGenerated.{GUID}` for shortcuts with arguments). Those never appear on windows, so
`IsExplicitAumid` rejects them and the executable identity is used instead, which is what the tracker
computes for the running process. Two shortcuts that start the same executable with different trusted
arguments (Command Prompt and Developer Command Prompt) keep distinct `shell:` identities.

Executable paths are validated before use: drive-absolute (`C:\…`) and `.exe`; device and NT-namespace
paths (`\\.\`, `\\?\GLOBALROOT`, `\\?\Volume{…}`, `\??\`), alternate data streams, wildcards, control
characters and directories are rejected; UNC paths are rejected unless `PathPolicy::allowUnc` is set
(only the tracker sets it, for identity, never for launching). Apps-folder parsing names (which can
come from persisted pins) are validated too: control characters, more than 1024 characters and any
`.`/`..` segment (either separator, or dot/space spellings such as `...`) are refused before
`shell:AppsFolder\<name>` is built, so an item can never lie outside the Apps folder.

## Discovery (`ShellApplicationCatalog`)

1. `SHGetKnownFolderItem(FOLDERID_AppsFolder)` → `BindToHandler(BHID_EnumItems)`. Per item:
   `SIGDN_NORMALDISPLAY`, the Apps-folder-relative parsing name, `PKEY_AppUserModel_ID`,
   `PKEY_Link_TargetParsingPath`, `PKEY_Link_Arguments` (`IShellItem2::GetString`, keys from `propkey.h`)
   and the presence of `System.AppUserModel.PackageFamilyName` / `PackageFullName` (looked up with
   `PSGetPropertyKeyFromName`).
2. `FOLDERID_CommonPrograms` and `FOLDERID_Programs`, walked recursively (directory depth ≤ 6, ≤ 5000
   `.lnk` files in total, junctions not followed). Each link is loaded with `IShellLinkW` and resolved
   with `SLR_NO_UI | SLR_NOUPDATE | SLR_NOSEARCH | SLR_NOTRACK | SLR_NOLINKINFO` (1 s timeout, never
   written back); shortcuts whose target is gone are dropped. Display names come from each directory's
   `IShellFolder` so `desktop.ini` localization applies ("Invite de commandes").
3. `ClassifyDiscoveredItem`: Packaged when the AUMID has the `!` form or package properties exist;
   Desktop when the target is an `.exe`; Other for shell objects with an explicit AUMID (Run, File
   Explorer, Control Panel). Skipped: names starting with "Uninstall", `unins*.exe`/`uninstall*.exe`/
   `msiexec.exe` targets, help viewers and help documents, URLs and `.url` files, other documents,
   advertised (Windows Installer) shortcuts, invalid or UNC targets.
4. `CombineCatalog`: a shortcut enriches the Apps-folder entry it describes (same AUMID, or same
   executable and arguments) with its `.lnk` path; unmatched shortcuts are added; duplicates merge by id,
   preferring the entry that has both an AUMID and an executable. Sorted by display name.

Measured on this machine (Windows 11 build 26300, 8 cores, other builds running): 112 applications
(40 packaged, 69 desktop, 3 other) from 135 Apps-folder items and 96 shortcuts in 2.6–3.7 s warm
(about 1.3 s of it inside the Apps folder's own enumeration); the first, cold run took 8.8 s. Each run
logs one INFO line with the totals, per-source timings and skip counts.

## Launching (`ShellApplicationLauncher`)

Routes, in order; a route whose item does not exist (or whose target is gone) falls through to the next
one, while a cancelled UAC prompt, an access denial or any other launch failure stops immediately so the
user is never prompted twice:

1. Apps-folder item (`SHCreateItemFromParsingName("shell:AppsFolder\<parsing name>")`) → PIDL →
   `ShellExecuteEx` with `SEE_MASK_INVOKEIDLIST` (implies `SEE_MASK_IDLIST`; runs the item's default
   verb, needed for non-file-system items), `SEE_MASK_FLAG_NO_UI`, `SEE_MASK_NOASYNC`, `SW_SHOWNORMAL`.
2. `IApplicationActivationManager::ActivateApplication` for packaged AUMIDs, after
   `CoAllowSetForegroundWindow`.
3. `ShellExecuteEx` on the validated `.lnk` (the shortcut supplies its own arguments and directory).
4. `ShellExecuteEx` on the validated `.exe`, working directory = its folder, no arguments. An entry
   whose arguments came from a shortcut that no longer exists is refused instead of started bare.

No command line is ever built: `ShellExecuteEx` receives a PIDL or a file path and `lpParameters` is
always null. Errors are user-facing (`LaunchRules.h`): NotFound "… could not be found. It may have been
moved or uninstalled.", Cancelled "The launch of … was cancelled." (declined UAC), AccessDenied, Unsupported,
Platform, with the Win32/HRESULT code in `platformCode`. Display names inside messages are bounded and
stripped of control characters.

The launcher's destructor drops queued launches and pending completions but waits for a launch in
progress; `ShellExecuteEx` (`SEE_MASK_NOASYNC`) stays blocked while a UAC prompt it raised is open, so
destroy the launcher at shutdown only.

`Activate` (UI thread): drops dead handles, refreshes `minimized` with `IsIconic`, then
`ChooseWindowToActivate`: most recent non-minimized window; when the app already owns the foreground
(directly or through an owned dialog's root owner) the least recently active window, so repeated clicks
cycle through all windows; a lone window already in front is left alone. Minimized windows are restored
with `ShowWindowAsync(SW_RESTORE)`. When `SetForegroundWindow` is refused, the taskbar button is flashed
(`FlashWindowEx`, `FLASHW_TRAY | FLASHW_TIMERNOFG`) and AccessDenied "Windows prevented activation" is
returned. No `AttachThreadInput`, no synthesized input.

## Running windows (`WindowTracker`)

- `Start` enumerates windows once, installs out-of-context hooks (`WINEVENT_OUTOFCONTEXT |
  WINEVENT_SKIPOWNPROCESS`) for show/hide/destroy, name change, foreground, minimize start/end and
  cloak/uncloak, filtered to `OBJID_WINDOW` / `CHILDID_SELF`. Events only arm a ~100 ms UI-thread
  timer; the timer re-enumerates and the listener runs only when the result changed. The first
  notification is delivered from the message loop, never from inside `Start`.
- Eligibility (`ClassifyWindow`, pure): not our process, visible, not DWM-cloaked, top-level, not
  Progman/WorkerW/Shell_TrayWnd/Shell_SecondaryTrayWnd, unowned unless `WS_EX_APPWINDOW`, not
  `WS_EX_TOOLWINDOW`/`WS_EX_NOACTIVATE` unless `WS_EX_APPWINDOW`, non-blank title. Titles of other
  processes' windows are read with `GetWindowTextW`, which does not send messages, so a hung app cannot
  block the UI thread.
- Identity (pure rule in `WindowIdentityRules.h`, unit tested branch by branch): the window's
  `PKEY_AppUserModel_ID` (`SHGetPropertyStoreForWindow`) when packaged or explicit, else the process's
  packaged AUMID (`GetApplicationUserModelId`), else `QueryFullProcessImageNameW`, through
  `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` and cached per process (creation time guards against
  pid reuse). `ApplicationFrameWindow` frames are identified through their hosted
  `Windows.UI.Core.CoreWindow`'s process; the identity seen while it is attached is remembered and
  reused while the CoreWindow is detached (app minimized or suspended), even when the frame carries the
  same AUMID itself, so the process id and executable do not flip to ApplicationFrameHost's. A frame
  that shows another app's AUMID is re-identified. Processes that cannot be opened are still listed as
  `window:<class>` with the title as name.
- Windows are grouped by id, most recent first (z-order); applications keep their first-seen order; the
  foreground app is flagged. Display names (Apps-folder name for AUMIDs, `FileDescription` for executables)
  are resolved on a small STA worker; until then the executable's file name is shown.
- `Stop` unhooks, kills the timer, destroys the message window, drops pending name results and empties
  the snapshot (a stopped tracker reports nothing rather than stale, possibly dead windows); idempotent;
  the tracker can be started again, and `Snapshot()` is filled again as soon as `Start` returns.

## Icons (`ShellIconProvider`)

- Desktop shell item: existing ordinary, validated local `.lnk` first, then Apps-folder item (parsing
  name, else explicit AUMID), then executable if the shortcut is absent or extraction fails. This
  preserves a supplied profile icon without changing launch routing. Packaged/Other items retain
  their original Apps-folder/AUMID, `.lnk`, `.exe` ordering.
  `IShellItemImageFactory::GetImage(size, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK)`; the result may be
  larger than requested and `IconImage` carries its real size.
- Read back with `GetDIBits` as 32-bpp top-down BGRA, then premultiplied. **The returned DIB is not
  premultiplied**: the integration survey on build 26300 found 45 of 48 icons provably straight alpha (a
  colour component above its alpha; packaged and desktop alike), 2 fully opaque, 1 ambiguous and none
  that could only be premultiplied. Images with translucent pixels are therefore always premultiplied
  (`AmbiguousAlpha::TreatAsStraight`); bitmaps whose alpha bytes are all 0 are made opaque; all-zero
  bitmaps count as "no icon". The survey test re-checks this on every run.
- Duplicate in-flight requests (same key and size) share one extraction; an LRU cache (256 entries,
  32 MiB of pixels) answers repeats without touching the shell; failures are not cached. Every
  coalesced caller is answered even when the extraction fails or throws, so a key can never get stuck.
- The destructor discards queued extractions but waits for the one in progress (a hung shell
  extension would delay it); destroy the provider at shutdown.

## Threading model

| Component | Thread |
| --- | --- |
| `ShellApplicationCatalog::DiscoverApplications` | caller's worker thread; runs on an STA: inline on an STA, after a temporary `ComApartment` when COM is not initialized (or only through the implicit MTA), or on a temporary STA thread when called from the MTA |
| `ShellApplicationLauncher::LaunchAsync` | dedicated STA `WorkerPool` thread; completion posted to `IUiDispatcher` |
| `ShellApplicationLauncher::Activate` | UI thread (InvalidState otherwise) |
| `WindowTracker` | UI thread with a message loop, ideally a COM STA (see below); names resolved on an STA worker |
| `ShellIconProvider` | one STA `WorkerPool` thread; callbacks posted to `IUiDispatcher` |

Nothing in the module blocks the UI thread on discovery, icon extraction or launching. Exceptions
(including `winrt::hresult_error`) are converted to `Error` inside the module (`shell::CurrentExceptionToError`)
and never escape a public function or a worker task.

## Failure behaviour

- Discovery: Cancelled when the token fires (checked between items and files); a failing source is
  logged and skipped; Unavailable when neither source produced anything.
- Launch: see the error table above; completions are dropped once the launcher is destroyed.
- Tracker: `Start` fails with InvalidState off the UI thread or when already running, and with the Win32
  error when the message window or a hook cannot be created (everything is released again).
- Icons: a null image means "draw the fallback icon". Invalid requests (empty key, size outside
  1–1024) complete with null. After `CancelAll()` called on the UI thread no earlier callback runs
  (called from another thread it cannot stop a callback the UI thread is already running); after
  destruction no callback runs at all.

## Limitations

- The tracker needs COM on the UI thread for per-window AUMIDs. Measured: without COM,
  `SHGetPropertyStoreForWindow` returns a store from which no AUMID can be read (0 of 18 windows vs 2
  with an STA), so PWAs and apps with explicit window AUMIDs are grouped by process. `Start` logs a
  warning in that case.
- `ApplicationFrameWindow` handling relies on the undocumented but long-stable window structure (a
  child `Windows.UI.Core.CoreWindow` owned by the app's process); no UWP frame was open during the
  automated tests, so that path was not exercised on this machine (its decision logic is unit tested
  with synthetic facts). A frame that is already minimized when the tracker starts, and that carries no
  AUMID of its own, can only be attributed to ApplicationFrameHost.exe until it is restored once.
- Processes that cannot be opened with `PROCESS_QUERY_LIMITED_INFORMATION` are grouped by window class,
  not by application.
- Packaged detection without the `!` form relies on the package properties exposed by the Apps folder.
- Discovery cost is dominated by the shell's own Apps-folder enumeration (~1.3 s warm, several seconds
  cold); call it once at startup on a worker and on demand afterwards.
- Display names of running executables use the version resource `FileDescription`, which some
  applications leave generic.
- 8.3 short names in UNC image paths are not expanded (no network access from the UI thread), so such a
  process keeps its short spelling in its `exe:` id. Mapped network drive letters are not detected and
  are expanded like local paths (on the tracker's UI thread, only when the path contains `~`).
- Launch and activation paths that would start an application or move focus are covered only by unit
  tests of their pure decisions; the automated suite never launches anything or activates windows.

## T17 follow-up: profile icons and a File Explorer pin

**Owner clarification resolved: browser PROFILE avatars and context menus for Dock application
icons.** This concerns separate browser profiles/windows, rather than signed-in accounts inside web
tabs. Matching metadata and end-to-end avatar acceptance remain pending. The implemented narrow
change prefers an existing validated desktop shortcut for icon extraction; its success still depends
on the application exposing a useful icon through that shortcut. No browser account store, cookies,
history, profile configuration, avatar directory or actual owner profile was inspected. No default
pins were changed. An exact avatar cannot be promised without exposed usable metadata.

Current source evidence:

| Source | Observed behavior |
| --- | --- |
| `src/applications/WindowTracker.cpp`: `WindowAppUserModelId`, `LookupWindowIdentity`; `WindowIdentityRules.cpp`: `ResolveWindowIdentity` | Reads the window's `PKEY_AppUserModel_ID`; explicit window identity takes precedence over process identity. Distinct exposed AUMIDs can already produce separate running application groups. This does not identify web accounts. |
| `src/dock/DockModel.cpp`: `BuildDockItems` | An unpinned running entry receives only id, name, executable and AUMID. Existing catalog/shortcut metadata is not reconciled into that entry. |
| `src/applications/ShellItems.cpp`: `ShouldPreferShortcutIcon`; `ShellIconProvider.cpp`: `ExtractOnSta` | The narrow T17 change prefers an existing ordinary, validated local desktop `.lnk` for icon extraction, then falls back to the generic route if extraction fails. Packaged/Other entries keep their existing order. It does not discover or reconcile a missing profile shortcut. |
| `src/dock/DockWindow.cpp`: `RequestIcons`; `src/applications/ShellIconProvider.cpp`: `State::Extract`; `IconCache.cpp` | Request/cache identity is application key plus pixel size. Richer shortcut metadata arriving later needs explicit invalidation or an icon-source revision, or an already cached generic icon may remain visible. |

Remaining implementation following clarification:

1. Feed already discovered catalog entries and already imported/persisted pins into a pure matching
   function. Match an explicit running AUMID exactly, case-insensitively. Never use executable-only
   matching to substitute a different explicit profile, and do not guess an ambiguous match.
2. Copy the matched shortcut and trusted launch metadata into the running item's presentation while
   preserving its running identity/windows. Keep argument-bearing profiles distinct. The desktop
   shortcut icon preference is already implemented in `ExtractOnSta`; missing/unusable overrides
   fall back honestly. Keep launch routing separate; changing an icon must not change which profile
   launches.
3. Refresh affected Dock images/request markers and the provider cache when that presentation's
   source changes. Avoid reading shortcut/icon files on the UI thread; extraction remains on its STA
   worker. A missing/unexposed avatar retains a generic icon with no invented account identity.

Microsoft documents window-level AUMIDs and matching shortcuts in its
[AppUserModelID guidance](https://learn.microsoft.com/en-us/windows/win32/shell/appids). Its optional
[RelaunchIconResource property](https://learn.microsoft.com/en-us/windows/win32/properties/props-system-appusermodel-relaunchiconresource)
can describe the taskbar group icon when explicitly supplied by an application. Reading or supporting
that additional property is a separate possible extension, not implemented or runtime verified here.
[IShellLink::GetIconLocation](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-geticonlocation)
exposes a shortcut's icon definition, and
[IShellItemImageFactory::GetImage](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishellitemimagefactory-getimage)
supports extracting its icon on a worker. These contracts establish possible sources, not a guarantee
about any particular browser's current metadata.

For File Explorer, the existing catalog classifier already accepts the `Microsoft.Windows.Explorer`
AppsFolder entry as `aumid:microsoft.windows.explorer` (synthetic test: "catalog rules: shell objects
from the Apps folder"). Prefer that existing entry when present. A proposed worker-only fallback is
to obtain the system directory with
[GetWindowsDirectoryW](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getwindowsdirectoryw),
handle zero/insufficient-buffer results, append `explorer.exe`, then validate an absolute local
executable and verify that the file exists. Construct the generic application with that executable,
the explicit Explorer AUMID/parsing name, and no user folder arguments. Do not launch it to discover
it. Add it through the existing pin/settings persistence path only after the intended default/import
preference is settled; deduplicate by identity and preserve existing order and user unpin decisions.
An added default Explorer pin must not be described as imported Windows state without source evidence.

Two routing-policy cases use temporary synthetic file metadata to cover existing desktop shortcuts,
Packaged/Other ordering, missing files, directories, wrong extensions, UNC and relative paths. They
do not demonstrate a real browser avatar or successful shell extraction of a profile-specific image.
Required checks before claiming the full follow-up complete: synthetic different-AUMID/same-executable
profiles, conflicting/ambiguous identities, missing shortcut/override, changed source after generic
cache fill, late callback invalidation, exact arguments after persistence, and Explorer insertion,
deduplication and preserved unpin/order behavior. Temporary `.lnk`/icon fixtures and fake windows can
exercise these without owner browser data. Any capture must contain only the project's synthetic
windows over the private opaque backdrop with existing occlusion guards. No real-profile screenshot,
account label, avatar, user-specific path or raw owner application listing belongs in public evidence.
Actual browser behavior would remain a separately authorized manual check, with a sanitized outcome
only. No runtime discovery, external launch or actual profile-icon extraction was performed for T17.

## Tests

- Unit (`tests/unit/applications`, 66 cases): identity normalization and path validation (device, UNC,
  ADS, 8.3 expansion, no expansion on UNC), packaged/explicit AUMID syntax, id preference order, catalog
  filters (uninstallers, help, URLs, documents), classification, dedup/merge and Start-menu enrichment,
  Apps-folder name hardening (`.`/`..` segments), the window eligibility predicate, the window identity
  rule (explicit/synthesized AUMIDs, packaged processes, inaccessible processes, attached/detached
  frames), launch error mapping and window choice, alpha analysis and premultiplication, the LRU cache,
  icon-provider cancellation and failure coalescing. Nine taskbar-import cases use only synthetic
  temporary shortcuts: retained launch metadata without running windows, deterministic deduplication,
  unchanged file bytes/write time, profile arguments, catalog reconciliation/ambiguity, packaged
  AUMIDs, skipped malformed/unsupported files, missing-folder no-creation, cancellation/limits and MTA
  dispatch. Current V2 focused Release `/W4 /WX` run: 66 passed, 0 failed; no actual user pins read.
- Integration (`tests/integration/applications`, 15 cases, real desktop, nothing launched): discovery
  (>10 apps with Packaged and Desktop entries, count, duration and log line noted; MTA caller;
  cancellation), every discovered Apps-folder entry resolving to its shell item (the launcher's first
  route; packaged apps also by AUMID), icon extraction for 3 discovered apps (premultiplied, non-empty),
  the pixel-format survey (fails if no translucent icon is provably straight any more), request
  coalescing, cache hits and `CancelAll`, launcher failure paths (including an argument-carrying entry
  refused instead of started bare), and the tracker with a pumping message loop (existing windows noted;
  a top-level window of the test process is excluded; empty snapshot after `Stop`, refilled on restart).
