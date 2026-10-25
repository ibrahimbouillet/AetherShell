# ADR 0011: Native form presentation and glass popup menus

Status: implemented decision under D41/FR-037 (D42, Claude, continued/reviewed by Codex,
2026-10-25). Independent implementation review completed; owner visual acceptance remains pending.
It changes no product requirement or owner-approved objective. Executed gate details are in S-049.

## Context
The owner rejected the stock Settings form (white client, grey label strips, blue list selection, raw
values) and the opaque owner-drawn HMENU, and asked for one coherent neutral-glass presentation across
every Aether-owned interface, the setup wizard included (D41). Two constraints shape the design:
NFR-007 requires standard controls with native editing, IME and accessibility for supporting dialogs,
and GDI child windows draw no usable pixels inside a `WS_EX_NOREDIRECTIONBITMAP` composition host, while
GDI text written onto a DWM-extended glass client has no alpha and shows as holes or additive light.

## Decision
- **`ui_form` module** for activatable application windows. The window stays a normal redirected
  top-level window (caption, snap, Alt+Tab, IME). Material: DWM system Acrylic
  (`DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_TRANSIENTWINDOW`, frame extended over the client) on Windows 11
  22H2+ with transparency on; an opaque fill otherwise; stock system-colored painting in high contrast
  or without Direct2D.
- **One scene, painted per window.** The client keeps `WS_CLIPCHILDREN`; the form and every registered
  control paint their region of the same scene (tint, grain anchored at the form origin, cards,
  dividers, decorations, control visuals, focus rings) through buffered paint and one premultiplied
  Direct2D DC render target, so child controls blend with the glass. Native controls keep their classes,
  messages and system UI Automation providers; only painting changes.
- **Opaque native editors.** EDIT and HOTKEY controls, inset lists and documents become opaque layered
  child windows inside drawn rounded fields; their caret, IME, selection and clipboard are untouched.
- **Scrolling regions** move a canvas child inside a clipping viewport and expose the UIA Scroll pattern.
- **Glass popups.** `GlassMenuPresenter` implements `IMenuPresenter` on composition `SurfaceWindow`
  levels with the TrackPopupMenu contract (modal, ids, foreground rule, keyboard taken by the presenter's
  nested loop, outside clicks consumed) and is used for the logo menu, Dock menus and Settings dropdowns.
  High contrast and failures fall back to the owner-drawn or native HMENU presenters. Native combo
  dropdowns retain their Selection/ExpandCollapse provider; replacing them with glass MenuItems
  without equivalent list semantics was rejected during Codex's review.
- **`MessageLoop::RunNested`** keeps the frame timer and dispatcher waits alive inside modal loops.
- **Graphics caches** keep up to four Direct2D devices per thread so DC render targets and the shared
  composition device do not evict each other's shadows or grain.

## Alternatives rejected
Compositor-only Settings with GDI children (airspace/redirection); recoloring stock controls (no real
backdrop, still stock geometry); painted text fields with mouse handlers (violates NFR-007); a
Settings-only menu (no shared component); Mica (samples only the wallpaper, not the scene behind).

## Consequences and limits
Windows draws an inactive window's Acrylic as its flat fallback. A combo box opens its native list
with field colors; a custom glass dropdown remains deferred until equivalent accessibility exists.
The HOTKEY control's drawing is replaced because it ignores
`WM_CTLCOLOR*`; its input stays native. Setup and the updater link graphics/ui_form but depend on no
running Aether instance, user profile or downloaded resource. Pre-renderer emergency dialogs remain
`MessageBoxW` by design. Physical DPI transitions, screen-reader walkthroughs and owner acceptance
remain open (see TEST_STATUS and the [surface inventory](../specs/v3-visual-unification-inventory.md)).
