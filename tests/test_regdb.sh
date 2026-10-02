#!/bin/sh
# regdb/build-regdb.sh must compile regdb/db.txt (stock wireless-regdb plus
# the mabur `XM` domain) into <outdir>/lib/firmware/regulatory.db, and `XM`
# must open the whole AR9344 5 GHz span 5150-5925 MHz at 40 MHz with no
# DFS / NO-IR flag (NO-IR would make mac80211 refuse the relay's TX inject).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
TMP=$(mktemp -d)
fail=0
ok() { echo "ok - $1"; }
nok() { echo "NOT ok - $1"; fail=1; }

# Relative outdir, as build.sh calls it (build/overlay) -- the script cds away.
(cd "$TMP" && sh "$ROOT/regdb/build-regdb.sh" overlay); rc=$?
[ "$rc" -eq 0 ] && ok "build-regdb exits 0" || nok "build-regdb exit $rc"
DB="$TMP/overlay/lib/firmware/regulatory.db"
[ -s "$DB" ] && ok "regulatory.db written" || nok "regulatory.db missing/empty"
magic=$(head -c 4 "$DB" 2>/dev/null)
[ "$magic" = "RGDB" ] && ok "RGDB magic" || nok "bad magic '$magic'"

# Country list in the binary: 2-byte alpha2 entries follow the header.
if grep -q -a "XM" "$DB"; then ok "XM present in binary db"; else nok "XM not in binary db"; fi

# Parse the source with wireless-regdb's own parser and check XM's one rule.
python3 - "$ROOT/regdb" <<'PY' && ok "XM = 5150-5925 @40, 30 dBm, no flags" || { nok "XM rule wrong (see above)"; }
import sys; sys.path.insert(0, sys.argv[1])
from dbparse import DBParser
countries = DBParser().parse(open(sys.argv[1] + "/db.txt"))
xm = countries.get("XM") or countries.get(b"XM")
assert xm is not None, "no country XM"
perms = list(xm.permissions)
assert len(perms) == 1, "expected one rule, got %d" % len(perms)
p = perms[0]
assert (p.freqband.start, p.freqband.end, p.freqband.maxbw) == (5150.0, 5925.0, 40.0), (p.freqband.start, p.freqband.end, p.freqband.maxbw)
assert p.power.max_eirp == 30.0, p.power.max_eirp
assert not p.flags, "flags set: %r" % (p.flags,)
assert b"US" in countries and b"00" in countries, "stock countries missing"
PY

# The stock world + US domains survive untouched (REG=US stays a valid choice).
grep -q "^country US: DFS-FCC" "$ROOT/regdb/db.txt" && ok "stock US kept" || nok "stock US missing"

rm -rf "$TMP"
[ "$fail" -eq 0 ] && echo "ALL PASS" || { echo "FAILURES"; exit 1; }
