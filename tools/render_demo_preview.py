#!/usr/bin/env python3
"""Map a real 320x240 Doom framebuffer to stage and proposed Lilka displays."""
from pathlib import Path
import struct
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


def read_stbar(wad_path):
    """Decode the original STBAR backing, before numbers/face are painted."""
    data = Path(wad_path).read_bytes()
    count, directory = struct.unpack_from('<II', data, 4)
    lumps = {}
    for i in range(count):
        at = directory + 16 * i
        offset, size = struct.unpack_from('<II', data, at)
        name = data[at + 8:at + 16].split(b'\0', 1)[0].decode('ascii')
        if name in ('STBAR', 'PLAYPAL'):
            lumps[name] = data[offset:offset + size]
    patch = lumps['STBAR']
    palette = lumps['PLAYPAL'][:768]
    width, height = struct.unpack_from('<HH', patch)
    assert (width, height) == (320, 32)
    pixels = [[b'\0\0\0'] * width for _ in range(height)]
    for x in range(width):
        offset = struct.unpack_from('<I', patch, 8 + 4 * x)[0]
        while patch[offset] != 255:
            top, length = patch[offset], patch[offset + 1]
            for i in range(length):
                index = patch[offset + 3 + i]
                pixels[top + i][x] = palette[3 * index:3 * index + 3]
            offset += length + 4
    return pixels


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: render_demo_preview.py Doom-frame.ppm IWAD.WAD output-prefix')
    source = read_ppm(sys.argv[1])
    stone = read_stbar(sys.argv[2])
    prefix = sys.argv[3]
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

    # Candidate: a 190px world, then four native ammo rows in 3D-framed
    # 70x10 cells. Only source y=213..218 per ammo type contains glyphs;
    # bevels and side caps come from the unpainted WAD STBAR patch.
    for y in range(190):
        for x in range(280):
            candidate[y][x] = source[y * 208 // 190][20 + x]
    for y in range(10):
        for x in range(280):
            ammo_type, within_cell = divmod(x, 70)
            if y < 2 or y >= 8:
                stone_y = y if y < 2 else 32 - (10 - y)
                color = stone[stone_y][250 + within_cell]
            elif within_cell < 2 or (ammo_type == 3 and within_cell >= 68):
                color = stone[y * 31 // 9][249 + within_cell % 2]
            else:
                color = source[213 + ammo_type * 6 + y - 2][250 + within_cell]
            candidate[190 + y][x] = color

    # Original status pixels x=0..250 remain native and centered at x=14.
    # Narrow margins use the WAD's unused carved-stone source pixels.
    for y in range(32):
        for x in range(280):
            if x < 14:
                candidate[200 + y][x] = stone[y][292 + x]
            elif x >= 265:
                candidate[200 + y][x] = stone[y][305 + x - 265]
            else:
                candidate[200 + y][x] = source[208 + y][x - 14]

    write_ppm(prefix + '-stage.ppm', stage)
    write_ppm(prefix + '-candidate.ppm', candidate)
    assert all(candidate[200 + y][14 + x] == source[208 + y][x]
               for y in range(32) for x in range(251))
    assert all(candidate[200 + y][x] == stone[y][292 + x]
               for y in range(32) for x in range(14))
    assert all(candidate[200 + y][x] == stone[y][305 + x - 265]
               for y in range(32) for x in range(265, 280))
    assert all(candidate[192 + y][ammo_type * 70 + x]
               == source[213 + ammo_type * 6 + y][250 + x]
               for ammo_type in range(4) for y in range(6)
               for x in range(2, 68))
    assert all(candidate[padding_y][x] == stone[stone_y][250 + x % 70]
               for padding_y, stone_y in ((190, 0), (191, 1), (198, 30), (199, 31))
               for x in range(280))
    assert all(any(candidate[y][x] != black for x in range(280))
               for y in range(190, 200))
    print('Candidate: world y=0..189; framed ammo strip y=190..199; original status x=14..264,y=200..231')


if __name__ == '__main__':
    main()
