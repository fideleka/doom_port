#!/usr/bin/env python3
"""Render reference/candidate UI comparisons from a Doom IWAD, with no WAD copy.

Usage: python3 tools/preview_classic_ui.py /path/to/doom1.wad /tmp/doom-ui
Outputs PNGs when ImageMagick `convert` is available, else portable PPMs.
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys

CROP_RIGHT = 251  # STBAR key-column right bevel ends at source x=250.
DISPLAY_WIDTH = 280
DISPLAY_HEIGHT = 240
STATUS_BOTTOM = 8


def read_wad(path):
    data = Path(path).read_bytes()
    if data[:4] not in (b'IWAD', b'PWAD'):
        raise ValueError('Not a Doom WAD')
    count, directory = struct.unpack_from('<II', data, 4)
    if directory + 16 * count > len(data):
        raise ValueError('Invalid WAD directory')
    lumps = {}
    for i in range(count):
        at = directory + 16 * i
        pos, size = struct.unpack_from('<II', data, at)
        if pos + size > len(data):
            raise ValueError('Invalid WAD lump')
        name = data[at + 8:at + 16].split(b'\0', 1)[0].decode('ascii')
        lumps[name] = data[pos:pos + size]
    return lumps


def patch(blob):
    width, height, left, top = struct.unpack_from('<hhhh', blob)
    pixels = [[None] * width for _ in range(height)]
    for x in range(width):
        offset = struct.unpack_from('<I', blob, 8 + 4 * x)[0]
        while blob[offset] != 255:
            row, length = blob[offset], blob[offset + 1]
            for i in range(length):
                if row + i < height:
                    pixels[row + i][x] = blob[offset + 3 + i]
            offset += length + 4
    return pixels, left, top


def paint(canvas, lumps, name, x, y):
    pixels, left, top = patch(lumps[name])
    x -= left
    y -= top
    for j, line in enumerate(pixels):
        if not 0 <= y + j < len(canvas):
            continue
        for i, color in enumerate(line):
            if color is not None and 0 <= x + i < len(canvas[0]):
                canvas[y + j][x + i] = color


def number(canvas, lumps, value, right):
    if not value:
        paint(canvas, lumps, 'STTNUM0', right - 14, 3)
    while value:
        right -= 14
        paint(canvas, lumps, f'STTNUM{value % 10}', right, 3)
        value //= 10


def save_ppm(path, pixels, palette):
    with path.open('wb') as out:
        out.write(f'P6\n{len(pixels[0])} {len(pixels)}\n255\n'.encode())
        for line in pixels:
            for index in line:
                i = 0 if index is None else index
                out.write(palette[3 * i:3 * i + 3])


def png(ppm):
    if shutil.which('convert'):
        subprocess.run(['convert', str(ppm), str(ppm.with_suffix('.png'))], check=True)


def render_status(lumps, palette, prefix):
    bar, _, _ = patch(lumps['STBAR'])
    assert len(bar) == 32 and len(bar[0]) == 320
    number(bar, lumps, 44, 44)
    number(bar, lumps, 85, 90)
    paint(bar, lumps, 'STTPRCNT', 90, 3)
    paint(bar, lumps, 'STARMS', 104, 0)
    for slot in range(6):
        name = f'STYSNUM{slot+2}' if slot < 2 else f'STGNUM{slot+2}'
        paint(bar, lumps, name, 111 + slot % 3 * 12, 4 + slot // 3 * 10)
    paint(bar, lumps, 'STFST00', 143, 0)
    number(bar, lumps, 95, 221)
    paint(bar, lumps, 'STTPRCNT', 221, 3)
    paint(bar, lumps, 'STKEYS0', 239, 3)
    crop = [line[:CROP_RIGHT] for line in bar]
    assert all(crop[y][x] == bar[y][x] for y in range(32) for x in range(CROP_RIGHT))
    # Stage is 256 wide/25 high plus 8 px bottom safety; proposed is native
    # 251 wide/32 high at x=14, preserving that bottom safety margin.
    stage = [[None] * DISPLAY_WIDTH for _ in range(48)]
    candidate = [[None] * DISPLAY_WIDTH for _ in range(48)]
    for y in range(25):
        sy = y * 32 // 25
        for x in range(DISPLAY_WIDTH):
            sx = 0 if x < 12 else 319 if x >= 268 else (x - 12) * 320 // 256
            stage[15 + y][x] = bar[sy][sx]
    left = (DISPLAY_WIDTH - CROP_RIGHT) // 2
    for y in range(32):
        candidate[8 + y][left:left + CROP_RIGHT] = crop[y]
    for name, image in [('status-stage', stage), ('status-candidate', candidate)]:
        path = Path(f'{prefix}-{name}.ppm')
        save_ppm(path, image, palette)
        png(path)
    print(f'Status: source x=0..{CROP_RIGHT - 1}, physical x={left}..{left + CROP_RIGHT - 1}, native 251x32')


def render_menu(lumps, palette, prefix):
    source, _, _ = patch(lumps['TITLEPIC'])
    items = ['M_NGAME', 'M_OPTION', 'M_LOADG', 'M_SAVEG', 'M_RDTHIS', 'M_QUITG']
    spans = [(2, 2 + len(patch(lumps['M_DOOM'])[0]))]
    for i, name in enumerate(items):
        pix, _, top = patch(lumps[name])
        y = 64 + 16 * i - top
        spans.append((y, y + len(pix)))
    for name in ('M_SKULL1', 'M_SKULL2'):
        pix, _, top = patch(lumps[name])
        for y in (59, 139):
            spans.append((y - top, y - top + len(pix)))
    low = min(a for a, _ in spans)
    high = max(b for _, b in spans)
    offset = 84 - (low + high) // 2  # current title menu's source centre
    paint(source, lumps, 'M_DOOM', 94, 2 + offset)
    for i, name in enumerate(items):
        paint(source, lumps, name, 97, 64 + offset + 16 * i)
    paint(source, lumps, 'M_SKULL1', 87, 59 + offset)
    for name, height, top in [('menu-stage', 210, 15), ('menu-candidate', 240, 0)]:
        image = [[None] * DISPLAY_WIDTH for _ in range(DISPLAY_HEIGHT)]
        for y in range(height):
            sy = y * 200 // height
            for x in range(DISPLAY_WIDTH):
                image[top + y][x] = source[sy][x * 320 // DISPLAY_WIDTH]
        path = Path(f'{prefix}-{name}.ppm')
        save_ppm(path, image, palette)
        png(path)
    print(f'Menu content: source y={low + offset}..{high + offset}; full-height physical y={(low + offset) * 240 // 200}..{(high + offset) * 240 // 200}')


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    lumps = read_wad(sys.argv[1])
    palette = lumps['PLAYPAL'][:768]
    prefix = sys.argv[2]
    render_status(lumps, palette, prefix)
    render_menu(lumps, palette, prefix)


if __name__ == '__main__':
    main()
