#!/usr/bin/env python3
"""Record everything tools/store_assets.py needs from the gabbro emulator.

Each variant is a build pinned with flags from the wscript (10:08 on the clock,
fixed weather, and either an act frozen at one moment or the demo run), so
nothing depends on pypkjs, buttons or touch once it is installed:

  demo       the GIF: laugh, cool, love, back to back     (recorded ~25s)
  time       resting face at 10:08                         (still)
  love       heart eyes                                    (still)
  bubblegum  pink theme, shades on                         (still)
  rain       raining, under the umbrella                   (still)
  party      the hourly party hat and confetti             (still, banner)
  snow       beanie and tongue out for snowflakes          (still, banner)

Usage: record_store.py <rec_dir> [variant ...]
Make sure no other session is using the gabbro emulator first.
"""
import os, re, shutil, subprocess, sys, time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
SDK = '4.33.1'
PLATFORM = 'gabbro'

src = open(os.path.join(ROOT, 'src', 'c', 'anim.h')).read()
body = src[src.index('ACT_NONE = 0'):src.index('ACT_COUNT')]
ACT = {n: i for i, n in enumerate(re.findall(r'ACT_(\w+)', body))}

BASE = {'ORBI_SHOW_TIME': '1008', 'ORBI_WEATHER': '1', 'ORBI_TEMP': '18'}
VARIANTS = {
    'demo': ({'ORBI_DEMO': '1'}, 25.0),   # two runs, so one can start at rest
    'time': ({'ORBI_FREEZE': '0'}, 0),
    'love': ({'ORBI_FREEZE': str(ACT['LOVE']), 'ORBI_FREEZE_MS': '1000'}, 0),
    'bubblegum': ({'ORBI_THEME': '2', 'ORBI_FREEZE': str(ACT['COOL']), 'ORBI_FREEZE_MS': '1500'}, 0),
    'rain': ({'ORBI_WEATHER': '6', 'ORBI_TEMP': '11', 'ORBI_FREEZE': str(ACT['RAIN']),
              'ORBI_FREEZE_MS': '3300'}, 0),
    'party': ({'ORBI_FREEZE': str(ACT['PARTY']), 'ORBI_FREEZE_MS': '1100'}, 0),
    'snow': ({'ORBI_WEATHER': '7', 'ORBI_TEMP': '-2', 'ORBI_FREEZE': str(ACT['SNOW']),
              'ORBI_FREEZE_MS': '2600'}, 0),
}
FLAGS = ('ORBI_SHOW_TIME', 'ORBI_FREEZE', 'ORBI_FREEZE_MS', 'ORBI_LOOP', 'ORBI_DEMO', 'ORBI_THEME',
         'ORBI_WEATHER', 'ORBI_TEMP', 'ORBI_WIND', 'ORBI_BATT', 'ORBI_DEBUG')


def env_for(flags, base=True):
    env = dict(os.environ, PATH=os.path.expanduser('~/.local/bin') + ':' + os.environ['PATH'])
    for k in FLAGS:
        env.pop(k, None)
    if base:
        env.update(BASE)
    env.update(flags)
    return env


def pebble(*args, env=None, timeout=240):
    try:
        r = subprocess.run(['pebble', *args], capture_output=True, text=True, timeout=timeout,
                           env=env or env_for({}), cwd=ROOT)
        return r.returncode == 0, r.stdout + r.stderr
    except subprocess.TimeoutExpired:
        return False, 'timeout'


def shot(path):
    subprocess.run(['python3', os.path.join(TOOLS, 'shot.py'), '%s@%s' % (PLATFORM, SDK), path],
                   capture_output=True)
    return os.path.exists(path)


def is_orbi(path):
    """Orbi fills the screen with his theme's base colour; anything else on
    screen (another face, a 'not responding' dialog) is grey or white."""
    from PIL import Image
    im = Image.open(path).convert('RGB')
    w, h = im.size
    centre = im.crop((w // 4, h // 3, w * 3 // 4, h * 2 // 3))
    colours = centre.getcolors(w * h)
    vivid = sum(n for n, (r, g, b) in colours if max(r, g, b) - min(r, g, b) >= 170)
    return vivid > centre.width * centre.height * 0.35


def install(pbw, check):
    for attempt in range(4):
        ok, out = pebble('install', '--emulator', PLATFORM, '--sdk', SDK, pbw)
        if ok:
            time.sleep(3)
            if shot(check) and is_orbi(check):
                return True
            print('  wrong screen, dismissing and retrying', flush=True)
        else:
            print('  install failed (%s), retrying' % out.strip().splitlines()[-1][:80], flush=True)
        pebble('emu-button', '--emulator', PLATFORM, '--sdk', SDK, 'click', 'back')
        time.sleep(4)
    return False


def main():
    rec = os.path.abspath(sys.argv[1])
    names = sys.argv[2:] or list(VARIANTS)
    os.makedirs(rec, exist_ok=True)
    for name in names:
        flags, seconds = VARIANTS[name]
        ok, out = pebble('build', env=env_for(flags))
        if not ok:
            sys.exit('build failed for %s:\n%s' % (name, out[-2000:]))
        pbw = os.path.join(rec, 'orbi_%s.pbw' % name)
        shutil.copy(os.path.join(ROOT, 'build', 'orbi-face.pbw'), pbw)
        check = os.path.join(rec, name + '.png')
        if not install(pbw, check):
            print(name, 'FAILED to get Orbi on screen', flush=True)
            continue
        if seconds:
            d = os.path.join(rec, name)
            subprocess.run(['python3', os.path.join(TOOLS, 'record.py'), '%s@%s' % (PLATFORM, SDK), d,
                            str(seconds), '--interval', '0.07'], check=True)
        else:
            time.sleep(1)
            shot(check)
        print(name, 'ok', flush=True)
    # Leave a normal, flag-free build behind.
    pebble('build', env=env_for({}, base=False))


if __name__ == '__main__':
    main()
