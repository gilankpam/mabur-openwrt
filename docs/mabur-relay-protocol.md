# mabur-relay wire protocol (version 4)

This is the committed contract between `mabur-relay` (this repo,
`feed/net/mabur-relay/src/`) and its clients — the mabur-side `RemoteCard` in
`maburgs` and the web GS's WebSocket source. It reflects spec §2 plus the
2026-09-29 v3 amendment in
`docs/superpowers/specs/2026-09-29-mabur-relay-design.md` and the 2026-10-05
v4 amendment in `docs/superpowers/specs/2026-10-05-cpe-relay-hop-design.md`
(dev-machine only, gitignored); this file is the one that ships. The source
of truth for the byte layout is `feed/net/mabur-relay/src/wire.c` plus the
golden vectors in `feed/net/mabur-relay/tests/test_wire.c`.

## 2. Wire protocol (version 4)

One message format on both transports: one message per UDP datagram, one
message per binary WebSocket message. **All fields little-endian, serialised
field by field** — the CPE is big-endian MIPS; never `memcpy` a struct. The
relay parses radiotap itself, on the CPE; no radiotap bytes cross the wire.

**Common header (4 B):** `magic u16 = 0x524D` (bytes `4D 52`, "MR"),
`ver u8 = 4`, `type u8`. `ver` is 4 for every message type — v3 is removed,
no compatibility shim.

| type | dir | body |
|---|---|---|
| `1 FRAME` | relay → client | 20 B header (below), then the 802.11 frame with the trailing 4-byte FCS stripped |
| `2 HELLO` | client → relay | empty |
| `3 TUNE` | client → relay | `tune_id u16`, `channel u8`, `sec u8` |
| `4 STATUS` | relay → client | `tune_id u16`, `state u8`, `channel u8`, `sec u8`, `owner u8`, `you_own u8`, then `u32`: `rx`, `fwd`, `foreign`, `bad_fcs`, `your_drops`, `uptime_s`, `tx`, `tx_fail`, `tx_refused`, `tx_scan_drop` |
| `5 TX` | client → relay | mcs u8, flags u8 (bit0 LDPC, bit1 STBC, bit2 short GI, bit3 40 MHz), then the 802.11 frame without FCS (24–1500 B) |
| `6 SURVEY` | relay → every subscriber, every 50 ms while ≥1 subscriber | `channel u8`, `sec u8`, `gen u16`, then `u32`: `active_ms`, `busy_ms`, `rx_ms`, `tx_ms`, `ofdm_err`, `foreign` |
| `7 SCAN` | client → relay, tune owner only | `scan_id u16`, `passes u8` (1–4), `observe_ms u8` (5–100), `n u8` (1–32), `ch[n] u8` |
| `8 SCAN_RESULT` | relay → the requester only | header: `scan_id u16`, `status u8` (0 ok, 1 not owner, 2 busy, 3 could not return, 4 invalid request), `back_channel u8`, `back_sec u8`, `count u8`; then `count` entries of `ch u8`, `pass u8`, `valid u8`, `active_ms u16`, `busy_ms u16`, `rx_ms u16`, `foreign u16`, `ofdm_err u16` |

**FRAME header (20 B), immediately after the 4 B common header:**

| bytes | field |
|---|---|
| 0–3 | common header: magic, `ver=4`, `type=1` |
| 4–7 | `seq u32` LE |
| 8 | `rx_channel u8` (0 = mid-retune) |
| 9 | `sec u8` |
| 10 | `flags u8`: bit0 bad FCS, bit1 dropped-before-this (per client), bit2 phy_valid, bit3 short GI, bit4 STBC, bit5 LDPC, bit6 40 MHz |
| 11 | `mcs u8` (`0xFF` = unknown) |
| 12–13 | `rssi[2] int8` dBm, per chain (`0x80` = absent) |
| 14–15 | `noise[2] int8` dBm, per chain (`0x80` = absent) |
| 16–19 | `tsf_lo u32` LE (radiotap TSFT, low 32 bits) |

then the 802.11 frame, FCS-stripped when the source radiotap had it (see
below).

Field semantics:

- `sec`: 0 = HT20, 1 = HT40+, 2 = HT40−. The client names the secondary; the
  relay computes no pairing.
