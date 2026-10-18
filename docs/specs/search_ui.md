<!-- Generated from the session's agent workflow; edit freely. -->
# Module specification: search_ui (phase B)

Read `AGENTS.md` first; it states the rules every implementer follows. This is the work order that
was given to the implementing engineer. The real headers in the repository are authoritative where
they differ from names used here.

Module: search_ui (directory src/search/ui, target aether_search_ui; deps aether_ui aether_accessibility aether_search aether_applications aether_graphics aether_platform aether_diagnostics aether_core user32 comctl32).
Your files: src/search/ui/**, tests/unit/search_ui/**, tests/integration/search_ui/**, docs/modules/search_ui.md.
SearchWindow : ui::SurfaceWindow (takes keyboard focus while open — noActivate=false; toolWindow; topmost):
- Centred horizontally ~22 % from the top of the monitor containing the cursor, ~640 DIP wide, glass with tokens.popupCornerRadius and blur when available; open/close fade + 8 DIP rise (~160 ms; instant under reduced motion).
- Text input: a real EDIT child control kept invisible (children do not render on WS_EX_NOREDIRECTIONBITMAP windows) that owns the text, caret and selection (EM_GETSEL), IME and screen-reader semantics (accessible name "Search applications"); subclass it for Up/Down/PageUp/PageDown/Enter/Escape/Tab. Render text, selection and caret with DirectWrite (GetCaretBlinkTime; stop when hidden), placeholder "Search applications", original vector magnifier glyph.
- Results from the SearchService on every edit change, up to 8 rows: icon (IIconProvider -> BitmapCache, fallback tile), name with highlight ranges, secondary line; keyboard selection, hover, click; Enter launches via IApplicationLauncher::LaunchAsync and closes; Escape clears, second Escape closes; deactivation closes. States: "Indexing applications..." while building, "No applications match", hint when empty. Launch errors inline, never modal.
- Never executes typed text: only ApplicationInfo entries from the index.
- Accessibility: accessibility::AccessibleSurface for results (ListItem with names and positions), focus events on selection changes.
Public API: Create(context with GraphicsDevice&, FrameScheduler&, IUiDispatcher&, SearchService&, IApplicationLauncher&, IIconProvider&), Toggle(), Open(), Close(), IsOpen(), SetTheme(...).
Tests: unit tests for row layout and highlight segmentation; integration test with ~30 fake apps in a real SearchService and a fake launcher: open over a test-pattern backdrop, set text via WM_SETTEXT on the EDIT, send keys, verify ordering/selection/launch callback, capture %TEMP%\AetherShellTests\search-*.png and LOOK; close the window at the end.
