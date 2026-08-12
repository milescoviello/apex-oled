# apex-oled

Drive the 128×40 OLED on a **SteelSeries Apex Pro Gen 3** keyboard from Linux.

No SteelSeries GG, no hidapi, no kernel module, no root. Just Python and
Pillow talking to `/dev/hidrawN`.

```
┌────────────────────────────────┐
│ RAM ▓▓▓▓░░░░   VRM ▓░░░░░░░    │
│ CPU    57c    2%       18W     │
│ GPU    51c    1%       11W     │
└────────────────────────────────┘
```

## The protocol

This is the part worth reading if you're here to port it somewhere else.

The keyboard exposes five HID interfaces. **Interface 1** (vendor usage page
`0xFFC0`) declares a 642-byte *feature* report — that's the screen:

```
642 bytes = [0x61] + 640-byte framebuffer + [0x00]
```

640 bytes is 128×40 pixels at 1 bit each. The framebuffer is in SSD1306
**page-major** order:

```
5 pages × 128 columns, one byte per column,
bit N of that byte = the pixel at y = page*8 + N
```

**Plain row-major packing renders as garbage.** That's the one non-obvious
detail — if pixels visibly change but look like noise, the transport is fine
and the packing is wrong.

Delivery is a `SET_REPORT` control transfer, done here with the
`HIDIOCSFEATURE` ioctl directly on the hidraw node, so there's no hidapi
dependency. hidraw expects a leading report-number byte, which is `0x00`
because this descriptor declares no report IDs; the kernel strips it before
the transfer.

Identify the right node by matching the **report descriptor** rather than
hardcoding `hidrawN` — the number moves across replugs:

```
06 c0 ff          Usage Page (Vendor 0xFFC0)
  09 f2 96 82 02  Usage 0xF2, Report Count 0x0282   ← 642 bytes
  b1 02           Feature
```

Verified on `1038:1640` (Apex Pro Gen 3, wired, full size). The Gen 3 TKL is
`1038:1642` and prior art suggests the same framing; other Apex models use a
`0x61` header with a 641-byte report.

## Install

Requires Python 3.11+ (for `tomllib`) and Pillow **built with freetype** —
without it everything falls back to a 6×11 bitmap font and font sizes are
ignored. On Gentoo that's `USE=truetype` on `dev-python/pillow`.

```bash
git clone https://github.com/kitslayer/apex-oled ~/apex-oled
~/apex-oled/install.sh
```

That symlinks the tools into `~/.local/bin`, installs a udev rule so you don't
need root, and drops a config file and an autostart entry.

## CLI

```bash
apex-oled text "line one" "line two"
apex-oled image cover.png            # auto-resized and dithered
apex-oled marquee "long scrolling text"
apex-oled watch "sensors | grep Tctl" --interval 2
journalctl -f | apex-oled pipe       # rolling tail on your keyboard
apex-oled clear
apex-oled info                       # device, protocol, daemon status
```

Also importable as a library:

```python
from importlib.machinery import SourceFileLoader
apex = SourceFileLoader('apex', '/path/to/apex-oled').load_module()

with apex.Display() as d:
    d.text('hello', 'world')
    d.show(some_pil_image)        # any PIL image, resized + dithered
    d.send(raw_640_byte_fb)       # or drive it yourself
```

## Daemon

`apex-oledd` owns the panel and arbitrates between *sources* by priority, so
producers don't race to be the last writer:

| source | priority | shows |
|---|---|---|
| `notify` | 100 | transient frames pushed by the CLI over a unix socket |
| `game` | 30 | GPU/CPU load and temps, plus **swap in/out**, while gamemode runs |
| `lyrics` | 20 | current synced lyric, else a now-playing card |
| `idle` | 0 | RAM/VRAM capacity bars over CPU and GPU rows |

While the daemon runs, CLI writes route through it as expiring top-priority
frames (`--notify SECS`, default 5). `--direct` bypasses it.

Adding a source is a subclass with `active()` and `render()`:

```python
class MySource(Source):
    name = 'mine'
    fps = 1.0

    def active(self, now):
        return something_is_happening()

    def render(self, now):
        img = Image.new('1', (WIDTH, HEIGHT), 0)
        draw_text(img, (3, 0), 'hello', self.ctx.fonts.get(10))
        return img
```

Then give it a priority in `[sources]`.

The `lyrics` source reads a small JSON state file (default
`~/.cache/nowplaying/state.json`) written by a separate now-playing daemon.
It only needs a handful of keys — `title`, `artist`, `playing`, `duration`,
`cover_file`, `lyrics_synced`, a `[[timestamp, text], ...]` lyrics list, and
an *anchor* pair (`anchor_wall`, `anchor_pos`) giving track position at a
wall-clock instant. Position is interpolated locally from that anchor, so the
display stays smooth without polling anything. Point `NOWPLAYING_STATE` at
whatever writes that shape.

## Design notes

Things learned the hard way on a 128×40 one-bit panel:

- **Derive row positions from font metrics**, never hardcode y. A 10px font
  gives a 13px line, so three rows fit 40px exactly; 11px silently pushed the
  bottom row off the panel.
- **Wrap balanced, not greedy.** Greedy strands a lone word on the last row
  and looks broken at this size.
- **Prefer smaller type over motion.** Scrolling is a last resort — a long
  lyric drops its title header and shrinks a step instead of marqueeing.
- **Album art:** a fixed 128 threshold blanked 42 of 104 test covers — every
  uniformly dark or bright one. Otsu's method per image took that to 27.
  Covers are sharpened at target size first, because downscaling to 34px
  destroys the edges 1-bit output depends on. Dithering loses to thresholding
  at this scale every time: speckle reads as noise.
- **Burn-in is real.** The frame walks a slow ±2px orbit and the panel blanks
  when the session locks.

`apex-oledd --preview out.png` renders one frame to a 4× PNG without touching
the panel — that's how to iterate on layout without squinting at a keyboard.

## Tests

```bash
apex-oled-test          # 20 checks, no keyboard required
```

Covers the packing against a slow reference implementation, a pack/unpack
round trip, balanced wrapping, truncation, bar edge cases, Otsu on dark and
bright histograms, source activation, and config fallback.

## Prior art

- [not-jan/apex-tux](https://github.com/not-jan/apex-tux) — Rust, covers
  several Apex models and lists this PID
- [SilasDaSilva/apex-pro-tkl-gen3-linux](https://github.com/SilasDaSilva/apex-pro-tkl-gen3-linux)
  — Gen 3 TKL

## License

MIT
