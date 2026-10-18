# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Ibrahim Bouillet
"""Regenerate original Aether Shell vector and raster branding. No font binaries are copied."""
from pathlib import Path
from io import BytesIO
import os
import struct
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256, 512, 1024)
PALETTE = ((91, 108, 255), (61, 217, 232))
# Two counter-flowing air ribbons; the same cubic geometry generates SVG and raster masks.
RIBBONS = (
    [('M', 150, 610), ('C', 260, 340, 445, 170, 810, 240), ('L', 700, 390),
     ('C', 500, 350, 365, 425, 280, 640), ('Z',)],
    [('M', 245, 770), ('L', 345, 600), ('C', 485, 655, 615, 575, 725, 375),
     ('L', 850, 375), ('C', 715, 700, 515, 850, 245, 770), ('Z',)],
)

def svg_path(commands):
    return ' '.join(command[0] + ' '.join(map(str, command[1:])) for command in commands)

def points(commands):
    result = []
    last = None
    for op, *v in commands:
        if op in ('M', 'L'):
            last = np.array(v, dtype=float)
            result.append(tuple(last))
        elif op == 'C':
            p0, p1, p2, p3 = last, np.array(v[:2]), np.array(v[2:4]), np.array(v[4:])
            for t in np.linspace(0, 1, 49)[1:]:
                point = (1-t)**3*p0 + 3*(1-t)**2*t*p1 + 3*(1-t)*t*t*p2 + t**3*p3
                result.append(tuple(point))
            last = p3
    return result

def mark(size, mono=False):
    edge = size * 4
    mask = Image.new('L', (edge, edge))
    draw = ImageDraw.Draw(mask)
    for ribbon in RIBBONS:
        # Optical correction keeps the mark legible in a Windows 16–32 px icon.
        optical = 1.18 if size <= 32 else 1.0
        geometry = [((500+(x-500)*optical)*edge/1000, (500+(y-500)*optical)*edge/1000) for x, y in points(ribbon)]
        if size <= 24:
            geometry = [(round(x/4)*4, round(y/4)*4) for x, y in geometry]
        draw.polygon(geometry, fill=255)
    y, x = np.mgrid[0:edge, 0:edge]
    mix = np.clip((x/edge + y/edge*0.35)/1.2, 0, 1)[..., None]
    colors = np.full((edge, edge, 3), 255, dtype=np.uint8) if mono else (
        np.array(PALETTE[0])*(1-mix) + np.array(PALETTE[1])*mix).astype(np.uint8)
    rgba = np.concatenate([colors, np.array(mask)[..., None]], axis=2)
    return Image.fromarray(rgba).resize((size, size), Image.Resampling.LANCZOS)

def font(size):
    fonts = Path(os.environ.get('WINDIR', 'C:/Windows')) / 'Fonts'
    for name in ('segoeui.ttf', 'SegUIVar.ttf'):
        if (fonts/name).is_file():
            return ImageFont.truetype(str(fonts/name), size)
    raise RuntimeError('An installed Segoe UI font is required; no font is redistributed')

def wordmark(size=(1200, 300), dark=True, tagline=False):
    w, h = size
    image = Image.new('RGBA', size, (0, 0, 0, 0))
    image.alpha_composite(mark(int(h*.83)), (int(h*.06), int(h*.08)))
    draw = ImageDraw.Draw(image)
    color = '#F1F5FF' if dark else '#11172A'
    draw.text((h*.94, h*.25), 'Aether Shell', font=font(int(h*.31)), fill=color)
    if tagline:
        draw.text((h*.94, h*.65), 'A modular native desktop interface for Windows', font=font(int(h*.055)), fill='#A4B5D3')
    return image

