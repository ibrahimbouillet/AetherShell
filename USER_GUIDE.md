# Using Aether Shell

Start Aether Shell from its Start Menu shortcut or installed launcher. It opens two desktop
surfaces: the Dock near the bottom and the Top Status Bar near the top. Windows Explorer continues
to run normally. Dock items have context menus. Select the Top Bar's logo to open its menu, which
also offers Settings and Quit.

Open Search from the Dock or with Ctrl+Alt+Space. Type an application name, use Up/Down to select,
and Enter to open it. Escape clears the query, then closes the search window. Only discovered
application entries can be launched; typed text is never a shell command. Ctrl+Alt+D focuses the
Dock. Both shortcuts are configurable in Settings → General; conflicts are reported there.

The Dock distinguishes pinned apps from other running apps. Use its context menu or Settings →
Dock to pin/unpin/reorder entries and adjust size, magnification and motion. A running app can be
activated from its Dock item. Some UWP window attribution still needs qualification.
Alpha 3: hovering a running app widens its marker and, after a short pause, shows a card with
previews of its windows. Click a preview to switch to that window, or its close button to ask that
one window to close normally; the app decides whether to close (for example after asking to save).

The Top Bar displays snapshots from documented Windows APIs. Unknown status remains unknown.
A desktop PC without a battery can hide the battery item. A microphone-in-use indicator is
unsupported; device availability/mute information is separate. Click available audio controls to
change output volume or mute only when you intend to do so. Network details never show an SSID.
Alpha 2 adds input-language switching through Win+Space from the keyboard item and Weather settings
from its visible weather item. The grouped network/audio/battery action opens Aether's quick controls,
including media transport when Windows provides an active session, volume, supported brightness and
system-action tiles. Unsupported toggles open the relevant Windows controls rather than pretending
to change a device. Live device qualification remains limited. Microphone click controls endpoint
mute; its right-click menu offers privacy settings and **Start Windows voice typing (Win+H)**.
The voice action returns focus to the previous application before asking Windows to open dictation
in its active field. Windows controls dictation availability, microphone permission and processing;
Aether Shell does not capture or store speech. Automated tests do not invoke real dictation.

Alpha 3 adds three Top Bar entry points. Click the date and time to open the Windows notification
center (Win+N on Windows 11, Win+A on Windows 10); its menu also opens the calendar. Aether never
reads, clears or counts your notifications. The small chevron opens Windows' own hidden
notification-area icons directly below it; the arrow points up while they are open, and a second
click closes them. Hide the chevron with Settings → Top Bar → **Show hidden icons button**. The
grouped network/audio/battery action opens Aether's Quick Settings panel, with a second page and
detail views for Wi-Fi, Bluetooth, accessibility and sound output; switches that Windows keeps to
itself open the matching Windows Settings page. On Windows 11, **Use Windows Quick Settings
(Windows 11)** on the same page opens Windows' own panel instead.

Settings has Dock, Top Bar, Weather, Updates, General, About, Desktop and Appearance pages. Weather
starts disabled. Choose automatic Windows location with consent or a manual location before enabling
it. Windows may deny location, in which case no location/weather is invented. Manual place search
sends the entered place name to Open-Meteo; forecasts send the chosen coarse coordinates. It does
not infer location from a Wi-Fi name or raw IP. Cached forecasts disappear after six hours.
Data attribution: Open-Meteo.com, CC BY 4.0.

General contains startup consent, motion preferences and shortcut editing. Fresh interactive setup
(since Alpha 2) visibly selects sign-in startup; clear that option before installation to opt out. Existing
choices are preserved. Silent setup requires the explicit startup flag. Startup is per-user and
optional; Settings → General → Apply can change it, and Task Manager can independently disable it.
Windows controls the launch order and delay after sign-in; it may show the native desktop/taskbar
before Aether is ready. The installed stable launcher retries a bounded number of abnormal Windows
exception crashes. Intentional Quit/update/uninstall and ordinary errors stop it; no process can be
guaranteed to run through every failure. High contrast and reduced motion follow
Windows where supported. The About page contains the MIT license and project attribution.

