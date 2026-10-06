#!/usr/bin/env python3
"""Build the appstore set in store/ from tools/record_store.py's recordings.

  gabbro_1_preview.gif    one full run of the demo (laugh, cool, love) at real
                          speed; one period of a repeating sequence, so it loops
  gabbro_2_time.png       resting face at 10:08
  gabbro_3_love.png       heart eyes
  gabbro_4_bubblegum.png  the pink theme, shades on
  gabbro_5_rain.png       raining, under the umbrella
  icon_80x80.png, icon_144x144.png   pebble publish --icon-small / --icon-large
  banner_720x320.png                 store header image

Icons are rendered by the Mac harness (tools/host), which draws exactly what
the watch draws, minus the clock. Screenshot filenames start with the platform
because `pebble publish` reads the platform from the text before the first
underscore.

Usage: store_assets.py <rec_dir>
"""
import glob, json, math, os, random, subprocess, sys, tempfile
from PIL import Image, ImageDraw, ImageFilter, ImageFont

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
OUT = os.path.join(ROOT, 'store')
HARNESS = os.path.join(os.environ.get('TMPDIR', '/tmp'), 'orbi-harness')
MAX_GIF = 4_300_000   # the store's limit is 4.4MB
PERIOD = 11.5         # demo run: laugh 3.6s + cool 4.1s + love 3.8s

NAVY = (0, 0, 85)
YELLOW = (255, 255, 0)
WHITE = (255, 255, 255)
FONT = '/System/Library/Fonts/Supplemental/Arial Rounded Bold.ttf'


def load(path):
    return Image.open(path).convert('RGB')


def round_mask(size):
    m = Image.new('L', size, 0)
    ImageDraw.Draw(m).ellipse((0, 0, size[0] - 1, size[1] - 1), fill=255)
    return m


# ------------------------------------------------------------------- GIF ----

def diff(a, b):
    """How far a frame is from the resting face, judged on the eyes and mouth
    at full size (a thumbnail can't see shades or a wink)."""
    box = (60, 60, 200, 190)
    a = a.crop(box).convert('L')
    b = b.crop(box).convert('L')
    return sum(abs(x - y) for x, y in zip(a.tobytes(), b.tobytes()))


def save_gif(d, rest, path):
    times = json.load(open(os.path.join(d, 'times.json')))
    frames = [load(os.path.join(d, 'f%03d.png' % i)) for i in range(len(times))]
    # Start on the resting face between two shows, so the GIF's first frame
    # (its thumbnail) is a calm Orbi, and run exactly one period so it loops.
    usable = [i for i, t in enumerate(times) if t + PERIOD <= times[-1]]
    start = min(usable, key=lambda i: diff(frames[i], rest))
    end = max(i for i, t in enumerate(times) if t <= times[start] + PERIOD)
    picked = frames[start:end]
    stamps = times[start:end + 1]
    colours = sorted({c for f in picked for _, c in f.getcolors(1 << 16)})
    pal = Image.new('P', (1, 1))
    flat = [v for c in colours for v in c][:768]
    pal.putpalette(flat + [0] * (768 - len(flat)))
    step = 1
    while True:
        idx = list(range(0, len(picked), step))
        pframes = [picked[k].quantize(palette=pal, dither=Image.Dither.NONE) for k in idx]
        bounds = [stamps[k] for k in idx] + [stamps[-1]]
        durations = [max(20, int(round((bounds[k + 1] - bounds[k]) * 1000))) for k in range(len(idx))]
        pframes[0].save(path, save_all=True, append_images=pframes[1:], duration=durations, loop=0,
                        optimize=False, disposal=1)
        if os.path.getsize(path) <= MAX_GIF or step > 3:
            return
        step += 1


# ----------------------------------------------------------------- icons ----

def harness_frame(act, ms, theme=0):
    if not os.path.exists(HARNESS):
        subprocess.run(['sh', os.path.join(TOOLS, 'host', 'run.sh'), tempfile.mkdtemp(), '0', '0.1'],
                       check=True, capture_output=True, cwd=ROOT)
    d = tempfile.mkdtemp()
    env = dict(os.environ, ORBI_NOPILL='1')
    subprocess.run([HARNESS, d, str(act), '%.3f' % (ms / 1000 + 1 / 30), '1', str(theme)],
                   check=True, capture_output=True, cwd=ROOT, env=env)
    last = sorted(f for f in os.listdir(d) if f.endswith('.ppm'))[-1]
    return load(os.path.join(d, last))


