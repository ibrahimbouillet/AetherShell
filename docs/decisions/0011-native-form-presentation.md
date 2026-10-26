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

## Amendment — coherent movement and the integrated title strip (D43, 2026-10-26)

Status: implemented by Claude; automated and own-window moving-frame evidence in TEST_STATUS S-052.
Owner acceptance of the moving interface remains pending.

**Context.** The owner saw scrolled Settings content break apart and reassemble on release, and a
caption strip of a different material above the glass client. An own-window frame sequence
reproduced the first: children repainted themselves at different moments while the canvas moved,
and the opaque layered editors (composed by DWM apart from the form surface) could reach the screen
one frame before or after it.

**Decision.**
- Moving containers present atomically: one buffered render of the container and all registered
  descendants at the destination position, the move, then one blit right behind it; everything
  drawn is validated so no child repaints a stale piece afterward. Hover/fade tweens inside a
  scrolling page present through the outermost registered container.
- Layered editors stay live native controls. Their private images are kept current underneath them
  at rest; during motion the scene draws the images and the editors stay in place, focusable and
  hit-testable, at the smallest nonzero opacity, with one identical frame held at each transition.
  Any input reveals an editor immediately. Painted placeholders never replace an editor at rest.
- Acrylic forms keep retained material/content layers and per-control visual caches so a scroll
  frame renders only exposed or changed regions. Wheel notches glide on a critically damped spring;
  continuous touchpad input moves immediately.
- Settings removes the standard caption over Acrylic (`WM_NCCALCSIZE`), keeps the side/bottom resize
  borders, lets DWM draw and hit-test the caption buttons (`DwmDefWindowProc`) and answers the strip,
  top edge and mark through `WM_NCHITTEST`. Solid material colors the standard caption with the
  window's own colors; Native keeps the system caption.

**Alternatives rejected.** Hiding editor windows during motion (`SW_HIDE` drops focus, IME and their
UIA presence);
replacing editors with painted fields (violates NFR-007); a hardware DC render target (measured no
better and added a creation hitch); a fully custom-drawn frame without DWM's buttons (loses Snap
layouts and native caption behavior).

**Limits.** The editor masking is a presentation technique for continuous motion only; an editor
that changes while masked is re-imaged before it is shown. Retained layers apply to the Acrylic
material; Solid/Native render complete frames through the same atomic path. Measurements are from one
Windows 11 configuration (150 %) and describe UI-thread cost, not compositor latency.
