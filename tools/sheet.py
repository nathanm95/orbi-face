#!/usr/bin/env python3
"""Lay screenshots out on one labelled contact sheet for side-by-side review.

Usage: sheet.py <out.png> <cols> <label>=<png> ...
"""
import sys
from PIL import Image, ImageDraw


def main():
    out, cols = sys.argv[1], int(sys.argv[2])
    items = [a.partition('=') for a in sys.argv[3:]]
    cell, label_h = 270, 18
    rows = (len(items) + cols - 1) // cols
    sheet = Image.new('RGB', (cell * cols, (cell + label_h) * rows), (40, 40, 40))
    d = ImageDraw.Draw(sheet)
    for i, (label, _, path) in enumerate(items):
        x, y = (i % cols) * cell + 5, (i // cols) * (cell + label_h) + label_h
        sheet.paste(Image.open(path).convert('RGB'), (x, y))
        d.text((x + 4, y - 15), label, fill=(255, 255, 255))
    sheet.save(out)


if __name__ == '__main__':
    main()
