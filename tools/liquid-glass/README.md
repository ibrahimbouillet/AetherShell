# Aether's styles and the arrival (prototype)

Alpha 4 starts from one idea (D62): Aether Shell is a bubble, and everything it shows belongs to it. It comes in
two styles (D63), chosen in Settings; the whole application follows the choice, Settings included:

- **Liquid**: pure water. It bends the picture behind it by itself and gathers the light into one bright place.
  No semi-transparency, no blur, no grain, and no transparency setting for now. Icons sit on the water; what passes
  between them and the water shows on it: an icon's shadow, the red light of an application that asks for attention.
- **Grain**: Aether's frosted material: the picture blurred behind a semi-transparent neutral shade, with a grain.

`welcome-bubble.html` is the arrival after the greeting, in both styles, as a prototype **outside the product**:
the owner judges it here before anything enters the shell or the setup. Open it in Chrome, Edge or Firefox
(double-click); it needs no network and no installation. **Picture…** (or a picture dropped on the page) puts your
own wallpaper behind it. Moving the pointer shows the settings: choosing **Liquid** or **Grain** plays the arrival in it.

| Key | Action |
| --- | --- |
| L / G | Liquid / Grain, and play the arrival |
| Space | Replay |
| P | Pause or play |
| ← → | Step 0.05 s |
| 1 2 3 4 | Speed 0.5×, 1×, 1.5×, 2× |
| F | Full screen |
| H | Hide or show the settings |

`?t=1.9` shows one moment, still; `?style=grain` chooses the style; `?picture=<path>` uses a picture served beside
the page.

## What happens (4 s at 1×)

| Time | Phase | |
| --- | --- | --- |
| 0 – 0.55 s | appear | A point becomes a drop with the Aether mark inside, with one soft overshoot; the welcome cover clears. |
| 0.55 – 1.3 s | float | Weightless: it swings slowly between long and wide and drifts a little, then grows calm. |
| 1.3 – 1.9 s | divide | It swells; its middle narrows into a bridge that holds, thins, and lets go. Only two drops: nothing is left between them. |
| 1.9 – 2.3 s | travel | One drop rises, the other falls, faster and faster, drawn out by their speed. |
| 2.3 – 3.0 s | spread | Each strikes its edge, flattens, bounces a little and spreads: the thin Top Bar above, the Dock below, at the shell's default sizes. |
| 3.0 – 3.8 s | contents | The bars' contents come in (placeholders for the shell's own). |

## The materials

**Liquid:** the water has no colour of its own, never blue: only the picture behind it, read once (no colour
separation). As a ball of water it turns the picture over and takes in a wide view of it; as a bar it bends it at
its edges. White light only: a highlight and a sheen at the top left, the light's focus on the far side, a bright
edge, and a soft shadow. The mark inside is a lighter, frosted shape.

**Grain:** the picture blurred, under a 42 % neutral shade, with a fixed grain and a hairline edge. No lens, no light.

## Fluidity

Most of the screen is only the picture: the shader computes the material within 36 pixels of a shape and nothing
more. Time is real time: a slow frame skips ahead rather than slowing the motion. Once the arrival has ended, the
page draws nothing.

## Porting, once validated

- **Motion:** `stateAt(t)` is a pure function of time built from damped springs (`spring`, `ring`), with continuous
  position and speed; it moves to C++ beside `WelcomeController`, and its constants become the future Settings
  (speed, off, detail).
- **Materials:** one fragment shader over a signed distance field (`body`, `smin`, `field`), one branch per style,
  becomes a Direct2D custom pixel-shader effect (HLSL) or a Composition effect.
- Geometry comes from the shell's defaults (Top Bar 32 DIPs high, 6 from the top; Dock of 40-DIP icons, 4 from the
  bottom); in the shell it will read the person's settings.

The web page is a design tool only: no web technology enters the product (AGENTS, "Non-negotiable constraints").
