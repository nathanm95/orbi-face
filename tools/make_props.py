#!/usr/bin/env python3
"""Turn the PixelLab props in art/raw/ into watch resources.

Each prop is cropped to its drawing, snapped to the watch's 64 colours with
binary alpha (the resource compiler would otherwise approximate them), flipped
where the art faces the wrong way, and written to resources/images/. A raw
RGBA copy also goes to build/host_props/ for the Mac harness.

Usage: make_props.py
"""
import os
import struct
from PIL import Image

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
RAW = os.path.join(ROOT, 'art', 'raw')
OUT = os.path.join(ROOT, 'resources', 'images')
HOST = os.path.join(ROOT, 'build', 'host_props')

# name -> flip horizontally. Order matches PropId in face.h.
PROPS = [
    ('sunglasses', False),
    ('beanie', False),
    ('umbrella', False),
    ('partyhat', False),
    ('witchhat', False),
    ('santahat', False),
    ('nightcap', True),    # its tip droops left; the face expects it on the right
    ('cloud', False),
]


# Generated smaller than the face needs: the shades must cover both eyes.
SCALE = {'sunglasses': 1.5}


def snap(v):
    return min(255, round(v / 85) * 85)


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(HOST, exist_ok=True)
    for i, (name, flip) in enumerate(PROPS, start=1):
        im = Image.open(os.path.join(RAW, name + '.png')).convert('RGBA')
        if flip:
            im = im.transpose(Image.FLIP_LEFT_RIGHT)
        px = im.load()
        for y in range(im.height):
            for x in range(im.width):
                r, g, b, a = px[x, y]
                px[x, y] = (snap(r), snap(g), snap(b), 255) if a >= 128 else (0, 0, 0, 0)
        im = im.crop(im.getbbox())
        if name in SCALE:
            # Nearest-neighbour keeps it pixel art; 3/2 doubles every other pixel.
            f = SCALE[name]
            im = im.resize((round(im.width * f), round(im.height * f)), Image.NEAREST)
        colours = {c for _, c in im.getcolors(1 << 16) if c[3]}
        if len(colours) > 15:
            # 15 colours plus transparent fits a 4-bit palette: half the RAM.
            alpha = im.getchannel('A')
            q = im.convert('RGB').quantize(colors=15, method=Image.Quantize.MEDIANCUT,
                                          dither=Image.Dither.NONE).convert('RGB')
            q = q.point(snap)
            im = Image.merge('RGBA', (*q.split(), alpha))
            colours = {c for _, c in im.getcolors(1 << 16) if c[3]}
        path = os.path.join(OUT, 'prop_%s.png' % name)
        im.save(path)
        with open(os.path.join(HOST, '%d.rgba' % i), 'wb') as f:
            f.write(struct.pack('<HH', im.width, im.height))
            f.write(im.tobytes())
        print('%-12s %3dx%-3d %2d colours' % (name, im.width, im.height, len(colours)))


if __name__ == '__main__':
    main()
