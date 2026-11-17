# Third-party notices

Aether Shell itself is licensed under the MIT License (see `LICENSE`). The MIT License covers the
project's own source code and artwork only; it does not cover the items below.

## Libraries

Aether Shell contains **no third-party source code or binaries**. It uses only components of the
Windows operating system and the Windows SDK (Win32, Direct2D, DirectWrite, Direct3D 11, Windows.UI.
Composition, WinHTTP, CNG, Core Audio, Windows Runtime APIs) and the C++/WinRT headers shipped with
the Windows SDK, linked against the Microsoft C/C++ runtime (statically).

## Data services

- **Open-Meteo** (optional weather, disabled by default): weather data by
  [Open-Meteo.com](https://open-meteo.com/), licensed under
  [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Attribution is displayed with the weather.
- **GitHub REST API** (update checks): used under the GitHub Terms of Service.

## Map data

- **Natural Earth** (the weather window's map, D78): country shapes and names and populated places from
  [Natural Earth](https://www.naturalearthdata.com/), which is in the public domain
  ([terms of use](https://www.naturalearthdata.com/about/terms-of-use/)). No permission or attribution is required;
  it is credited in the weather window ("Map: Natural Earth"). The data is converted into `resources/weather/world-map.bin`
  and embedded in the application; its source files and hashes are listed in `resources/weather/README.md`. Borders are
  shown for orientation only.

## Fonts

The user interface uses fonts installed with Windows (Segoe UI / Segoe UI Variable). No font files
are distributed. Branding images that contain the wordmark were rasterized from an installed system
font; the font itself is not redistributed.

## Showcase media

Versioned presentation captures may include the scenic train photograph by **Jack Anstey** on
[Unsplash](https://unsplash.com/photos/train-on-bridge-surrounded-with-trees-at-daytime-XVoyX7l9ocY).
That photograph uses the separate [Unsplash License](https://unsplash.com/license), not the project's
MIT License. Genuine installed application names/icons and Windows interface pixels retain their
respective owners' rights; their appearance demonstrates the product and implies no endorsement.
No standalone third-party icon pack or font is distributed. Detailed provenance is recorded in
[the showcase asset register](video_production/03_ASSET_REGISTER.md).

## Development tools (not distributed)

Graphify, Python and OpenSSL may be used by developers and the release pipeline. None of
them is shipped with or required by Aether Shell.