def act_id(name):
    src = open(os.path.join(ROOT, 'src', 'c', 'anim.h')).read()
    body = src[src.index('ACT_NONE = 0'):src.index('ACT_COUNT')]
    import re
    return re.findall(r'ACT_(\w+)', body).index(name)


def make_icon(size):
    # The cheeky wink: the most Orbi of his faces, and it still reads at 48px.
    face = harness_frame(act_id('CHEEKY'), 900)
    S = 520
    bg = Image.new('RGB', (S, S), NAVY)
    orb = face.resize((S - 24, S - 24), Image.LANCZOS)
    bg.paste(orb, (12, 12), round_mask(orb.size))
    return bg.resize((size, size), Image.LANCZOS)


# ---------------------------------------------------------------- banner ----

def make_banner(rec):
    W, H = 720, 320
    out = Image.new('RGBA', (W, H))
    d = ImageDraw.Draw(out)
    for y in range(H):   # night sky, navy into purple
        t = y / H
        d.line([(0, y), (W, y)], fill=(int(20 + 50 * t), int(10 + 5 * t), int(90 + 40 * t), 255))
    rnd = random.Random(4)
    for _ in range(70):   # stars
        x, y = rnd.randrange(W), rnd.randrange(H)
        d.point((x, y), fill=(200, 200, 255, 255))
    cols = [(255, 0, 0), (0, 170, 255), (0, 255, 85), (255, 0, 255), (255, 170, 0), (0, 255, 255)]
    for _ in range(40):   # confetti
        x, y = rnd.randrange(W), rnd.randrange(H)
        a = rnd.uniform(0, math.pi)
        dx, dy = 4 * math.cos(a), 4 * math.sin(a)
        d.line([(x - dx, y - dy), (x + dx, y + dy)], fill=rnd.choice(cols) + (255,), width=3)

    screens = [load(os.path.join(rec, n + '.png')) for n in ('party', 'rain')]
    scale = 0.86
    screens = [s.resize((round(s.width * scale), round(s.height * scale)), Image.LANCZOS) for s in screens]
    sw = screens[0].width
    gap = 22
    x0 = W - 2 * sw - gap - 24
    y0 = (H - sw) // 2
    shadow = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    sd = ImageDraw.Draw(shadow)
    for k in range(2):
        x = x0 + k * (sw + gap)
        sd.ellipse((x - 7, y0 - 3, x + sw + 7, y0 + sw + 11), fill=(0, 0, 0, 170))
    out.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(9)))
    bez = ImageDraw.Draw(out)
    for k, s in enumerate(screens):
        x = x0 + k * (sw + gap)
        bez.ellipse((x - 6, y0 - 6, x + sw + 5, y0 + sw + 5), fill=(28, 28, 34, 255))
        out.paste(s, (x, y0), round_mask(s.size))

    d = ImageDraw.Draw(out)
    d.text((28, 84), 'Orbi', font=ImageFont.truetype(FONT, 70), fill=YELLOW)
    tag = ImageFont.truetype(FONT, 18)
    d.text((32, 178), 'A little orb with', font=tag, fill=WHITE)
    d.text((32, 201), 'big feelings', font=tag, fill=WHITE)
    small = ImageFont.truetype(FONT, 13)
    d.text((32, 236), 'Weather. Hats. Faces.', font=small, fill=(255, 210, 120))
    return out.convert('RGB')


def main():
    rec = sys.argv[1]
    os.makedirs(OUT, exist_ok=True)
    for old in glob.glob(os.path.join(OUT, 'gabbro_[1-5]_*')):
        os.remove(old)
    written = []
    rest = load(os.path.join(rec, 'time.png'))
    path = os.path.join(OUT, 'gabbro_1_preview.gif')
    save_gif(os.path.join(rec, 'demo'), rest, path)
    written.append(path)
    for slot, name in (('2_time', 'time'), ('3_love', 'love'), ('4_bubblegum', 'bubblegum'),
                       ('5_rain', 'rain')):
        path = os.path.join(OUT, 'gabbro_%s.png' % slot)
        load(os.path.join(rec, name + '.png')).save(path)
        written.append(path)
    for size in (80, 144):
        path = os.path.join(OUT, 'icon_%dx%d.png' % (size, size))
        make_icon(size).save(path)
        written.append(path)
    path = os.path.join(OUT, 'banner_720x320.png')
    make_banner(rec).save(path)
    written.append(path)
    for w in written:
        im = Image.open(w)
        print('%-28s %dx%d  %dKB' % (os.path.relpath(w, ROOT), im.size[0], im.size[1],
                                     os.path.getsize(w) // 1024))


if __name__ == '__main__':
    main()
