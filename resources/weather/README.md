# Weather map data

`world-map.bin` is the offline map of the weather window (D78): country shapes with their names in the ten
interface languages, and populated places. It is generated, deterministically, by
[`tools/world-map/make_world_map.py`](../../tools/world-map/make_world_map.py), which also documents the format.

## Source

[Natural Earth](https://www.naturalearthdata.com/), public domain
([terms of use](https://www.naturalearthdata.com/about/terms-of-use/)), from the
[`nvkelso/natural-earth-vector`](https://github.com/nvkelso/natural-earth-vector) repository, `geojson/` folder,
downloaded on 2026-11-16:

| File | SHA-256 |
| --- | --- |
| `ne_50m_admin_0_countries.geojson` | `3e458fc036ad0a66411f2c1e6cac49c5d7bfb81cb1123bc513b22511a2b7fdeb` |
| `ne_10m_populated_places_simple.geojson` | `fd3fa867a320cbd5c5b6bb5bc550afeec2939fb2cef688e508007282a55ac42f` |

Output: `world-map.bin`, 530,477 bytes, SHA-256 `95c8d58b5f1513f138cb9e0d3a783191c7d6b9d3ce53f27ae1868469f4705ce3`
(240 countries, 1,623 rings, 69,887 points, 7,342 places).

## Regenerating

Download the two files into a new, empty folder, then run the converter isolated:

```powershell
python -I tools/world-map/make_world_map.py <folder>/ne_50m_admin_0_countries.geojson <folder>/ne_10m_populated_places_simple.geojson resources/weather/world-map.bin
```

Natural Earth draws borders as they are administered on the ground; the map shows them for orientation only.