In the V2 candidate, Desktop contains **Replace the Windows taskbar**, enabled by default (D23).
An explicit saved opt-out is preserved. Unselect it and press Apply to restore coexistence; select
it and press Apply to enable replacement again. Closing/navigating away discards an unapplied change.
In replacement mode the Dock reserves space below maximized applications. Ordinary manually placed
windows may still need moving. The status message shows whether replacement is active or unavailable.
Windows Settings retains its native taskbar page; change Aether Shell's behavior here.
In Alpha 3 replacement mode, eligible maximized windows get rounded corners and a thin rim, without
changing their size or maximize state; Aether undoes this when it exits. Some applications and
window types keep square corners.

**Auto-Hide** (Alpha 3) is set separately for each bar: Settings → Dock → **Automatically hide the
Dock** and Settings → Top Bar → **Automatically hide the Top Bar**. Both are off by default. An
auto-hiding bar slides away while an application window covers it. Rest the pointer on the bottom
(Dock) or top (Top Bar) edge of the screen to bring it back. It stays while you use it or one of its
menus or panels is open, and hides again shortly after. On the desktop, with no window over it, the
bar stays visible. An auto-hiding bar reserves no screen space, so maximized windows fill the screen
and the revealed bar appears over them, like Windows' own auto-hide taskbar.

Desktop also offers Import Windows taskbar pins. It reads available supported pinned shortcuts and
adds missing entries without changing Windows pins or replacing your Aether pins. Some Windows 11
pins and their original order are unavailable. Replacement mode imports supported pins and supplies
File Explorer once. Automatic synchronization adds new available pins on startup, enabling sync or
Explorer restart; it preserves user order and explicit removals. Manual import can reinstate removed
pins. Appearance contains the automatic-sync switch, theme/accent, icon presets and motion controls.

The candidate Dock has Start, Search and Task View entries. Drag pinned icons to change their saved
order; dragging a running-only icon changes this session's order without pinning it. Supported
per-window profile icon metadata can distinguish Chrome profiles; missing metadata remains generic.
Dock context menus offer applicable window/new-instance/pin actions without guessing a profile.

Top Bar menus expose clock, power, sound and network settings. The visible trailing native-taskbar
arrow was removed for Alpha 2; since Alpha 3 the hidden-icons chevron gives access to Windows' own
notification-area overflow instead. Recovery remains available through Desktop settings, Quit and the
standalone recovery command. The compatibility suspend/resume path remains internal.

To restore the native bar, disable the Desktop option or choose Quit from the Top Bar menu.

From the installation folder, `AetherShell.exe --restore-taskbar` asks a running instance to disable
the mode. When Aether Shell is not running, the same command restores the Windows taskbar itself:
- it uses the state saved before the taskbar was hidden;
- it turns the mode off;
- it shows the result.

`AetherShell.exe --quit` asks a running instance to exit and does nothing otherwise.

A separate recovery helper restores the taskbar after an application failure, and the application
does the same if its helper exits. **Repair** or **Uninstall** in Windows Settings > Apps > Installed
apps also restore a taskbar that Aether Shell left hidden. Repair keeps your settings, including the
Desktop choice, so Aether Shell hides the taskbar again on its next start unless you turn the option
off. If none of this works, end Aether Shell's processes in Task Manager and restart Windows Explorer.
Recovery across all failures and Windows versions is not yet qualified; see the known limitations
before enabling this mode.

Local data lives under `%LOCALAPPDATA%\AetherShell`: settings, cache, updates and bounded logs.
Open the log folder from General. Basic diagnostic export contains the product version and Windows
build; review any files yourself before sharing them. Reset defaults asks for confirmation and
keeps a settings backup. Uninstall keeps user data unless removal is explicitly selected.

See [known limitations](KNOWN_LIMITATIONS.md) for current qualification gaps.
