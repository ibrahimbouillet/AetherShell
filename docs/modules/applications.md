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

Dependencies: `aether_core`, `aether_diagnostics`, `aether_platform` (including `InputInjection.h`),
`shell32 ole32 oleaut32 propsys shlwapi user32 gdi32 dwmapi uuid advapi32` (advapi32 for the
read-only `RegGetValueW` of the taskbar pin list), and `windowsapp` for the C++/WinRT support code
behind `winrt::com_ptr` / `winrt::hresult_error`. No third-party library.

## Public API

| Header | Contents |
| --- | --- |
| `ApplicationIdentity.h` | Pure identity helpers: `MakeIdForAumid`, `MakeIdForExecutable`, `MakeIdForParsingName`, `MakeIdForWindowClass`, `GetIdScheme`, `IsPackagedAumid`, `IsExplicitAumid`, `NormalizeExecutablePath` / `NormalizeShortcutPath` / `NormalizeAbsoluteFilePath` (with `PathPolicy{allowUnc}`), `ValidateParsingName`, `MakeApplicationId`, `RefersToSameApplication` |
| `CatalogRules.h` | Pure catalog rules: `DiscoveredItem`, `ClassifyDiscoveredItem` → `CatalogDecision`, `IsUninstallerName`, `IsUninstallerExecutable`, `IsHelpViewerExecutable`, `IsUrlTarget`, `IsShellNamespaceTarget`, `kAppsFolderParsingPrefix`, `AppsFolderChildName`, `IsAppsFolderParsingName`, `MergeDuplicateApplications`, `CombineCatalog` |
| `ShellApplicationCatalog.h` | `ShellApplicationCatalog(Options{includeStartMenuShortcuts, maxShortcutDepth = 6, maxShortcutFiles = 5000})`, `DiscoverApplications(cancel)` |
| `TaskbarPins.h` | Worker-only `DiscoverTaskbarPinSet(cancel, TaskbarPinImportOptions{directory, maxShortcutFiles = 512, catalog, useTaskbandOrder = true, taskbandReader, maxTaskbandBytes = 512 KiB, windowsDirectory})` → `TaskbarPinSet{pins, windowsOrder, fromTaskband, pidlShortcuts, skipped}`; `DiscoverTaskbarPins` (the pins only); pure `SelectTaskbandSource` → `TaskbandSource{None, Injected, Registry}` |
| `TaskbandScan.h` | Pure, fail-closed `ScanTaskband(blob, shortcutFileNames, isKnownPackagedAumid, maxBytes)` → `TaskbandScan{shortcutOffsets, packaged}` |
| `TaskbarPinSync.h` | Pure `PlanTaskbarPinSync(PinSyncInput)` → `PinSyncPlan`, `SameLaunchIdentity(PinIdentity, ApplicationInfo)`, `kMaxDockPins = 64` |
| `KnownApplications.h` | File Explorer: `kFileExplorerAumid`, `FileExplorerId`, `SystemWindowsDirectory`, `IsFileExplorerExecutable`, `IsFileExplorer`, `CanonicalizeFileExplorer`, `CanonicalizeFileExplorerWindow`, worker-only `MakeFileExplorerApplication(catalog, windowsDirectory)` |
| `RunningPresentation.h` | Pure `ApplicationIndex(catalog)` (`FindByAumid`, `FindByExecutable`, `HasExecutableWithOtherAumid`), `ResolveRunningPresentation(running, index)`, `HasLaunchRoute`, `IconSourceFingerprint` |
| `WindowProperties.h` | `WindowShellProperties{aumid, relaunchIconResource, relaunchDisplayName}`, `ReadWindowShellProperties(HWND)` (COM STA), pure `SanitizeRelaunchDisplayName`, `SanitizeRelaunchIconResource` |
| `IconResource.h` | Pure `ParseIconResource("<path>.ico,0")` → `IconResource{path, index}` |
| `SystemShellActions.h` | User-triggered `OpenStartMenu(hwnd, injector, StartMenuRoute{SystemCommand, WindowsKey})`, `OpenTaskView(injector)` (Win+Tab), `OpenWindowsSearch(injector)` (Win+S); overloads without an injector use `platform::SystemKeyboardInjector()` |
| `LaunchRules.h` | Pure launcher decisions: `MakeLaunchError`, `MakeLaunchErrorFromHResult`, `ShouldTryNextLaunchRoute`, `ChooseWindowToActivate` → `ActivationChoice{window, alreadyActive, cycled, minimize}` |
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

