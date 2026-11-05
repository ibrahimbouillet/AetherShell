# Liquid Glass: the welcome bubble (prototype)

Alpha 4 starts from one idea (D62): Aether Shell is a bubble of water, and everything it shows belongs to that
bubble. `welcome-bubble.html` is the first piece, the arrival after the greeting, as a prototype **outside the
product**: the owner judges the motion and the material here before anything enters the shell or the setup.

Open `welcome-bubble.html` in Chrome, Edge or Firefox (double-click). It needs nothing else: no network, no
installation. **Picture…** (or a picture dropped on the page) puts your own wallpaper behind it.

| Key | Action |
| --- | --- |
| Space | Replay |
| P | Pause or play |
| ← → | Step 0.05 s |
| 1 2 3 4 | Speed 0.5×, 1×, 1.5×, 2× |
| F | Full screen |
| H | Hide or show the controls (they also step aside while it plays) |

`welcome-bubble.html?t=1.97` shows one moment, still, to talk about a single frame.
`?picture=<path>` uses a picture served beside the page.

## What happens (4.2 s at 1×)

| Time | Phase | |
| --- | --- | --- |
| 0 – 0.45 s | appear | A point becomes a drop with the Aether mark inside; the welcome cover clears to the desktop. |
| 0.45 – 1.25 s | float | Weightless water: the drop drifts and its surface rings slowly. |
| 1.25 – 1.94 s | divide | It swells, draws a neck that thins and snaps; a tiny satellite drop falls into the lower one. |
| 1.94 – 2.25 s | travel | One drop shoots up, the other down, drawn out by their speed. |
| 2.2 – 3.15 s | spread | Each strikes its edge, flattens, bounces and spreads: the thin Top Bar above, the Dock below, at the shell's default sizes. Water turns into frosted glass. |
| 3.05 – 4.1 s | contents | The bars' contents come in (placeholders for the shell's own). |

## The material

The water has **no colour of its own, and never blue**: it shows only what is behind it (owner, D62). As a real
ball of water it turns the picture over and takes in a wide view of it; white light only, from above (a sharp
highlight and a sheen at the top, the light gathered at the bottom of its inner rim, a bright edge), a soft
shadow, and a fine grain. The mark inside has no colour either: it is a lighter, frosted shape in the water.
The bars end as Liquid Glass: frosted, bending the picture at their edges.

## Porting, once validated

- **Motion:** `stateAt(t)` is a pure function of time built from damped springs (`spring`, `ring`); it moves to
  C++ beside `WelcomeController` as it is, and its constants are the future Settings (speed, off, detail).
- **Material:** one fragment shader over a signed distance field (`body`, `smin`, `field`) becomes a Direct2D
  custom pixel-shader effect (HLSL) or a Composition effect; the picture behind is the welcome's own backdrop.
- Geometry comes from the shell's defaults (Top Bar 32 DIPs high, 6 from the top; Dock of 40-DIP icons, 4 from
  the bottom), so it lands where the real bars are; in the shell it will read the person's settings.

The web page is a design tool only: no web technology enters the product (AGENTS, "Non-negotiable constraints").
