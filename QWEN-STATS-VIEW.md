# Task: add a "local AI stats" view to the Apex OLED

Add a new display source showing live stats for the local Qwen3.8-27B inference stack
(llama.cpp on the RTX 5090). The interesting thing to surface is **prefill progress** —
a large session can take 5+ minutes to process its prompt before emitting a single
token, and right now there is no way to tell "working" from "hung" without tailing logs.

This should coexist with the existing lyrics view, not replace it.

## Data source — do not parse logs, use this

`qwen-status --json` (at `~/.local/bin/qwen-status`) is the stable contract. It shells
out to `nvidia-smi` and parses llama-server's log internally so you don't have to.
Costs ~200 ms, safe to call once per second.

```json
{
  "model_up": true,
  "proxy_up": true,
  "ctx_size": 229376,
  "prefill": {
    "tokens_done": 200759,
    "tokens_total": 200759,
    "fraction": 1.0,
    "tok_per_s": 503.2,
    "elapsed_s": 398.9,
    "eta_s": 0.0,
    "complete": true
  },
  "gen_tok_s": 99.9,
  "gpu": {
    "util_pct": 96, "power_w": 135.6,
    "mem_used_mib": 23495, "mem_total_mib": 24463, "temp_c": 74
  },
  "ts": 1786993562.8
}
```

Every key is always present. `prefill`, `gen_tok_s` and `gpu` are `null` when unknown
(e.g. `prefill` is null before the server has handled any request). Treat `null`
defensively — the server gets restarted often and the log is truncated on restart.

Field notes:
- `prefill.fraction` is 0.0–1.0. `complete: true` means the prompt is fully ingested
  and the model is now generating.
- `prefill.tokens_total` is derived (`done / fraction`), so it wobbles by a few tokens
  between samples. Don't display it as authoritative to the token.
- `gen_tok_s` is from the **last completed** turn, not live. There is no live decode
  rate available.
- `ctx_size` is the server's `n_ctx_slot`, currently 229376. Useful as the denominator
  for "how full is the window": `prefill.tokens_done / ctx_size`.

## Display constraints

From the existing `apex-oled` code: **128x40, 1bpp**, framebuffer is SSD1306
**page-major** (5 pages x 128 columns, bit N of each byte = pixel at `y = page*8 + N`).
Row-major packing renders as garbage — this is already handled by `apex.render_lines()`
and `apex.pack()`, so use those rather than rolling your own.

Practically 128x40 fits about **3 lines** of the existing small font, or 2 lines plus a
progress bar.

## Suggested layout

While prefilling (the state worth watching):

```
FILL  68%  1376/s
[############....]
183k/224k  eta 34s
```

While generating or idle:

```
QWEN  gen 99.9 t/s
GPU 96%  136W  74C
ctx 201k/224k
```

Down state — worth making visually distinct, since a dead server is the thing you most
want to notice:

```
QWEN  MODEL DOWN
```

A progress bar drawn as filled pixels will read better than ASCII blocks at this size;
your call.

## Activity gating

Don't stomp the lyrics view. Suggested rule: this source is **active** only when

- `model_up` is false (something is broken — worth interrupting for), or
- `prefill` exists and `complete` is false (a prefill is genuinely in progress), or
- a generation finished within the last ~10 s (`gen_tok_s` fresh)

Otherwise it should yield. In practice the stack is idle most of the time, so an
always-on AI view would just displace lyrics for no benefit.

## Acceptance

1. During a long prefill, the OLED shows a progress bar that visibly advances, plus
   tok/s and an ETA.
2. When the model server is down, that is obvious at a glance.
3. When the stack is idle, lyrics still display as they do now.
4. `qwen-status --json` returning `null` fields, or failing entirely, does not crash the
   daemon or wedge the display.

## Easy way to generate a long prefill for testing

```bash
# ~150k tokens of filler -> several minutes of prefill to watch
python3 -c "print('The maintenance crew logged the reading. ' * 15000)" > /tmp/big.txt
curl -s http://127.0.0.1:8090/v1/chat/completions -H 'content-type: application/json' \
  -d "$(python3 -c "
import json;print(json.dumps({'model':'m','max_tokens':16,
 'messages':[{'role':'user','content':open('/tmp/big.txt').read()+' Reply OK.'}]}))")" >/dev/null &
watch -n1 'qwen-status --json'
```

## Context you may want

- Full writeup of the inference stack, including why prefill is the bottleneck:
  `~/models/QWEN3.8-27B-RESULTS.md`
- Launchers: `qwen38` (server), `qwen-claude` (Claude Code on the local model),
  `qwen-agent`, `qwen-status`
- Repo: `github.com/kitslayer/qwen38-local` (private)
- The terminal version of this view is `qwen-status` — reuse its layout ideas, but note
  it assumes 96 columns, which is 6x the OLED's usable width.
