# orbi-face

An animated, expressive face watchface for the Pebble Round 2 (**gabbro**),
written in C using the Pebble SDK. The face blinks, winks, and reacts idly
between digital time/date/battery readouts, with a battery-saver mode that
freezes animation below 20%.

## Building & running

```sh
pebble build
pebble install --emulator gabbro
pebble install --phone <ip>
```

## Note on target platform

The layout is hardcoded to the 260x260 circular gabbro display (`SCREEN_W`/
`SCREEN_H`/`CX`/`CY` in `src/c/orbi_face.c`), so `targetPlatforms` in
`package.json` is deliberately restricted to `gabbro` only — it will not
look right on other screen sizes without reworking the geometry to be
resolution-independent.

## Project layout

```
src/c/           C source for the watchface
package.json     Project metadata (UUID, platforms, resources, message keys)
wscript          Build rules — usually no need to edit
```

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
