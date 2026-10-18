<!-- Generated from the session's agent workflow; edit freely. -->
# Module specification: branding (phase A)

Read `AGENTS.md` first; it states the rules every implementer follows. This is the work order that
was given to the implementing engineer. The real headers in the repository are authoritative where
they differ from names used here.

Module: branding (directory resources/branding). No C++.
Your files: resources/branding/**, docs/BRANDING.md.
Create an ORIGINAL, professional logo and visual identity for "Aether Shell" (author Ibrahim Bouillet). Direction: futuristic simplicity, premium desktop software, fluidity, transparency, precision, reliability, elegant glass aesthetics; an abstract mark inspired by "aether" (luminous upper air, light, orbit). It must stay recognizable at 16x16 and must NOT resemble Apple, Microsoft/Windows, macOS or any existing OS or app logo (no apple shapes, no four-pane window, no Finder face, no stock "A in a circle").
Palette: define primary, secondary and accent colors (for example a deep indigo-to-cyan aurora gradient with a soft white highlight) plus light and dark neutrals; the app's glass UI will reuse it.
Deliverables, all generated from ONE parametric geometry definition so they stay consistent:
- tools/render_logo.py (Python 3 standard library + Pillow + numpy, both installed) that writes the SVG sources and renders the PNG rasters with 4x supersampling, numpy gradients, and LANCZOS downsampling.
- svg/aether-mark.svg (full color, transparent), svg/aether-mark-mono.svg (single color, currentColor-friendly), svg/aether-logo-horizontal-light.svg and svg/aether-logo-horizontal-dark.svg (mark + wordmark "Aether Shell"; the SVG may reference "Segoe UI Variable Display, Segoe UI, sans-serif"; rasterize the wordmark with an installed font from C:\Windows\Fonts and note in the docs that only rasterized images are distributed, not the font).
- png/aether-mark-{16,20,24,32,40,48,64,96,128,256,512,1024}.png (transparent), png/aether-mark-mono-{32,256}.png, png/aether-logo-horizontal-light.png and -dark.png (about 1200x300), png/readme-banner.png (1280x400, dark aurora background, mark + wordmark + tagline "A modular native desktop interface for Windows"), installer/installer-header.png (about 900x180) and installer/installer-sidebar.png (about 328x628).
- ico/AetherShell.ico with 16, 20, 24, 32, 40, 48, 64 and 256 px images (256 PNG-compressed). Re-open it with Pillow and list the sizes to verify. For 16-24 px use a simplified, pixel-hinted variant so it stays crisp.
- png/preview-contact-sheet.png showing the mark on white and on dark at several sizes.
- docs/BRANDING.md: concept, construction, palette with hex values, clear space, minimum sizes, do and don't, file inventory, how to regenerate.
Quality bar: LOOK at your renders with the Read tool (16, 32, 256, 1024, the banner, the contact sheet) and iterate at least twice until it is polished and legible at 16 px on light and dark backgrounds. Keep resources/branding under 5 MB. No third-party artwork or fonts beyond rasterizing an installed system font.
Your result fields: put the list of generated files in "files"; set buildSucceeded true when the renderer runs cleanly; tests.passed = number of verification checks you ran successfully (ICO sizes, file existence, dimensions).
