# ADR 0014: Rounded maximized windows without touching the application

Status: implemented decision under D72 (Claude, 2026-11-10), at the owner's instruction to solve the rounded
corners "for every page" and the faults that came with drawing them. It replaces the mechanism of
[ADR 0012](0012-maximized-window-perimeter.md) for other applications' windows; that ADR's part about
Aether's own Settings window stands. Executed evidence: TEST_STATUS S-074. In test build 4.6; the owner's
judgement on real applications is pending.

## Context

Since D40 the guard rounded a maximized application by writing to that application's window from another
process: a rounded region (`SetWindowRgn`), a changed DWM material, two window properties, and a probe
message to see whether it answered. Everything the owner reported as unsolved traces back to those writes:

- **Not every window could be served.** Writing to a window needs the right to: Explorer, elevated and
  protected applications, packaged applications and anything with a region of its own were left square by
  rule. A browser re-asserts its own rectangle and material, so the guard and the application took the
  shape from each other (D46, D56 added retries and budgets, never an end to it).
- **Applications were changed.** A region removes the caption buttons Windows draws (measured, ADR 0012),
  makes DWM drop a window's material outside the clip (hence the material lease), and stays on the window
  if the guard dies before putting it back (AS4-05): a window restored afterwards is still cut to its
  maximized outline, which is one way a window "disappears". `SetWindowRgn` sends the target synchronous
  messages, so a page whose thread was busy could hold the guard or receive a write half-way (AS4-03).
  Blank pages (AS4-01) were never reproduced, but every correction of that audit was a way of writing more
  carefully into someone else's window.
- **Its own tests failed.** The corner pixel case had been failing in the native suite since S-067.

## Decision

**The guard never writes to another application's window.** It reads what Windows reports about top-level
windows (visible, maximized, minimized, cloaked, extended styles, bounds, DWM's visible frame, the kind and
box of a window region) and draws with two click-through windows of its own, kept directly above the front
maximized window of each monitor:

- **Corner pieces** (`AetherShell.WindowCornerGuard.Corners`): the pixels outside the rounded outline, in
  the four corners. Each rectangle of them shows the desktop that lies at its own place through a DWM
  thumbnail of the desktop window at natural size: live, pixel for pixel, including a picture that changes.
  DWM draws a thumbnail over its whole rectangle whatever the shape of the window that holds it (measured),
  so there is one thumbnail per rectangle of the shape, about ten per corner.
- **Rim** (`AetherShell.WindowCornerGuard.Rim`): the band one DIP wide just inside the outline, in the
  neutral color ADR 0012 chose.

Both are `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` popups without owner:
input goes through them, they are never activated and appear neither in the taskbar nor in Alt+Tab. They
are stacked directly above their target, which is itself never moved or restacked; they are always-on-top
only when the target is. Whatever is in front of the target is therefore in front of them, with nothing to
compute.

Which window: the first, front to back, that is visible, genuinely maximized (`IsZoomed`), top-level, not
cloaked, not a tool or click-through window, not covering its whole monitor (D56), and without a shape of
its own. A plain rectangular region that still shows the whole frame is no shape (a browser's). Windows of
the `AetherShell.` family are left alone: Settings rounds itself, and test fixtures are not applications.
Nothing is asked about the process: no handle to it is opened, so privilege and packaging do not matter.

The outline and the two shapes are pure functions (`ClassifyCornerWindow`, `CornerFrameFor`,
`CornerPieceRects`, `CornerRimRects`): the pieces and the rim tile the outline's edge exactly, no pixel in
both and none between.

What makes the guard look: a window coming to the front, top-level windows changing order, and show, hide,
move, minimize or cloak of the target or of a maximized window; display, work-area and theme broadcasts;
Explorer starting again. However many events arrive together, the windows are looked at once. While it
shows something it also looks every two seconds; with nothing maximized it has no timer. An application
that keeps putting itself above the guard's windows is not fought: after twelve restackings in two seconds
the guard leaves that monitor alone for two seconds.

**Clips left by earlier versions.** A guard of an earlier version that was killed left its region on the
windows it had clipped. At start, once, the guard removes the region and the marks of a window whose
marking guard process is gone, on a thread of its own so that a window that does not answer holds nothing
up. A window whose guard still runs is left to it.

The separate child process, its inherited handles, its supervision and the D56 rule are unchanged. The
lease, the backend interface, the material lease, the retries and their tests are removed.

## Alternatives rejected

Keeping the region and correcting it further (each correction left the cause in place); making the
guard's windows owned by the application (joins the two threads' input, so a hung application could hold
the guard, and gives the application an owned window it does not know); always-on-top pieces with a
computed "is something in front" test (draws over whatever comes in front until the guard notices);
painting the corners from the decoded desktop picture (not live, and wrong for any fit other than Fill);
one thumbnail per corner clipped by the window's region (DWM ignores the region: square corners,
measured).

## Consequences and limits

- Every kind of maximized window between the bars is rounded: browsers, Electron, Explorer, packaged,
  elevated. Applications keep their caption buttons, material, placement and input, because nothing of
  theirs is changed. If the guard stops or dies its windows go with it, and nothing is left behind.
- **The corners show the desktop, not whatever is behind the window.** Behind a maximized window that is
  the desktop, or another maximized window rounded the same way. A window that is not maximized and lies
  behind exactly at a corner is not shown there: a few pixels differ from a true hole.
- The rim covers the outermost DIP of the application instead of a band freed by a clip. Clicks go through
  it.
- Corners and rim are still not antialiased. Thumbnails carry an opacity, so a later version can blend the
  edge pixels; not done here.
- Between a maximized window coming to the front and the guard restacking its windows there can be one
  composed frame with square corners (measured: the guard follows within 1 to 5 ms).
- Where a live-wallpaper program has moved the desktop's icons into a window of their own, the picture is
  taken from the window right behind that one; that arrangement was not available to test.
- The ring color still follows the system app theme. Physical multi-monitor, 192 DPI and Windows 10 are
  not observed.
