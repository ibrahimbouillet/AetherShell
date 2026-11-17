# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Builds resources/weather/world-map.bin for the weather window's map (D78).

Inputs are two public-domain Natural Earth GeoJSON files (https://www.naturalearthdata.com/about/terms-of-use/),
taken from https://github.com/nvkelso/natural-earth-vector (geojson/ folder):
  ne_50m_admin_0_countries.geojson        country shapes and their names in the ten interface languages
  ne_10m_populated_places_simple.geojson  towns and cities, for naming a position and sampling temperatures

Usage (run isolated; the inputs are downloaded data):
  python -I tools/world-map/make_world_map.py <countries.geojson> <places.geojson> resources/weather/world-map.bin

Format "AWM1", little-endian, read by src/weather/WorldMap.cpp:
  header   char magic[4] = "AWM1"; u32 countries, rings, points, places, stringBytes
  country  char code[4] (ADM0_A3, NUL padded); u32 name[10] (string offsets: en fr es zh hi pt ru ar bn ur);
           i16 bbox[4] (west, south, east, north); u32 firstRing, ringCount                       60 bytes
  ring     u32 firstPoint, pointCount                                                              8 bytes
  point    i16 lon, lat                                                                            4 bytes
  place    i16 lon, lat; u32 name; u32 population; u16 country (0xFFFF: none); u8 scalerank; u8 0   16 bytes
  strings  UTF-8, each NUL terminated
Coordinates are hundredths of a degree (about 1.1 km). Rings use the even-odd rule: holes need no flag.
Rings are simplified (Douglas-Peucker, one hundredth of a degree) and need at least three distinct points.
The output is deterministic: the same inputs always give the same bytes.
"""
import json
import struct
import sys

LANGUAGES = ("EN", "FR", "ES", "ZH", "HI", "PT", "RU", "AR", "BN", "UR")
TOLERANCE = 1.0  # in hundredths of a degree


def quantize(value):
    return int(round(value * 100.0))


def simplify(points, tolerance):
    """Douglas-Peucker on a closed ring of integer points, keeping the first point."""
    if len(points) < 4:
        return points
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        first, last = stack.pop()
        ax, ay = points[first]
        bx, by = points[last]
        dx, dy = bx - ax, by - ay
        length = (dx * dx + dy * dy) ** 0.5
        best, index = -1.0, -1
        for i in range(first + 1, last):
            px, py = points[i]
            if length == 0:
                distance = ((px - ax) ** 2 + (py - ay) ** 2) ** 0.5
            else:
                distance = abs(dy * px - dx * py + bx * ay - by * ax) / length
            if distance > best:
                best, index = distance, i
        if index >= 0 and best > tolerance:
            keep[index] = True
            stack.append((first, index))
            stack.append((index, last))
    return [p for p, k in zip(points, keep) if k]


def ring_points(coordinates):
    points = []
    for lon, lat in coordinates:
        point = (max(-18000, min(18000, quantize(lon))), max(-9000, min(9000, quantize(lat))))
        if not points or points[-1] != point:
            points.append(point)
    if len(points) > 1 and points[0] == points[-1]:
        points.pop()  # stored open; readers close it
    if len(points) < 3:
        return []
    closed = simplify(points + [points[0]], TOLERANCE)[:-1]
    return closed if len(set(closed)) >= 3 else []


class Strings:
    def __init__(self):
        self.data = bytearray()
        self.offsets = {}

    def add(self, text):
        text = (text or "").strip()
        if text not in self.offsets:
            self.offsets[text] = len(self.data)
            self.data += text.encode("utf-8") + b"\0"
        return self.offsets[text]


def main(countries_path, places_path, output_path):
    with open(countries_path, encoding="utf-8") as handle:
        countries = json.load(handle)["features"]
    with open(places_path, encoding="utf-8") as handle:
        places = json.load(handle)["features"]

    strings = Strings()
    strings.add("")
    country_rows, ring_rows, point_rows = [], [], []
    code_index = {}
    countries.sort(key=lambda feature: (feature["properties"]["ADM0_A3"], feature["properties"]["NAME_EN"] or ""))
    for feature in countries:
        properties = feature["properties"]
        code = (properties["ADM0_A3"] or "").strip()
        geometry = feature["geometry"]
        polygons = [geometry["coordinates"]] if geometry["type"] == "Polygon" else geometry["coordinates"]
        first_ring = len(ring_rows)
        west, south, east, north = 18000, 9000, -18000, -9000
        for polygon in polygons:
            for ring in polygon:
                points = ring_points(ring)
                if not points:
                    continue
                ring_rows.append((len(point_rows), len(points)))
                point_rows.extend(points)
                for lon, lat in points:
                    west, east = min(west, lon), max(east, lon)
                    south, north = min(south, lat), max(north, lat)
        if len(ring_rows) == first_ring or len(code) != 3 or code in code_index:
            if len(ring_rows) > first_ring:
                del point_rows[ring_rows[first_ring][0]:]
                del ring_rows[first_ring:]
            continue
        names = []
        for language in LANGUAGES:
            # English uses the short map label ("China", not "People's Republic of China").
            preferred = properties.get("NAME") if language == "EN" else properties.get("NAME_" + language)
            name = preferred or properties.get("NAME") or properties.get("NAME_EN") or code
            names.append(strings.add(name))
        code_index[code] = len(country_rows)
        country_rows.append((code.encode("ascii") + b"\0", names, (west, south, east, north),
                             first_ring, len(ring_rows) - first_ring))

    place_rows = []
    places.sort(key=lambda feature: (feature["properties"]["scalerank"], -(feature["properties"]["pop_max"] or 0),
                                     feature["properties"]["name"] or ""))
    for feature in places:
        properties = feature["properties"]
        name = (properties.get("name") or properties.get("nameascii") or "").strip()
        if not name:
            continue
        lon = max(-18000, min(18000, quantize(properties["longitude"])))
        lat = max(-9000, min(9000, quantize(properties["latitude"])))
        population = max(0, min(0xFFFFFFFF, int(properties.get("pop_max") or 0)))
        country = code_index.get((properties.get("adm0_a3") or "").strip(), 0xFFFF)
        rank = max(0, min(255, int(properties.get("scalerank") or 0)))
        place_rows.append((lon, lat, strings.add(name), population, country, rank))

    out = bytearray(b"AWM1")
    out += struct.pack("<5I", len(country_rows), len(ring_rows), len(point_rows), len(place_rows), len(strings.data))
    for code, names, box, first_ring, ring_count in country_rows:
        out += code + struct.pack("<10I", *names) + struct.pack("<4h", *box) + struct.pack("<2I", first_ring, ring_count)
    for first_point, count in ring_rows:
        out += struct.pack("<2I", first_point, count)
    for lon, lat in point_rows:
        out += struct.pack("<2h", lon, lat)
    for lon, lat, name, population, country, rank in place_rows:
        out += struct.pack("<2hIIHBB", lon, lat, name, population, country, rank, 0)
    out += strings.data
    with open(output_path, "wb") as handle:
        handle.write(out)
    print(f"{len(country_rows)} countries, {len(ring_rows)} rings, {len(point_rows)} points, "
          f"{len(place_rows)} places, {len(strings.data)} string bytes -> {len(out)} bytes")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], sys.argv[3])
