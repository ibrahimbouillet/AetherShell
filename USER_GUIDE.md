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

The Top Bar displays snapshots from documented Windows APIs. Unknown status remains unknown.
A desktop PC without a battery can hide the battery item. A microphone-in-use indicator is
unsupported; device availability/mute information is separate. Click available audio controls to
change output volume or mute only when you intend to do so. Network details never show an SSID.
The current V2 candidate adds input-language switching through Win+Space from the keyboard item,
access to Windows network settings from the network item, and access to Weather settings from its
visible weather item. These new routes are being qualified in T17. Microphone click controls endpoint
mute; its right-click menu offers privacy settings. It does not start recording or dictation.

Settings has Dock, Top Bar, Weather, Updates, General, About and Desktop pages. Weather starts disabled.
Choose a location explicitly before enabling it; searching sends only the entered place name to
Open-Meteo, and forecasts send the chosen coordinates. Cached forecasts disappear after six hours.
Data attribution: Open-Meteo.com, CC BY 4.0.

General contains startup consent, motion preferences and shortcut editing. Startup is per-user
and optional. Task Manager can independently disable it. High contrast and reduced motion follow
Windows where supported. The About page contains the MIT license and project attribution.

In the V2 candidate, Desktop contains **Replace the Windows taskbar**. Select it and press Apply to
consent; unselect it and press Apply to restore coexistence. Checking the box and closing/navigating
away does not apply that choice. The option starts off; saved consent can resume on later launches.
In replacement mode the Dock reserves space below maximized applications. Ordinary manually placed
windows may still need moving. The status message shows whether replacement is active or unavailable.
Windows Settings retains its native taskbar page; change Aether Shell's behavior here.

Desktop also offers Import Windows taskbar pins. It reads available supported pinned shortcuts and
adds missing entries without changing Windows pins or replacing your Aether pins. Some Windows 11
pins and their original order are unavailable. An empty Dock can import available shortcuts
automatically after replacement is selected. Use the Dock context menu to adjust entries afterward.

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
