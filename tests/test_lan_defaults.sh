#!/bin/sh
# 90-mabur-lan must set the LAN to 10.83.11.1/24 and a DHCP pool that hands
# out NO router and NO DNS (a laptop on Wi-Fi keeps its internet). Runs the
# script against a stub `uci` that logs its args and stdin.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)
SCRIPT="$ROOT/files/etc/uci-defaults/90-mabur-lan"
TMP=$(mktemp -d); BIN="$TMP/bin"; mkdir -p "$BIN"
printf '#!/bin/sh\necho "ARGS $*" >> "%s/log"\ncat >> "%s/log"\n' "$TMP" "$TMP" > "$BIN/uci"
chmod +x "$BIN/uci"
fail=0
PATH="$BIN:$PATH" sh "$SCRIPT"; rc=$?
[ "$rc" -eq 0 ] && echo "ok - script exits 0" || { echo "NOT ok - exit $rc"; fail=1; }
need() {
  if grep -qxF "$1" "$TMP/log"; then echo "ok - $1"; else echo "NOT ok - missing: $1"; fail=1; fi
}
need "set network.lan.ipaddr='10.83.11.1'"
need "set network.lan.netmask='255.255.255.0'"
need "set dhcp.lan.start='100'"
need "set dhcp.lan.limit='100'"
need "set dhcp.lan.leasetime='12h'"
need "delete dhcp.lan.ignore"
need "delete dhcp.lan.dhcp_option"
need "add_list dhcp.lan.dhcp_option='3'"
need "add_list dhcp.lan.dhcp_option='6'"
need "set dhcp.@dnsmasq[0].port='0'"
need "commit network"
need "commit dhcp"
# Revert check: an option-3/6 list with a value (e.g. '3,10.83.11.1') would
# hand out a default route and kill the client's Wi-Fi internet.
if grep -q "dhcp_option='[36]," "$TMP/log"; then echo "NOT ok - router/DNS option carries a value"; fail=1; fi
# Order check: `delete dhcp.lan.dhcp_option` must run BEFORE the two
# `add_list` calls that populate the (empty) router/DNS options. Reversed,
# the trailing delete wipes the suppression and the lease gets a real
# router/DNS again, silently.
del_line=$(grep -nxF "delete dhcp.lan.dhcp_option" "$TMP/log" | head -1 | cut -d: -f1)
add3_line=$(grep -nxF "add_list dhcp.lan.dhcp_option='3'" "$TMP/log" | head -1 | cut -d: -f1)
add6_line=$(grep -nxF "add_list dhcp.lan.dhcp_option='6'" "$TMP/log" | head -1 | cut -d: -f1)
if [ -n "$del_line" ] && [ -n "$add3_line" ] && [ -n "$add6_line" ] \
   && [ "$del_line" -lt "$add3_line" ] && [ "$del_line" -lt "$add6_line" ]; then
  echo "ok - delete dhcp.lan.dhcp_option precedes both add_list dhcp_option lines"
else
  echo "NOT ok - delete dhcp.lan.dhcp_option must precede both add_list dhcp_option lines"
  fail=1
fi
rm -rf "$TMP"

# Failure path: uci-defaults deletes a script that exits 0, so a script that
# swallows uci's exit status would never get retried on the next boot after
# a failed apply. The script's own exit status must be uci's.
TMP2=$(mktemp -d); BIN2="$TMP2/bin"; mkdir -p "$BIN2"
printf '#!/bin/sh\necho "ARGS $*" >> "%s/log"\ncat >> "%s/log"\nexit 1\n' "$TMP2" "$TMP2" > "$BIN2/uci"
chmod +x "$BIN2/uci"
PATH="$BIN2:$PATH" sh "$SCRIPT"; rc2=$?
if [ "$rc2" -ne 0 ]; then
  echo "ok - script exits non-zero when uci fails (so uci-defaults retries it)"
else
  echo "NOT ok - script exited 0 despite uci failing; uci-defaults will delete it and never retry"
  fail=1
fi
rm -rf "$TMP2"

[ "$fail" -eq 0 ] && echo "ALL PASS" || { echo "FAILURES"; exit 1; }