`DiscoverTaskbarPinSet` supports the owner's V2 request to show in the Dock what is pinned to the
Windows taskbar, even without open windows (FR-002). The app owns the trigger, persistence and user
feedback (see "Pin sync policy" below); this module returns ordinary `ApplicationInfo` records with
identity, shortcut, arguments, executable and AUMID metadata, plus counts. It performs no launch and
never modifies Windows pins, the registry or application settings.

**Sources, in order of trust.**

1. The documented `FOLDERID_UserPinned` plus the legacy `TaskBar` subdirectory. `SHGetKnownFolderPath`
   uses `KF_FLAG_DONT_VERIFY`, without folder creation. Immediate local `.lnk` files are loaded through
   `IPersistFile` with `STGM_READ`; `IShellLink::Resolve` is deliberately omitted, so link
   tracking/search, shortcut rewriting and missing-target repair do not run. A shortcut without a file
   target whose ID list (`IShellLink::GetIDList` → `SHCreateItemFromIDList`) names an Apps-folder item
   (its parent is the Apps folder) is read through that item, like a catalog entry, with the `.lnk`
   kept for its icon. Measured on build 26300: such an item reports only its child name as its absolute
   parsing name, so the parent decides.
2. The Windows pin list: `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Taskband`,
   value `Favorites` (`REG_BINARY`), read with the documented `RegGetValueW` (size checked before
   allocating, never written). **Its layout is undocumented** (a disclosed compatibility seam, like
   D19's), so `TaskbandScan.h` never interprets it: no PIDL or record framing is parsed and no byte is
   handed to a shell API. The blob is decoded as UTF-16LE at both byte parities, case-folded with the
   invariant locale, and searched only for strings the caller already knows: the long file names of
   the folder's shortcuts (first position = Windows order; a name is not matched where it is only the
   tail of a longer pinned name) and packaged AUMIDs, accepted only when `IsPackagedAumid` holds and the
   AUMID is installed (in `options.catalog`, or, without a catalog, confirmed in the Apps folder, at
   most 64 probes). Nothing is invented; a blob over `maxTaskbandBytes` (512 KiB) or a failing read
   degrades to file-name order without an error.

**Seam.** `SelectTaskbandSource`: `None` when `useTaskbandOrder` is false or when `directory` is
overridden without an injected `taskbandReader`; `Injected` with a reader; `Registry` only with the
default directory. Tests therefore never read the user's pin list.

**Result.** Shortcuts the list names come first in Windows order; the rest follow in deterministic
file-name order; installed packaged pins found only in the list are inserted at their position
(`fromTaskband`). `windowsOrder` is true when the list matched at least one shortcut or packaged pin.
File Explorer forms are canonicalized (see "Known applications"), then identities are merged
(`MergeDuplicateApplications`, which keeps the first position). One INFO line reports counts and the
order source only; names, paths, AUMIDs and the blob are never logged.

Supported entries use the catalog's existing filters and identity rules. Optional `options.catalog`
reuses an exact identity or an unambiguous AUMID/executable match with the same arguments; conflicting
AUMIDs, ambiguous profiles and differing arguments do not collapse. Enumeration is bounded at 4096
entries and 512 shortcut files by default; exceeding a bound fails instead of returning an unmarked
partial import. A missing folder has no shortcuts (the pin list can still name packaged pins); an
unreadable folder returns a generic error without user names or paths. `DiscoverTaskbarPins` remains as
the pins-only wrapper.

**Not covered.** Windows Installer (advertised) pinned shortcuts are still dropped (phase 2:
`MsiGetShortcutTargetW`); Internet `.url` files, document links and unsupported shell targets are
skipped; `CloudStore`, Start's `start2.bin` and layout XML are never read (private databases or
provisioning defaults); `TaskbarManager` cannot enumerate pins. Completeness and order cannot be
promised because the list's format is undocumented. Fallbacks: Settings > Dock > Add application and
"Pin to Dock" on a running icon. A live folder and registry watcher is deferred (pins appear at the next
sync).

Primary references checked for this implementation:

