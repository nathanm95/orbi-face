#!/usr/bin/env python3
"""Render one frame of each act at a chosen moment, labelled, on one sheet.

Usage: gallery.py <out.png> [scale] act:ms[:theme[:weather[:temp]]][=label] ...
Act may be a number or a name from anim.h (e.g. LAUGH). Needs the harness
binary built by tools/host/run.sh.
"""
import os, re, subprocess, sys, tempfile
from PIL import Image, ImageDraw

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
BIN = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'orbi-harness')

src = open(os.path.join(ROOT, 'src', 'c', 'anim.h')).read()
body = src[src.index('ACT_NONE = 0'):src.index('ACT_COUNT')]
NAMES = {n: i for i, n in enumerate(re.findall(r'ACT_(\w+)', body))}


def frame(act, ms, theme, weather, temp):
    d = tempfile.mkdtemp()
    secs = ms / 1000 + 1 / 30
    subprocess.run([BIN, d, str(act), '%.3f' % secs, '1', str(theme), str(weather), str(temp)],
                   check=True, capture_output=True, cwd=ROOT, env=os.environ)
    last = sorted(f for f in os.listdir(d) if f.endswith('.ppm'))[-1]
    return Image.open(os.path.join(d, last)).convert('RGB')


def main():
    out = sys.argv[1]
    args = sys.argv[2:]
    scale = 1.0
    if args and re.fullmatch(r'[0-9.]+', args[0]):
        scale = float(args.pop(0))
    cells = []
    for a in args:
        spec, _, label = a.partition('=')
        parts = spec.split(':')
        act = int(parts[0]) if parts[0].isdigit() else NAMES[parts[0].upper()]
        ms = int(parts[1]) if len(parts) > 1 else 0
        theme = int(parts[2]) if len(parts) > 2 else 0
        weather = int(parts[3]) if len(parts) > 3 else 0
        temp = int(parts[4]) if len(parts) > 4 else 12
        im = frame(act, ms, theme, weather, temp)
        if scale != 1:
            im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
        cells.append((label or '%s@%d' % (parts[0], ms), im))
    cols = min(4, len(cells))
    w, h = cells[0][1].size
    rows = (len(cells) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * (w + 6), rows * (h + 20)), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, (label, im) in enumerate(cells):
        x, y = (i % cols) * (w + 6), (i // cols) * (h + 20) + 16
        sheet.paste(im, (x, y))
        d.text((x + 4, y - 14), label, fill=(255, 255, 255))
    sheet.save(out)
    print(out)


if __name__ == '__main__':
    main()
