# ADR 0013: Transient authenticated-session welcome

Status: owner-requested scope accepted under D51 (2026-10-28). Implemented in source (S-061, D53) and
redirected by the owner after a first trial (D54). Real sign-in, the cover at a real lock, physical touch
and the owner's acceptance of the result are still pending.
Requirements: FR-040/FR-041; T51/T52. Detailed acceptance is in PROJECT_SPEC and the V4 work order.

## Context

The owner wants a personalized FR/EN handwritten welcome at sign-in and unlock, using the time to
prepare Aether's essential surfaces. Existing constraints protect Windows authentication, disclosed
per-user startup, intentional Quit and exactly two persistent desktop surfaces.

## Decision

Permit one optional, short-lived supporting welcome on the authenticated user's normal desktop.
Do not draw over the secure desktop, change credentials/Winlogon, add elevation/services or claim
pre-authentication rendering. Keep disclosed D34 startup and Windows/Task Manager choices intact.
Current-session lock/unlock/desktop readiness and first successful essential presents inform the
lifecycle. Registration/initialization failure must degrade safely, with bounded asynchronous work.

An optional display name is entered locally after its purpose is explained; skip/edit/delete remain
available. No account or account-database scraping is needed; never transmit/log/publish the name.
The writing uses original vector strokes and supports real variable-name layout and readable
fallback. It is skippable by touch, mouse and Escape; reduced motion/high contrast are mandatory.

Welcome lifetime has an independent hard deadline and immediate cancellation on re-lock. Optional
providers cannot hold the desktop covered. Unlock neither restarts an already running shell nor
reverses intentional Quit. Two persistent surfaces remain the product constraint; this feature
adds no third persistent bar and no new platform target.

## Consequences

The owner-visible goal is authorized; the exact renderer, defaults, timing bounds and shared touch
implementation still require documented engineering choices and measured validation. Tests must
cover session duplication, failure/cancellation, first-frame readiness, privacy and physical input.
Source tests do not establish reboot timing or owner acceptance of the handwriting/motion.

## Engineering choices made (D53)

- Strokes are authored by hand in `tools/handwriting/strokes.py`, which generates the C++ table. Letters
  join; dots, crossbars and accents follow when a word is finished. Accented Latin is decomposed.
- Timing comes from the line's shape: slower through curves and at stroke ends, slightly uneven, thicker
  when slow, with real pen lifts. Writing takes 3.6-6.2 s whatever the name's length (D54).
- Readiness is a first successful present of each bar meant to be on screen. Optional providers play no
  part. The hard deadline is 12 s (D54), on a UI-thread timer: a hung UI thread is outside it (Windows' own
  hung-window handling applies there).
- Reduced motion and high contrast show the finished words for 3 s, without a pen.
- Skip: any touch or click, or Escape through a temporary hotkey, so it never needs focus.
- A crash restart by the supervisor does not count as a sign-in. Sign-in means `--autostart`.

## Owner direction after the first trial (D54)

The greeting covers the whole primary monitor with the person's own desktop picture, blurred and opaque. To
avoid showing the desktop first, that background goes up when the session locks and waits behind Windows'
lock screen: this stays on the normal desktop, draws nothing over the secure desktop and changes nothing
about authentication. Windows' own report of the session state independently removes a cover that a missed
notification would otherwise leave up, and any click or touch removes it. At sign-in the greeting can appear no
earlier than Windows starts Aether.

D56 adds that the bars stay off the screen until it ends, and gives the same paths a calligrapher's pen, as large
as the screen allows.
