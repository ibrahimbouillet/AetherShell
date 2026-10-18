# Using Aether Shell

Start Aether Shell from its Start Menu shortcut or installed launcher. It opens two desktop
surfaces: the Dock near the bottom and the Top Status Bar near the top. Windows Explorer continues
to run normally. Right-click either surface for its available settings and commands; the Top Bar
menu also offers Quit.

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

Settings has Dock, Top Bar, Weather, Updates, General and About pages. Weather starts disabled.
Choose a location explicitly before enabling it; searching sends only the entered place name to
Open-Meteo, and forecasts send the chosen coordinates. Cached forecasts disappear after six hours.
Data attribution: Open-Meteo.com, CC BY 4.0.

General contains startup consent, motion preferences and shortcut editing. Startup is per-user
and optional. Task Manager can independently disable it. High contrast and reduced motion follow
Windows where supported. The About page contains the MIT license and project attribution.

Local data lives under `%LOCALAPPDATA%\AetherShell`: settings, cache, updates and bounded logs.
Open the log folder from General. Basic diagnostic export contains the product version and Windows
build; review any files yourself before sharing them. Reset defaults asks for confirmation and
keeps a settings backup. Uninstall keeps user data unless removal is explicitly selected.

See [known limitations](KNOWN_LIMITATIONS.md) for current qualification gaps.
