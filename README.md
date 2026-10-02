# mabur RX relay firmware for TP-Link CPE510

[![build](https://github.com/gilankpam/mabur-openwrt/actions/workflows/build.yml/badge.svg)](https://github.com/gilankpam/mabur-openwrt/actions/workflows/build.yml)

**[⬇ Download the latest firmware](https://github.com/gilankpam/mabur-openwrt/releases/latest)** — built by CI from every push to `master` (rolling `latest` prerelease).

Minimal OpenWrt firmware that turns a TP-Link CPE510 into a **remote receive
card** for mabur, an RTP-free FPV video link: the CPE hears the drone's
downlink on its panel antenna, parses radiotap on-device, and forwards every
mabur frame — a compact 20-byte header (MCS, per-chain RSSI/noise, TSF) ahead
of the 802.11 payload with FCS stripped, CRC-failed frames included — to
`maburgs` over UDP and to the web GS in **Spotter** mode over plain `ws://`.
Clients steer the radio (channel + HT40 secondary) over the same link. No key
or decrypt logic lives on the device; that stays in mabur's receivers.

- OpenWrt **25.12** (`ath79/generic`), CPE510 **v1/v2/v3**.
- Ships the in-tree `mabur-relay` daemon (single-threaded C, no library
  dependencies beyond libc) as a procd service that starts at boot and stays
  passive until a client subscribes, plus four ath9k kmod patches (SNR
  antnoise, EVM lock-quality, TX-power uncap, full 5 GHz channel grid) and a
  regulatory database with an all-5-GHz `XM` domain (see "Channels").
- Design + plan: `docs/superpowers/specs/` and `docs/superpowers/plans/`
  (gitignored, dev-machine only); the committed protocol contract is
  `docs/mabur-relay-protocol.md`.

## Build

Requires Docker. All inputs are pinned in `versions.env`.

```sh
./build.sh all       # package (+ qemu big-endian self-test) then images
# or individually:
./build.sh package   # compile the mabur-relay .apk (+ qemu big-endian self-test)
./build.sh image     # assemble the CPE510 images
```

The first run builds two Docker images (OpenWrt SDK ~3.5 GB, ImageBuilder
~1.7 GB) — it downloads the pinned SDK/ImageBuilder and clones the `base` +
`packages` feeds, then caches all of it. Subsequent builds reuse the images.
The relay source lives in-tree at `feed/net/mabur-relay/src/`; edit it and
rerun `./build.sh package`.

> The ImageBuilder stage fetches package indexes from `downloads.openwrt.org`
> at image-build time. On a host with a broken IPv6 route to that server, GNU
> `wget` stalls retrying over IPv6 before it gives up — the build scripts do
> not force IPv4, so the fix is the host's IPv6 route, not a build flag.

Outputs land in `output/`:
- `*tplink_cpe510-v{1,2,3}-squashfs-sysupgrade.bin` — flash from OpenWrt.
- `*tplink_cpe510-v{1,2,3}-squashfs-factory.bin` — flash from TP-Link firmware / TFTP.

### Notes on OpenWrt 25.12 (apk)

25.12 uses the `apk` package manager. The package is built as
`mabur-relay-<version>-r<rel>.apk`; the ImageBuilder picks it up from its
local `packages/` dir and `ADD_LOCAL_KEY=1` makes the image trust it. The
firewall stack (`firewall4` + `nftables` + `kmod-nft-*`) is removed —
this is a one-port appliance, so it adds no value and frees rootfs
(installed footprint 84 packages; measured sysupgrade images 7,803,460–
7,803,748 bytes against the 7,864,320-byte partition budget). The CPE510
sysupgrade image is a **fixed-layout** ~7.8 MB (tplink-safeloader), so
removing packages frees read-only rootfs/overlay space rather than shrinking
the `.bin`; if you add
packages and the rootfs overflows the partition, trimming further defaults is
the lever. With dnsmasq shipped (the DHCP lease, `90-mabur-lan`), the v3
sysupgrade image measures **7,803,748 B of 7,864,320 B** (~60 KB headroom).

## Flash

Pick the file matching your hardware revision. From stock TP-Link: use the
`-factory.bin` (Pharos web UI or TFTP recovery). From an existing OpenWrt:
`sysupgrade -n <...-sysupgrade.bin>` (use `-n`, do not keep settings).

## Configure & run

1. Connect your computer to the CPE510 (PoE LAN port) and leave its NIC on
   DHCP: the CPE is **10.83.11.1** and leases **10.83.11.100–199** with no
   gateway and no DNS, so your Wi-Fi internet keeps working.

   dnsmasq refuses to serve any leases at all if it detects another DHCP
   server already on the LAN at start (OpenWrt's `dhcp_check`), and it
   caches that refusal until reboot. If the CPE was ever plugged into a LAN
   with its own router/DHCP server, power-cycle it before plugging a laptop
   in directly, or it will sit there handing out nothing. We deliberately do
   **not** set `dhcp.lan.force`: forcing dnsmasq to serve anyway would hand
   out gateway-less 10.83.11.x leases to every other host on such a LAN too.
2. The `mabur-relay` service **starts at boot** and stays passive — radio up,
   nothing forwarded to anyone — until a client subscribes. There is nothing
   to enable and no key to configure.
3. `/etc/mabur-relay.conf` (shell-sourced; edit, then reboot or
   `/etc/init.d/mabur-relay restart` to apply):

   ```
   PHY=phy0            # radio phy
   MON=mon0            # monitor vif the relay listens on
   REG=XM              # regulatory domain: XM = every 5 GHz channel the AR9344 tunes (36-177, no DFS/NO-IR); US etc. = that country's limits. Your legal responsibility.
   TXPOWER=            # phy TX power in mBm (e.g. 2000 = 20 dBm); empty = driver default
   BOOT_CHANNEL=136    # channel until a client tunes (a respawn uses the last tuned one)
   BOOT_SEC=HT40-      # HT20 | HT40+ | HT40-
   UDP_PORT=8310       # maburgs: HELLO/TUNE in, FRAME/STATUS out
   WS_PORT=8311        # web GS (Spotter) over ws://
   ```

4. Ports: **UDP 8310** for `maburgs`, **`ws://` 8311** for the web GS Spotter.
   Whichever subscriber owns the tune (the longest-lived UDP client, else the
   oldest WebSocket client, else none) steers the radio with `TUNE` messages.
5. Web GS: the hosted page (<https://gilankpam.github.io/mabur/>) connects to
   `ws://10.83.11.1:8311` by itself — Chrome 142+ lets an https page reach a
   private IP after a one-time "local network access" prompt. Firefox needs
   the local page (`python3 web/serve.py 8808` in the mabur repo). See mabur
   `docs/web-gs.md`, "CPE relay radio".

See `docs/mabur-relay-protocol.md` for the wire protocol contract and
`docs/verify-mabur-relay-on-device.md` for the on-device acceptance runbook.

## Channels and regulatory domain

Stock ath9k lists only 36-140 and 149-165: its static 5 GHz channel table
predates channel 144 and was never extended, so a `TUNE` to 144 (or 169-177)
fails in `iw` whatever the regdomain says. Patch
`997-ath9k-full-5ghz-chantable` fills in the whole 20 MHz grid **36-177**
(adds 68-96, 144, 169, 173, 177; the AR9344 synthesizer tunes all of it, cal
data past 5825 MHz is the last pier's).

Which of those channels may be *used* is then purely the kernel regdb's call
(`iw reg set $REG`; the ath driver is built with `ATH_USER_REGD`, so the
EEPROM region is ignored). The image ships `regdb/db.txt` = stock
wireless-regdb 2026.05.30 plus one extra domain:

- **`XM`** (shipped default): one rule `5150-5925 MHz @ 40, 30 dBm`, no DFS,
  no NO-IR. Every channel in the table is tunable and injectable.
- **`US`, `DE`, ...**: the real rules. Note that NO-IR (e.g. US 5850-5895,
  channels 169-177) lets the relay *receive* but makes mac80211 refuse its
  TX injection, so the RCF uplink silently dies there; DFS alone is fine for
  a monitor vif.

`regdb/build-regdb.sh` compiles the db at image time (`tests/test_regdb.sh`
checks the `XM` rule). Using channels your jurisdiction does not allow is
your responsibility.

## Radio metrics: SNR and EVM

The image carries patched `mac80211`/`ath9k` kmods that expose two extra
per-frame metrics in the monitor-mode radiotap header, which `mabur-relay`
parses on the CPE:

- **SNR [dB]** — the relay forwards per-chain RSSI and noise (from the
  calibrated noise floor, radiotap `DBM_ANTNOISE`) in the v2 `FRAME` header;
  maburgs / the web GS compute SNR client-side from that pair (see
  `docs/mabur-relay-protocol.md`).
- **EVM [dB]** — `|EVM|` in dB, higher = better, from the ar9003 RX descriptor's
  per-pilot EVM (radiotap `LOCK_QUALITY`). EVM is per *spatial stream*, not
  per antenna. It is **not carried over the wire** in v2 — see the limitation
  below for why it stays CPE-local.

### ⚠ ath9k EVM limitation (CPE510 / AR9344)

The AR9344's ar9003 hardware only measures EVM reliably for **short frames**.
For **long frames it returns "measurement-failed" markers** instead of real EVM,
so the driver drops those readings (requires ≥3 valid pilot bytes, and discards
any frame containing a failure marker). In practice this means **EVM is reported
only for the short control streams (mavlink / tunnel) and is blank (`--`) for the
video stream** — the high-bitrate stream you'd most want it for. Control streams
typically read ~15–25 dB; the gap below SNR is genuine non-thermal impairment
(multipath / interference). For the video link, rely on **SNR + FEC-recovery**
stats instead. (This is hardware-specific to ar9003; the Realtek 88x2 path
computes EVM differently and isn't subject to this limit.)

## On-device smoke test

```sh
ssh root@10.83.11.1
logread | grep mabur-relay   # e.g. "up: mon0 on 136 HT40-"
iw dev                       # mon0 present, type monitor
```

From the host wired to the CPE:

```sh
python3 relayprobe.py 10.83.11.1
```

(`relayprobe.py` is the throwaway client in
`docs/verify-mabur-relay-on-device.md`, which also has the full acceptance
runbook: frame-rate/loss gates, retune latency, and the browser Spotter
smoke test.)