def ico(images):
    encoded = []
    for image in images:
        stream = BytesIO(); image.save(stream, format='PNG'); encoded.append(stream.getvalue())
    offset = 6 + 16*len(images)
    header = struct.pack('<HHH', 0, 1, len(images))
    for image, data in zip(images, encoded):
        n = image.width
        header += struct.pack('<BBBBHHII', 0 if n == 256 else n, 0 if n == 256 else n, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return header + b''.join(encoded)

def main():
    for name in ('svg', 'png', 'ico', 'installer'):
        (ROOT/name).mkdir(parents=True, exist_ok=True)
    paths = ''.join(f'<path d="{svg_path(p)}"/>' for p in RIBBONS)
    base = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 1000">'
    gradient = '<defs><linearGradient id="air" x2="1" y2=".35"><stop stop-color="#5B6CFF"/><stop offset="1" stop-color="#3DD9E8"/></linearGradient></defs>'
    (ROOT/'svg/aether-mark.svg').write_text(base+gradient+'<g fill="url(#air)">'+paths+'</g></svg>\n', encoding='utf-8')
    (ROOT/'svg/aether-mark-mono.svg').write_text(base+'<g fill="currentColor">'+paths+'</g></svg>\n', encoding='utf-8')
    for size in SIZES:
        mark(size).save(ROOT/f'png/aether-mark-{size}.png', optimize=True)
    for size in (32, 256):
        mark(size, True).save(ROOT/f'png/aether-mark-mono-{size}.png', optimize=True)
    for theme in ('light', 'dark'):
        wordmark(dark=theme=='dark').save(ROOT/f'png/aether-logo-horizontal-{theme}.png', optimize=True)
        text = '#F1F5FF' if theme=='dark' else '#11172A'
        svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1200 300">'+gradient+'<g transform="translate(18,24) scale(.249)" fill="url(#air)">'+paths+'</g>'
        svg += f'<text x="282" y="183" fill="{text}" font-family="Segoe UI Variable Display, Segoe UI, sans-serif" font-size="93">Aether Shell</text></svg>\n'
        (ROOT/f'svg/aether-logo-horizontal-{theme}.svg').write_text(svg,encoding='utf-8')
    backdrop = Image.new('RGBA', (1280, 400), '#0B1020')
    glow = Image.new('RGBA', backdrop.size)
    ImageDraw.Draw(glow).ellipse((40, 30, 780, 410), fill=(50, 86, 150, 80))
    backdrop.alpha_composite(glow.filter(ImageFilter.GaussianBlur(80)))
    backdrop.alpha_composite(wordmark((1280,400), tagline=True))
    backdrop.save(ROOT/'png/readme-banner.png',optimize=True)
    wordmark((900,180)).save(ROOT/'installer/installer-header.png',optimize=True)
    side = Image.new('RGBA',(328,628),'#0B1020'); side.alpha_composite(mark(300),(14,100))
    ImageDraw.Draw(side).text((28,450),'Aether Shell',font=font(34),fill='white')
    side.save(ROOT/'installer/installer-sidebar.png',optimize=True)
    sizes=(16,20,24,32,40,48,64,256)
    (ROOT/'ico/AetherShell.ico').write_bytes(ico([mark(n) for n in sizes]))
    sheet=Image.new('RGBA',(960,400),'#11172A'); draw=ImageDraw.Draw(sheet)
    draw.rectangle((0,0,960,199),fill='#FFFFFF')
    for i,size in enumerate((16,24,32,64,128,256)):
        x=20+i*150
        for row in (0,200):
            n=min(size,140); sheet.alpha_composite(mark(n),(x,row+15))
            draw.text((x,row+162),f'{size}px',font=font(16),fill='#24304A' if row==0 else '#C6D2E8')
    sheet.save(ROOT/'png/preview-contact-sheet.png',optimize=True)
    actual=Image.open(ROOT/'ico/AetherShell.ico').ico.sizes()
    assert actual=={(n,n) for n in sizes},actual
    assert sum(p.stat().st_size for p in ROOT.rglob('*') if p.is_file())<5*1024*1024
    print(f'Brand assets generated; verified {len(SIZES)} raster sizes and {len(actual)} ICO entries.')

if __name__ == '__main__':
    main()
