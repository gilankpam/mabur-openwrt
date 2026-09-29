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
rm -rf "$TMP"
[ "$fail" -eq 0 ] && echo "ALL PASS" || { echo "FAILURES"; exit 1; }
