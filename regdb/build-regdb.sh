#!/bin/sh
# Compile regdb/db.txt (stock wireless-regdb 2026.05.30 + the mabur `XM`
# domain, see the comment at its tail) into <outdir>/lib/firmware/regulatory.db
# with wireless-regdb's own db2fw.py. The ImageBuilder copies <outdir> over the
# rootfs (FILES=), replacing the stock wireless-regdb file; cfg80211 in OpenWrt
# loads the unsigned db (CFG80211_REQUIRE_SIGNED_REGDB is off), and the ath
# driver is built with ATH_USER_REGD, so `iw reg set` against this db is the
# only channel restriction left on the CPE.
# Usage: regdb/build-regdb.sh <outdir>
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
OUT="${1:?usage: $0 <outdir>}/lib/firmware"
mkdir -p "$OUT"
OUT=$(cd "$OUT" && pwd)   # absolute: we cd into regdb/ next
cd "$HERE"
python3 ./db2fw.py "$OUT/regulatory.db" ./db.txt
echo "regulatory.db: $(wc -c < "$OUT/regulatory.db") bytes -> $OUT"