- `seq`: incremented once per forwarded frame, shared across subscribers — a
  gap means relay→client loss (UDP / IP fragment / WS queue), distinct from air
  loss (visible in mabur's own dot11 seq).
- `rx_channel`: channel the radio was known to be on when the frame was read;
  **0 while a retune is in flight** (mirrors `RadioFrontend`'s `rx_channel`
  stamp). `sec` alongside it is the matching width.
- `flags`: bit0 bad FCS (copied from radiotap, when present); bit1 this client
  had at least one frame dropped since its previous delivered frame; bit2
  `phy_valid` — the first radiotap namespace carried `DBM_ANTSIGNAL` (on
  ath9k A-MPDU traffic, only the last subframe); bit3 short GI; bit4 STBC;
  bit5 LDPC; bit6 40 MHz. Bits 3–6 and `rssi`/`noise` are meaningful only
  when bit2 is set; `mcs` is valid on every frame regardless of `phy_valid`.
  The `flags` byte (and `dropped-before-this`, bit1) is per-client; every
  other FRAME field is identical across subscribers.
- `mcs`: the MCS index from radiotap; `0xFF` when absent/unknown.
- `rssi[2]` / `noise[2]`: per-chain (antenna 0/1) signal and noise in dBm,
  read from the extended radiotap namespaces whose `ANTENNA` field is 0 or 1;
  `0x80` (-128) means that chain's reading is absent. When `phy_valid` is set
  (the first namespace carried `DBM_ANTSIGNAL`) but no extended namespace
  supplied a per-chain reading, the relay falls back to putting that first
  namespace's combined signal/noise in `rssi[0]`/`noise[0]`; `rssi[1]`/
  `noise[1]` stay absent in that case. EVM is not carried — the ar9003
  hardware only measures it reliably on short frames, so it is meaningless
  for the video stream and stays CPE-local.
- `tsf_lo`: the low 32 bits of the first radiotap namespace's TSFT.
- `STATUS.state`: 0 tuned, 1 retuning, 2 failed, 3 refused (not owner, or
  subscriber table full). `owner`: 0 none, 1 UDP, 2 WS. `you_own`: 1 if the
  recipient is the owner.
- `STATUS.tune_id`: the request the status concerns — the in-flight one for
  state 1, the last finished one for 0/2 (0 before any `TUNE`), and the
  refused request's own id for 3 (a refusal goes only to the requester and
  does not change the relay's tune state). The reply to an *invalid* `TUNE`
  (state 2, sent only to the requester) likewise carries that request's own
  id, even while another retune is in flight.
- `STATUS` is sent to all subscribers on retune start, completion/failure and
  ownership change, and to the sender in reply to every `HELLO`.
- `STATUS`'s `u32` counters (`rx`, `fwd`, `foreign`, `bad_fcs`, `your_drops`,
  `uptime_s`, `tx`, `tx_fail`, `tx_refused`, `tx_scan_drop`) wrap at 2^32
  (~15 days at full video rate); a client comparing two readings must compute
  the delta modulo 2^32, not by plain subtraction with a sign check.
- `STATUS.tx_scan_drop`: owner `TX` frames dropped because a sweep (`SCAN`)
  was running — an RCF sent while the radio is off the op channel never
  reaches the drone. See "Sweep" below.
- A HELLO refused because the subscriber table is full (`state=3`) carries the
  relay's current/last `tune_id` (the same value a `STATUS` would carry for
  state 0/1/2) — `HELLO` has no request id of its own for the refusal to echo
  back, unlike a refused `TUNE`, which carries the request's own `tune_id`.
- A pending `TUNE` that is replaced by a newer one before it runs (see retune
  step 6 below) never gets its own `STATUS`: only the request that is
  actually latest-and-executing generates `state=1`/`0`/`2` traffic. A client
  that sent an intermediate `TUNE` in a rapid sequence should not wait for a
  `STATUS` carrying that request's `tune_id` — it will not arrive.

**TX.** Only the tune owner's `TX` is injected. A `TX` from a non-owner, with
`mcs` > 7, with a reserved flag bit set, or with a dot11 part outside 24–1500 B
is dropped and counted in `tx_refused`; the relay sends **no** STATUS for it
(at 20–60 uplink frames/s a per-frame refusal would be spam — watch the
counter). The relay builds the radiotap header (TX_FLAGS = NOACK, MCS with
known BW|MCS|GI|FEC|STBC) and appends nothing: the hardware adds the FCS. A
send error counts `tx_fail`; the frame is dropped, never retried.
Client sockets are read before the monitor socket each loop pass, so a `TX`
waits at most one in-progress forward batch (≤ 64 frames). mac80211 reports
injected frames back to the monitor vif (radiotap `TX_FLAGS`); the relay
drops these and counts `txecho` — they never reach clients.

