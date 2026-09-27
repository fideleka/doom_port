#!/usr/bin/env python3
"""Map a real 320x240 Doom framebuffer to stage and proposed Lilka displays."""
from pathlib import Path
import sys


def read_ppm(path):
    data = Path(path).read_bytes()
    header = b'P6\n320 240\n255\n'
    if not data.startswith(header) or len(data) != len(header) + 320 * 240 * 3:
        raise ValueError('Expected a 320x240 P6 Doom frame')
    data = data[len(header):]
    return [[data[(y * 320 + x) * 3:(y * 320 + x + 1) * 3]
             for x in range(320)] for y in range(240)]


def write_ppm(path, image):
    with Path(path).open('wb') as output:
        output.write(b'P6\n280 240\n255\n')
        for row in image:
            for color in row:
                output.write(color)


def main():
    if len(sys.argv) != 3:
        raise SystemExit('usage: render_demo_preview.py Doom-frame.ppm output-prefix')
    source = read_ppm(sys.argv[1])
    prefix = sys.argv[2]
    black = b'\0\0\0'
    stage = [[black] * 280 for _ in range(240)]
    candidate = [[black] * 280 for _ in range(240)]

    # Exact stage menu-free mapping: 207 world + 25 status + 8 bottom.
    for y in range(207):
        for x in range(280):
            stage[y][x] = source[y * 208 // 207][20 + x]
    for y in range(25):
        source_y = 208 + y * 32 // 25
        for x in range(280):
            source_x = 0 if x < 12 else 319 if x >= 268 else (x - 12) * 320 // 256
            stage[207 + y][x] = source[source_y][source_x]

    # Candidate: a 192px world, then four original 70x6 ammo-table rows
    # across a thin 8px strip. Clean STBAR y=209 pads the glyphs above/below
    # so no neighbor-row numbers leak into another cell.
    for y in range(192):
        for x in range(280):
            candidate[y][x] = source[y * 208 // 192][20 + x]
    for y in range(8):
        for x in range(280):
            ammo_type, within_cell = divmod(x, 70)
            source_y = 209 if y in (0, 7) else 213 + ammo_type * 6 + y - 1
            candidate[192 + y][x] = source[source_y][250 + within_cell]

    # Original status pixels x=0..250 remain native and centered at x=14.
    # Narrow margins repeat original edge texels; no black side boxes.
    for y in range(32):
        for x in range(280):
            source_x = 0 if x < 14 else 250 if x >= 265 else x - 14
            candidate[200 + y][x] = source[208 + y][source_x]

    write_ppm(prefix + '-stage.ppm', stage)
    write_ppm(prefix + '-candidate.ppm', candidate)
    assert all(candidate[200 + y][14 + x] == source[208 + y][x]
               for y in range(32) for x in range(251))
    assert all(any(candidate[y][x] != black for x in range(280))
               for y in range(192, 200))
    print('Candidate: world y=0..191; ammo strip y=192..199; original status x=14..264,y=200..231')


if __name__ == '__main__':
    main()
