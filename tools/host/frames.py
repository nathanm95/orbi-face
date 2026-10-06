#!/usr/bin/env python3
"""Turn harness frames (fNNNN.ppm) into sheet.png (every frame, small) and
anim.gif (real speed, assuming the harness stride in frames.json or 1)."""
import glob, os, sys
from PIL import Image

out = sys.argv[1]
paths = sorted(glob.glob(os.path.join(out, 'f*.ppm')))
frames = [Image.open(p).convert('RGB') for p in paths]
if not frames:
    sys.exit('no frames in ' + out)
cols = min(8, len(frames))
cell = 132
rows = (len(frames) + cols - 1) // cols
sheet = Image.new('RGB', (cols * cell, rows * cell), (30, 30, 30))
for i, f in enumerate(frames):
    sheet.paste(f.resize((cell - 2, cell - 2), Image.LANCZOS), ((i % cols) * cell, (i // cols) * cell))
sheet.save(os.path.join(out, 'sheet.png'))
stride = int(os.environ.get('STRIDE', '1'))
frames[0].save(os.path.join(out, 'anim.gif'), save_all=True, append_images=frames[1:],
               duration=int(1000 / 30 * stride), loop=0)
print(len(frames), 'frames ->', os.path.join(out, 'sheet.png'))