**SURVEY.** Sent to every subscriber every 50 ms while at least one
subscriber exists (no SURVEY traffic with zero subscribers), the relay has a
radio backend, and neither a retune nor a sweep is running — **SURVEY is
paused for a sweep's whole duration** (and while a `TUNE`'s `iw` child
runs). `channel`/`sec` are the current tuning. `gen` is bumped only when
the relay rebases its survey baseline: at startup, after every retune
completes (`finish_tune`) and when a sweep returns (`finish_sweep`) —
**not** at sweep start, and not on a refused `SCAN`. The four `_ms`
counters and `ofdm_err`/`foreign` are cumulative *since `gen` began* — the
baseline is taken right after the tune completes, so the brief
tune-artifact spike in `busy_ms` is excluded. A client must only
delta two SURVEYs sharing the same `gen`; a `gen` change invalidates the
window (do not subtract across it). `ofdm_err` is the relay's cumulative
OFDM/HT PHY error count; `foreign` is cumulative non-mabur frames seen,
distinct from `STATUS.foreign` (counted at the FRAME layer, lifetime).

**SCAN.** Only the tune owner may send one; a non-owner's `SCAN` gets
`SCAN_RESULT.status = 1` and is otherwise ignored (no radio ownership
change). `passes` (1–4) and `observe_ms` (5–100) and `n` (1–32) outside
range → `status = 4`, radio untouched; a relay with **no radio backend**
(nl80211 open failed at startup — it logs "no radio backend: SCAN/SURVEY
disabled") also answers every owner `SCAN` with `status = 4`. On a valid request the relay tunes
to each of the `n` channels at HT20 in turn, `passes` times back to back
(pass 1 over every channel, then pass 2, …), spending `observe_ms` on each
before reading the survey/PHY-error delta, then returns to the tuning it
had when the `SCAN` arrived.

**SCAN_RESULT.** Goes only to the requester, never broadcast. `status`: 0 ok,
1 not owner, 2 busy (a retune or another sweep was already in progress),
3 could not retune back to the pre-sweep channel, 4 invalid request (bad
`passes`/`observe_ms`/`n`) or no radio backend. Status 1/2/4
carry `count = 0` — the sweep never ran. On status 3 the relay reads the
radio back, **adopts** that channel/`sec` as its current tuning, sets
`STATUS state = 2` and broadcasts a `STATUS` (so the owner sees itself
mistuned and re-`TUNE`s); the `SCAN_RESULT`'s `back_channel`/`back_sec`
report that readback. A `SCAN_RESULT` to a WebSocket client is sent
**priority**: when that client's send queue is full it evicts a queued
`FRAME` rather than being dropped (`SURVEY` is not priority). Each entry's `valid` is 0 when that
channel's tune during the sweep failed (the other fields are then
meaningless, not zeroed).

**Validation:** wrong magic, a short message, or unknown type → dropped and
counted. `ver` ≠ 4 → dropped and counted; the client learns the relay's version
from any `STATUS` (it answers every `HELLO`). No compatibility shims.

