# Doom on the OLED

128x40, one bit deep, running the shareware WAD.

```bash
./build.sh          # fetches doomgeneric, builds, installs
apex-doom           # APEX_DOOM_WAD=/path/to/other.wad to change WAD
```

## How it looks like anything at all

**Geometry.** Doom renders 320x200. The status bar is the bottom 32 rows, and
taking the middle 100 rows of the remaining play area gives exactly 320x100 -
3.2:1, the panel's aspect - so the downscale to 128x40 is a uniform 2.5x with
no distortion, just letterboxed to the horizon.

**Max-pooling, not averaging.** Averaging a 2.5x2.5 block of a detailed frame
converges everything toward mid-grey, and mid-grey at one bit is noise. Taking
the brightest pixel in each block keeps small bright features - an imp against
a dark wall, a muzzle flash - that averaging would blend away.

**Auto-exposure.** A fixed threshold cannot serve both a black corridor and a
bright courtyard. The cutoff is chosen per frame from a histogram so a roughly
constant fraction of the panel stays lit.

**Threshold, not dither, by default.** Ordered dithering is available, but
error-diffusion style noise reshuffles every pixel each frame and the whole
screen boils in motion.

Tunable at runtime, no rebuild:

| variable | default | effect |
|---|---|---|
| `APEX_DOOM_LIT` | `0.38` | target fraction of the panel lit, 0.05-0.95 |
| `APEX_DOOM_POOL` | `max` | `max` or `avg` downsampling |
| `APEX_DOOM_DITHER` | `0` | `1` for ordered dithering |

## Input

The Apex keyboard's evdev node is grabbed with `EVIOCGRAB`, so WASD does not
type into whatever window has focus. A game should own input; this is the one
place that trade is worth it. The grab is per-fd, so it dies with the process
even on a crash, and your laptop's built-in keyboard is a separate device that
stays usable as an escape hatch.

`linux/input-event-codes.h` and `doomkeys.h` both define `KEY_*` with different
values - `KEY_ENTER` is 28 in one and 13 in the other - so evdev codes are
spelled out numerically in the backend rather than by name. Mapping Enter to
`KEY_USE` instead of `KEY_ENTER` leaves the menu navigable but unselectable.