- [Microsoft KNOWNFOLDERID reference](https://learn.microsoft.com/en-us/windows/win32/shell/knownfolderid)
  documents `FOLDERID_UserPinned`; its `TaskBar` child is treated as a best-effort legacy convention.
- [Microsoft IShellLink::Resolve reference](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-resolve)
  describes the resolution/search/update behavior omitted by the importer.
- [Microsoft IShellLink::GetIDList](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-ishelllinkw-getidlist)
  and [RegGetValueW](https://learn.microsoft.com/en-us/windows/win32/api/winreg/nf-winreg-reggetvaluew).
- [Microsoft TaskbarManager reference](https://learn.microsoft.com/en-us/uwp/api/windows.ui.shell.taskbarmanager)
  lists pin requests and pin-status checks, without a method that returns every user's pin in order.

## Pin sync policy (`TaskbarPinSync.h`)

`PlanTaskbarPinSync` is the pure, additive policy the app applies to `dock.pinned` (the app runs the
discovery on a worker and applies the plan in one settings update):

- Existing Dock pins are never reordered or removed; Windows pins that are not in the Dock yet are
  appended in Windows order (`append`), up to `maxPins` (64), and what the limit cut is reported.
- Same application (`SameLaunchIdentity`): equal ids ignoring case; else, when both carry an AUMID,
  equal AUMIDs with equal arguments (browser profiles of one executable stay distinct); else equal
  executable ids with equal arguments, so an `exe:` pin and an `aumid:` import of the same program do
  not duplicate.
- `dock.dismissedPins` are never re-added automatically (also matched through the executable or AUMID
  id the import would carry); a manual import (`ignoreDismissed`) reinstates them and lists them in
  `reinstatedIds` so the app removes them from `dismissedPins`.
- The default File Explorer pin (`MakeFileExplorerApplication`) is offered once, at the front, unless
  the Dock or the Windows pins already contain File Explorer or the user dismissed it;
  `markExplorerApplied` sets `dock.explorerDefaultApplied`. At the pin limit nothing is marked, so it is
  offered again once there is room. A manual import never reinstates a dismissed File Explorer default.
- Running the plan again after applying it adds nothing.

## Known applications (`KnownApplications.h`)

File Explorer has one identity in every form: id `aumid:microsoft.windows.explorer`, AUMID and parsing
name `Microsoft.Windows.Explorer`, executable `<system Windows directory>\explorer.exe`
(`GetSystemWindowsDirectoryW`, correct under Terminal Services), no arguments, Desktop kind (Other when
no executable is known). `CanonicalizeFileExplorer` applies to the Explorer AUMID or to the system
`explorer.exe` without an AUMID of its own, never to shortcuts with folder arguments or look-alike
paths. The executable path lets Explorer windows that expose no AUMID group with the pin; the tracker
also gives such windows the canonical identity (`CanonicalizeFileExplorerWindow`), so all File Explorer
windows form one running group. The desktop (`Progman`/`WorkerW`) and taskbar windows of the same
process stay excluded by class. Icons come from the pinned shortcut when there is one, else from the
Apps-folder item.

## Running presentation (`RunningPresentation.h`)

For an unpinned running group, `ResolveRunningPresentation(running, ApplicationIndex)` builds the
`ApplicationInfo` used to launch, pin and draw it, always keeping `running.id` (the running identity and
grouping never change):

- (a) an explicit or packaged window AUMID with an exact catalog match copies that entry's kind, parsing
  name, shortcut, arguments and executable;
- (b) without an AUMID, exactly one catalog entry that starts the same executable without arguments is
  copied the same way (several: never guessed);
- (c) otherwise running facts only (packaged AUMIDs launch through the Apps folder); the window's
  relaunch icon becomes `iconResource` only here, when the AUMID is explicit and the value has the
  supported form;
- (d) under (c), an explicit AUMID whose executable the catalog starts under another AUMID loses its
  executable path, so no bare-executable launch can open another browser profile.

`HasLaunchRoute` is true for a parsing name, a packaged AUMID, a shortcut or an executable without
arguments; the Dock disables "New instance", middle-click and "Pin to Dock" otherwise ("Windows does not
expose how to open this profile"). `IconSourceFingerprint` (FNV-1a 64, 16 hex digits over the
case-folded kind, parsing name, AUMID, shortcut, executable and icon resource) belongs in Dock icon keys
so late presentation metadata (an avatar) refreshes a cached generic icon. The index is pure (lexical
executable comparison, no 8.3 expansion), so it can be built on the UI thread.

## Shell actions (`SystemShellActions.h`)

User-triggered only, on the UI thread. `OpenStartMenu` sends the documented `WM_SYSCOMMAND` /
`SC_TASKLIST` ("Activates the Start menu") through `DefWindowProcW` on a window of the calling thread,
with no synthesized input; with `StartMenuRoute::WindowsKey`, or without a usable window of the calling
thread, it sends `VK_LWIN` through `platform::SendKeyChord`. Windows reports no outcome for
`SC_TASKLIST`, so which route works with the native taskbar hidden is a real-desktop decision for the
app (the route is a parameter). `OpenTaskView` (Win+Tab) and `OpenWindowsSearch` (Win+S) use one key
chord each because Windows documents only the shortcuts. Chords use the platform helper's key-up
recovery; failures return Platform with a user-presentable message. UIPI silently drops synthesized
input while an elevated window is in the foreground. Tests use a fake injector only.
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
cycle through all windows; a lone window already in front is minimized with
`ShowWindowAsync(SW_MINIMIZE)` (`ActivationChoice::minimize`, the Windows taskbar's click behaviour;
before 0.2.0-alpha.1 such a click did nothing). Minimized windows are restored with
`ShowWindowAsync(SW_RESTORE)`. Both are asynchronous, so a hung application cannot block the UI
thread. When `SetForegroundWindow` is refused, the taskbar button is flashed (`FlashWindowEx`,
`FLASHW_TRAY | FLASHW_TIMERNOFG`) and AccessDenied "Windows prevented activation" is returned. No
`AttachThreadInput`, no synthesized input.

Launch log lines name the route, the identity scheme and the error code, never the display name or
the error text: the display name of a running group can be a browser profile name a person chose.
Returned errors still name the application for the user interface.

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
- One property store per window (`ReadWindowShellProperties`, `WindowProperties.h`) gives the AUMID and
  the documented `System.AppUserModel.RelaunchIconResource` and `RelaunchDisplayNameResource` that
  applications declare for their taskbar group (browsers set them per profile). Each group carries
  the values of its most recent window that declares them (`RunningApplication::relaunchIconResource`,
  `relaunchDisplayName`); a change notifies the listener. A plain-text relaunch display name (no
  leading `@`, no control characters, at most 128 units) becomes the group's display name, so profile
  groups of one browser get distinct tooltip and UIA names. The icon resource is kept raw (at most
  1024 units) and parsed only for extraction. The store is never written; no browser file, profile
  database, account store or avatar folder is read; neither value is ever logged.
- Windows of the system's `explorer.exe` that expose no AUMID take the File Explorer identity
  (`KnownApplications.h`), so all File Explorer windows form one group that joins the File Explorer
  pin by id; the desktop and taskbar windows are excluded by class before that.
- `Stop` unhooks, kills the timer, destroys the message window, drops pending name results and empties
  the snapshot (a stopped tracker reports nothing rather than stale, possibly dead windows); idempotent;
  the tracker can be started again, and `Snapshot()` is filled again as soon as `Start` returns.

## Icons (`ShellIconProvider`)

- A declared icon resource (`ApplicationInfo::iconResource`, set by `ResolveRunningPresentation` from a
  window's relaunch icon) is tried first, only in the form `ParseIconResource` accepts: a drive-absolute
  `<path>.ico,0` (the form browsers use for profile icons; executable and DLL resource indexes, `@`
  indirect strings, `ms-resource:`, quotes, UNC, device and NT paths and alternate data streams are
  refused), on a non-network drive (`GetDriveTypeW`), as an ordinary file (no directory, reparse point,
  device, offline or cloud placeholder). It is extracted through the same shell item image factory.
  Measured on build 26300: a synthetic 32x32 32-bpp `.ico` comes back as straight alpha with small value
  shifts (alpha 254 for 255, colour channels within a few units) and is premultiplied like every other
  icon. Any failure falls back to the chain below; the path is never logged.
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

## T17 follow-up: profile icons and a File Explorer pin (0.2.0-alpha.1 status)

**Owner clarification resolved: browser PROFILE avatars and context menus for Dock application
icons.** This concerns separate browser profiles/windows, not signed-in accounts inside web tabs.

Implemented in this module (unit and integration tested with synthetic data only):

1. Window relaunch presentation: `ReadWindowShellProperties` reads the documented
   [RelaunchIconResource](https://learn.microsoft.com/en-us/windows/win32/properties/props-system-appusermodel-relaunchiconresource)
   and RelaunchDisplayNameResource of each window; the tracker carries them per group and uses a
   plain-text relaunch name as the group's display name.
2. `ResolveRunningPresentation` matches an explicit running AUMID exactly (case-insensitively) with the
   catalog, never substitutes a different explicit profile through an executable-only match, never
   guesses an ambiguous one, keeps argument-bearing profiles distinct and clears the executable of an
   unmatched profile so a bare launch cannot open another profile (`HasLaunchRoute` false).
3. `iconResource` extraction for the supported `<path>.ico,0` form, with honest fallback to the generic
   chain, and `IconSourceFingerprint` for Dock icon keys so an avatar that arrives later replaces a
   cached generic icon.
4. File Explorer: one canonical identity (`KnownApplications.h`), the default pin built by the worker-only
   `MakeFileExplorerApplication` (catalog entry preferred, `GetSystemWindowsDirectoryW` +
   `explorer.exe` validated and checked to be an existing regular file, no folder arguments, never
   launched), offered once through `PlanTaskbarPinSync`.

Remaining for other streams: the Dock must build its `ApplicationIndex` from the catalog, use
`ResolveRunningPresentation` for unpinned running items, add `IconSourceFingerprint` to its icon keys and
gate "New instance", middle-click and "Pin to Dock" on `HasLaunchRoute`; the app must wire the pin sync.
The actual browser behaviour (whether a given browser sets these properties, and to what) is a
separately authorized real-desktop check with a sanitized outcome only. No browser account store,
cookies, history, profile configuration, avatar directory or actual owner profile was inspected, and
no runtime discovery, external launch or real profile-icon extraction was performed by the automated
tests. An exact avatar cannot be promised: a browser can stop setting the property, set it only with
several profiles, or point it at a file it has not written yet; the generic icon and the relaunch name
remain. The avatar overlay badge the Windows taskbar shows (`ITaskbarList3::SetOverlayIcon`) cannot be
read by other applications.

## Tests

- Unit (`tests/unit/applications`, 116 cases; 69 before 0.2.0-alpha.1): identity normalization and
  path validation (device, UNC, ADS, 8.3 expansion, no expansion on UNC), packaged/explicit AUMID
  syntax, id preference order, catalog filters, classification, dedup/merge and Start-menu enrichment,
  Apps-folder name hardening and child names, the window eligibility predicate, the window identity
  rule, launch error mapping and window choice (minimize when the lone window is in front, restore,
  cycle), alpha analysis and premultiplication, the LRU cache, icon-provider cancellation and failure
  coalescing. Taskbar import with synthetic temporary shortcuts and injected pin lists only: retained
  launch metadata, deterministic deduplication, unchanged file bytes/write time, profile arguments,
  catalog reconciliation/ambiguity, packaged AUMIDs, skipped files, missing-folder no-creation,
  cancellation/limits, MTA dispatch, Windows order, packaged pins inserted at their list position, a
  packaged pin with its own shortcut kept once, failing and oversized lists degrading to file-name order,
  source selection (directory overrides never read the registry; a reader call counter), and File
  Explorer canonicalization. New suites: `TaskbandScanTests` (both parities, case, order, duplicates,
  truncation, tail-of-longer names, empty/oversized/garbage blobs, packaged AUMIDs confirmed only by the
  caller, malformed identities, bounded starts next to alphanumeric bytes), `TaskbarPinSyncTests`,
  `RunningPresentationTests`, `KnownApplicationsTests` (injected Windows directory, temporary fixture
  file), `IconResourceTests` (parser and window-property sanitizers) and `SystemShellActionsTests` (fake
  injector only; chord order, the extended Windows key, key-up recovery, the Start fallback without a
  usable window; the `SC_TASKLIST` route is never exercised because it would reach the real shell).
- Integration (`tests/integration/applications`, 20 cases, real desktop, nothing launched): discovery
  (>10 apps with Packaged and Desktop entries, count, duration and log line noted; MTA caller;
  cancellation), every discovered Apps-folder entry resolving to its shell item, icon extraction for 3
  discovered apps, the pixel-format survey, request coalescing, cache hits and `CancelAll`, launcher
  failure paths, and the tracker with a pumping message loop (its notes now carry counts, id schemes and
  relaunch-property counts only, never titles, names or ids). New: window properties set on, read back
  from and cleared on a never-shown window of the test (an `@` name is dropped; a destroyed window is
  rejected); a synthetic 32x32 `.ico` in a temporary folder extracted through `iconResource` (colour and
  premultiplied alpha within a measured tolerance) and unusable resources falling back to NotFound; a
  temporary `.lnk` whose ID list points to the Apps-folder File Explorer item imported as the canonical
  File Explorer pin with its bytes and write time unchanged.