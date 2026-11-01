# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Original single-line handwriting strokes for the FR-041 welcome.

Every letter is drawn here, by hand, as pen paths: not a font, and not traced from one. Coordinates
are in a grid with the baseline at y=0, the x-height at y=50, ascenders near y=96 and descenders near
y=-36. A glyph is (advance, [stroke, ...]); a stroke is a list of (x, y) points that the renderer
smooths with a Catmull-Rom spline. The first stroke of a lowercase letter joins the previous letter's
pen exit unless the letter is marked as a lift; later strokes (dots, crossbars) are real pen lifts.

  python tools/handwriting/strokes.py --cpp > src/welcome/HandwritingStrokes.inc
  python tools/handwriting/strokes.py --preview out.png "Salut Ibrahim" "Hello Noëlle"

The preview shows the paths with a plain pen: the lean and the swelling strokes are added by Handwriting.cpp (D56).
"""
import math
import sys
import unicodedata

JOIN_MERGE = 5.0  # grid units: mirrors kJoinMerge in Handwriting.cpp

# Lowercase: pens enter low-left and leave low-right, so neighbours join into one continuous line.
LOWER = {
    'a': (46, [[(30, 44), (20, 50), (9, 43), (3, 24), (8, 6), (17, 1), (26, 8), (32, 26), (33, 48), (32, 26), (34, 8), (40, 2), (47, 9)]]),
    'b': (42, [[(1, 16), (10, 44), (15, 76), (14, 95), (9, 88), (7, 56), (7, 22), (9, 4), (18, 0), (29, 6), (34, 24), (29, 42), (18, 46), (10, 38), (20, 24), (32, 18), (43, 22)]]),
    'c': (36, [[(28, 40), (22, 49), (12, 47), (4, 32), (4, 14), (12, 3), (22, 1), (37, 12)]]),
    'd': (47, [[(30, 44), (20, 50), (9, 43), (3, 24), (8, 6), (17, 1), (26, 8), (32, 40), (35, 94), (34, 50), (34, 22), (37, 7), (42, 2), (48, 10)]]),
    'e': (36, [[(2, 16), (14, 21), (27, 32), (27, 45), (19, 50), (8, 42), (3, 24), (8, 7), (18, 1), (28, 4), (37, 14)]]),
    'f': (30, [[(1, 16), (11, 48), (20, 80), (20, 96), (13, 92), (10, 66), (10, 24), (8, -14), (4, -34), (0, -30), (4, -8), (14, 8), (22, 12), (31, 15)], [(1, 50), (22, 54)]]),
    'g': (42, [[(30, 44), (20, 50), (9, 43), (3, 24), (8, 7), (17, 2), (26, 10), (31, 46), (31, 18), (28, -16), (19, -35), (10, -32), (12, -16), (26, -2), (35, 6), (43, 14)]]),
    'h': (45, [[(1, 16), (10, 46), (15, 78), (14, 95), (9, 88), (7, 56), (7, 22), (7, 1), (9, 22), (17, 44), (26, 49), (31, 40), (30, 18), (33, 5), (39, 2), (46, 11)]]),
    'i': (22, [[(1, 16), (6, 32), (9, 48), (9, 22), (11, 6), (16, 2), (23, 11)], [(9, 66), (10, 68)]]),
    'j': (22, [[(1, 16), (7, 32), (11, 48), (11, 12), (8, -16), (2, -35), (-6, -32), (-3, -14), (10, 2), (23, 12)], [(11, 66), (12, 68)]]),
    'k': (40, [[(1, 16), (10, 46), (15, 78), (14, 95), (9, 88), (7, 56), (7, 22), (7, 1), (10, 24), (20, 42), (28, 46), (28, 34), (9, 24), (21, 18), (29, 5), (36, 2), (41, 10)]]),
    'l': (24, [[(1, 16), (9, 46), (15, 78), (14, 95), (9, 88), (7, 56), (7, 22), (10, 5), (16, 1), (24, 10)]]),
    'm': (63, [[(1, 14), (6, 32), (9, 49), (8, 22), (8, 1), (10, 24), (18, 44), (25, 49), (29, 40), (28, 18), (28, 1), (30, 24), (38, 44), (45, 49), (49, 40), (48, 18), (51, 5), (57, 2), (64, 11)]]),
    'n': (45, [[(1, 14), (6, 32), (9, 49), (8, 22), (8, 1), (10, 24), (18, 44), (25, 49), (29, 40), (28, 18), (31, 5), (37, 2), (46, 11)]]),
    'o': (40, [[(24, 48), (14, 49), (5, 38), (3, 20), (10, 4), (20, 1), (29, 10), (31, 30), (26, 46), (18, 48), (22, 36), (31, 32), (41, 34)]]),
    'p': (44, [[(1, 14), (7, 30), (11, 49), (10, 10), (8, -20), (6, -36), (8, -8), (13, 26), (21, 45), (30, 47), (35, 32), (31, 12), (22, 2), (13, 5), (22, 14), (34, 14), (45, 20)]]),
    'q': (42, [[(30, 44), (20, 50), (9, 43), (3, 24), (8, 7), (17, 2), (26, 10), (31, 46), (31, 16), (30, -16), (29, -36), (34, -18), (34, -4), (36, 6), (43, 14)]]),
    'r': (34, [[(1, 14), (6, 32), (10, 49), (17, 42), (24, 40), (22, 28), (22, 12), (25, 4), (35, 12)]]),
    's': (34, [[(1, 14), (10, 32), (20, 50), (23, 36), (24, 18), (19, 4), (9, 1), (3, 7), (14, 6), (26, 6), (35, 14)]]),
    't': (30, [[(1, 16), (9, 40), (14, 76), (13, 92), (11, 62), (10, 22), (13, 6), (20, 2), (31, 12)], [(1, 54), (24, 58)]]),
    'u': (45, [[(1, 16), (5, 32), (7, 48), (6, 22), (9, 5), (17, 1), (25, 8), (30, 28), (31, 48), (30, 22), (33, 6), (39, 2), (46, 11)]]),
    'v': (38, [[(1, 18), (5, 34), (7, 48), (9, 26), (15, 4), (19, 1), (25, 18), (29, 40), (30, 49), (24, 42), (28, 33), (39, 34)]]),
    'w': (56, [[(1, 18), (5, 34), (7, 48), (9, 24), (14, 3), (18, 1), (23, 16), (26, 34), (29, 16), (33, 2), (37, 1), (43, 22), (46, 49), (40, 42), (45, 34), (57, 35)]]),
    'x': (38, [[(1, 16), (8, 40), (14, 48), (21, 34), (24, 14), (29, 3), (39, 11)], [(30, 48), (20, 32), (10, 12), (3, 2)]]),
    'y': (42, [[(1, 16), (5, 32), (7, 48), (6, 22), (9, 6), (17, 2), (26, 10), (31, 48), (30, 16), (27, -16), (18, -35), (9, -32), (11, -16), (26, -2), (35, 6), (43, 14)]]),
    'z': (38, [[(1, 16), (10, 38), (16, 48), (28, 49), (26, 38), (12, 18), (4, 6), (15, 8), (26, 6), (24, -16), (14, -33), (8, -28), (14, -12), (29, 2), (39, 13)]]),
}

# Letters whose first stroke starts at the top right of a bowl are reached over the top when joined, as
# real cursive does; a straight connector would cut through the bowl and turn an o into an o-slash.
LEADS = {
    'a': [(5, 30), (15, 49)],
    'c': [(4, 30), (14, 48)],
    'd': [(5, 30), (15, 49)],
    'g': [(5, 30), (15, 49)],
    'o': [(3, 30), (12, 48)],
    'q': [(5, 30), (15, 49)],
}

# Capitals start a word: they are written freely and most lift before the next letter.
UPPER = {
    'A': (66, [[(3, 2), (16, 28), (30, 70), (39, 96), (42, 60), (46, 22), (50, 5), (56, 1), (64, 9)], [(14, 30), (30, 36), (50, 36)]]),
    'B': (60, [[(14, 88), (16, 56), (14, 24), (12, 2), (10, 8)], [(4, 80), (20, 94), (40, 95), (48, 82), (40, 64), (22, 52), (38, 50), (51, 36), (47, 14), (30, 2), (12, 2), (22, 8), (40, 12), (61, 20)]]),
    'C': (56, [[(50, 84), (42, 95), (28, 92), (12, 72), (4, 44), (8, 16), (22, 2), (38, 2), (58, 18)]]),
    'D': (66, [[(18, 92), (20, 56), (17, 22), (12, 2), (8, 8)], [(2, 78), (18, 94), (40, 94), (58, 76), (60, 46), (48, 18), (28, 3), (10, 2), (22, 8), (42, 12), (67, 20)]]),
    'E': (52, [[(46, 84), (36, 95), (22, 90), (20, 72), (32, 58), (40, 54), (24, 52), (10, 40), (8, 20), (20, 4), (36, 2), (54, 16)]]),
    'F': (52, [[(22, 90), (20, 56), (18, 22), (12, 0), (6, -8)], [(4, 88), (24, 92), (40, 94), (56, 96)], [(8, 48), (24, 50), (42, 52)]]),
    'G': (60, [[(50, 84), (42, 95), (28, 92), (12, 72), (5, 44), (9, 16), (22, 2), (36, 4), (46, 20), (48, 44), (48, 22), (46, -8), (36, -32), (24, -32), (26, -14), (44, 4), (61, 16)]]),
    'H': (70, [[(14, 92), (15, 56), (12, 22), (8, 2)], [(50, 94), (50, 56), (50, 22), (53, 6), (60, 2), (70, 12)], [(4, 44), (28, 50), (58, 52)]]),
    'I': (40, [[(8, 86), (22, 94), (34, 92), (26, 90), (24, 56), (22, 20), (16, 2), (6, 2), (2, 10), (14, 16), (28, 14), (41, 16)]]),
    'J': (44, [[(10, 86), (24, 94), (38, 92), (30, 90), (28, 50), (26, 8), (20, -22), (8, -36), (0, -28), (8, -6), (26, 8), (45, 16)]]),
    'K': (62, [[(16, 92), (16, 56), (13, 22), (8, 2)], [(50, 90), (34, 66), (16, 44), (28, 42), (40, 22), (50, 4), (56, 1), (63, 10)]]),
    'L': (54, [[(40, 86), (34, 96), (26, 88), (22, 56), (16, 22), (8, 4), (2, 6), (10, 14), (28, 6), (40, 2), (55, 14)]]),
    'M': (82, [[(6, 2), (12, 40), (18, 92), (28, 56), (38, 16), (50, 60), (60, 92), (62, 50), (64, 16), (68, 4), (74, 2), (83, 12)]]),
    'N': (70, [[(6, 2), (12, 44), (16, 92), (26, 60), (42, 16), (48, 4), (54, 40), (56, 94), (58, 50), (60, 18), (63, 6), (69, 2), (71, 8)]]),
    'O': (64, [[(36, 92), (20, 88), (6, 62), (4, 32), (16, 6), (32, 1), (50, 14), (58, 44), (50, 78), (36, 92), (26, 86), (34, 70), (50, 66), (65, 70)]]),
    'P': (56, [[(16, 88), (18, 50), (15, 16), (10, -8), (6, -4)], [(2, 76), (18, 93), (38, 95), (50, 78), (46, 56), (26, 42), (12, 42), (30, 34), (46, 28), (57, 30)]]),
    'Q': (66, [[(36, 92), (20, 88), (6, 62), (4, 32), (16, 6), (32, 1), (50, 14), (58, 44), (50, 78), (36, 92)], [(34, 16), (48, 6), (58, -8), (66, -14)]]),
    'R': (64, [[(16, 88), (18, 50), (15, 16), (10, 2), (6, 6)], [(2, 76), (18, 93), (38, 95), (50, 78), (46, 56), (26, 44), (14, 42), (28, 38), (40, 18), (48, 4), (56, 1), (65, 10)]]),
    'S': (52, [[(46, 82), (38, 95), (24, 92), (14, 76), (22, 58), (38, 44), (42, 24), (30, 6), (14, 2), (4, 14), (14, 18), (32, 12), (53, 18)]]),
    'T': (56, [[(30, 90), (28, 56), (24, 20), (16, 2), (8, 4)], [(6, 84), (22, 92), (40, 92), (56, 96)]]),
    'U': (68, [[(6, 80), (14, 94), (16, 70), (11, 34), (14, 10), (26, 2), (40, 10), (50, 40), (52, 92), (50, 52), (52, 18), (56, 5), (62, 2), (69, 10)]]),
    'V': (58, [[(4, 82), (12, 94), (18, 66), (24, 26), (28, 4), (34, 2), (44, 38), (50, 74), (52, 94), (46, 78), (50, 62), (59, 64)]]),
    'W': (88, [[(4, 82), (12, 94), (17, 60), (22, 22), (26, 3), (32, 2), (42, 40), (46, 66), (52, 34), (58, 4), (64, 2), (74, 40), (78, 94), (72, 76), (78, 62), (89, 64)]]),
    'X': (60, [[(6, 86), (16, 94), (26, 64), (36, 24), (46, 4), (61, 12)], [(52, 90), (36, 66), (20, 30), (6, 2)]]),
    'Y': (58, [[(6, 80), (14, 94), (16, 66), (16, 42), (24, 30), (36, 36), (44, 62), (48, 92), (46, 40), (40, -8), (26, -36), (16, -30), (22, -12), (40, 6), (59, 16)]]),
    'Z': (58, [[(8, 84), (22, 92), (42, 94), (48, 88), (32, 62), (10, 22), (4, 6), (18, 10), (36, 6), (50, 4), (59, 14)], [(14, 46), (28, 50), (44, 54)]]),
}

PUNCTUATION = {
    ' ': (24, []),
    '-': (26, [[(3, 26), (23, 28)]]),
    "'": (12, [[(6, 96), (5, 72)]]),
    '.': (12, [[(5, 2), (6, 4)]]),
}

# Combining marks, placed by the composer above (or below) the base letter's pen range: (x, y) offsets
# from the top centre of the base. Each is a real pen lift.
MARKS = {
    0x0300: [[(-7, 16), (6, 4)]],                       # grave
    0x0301: [[(-6, 4), (8, 16)]],                       # acute
    0x0302: [[(-10, 4), (0, 15), (10, 4)]],             # circumflex
    0x0303: [[(-12, 6), (-5, 13), (4, 7), (12, 14)]],   # tilde
    0x0304: [[(-10, 8), (11, 9)]],                      # macron
    0x0308: [[(-8, 8), (-7, 10)], [(7, 8), (8, 10)]],   # diaeresis
    0x030A: [[(0, 5), (-5, 9), (0, 15), (5, 9), (0, 5)]],  # ring
    0x030C: [[(-10, 15), (0, 4), (10, 15)]],            # caron
}
CEDILLA = 0x0327
CEDILLA_STROKE = [[(0, 0), (4, -10), (-1, -17), (-7, -15)]]

# Letters without a decomposition are written as the letters they stand for.
EXPANSIONS = {'æ': 'ae', 'Æ': 'Ae', 'œ': 'oe', 'Œ': 'Oe', 'ß': 'ss', 'ø': 'o', 'Ø': 'O', 'ł': 'l', 'Ł': 'L', 'đ': 'd', 'Đ': 'D', 'ı': 'i'}


def glyph(ch):
    return LOWER.get(ch) or UPPER.get(ch) or PUNCTUATION.get(ch)


def spline(points, per=10):
    """Catmull-Rom through the points, matching the renderer."""
    if len(points) < 2:
        return list(points)
    out = []
    p = [points[0]] + list(points) + [points[-1]]
    for i in range(1, len(p) - 2):
        for s in range(per):
            t = s / per
            a, b, c, d = p[i - 1], p[i], p[i + 1], p[i + 2]
            out.append(tuple(0.5 * (2 * b[k] + (c[k] - a[k]) * t + (2 * a[k] - 5 * b[k] + 4 * c[k] - d[k]) * t * t +
                                    (3 * b[k] - a[k] - 3 * c[k] + d[k]) * t * t * t) for k in (0, 1)))
    out.append(points[-1])
    return out


def compose(text):
    """Pen paths for `text` in grid units (mirrors Handwriting.cpp). A word is written as one line; its
    dots, crossbars and accents follow when the word is finished, as a hand does."""
    strokes, later, x, joinable = [], [], 0.0, False

    def finish_word():
        nonlocal later, joinable
        strokes.extend(later)
        later, joinable = [], False

    decomposed = unicodedata.normalize('NFD', ''.join(EXPANSIONS.get(c, c) for c in text))
    i = 0
    while i < len(decomposed):
        ch = decomposed[i]
        i += 1
        marks = []
        while i < len(decomposed) and unicodedata.combining(decomposed[i]):
            marks.append(ord(decomposed[i]))
            i += 1
        g = glyph(ch)
        if g is None:
            raise SystemExit('no strokes for ' + repr(ch))
        advance, paths = g
        if ch in PUNCTUATION:
            finish_word()
        for index, path in enumerate(paths):
            if ch in 'ij' and index == 1 and marks:
                continue  # an accent takes the place of the dot, as in French i-diaeresis
            placed = [(px + x, py) for px, py in path]
            if ch in LOWER and index == 0:
                if joinable:
                    lead = [(px + x, py) for px, py in LEADS.get(ch, [])]
                    joined = lead + placed
                    # A letter usually starts where the last one ended; a doubled point would knot the spline.
                    if math.hypot(joined[0][0] - strokes[-1][-1][0], joined[0][1] - strokes[-1][-1][1]) < JOIN_MERGE:
                        joined = joined[1:]
                    strokes[-1] = strokes[-1] + joined
                else:
                    strokes.append(placed)
            elif ch in LOWER:
                later.append(placed)
            else:
                strokes.append(placed)  # a capital's strokes keep their own order
        top = max((py for path in paths[:1] for _, py in path), default=50)
        if ch in 'ij':
            top = 50
        centre = x + advance * 0.46
        for mark in marks:
            base = CEDILLA_STROKE if mark == CEDILLA else MARKS.get(mark)
            if base is None:
                raise SystemExit('no strokes for mark %04x' % mark)
            y0 = 0 if mark == CEDILLA else top + 8
            later.extend([(centre + px, y0 + py) for px, py in path] for path in base)
        x += advance
        joinable = ch in LOWER or (ch in UPPER and len(paths) == 1)
    finish_word()
    return strokes, x


def preview(lines, path):
    from PIL import Image, ImageDraw
    scale, pad, gap = 3.0, 60, 40
    composed = [compose(line) for line in lines]
    width = int(max(w for _, w in composed) * scale + pad * 2)
    height = int(len(lines) * (140 * scale + gap) + pad * 2)
    image = Image.new('RGB', (width, height), (18, 20, 26))
    draw = ImageDraw.Draw(image)
    for row, (strokes, _) in enumerate(composed):
        baseline = pad + (row + 1) * (140 * scale + gap) - 40 * scale
        for stroke in strokes:
            points = spline(stroke)
            pts = [(pad + px * scale, baseline - py * scale) for px, py in points]
            draw.line(pts, fill=(240, 242, 248), width=5, joint='curve')
    image.save(path)


def cpp():
    def path_text(path):
        return '{' + ', '.join('{%d, %d}' % p for p in path) + '}'
    print('// SPDX-License-Identifier: MIT')
    print('// Copyright (c) 2026 Ibrahim Bouillet')
    print('// Generated by tools/handwriting/strokes.py, where the strokes are authored. Do not edit by hand.')
    print('static const Tables tables{')
    print('    {')
    for table in (LOWER, UPPER, PUNCTUATION):
        for ch, (advance, paths) in sorted(table.items()):
            literal = "L'\\''" if ch == "'" else "L'%s'" % ch
            joined = 'true' if ch in LOWER else 'false'
            print('        {%s, %d, %s, {%s}, %s},' % (literal, advance, joined, ', '.join(path_text(p) for p in paths), path_text(LEADS.get(ch, []))))
    print('    },')
    print('    {')
    for code, paths in sorted(MARKS.items()):
        print('        {0x%04X, {%s}},' % (code, ', '.join(path_text(p) for p in paths)))
    print('        {0x%04X, {%s}},' % (CEDILLA, ', '.join(path_text(p) for p in CEDILLA_STROKE)))
    print('    },')
    print('};')


if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == '--cpp':
        cpp()
    elif len(sys.argv) >= 4 and sys.argv[1] == '--preview':
        preview(sys.argv[3:], sys.argv[2])
    else:
        print(__doc__)