**RX-socket drops.** The relay's AF_PACKET receive socket can drop frames in
the kernel before `mabur-relay` ever reads them (burst beyond `SO_RCVBUF`,
even after the 1 MiB `SO_RCVBUFFORCE`/`SO_RCVBUF` sizing). These drops are
**not visible to clients in-band** — there is no wire field for them, and to
a client they look identical to air loss visible in the dot11 sequence number
inside the frame. They are counted (`PACKET_STATISTICS`'s `tp_drops`,
accumulated into the relay's own `rxdrop` counter) and logged on the device
only: the `-v` per-second debug line includes
`rxdrop N tx N txfail N txrefused N txecho N`, and each nonzero poll
additionally logs `rx socket dropped N frames` at `LOG_WARNING`. Diagnose
with `logread | grep mabur-relay` on the CPE, not from client-side counters.

**MTU:** the largest video frame today is 20 + 1,431 + 28 = 1,479 B — one IP
packet; an FCS-stripped dot11 part over 1,452 B (1500 − 20 IP − 8 UDP − 20
header) fragments. On the direct cable the kernel handles fragmentation
cheaply and `seq` exposes any loss; the acceptance run measures it. WebSocket
(TCP) is unaffected.

**Ports (defaults):** UDP 8310, WebSocket 8311.

## 3. State, retune, errors (subscribers, tune ownership, retune)

**Subscribers.** Up to 4 UDP (keyed by `addr:port`) and 2 WebSocket clients.
UDP: created by the first `HELLO` or `TUNE`, reaped after **2 s without a
`HELLO`**. WS: lives with its TCP connection and must also `HELLO` within 2 s
(reaps hung tabs). A full UDP table → `STATUS state=3` to the sender, counted.
A full WS table → the new TCP connection is closed at accept (no `STATUS` can
precede the handshake), counted. Accepted WS sockets get `SO_SNDBUF` 64 KiB so
kernel buffering cannot hide a stalled tab from the 64-message queue. Clients send
`HELLO` every 500 ms.

**Tune ownership**, re-evaluated each loop pass: the longest-lived UDP
subscriber; else the oldest WS client; else none. A `TUNE` from a non-owner →
`state=3`, radio untouched. On ownership change the radio stays put. With no
subscribers the radio stays on its last channel (a restarting `maburgs` finds
the CPE where the link was).

**Retune:**

1. Owner sends a valid `TUNE` (channel in the 5 GHz range 36–177, `sec` ∈
   {0,1,2}); invalid → `state=2`, nothing spawned. A channel the phy or
   regdomain disallows is left to `iw` to reject, which surfaces as `state=2`
   in step 4. With the shipped kmods + `REG=XM` the phy lists the whole
   20 MHz grid 36–177, so only off-grid numbers (e.g. 66, 146) get there.
2. Set `rx_channel = 0`, broadcast `state=1`, fork/exec
   `iw dev mon0 set channel <ch> <HT20|HT40+|HT40->`.
3. Frames keep flowing, stamped `rx_channel = 0`.
4. On child exit (SIGCHLD → self-pipe in `poll`): exit 0 → read back
   `iw dev mon0 info` and trust the readback, not the request; set
   `rx_channel`/`sec`, write `/tmp/mabur-relay.state`, broadcast `state=0` with
   the `tune_id`. Non-zero exit or readback ≠ request → broadcast `state=2`
   with the readback channel; radio left as is.
5. Watchdog: a child still running after 1 s is killed → `state=2`.
6. A `TUNE` arriving mid-retune becomes the single pending request (a newer
   one replaces it) and runs when the current one finishes — rapid hop
   sequences end on the latest target. A replaced pending request is simply
   discarded: it never gets a `STATUS` of its own (see the field-semantics
   note above).

**Sweep** (entered by a valid `SCAN` from the tune owner):

- `FRAME`s keep flowing throughout a sweep, stamped with the channel the
  relay is actually listening on at that instant — `rx_channel = 0` only
  while a retune between sweep channels is in flight, exactly as for a
  normal `TUNE`.
- The owner's `TX` is dropped for the sweep's whole duration and counted in
  `tx_scan_drop`, never `tx_refused`: an RCF the drone needed never crosses
  the air, because the radio is off the op channel.
- A `TUNE` arriving mid-sweep becomes the pending request (the same one-deep
  queue as mid-retune, step 6 above) and runs once the sweep returns to the
  op channel. A second `SCAN` arriving mid-sweep is refused with
  `SCAN_RESULT.status = 2`; it does not interrupt the running sweep.
- `SURVEY` is **paused** for the sweep's whole duration (no `SURVEY` is
  sent while it runs). On return the relay rebases its survey baseline and
  bumps `gen` once, so the first `SURVEY` after a sweep carries a new `gen`
  and no client-visible delta window straddles the sweep.
- The `SCAN_RESULT` is delivered only to the subscriber that sent the
  `SCAN`. The relay forgets the requester if that subscriber is reaped (UDP:
  2 s without a `HELLO`) or its WS socket closes mid-sweep — the slot may be
  reused — and the finished sweep's result is then simply dropped: nobody is
  waiting for it, and a new client in the same slot never receives it.

## Client obligations

- Send `HELLO` every 500 ms; a silent client is dropped after 2 s.
- After a client restarts (typically a new UDP socket, so a new ephemeral
  source port), its *old* `addr:port` entry is a distinct subscriber slot
  that the relay does not know is gone: it keeps its subscriber slot, and
  keeps tune ownership if it held it, until the relay reaps it (≤ 2 s
  without a `HELLO`). A `TUNE` sent by the newly-started client during that
  window is refused (`state=3`) even though it is, from the operator's point
  of view, the same logical client re-owning the link. Retry on `state=3`
  rather than treating it as a hard refusal.
- The `FRAME` payload's trailing bytes are the dot11 frame with the trailing
  4-byte FCS already stripped by the relay (when the source radiotap's
  `flags` field had bit 4, `0x10`, "frame includes FCS", set — ath9k sets
  this in monitor mode — and the dot11 part was at least 28 B). A frame
  whose dot11 part is only 24–27 B keeps its FCS: the strip only fires at
  ≥ 28 B, since stripping below that would leave less than a minimal dot11
  header. Do not parse radiotap or strip the FCS yourself; there is no
  radiotap on the wire at all.
