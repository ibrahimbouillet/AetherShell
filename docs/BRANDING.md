# Aether Shell branding

This development identity uses two original counter-flowing air ribbons: an open upper current and
a returning lower current. Geometry is defined once in `resources/branding/tools/render_logo.py`
and generates both cubic SVG and antialiased PNG/ICO assets. It is an implementation proposal for
the first alpha, not a claim of final owner visual approval (PROJECT_SPEC Q-002).

The palette follows the already implemented graphics tokens: indigo `#5B6CFF`, cyan `#3DD9E8`, dark
neutral `#0B1020`/`#11172A`, light neutral `#F1F5FF`. Keep at least 15% mark-width clear space. Use the
pixel-hinted variant at 16–24 px; do not stretch, rotate or add outlines/shadows to the symbol.
Use the white mono version over a contrasting dark surface or adapt SVG currentColor for contrast.

Inventory: mark SVG/mono SVG, horizontal light/dark SVG/PNG, twelve transparent mark sizes 16–1024,
two mono rasters, eight-size ICO, README banner, installer header/sidebar and preview contact sheet.
All SVG/PNG/ICO geometry is original. Wordmarks rasterize the installed Segoe UI system font;
SVG references a font family, and no font binary is redistributed.

Regenerate from the repository root:

```powershell
python resources/branding/tools/render_logo.py
```

The renderer verifies the ICO entry sizes and the 5 MiB resource limit. Visual verification is
recorded in TEST_STATUS; generation alone is not proof of appearance or owner approval.