- **WS subscription starts at the upgrade, not the first `HELLO`.** As soon as
  the WebSocket handshake completes, the client is a subscriber: it starts
  receiving `STATUS` and `FRAME` messages immediately, and — if it is the
  oldest WebSocket client and no UDP subscriber exists — it is already tune
  owner, all before it has sent its first `HELLO`. `HELLO` is purely the 2 s
  keepalive that stops the relay reaping an otherwise-idle connection; it is
  not what makes the connection a subscriber.
- A `Sec-WebSocket-Key` of 64 characters or more is rejected: the relay closes
  the TCP connection without sending an HTTP response (no `101`, no error
  status — the handshake just fails). Real browsers always send a 24-byte
  key, so this only matters to a hand-rolled client.
- Treat `FRAME.rx_channel == 0` as "channel unknown" (mid-retune).
- Detect relay→client loss from `FRAME.seq` gaps and `flags` bit1; detect
  air loss from the dot11 sequence number inside the frame.
- `flags` bits 3–6 (short GI, STBC, LDPC, 40 MHz) and `rssi`/`noise` are
  meaningful only when bit2 (`phy_valid`) is set — on ath9k A-MPDU traffic
  that's only the aggregate's last subframe. `mcs` is valid on every frame
  regardless. Width should still come from the tuned `sec`, not bit6, since
  a channel-width claim only exists on the frames where bit2 is set.
  EVM is not carried on the wire at all.
- Only the owner's `TUNE` moves the radio; watch `STATUS.you_own`.

## Golden byte vectors

From `feed/net/mabur-relay/tests/test_wire.c` (pinned so a big-endian MIPS
write regression fails the host tests and the qemu big-endian self-test the
same way):

**FRAME header**, `seq=0x01020304`, `rx_channel=136`, `sec=2` (HT40−),
`flags=MR_FLAG_BADFCS|MR_FLAG_PHY_VALID|MR_FLAG_STBC` (`0x15`), `mcs=4`,
`rssi=[-37,-44]`, `noise=[-95,-95]`, `tsf_lo=0xA1B2C3D4`:

```
4D 52 04 01 04 03 02 01 88 02 15 04 DB D4 A1 A1 D4 C3 B2 A1
└magic┘ver type └──seq (LE)───┘ch sec fl mcs └rssi┘ └noise┘ └─tsf_lo (LE)──┘
```

**TUNE**, `tune_id=0xBEEF`, `channel=149`, `sec=1` (HT40+):

```
4D 52 04 03 EF BE 95 01
└magic┘ver type └tid┘ch sec
```

**TX**, `mcs=0`, `flags=LDPC|STBC`:

```
4D 52 04 05 00 03 <dot11 frame, no FCS>
└magic┘ver type mcs flags
```

**SURVEY**, `channel=112`, `sec=2` (HT40−), `gen=0x0102`, `active_ms=1000`,
`busy_ms=300`, `rx_ms=100`, `tx_ms=0`, `ofdm_err=0x0A0B0C0D`, `foreign=7`:

```
4D 52 04 06 70 02 02 01 E8 03 00 00 2C 01 00 00 64 00 00 00 00 00 00 00 0D 0C 0B 0A 07 00 00 00
└magic┘ver type ch sec └gen┘ └──active_ms──┘ └───busy_ms───┘ └────rx_ms────┘ └────tx_ms────┘ └──ofdm_err───┘ └───foreign───┘
```

**SCAN**, `scan_id=0xBEEF`, `passes=2`, `observe_ms=20`, `n=3`,
`ch=[40, 64, 144]`:

```
4D 52 04 07 EF BE 02 14 03 28 40 90
└magic┘ver type └sid┘pa ob n  └──ch[3]──┘
```

**SCAN_RESULT**, `scan_id=0xBEEF`, `status=MR_SCAN_OK`, `back_channel=112`,
`back_sec=2`, one entry `ch=64`, `pass=1`, `valid=1`, `active_ms=20`,
`busy_ms=14`, `rx_ms=1`, `foreign=3`, `ofdm_err=0x0102`:

```
4D 52 04 08 EF BE 00 70 02 01 40 01 01 14 00 0E 00 01 00 03 00 02 01
└magic┘ver type └sid┘st bc bs cn ch pa vl └act┘ └bsy┘ └rx─┘ └fgn┘ └err┘
```
