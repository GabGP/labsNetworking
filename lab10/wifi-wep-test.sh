#!/bin/sh
#
# wifi-wep-test.sh — GL-AR300M16 target AP configuration for WEP testing (Lab #10, CS VIII)
#
# Reconfigures a GL-AR300M16 (or OpenWrt) router to broadcast a target WEP network
# for bench testing and practicing the WEP key recovery attack (wep-crack.sh).
#
# For authorized testing only. WEP is cryptographically broken (the key can be
# recovered from captured traffic in minutes without a dictionary), so never leave
# the access point on this config — restore the original WPA2 setup afterwards
# using the backup created by this script.
#
# Runs ON the router (BusyBox ash). From the host laptop connected to the LAN port:
#   ssh root@your-router-ip "sh -s -- '<key1>' ['<key2>' ...]" < wifi-wep-test.sh
#
# Environment overrides can precede 'sh' inside the remote command string:
#   ssh root@your-router-ip "AUTH=open SSID=Test sh -s -- '12345'" < wifi-wep-test.sh
#   ssh root@your-router-ip "AUTH=shared SLOT=2 sh -s -- '12345' '67890'" < wifi-wep-test.sh
#
# Keys:
#   Provide 1 to 4 keys, mapped to slots 1–4 in order.
#   Each key must be 5 or 13 ASCII characters (WEP-64 / WEP-128), or 10 or 26 hex digits.
#   152-bit keys are not supported by the router's mac80211 driver.
#
# Environment overrides:
#   SSID     network name for the main AP (default: Test)
#   AUTH     open (default, recommended for lab practice) | shared | mixed
#   SLOT     key slot used to transmit, 1–4 (default: 1); must hold a key
#   RADIO    radio device of the main AP (default: radio0)
#   IFACE    uci section of the main AP; auto-detected when unset
#   ALL_APS  1 = also put all other AP interfaces (e.g. guest network) on the
#            same WEP keys; 0 = configure main AP only (default: 0)
#
# Restore:
#   A timestamped backup is saved to /root/wireless.<timestamp>.bak before changes.
#   Restore original configuration on the router with:
#     cp /root/wireless.<timestamp>.bak /etc/config/wireless && wifi reload
#

set -eu

# ---- defaults -------------------------------------------------------------
SSID=${SSID:-Test}
AUTH=${AUTH:-open}
SLOT=${SLOT:-1}
RADIO=${RADIO:-radio0}
IFACE=${IFACE:-}
ALL_APS=${ALL_APS:-0}

# ---- helper functions -----------------------------------------------------
die() { echo "error: $*" >&2; exit 1; }

# OpenWrt wants ASCII keys prefixed with 's:' and hex keys bare.
wep_key() {
  case ${#1} in
    5|13) echo "s:$1" ;;
    10|26)
      case $1 in *[!0-9A-Fa-f]*) die "a ${#1}-character WEP key must be hex digits" ;; esac
      echo "$1" ;;
    16|32) die "152-bit WEP keys are not supported by this router's Wi-Fi driver; use 13 ASCII characters or 26 hex digits" ;;
    *) die "WEP key must be 5 or 13 ASCII characters, or 10 or 26 hex digits (got ${#1})" ;;
  esac
}

# ---- argument & environment validation ------------------------------------
[ $# -ge 1 ] && [ $# -le 4 ] || die "usage: $0 <key1> [<key2> [<key3> [<key4>]]]"
nkeys=$#
n=0
for k in "$@"; do
  n=$((n + 1))
  v=$(wep_key "$k") || exit 1
  eval "key$n=\$v"
done

case $AUTH in
  open) encryption=wep-open ;;
  shared) encryption=wep-shared ;;
  mixed) encryption=wep-mixed ;;
  *) die "AUTH must be open, shared or mixed (got '$AUTH')" ;;
esac

case $SLOT in
  1|2|3|4) [ "$SLOT" -le "$nkeys" ] || die "SLOT=$SLOT but only $nkeys key(s) given" ;;
  *) die "SLOT must be 1–4 (got '$SLOT')" ;;
esac

case $ALL_APS in 0|1) ;; *) die "ALL_APS must be 0 or 1 (got '$ALL_APS')" ;; esac

# ---- preflight ------------------------------------------------------------
command -v uci >/dev/null 2>&1 || die "uci not found — run this on the router, not the laptop"

# ---- AP interface discovery -----------------------------------------------
ap_sections() {
  for s in $(uci -q show wireless | sed -n 's/^wireless\.\([^.=]*\)=wifi-iface$/\1/p'); do
    [ "$(uci -q get "wireless.$s.mode")" = ap ] && echo "$s"
  done
  return 0
}

# GL.iNet firmware defines several wifi-iface sections (main AP, guest, repeater
# station). Pick the main AP on $RADIO, never the guest or 'sta' ones.
if [ -z "$IFACE" ]; then
  for s in $(ap_sections); do
    [ "$(uci -q get "wireless.$s.device")" = "$RADIO" ] || continue
    case $s in *guest*) continue ;; esac
    IFACE=$s
    break
  done
fi
[ -n "$IFACE" ] || die "no AP interface found on $RADIO; check 'uci show wireless' and pass IFACE=<section>"
[ "$(uci -q get "wireless.$IFACE")" = wifi-iface ] || die "wireless.$IFACE is not a wifi-iface section"

extras=
if [ "$ALL_APS" = 1 ]; then
  for s in $(ap_sections); do
    [ "$s" = "$IFACE" ] || extras="$extras $s"
  done
fi

# ---- UCI configuration ----------------------------------------------------
apply_wep() {
  uci set "wireless.$1.encryption=$encryption"
  uci set "wireless.$1.key=$SLOT"            # in WEP mode 'key' selects the slot, key1..key4 hold keys
  i=1
  while [ $i -le 4 ]; do
    if [ $i -le "$nkeys" ]; then
      eval "uci set \"wireless.$1.key$i=\$key$i\""
    else
      uci -q delete "wireless.$1.key$i" || true
    fi
    i=$((i + 1))
  done
}

# ---- backup & apply settings ----------------------------------------------
backup=/root/wireless.$(date +%Y%m%d-%H%M%S).bak
cp /etc/config/wireless "$backup"
echo "Backed up /etc/config/wireless to $backup"
echo "Configuring wireless.$IFACE: SSID '$SSID', $encryption, $nkeys key(s), transmit slot $SLOT"

radio=$(uci -q get "wireless.$IFACE.device")
uci set "wireless.$radio.disabled=0"
uci set "wireless.$IFACE.disabled=0"
uci set "wireless.$IFACE.mode=ap"
uci set "wireless.$IFACE.ssid=$SSID"
uci set "wireless.$IFACE.hidden=0"
apply_wep "$IFACE"
for s in $extras; do
  echo "Configuring wireless.$s: SSID '$(uci -q get "wireless.$s.ssid")' unchanged, $encryption"
  apply_wep "$s"
done
uci commit wireless
wifi reload

# ---- verify AP broadcast --------------------------------------------------
# Only interfaces that are enabled on an enabled radio should come up.
check=
for s in $IFACE $extras; do
  [ "$(uci -q get "wireless.$s.disabled")" = 1 ] && continue
  [ "$(uci -q get "wireless.$(uci -q get "wireless.$s.device").disabled")" = 1 ] && continue
  check="$check $s"
done

# hostapd builds without WEP support refuse the config and the interface never
# comes up, so confirm every enabled AP is actually broadcasting before reporting success.
pending=$check
i=0
while [ -n "$pending" ] && [ $i -lt 20 ]; do
  sleep 1
  i=$((i + 1))
  still=
  for s in $pending; do
    iwinfo 2>/dev/null | grep -qF "ESSID: \"$(uci -q get "wireless.$s.ssid")\"" || still="$still $s"
  done
  pending=$still
done

if [ -n "$pending" ]; then
  for s in $pending; do
    echo "error: wireless.$s ('$(uci -q get "wireless.$s.ssid")') did not come up after 20 s." >&2
  done
  echo "Likely cause: this firmware's hostapd was built without WEP, or does not accept '$encryption'." >&2
  echo "Check 'logread | grep -i hostapd'. Restore with: cp $backup /etc/config/wireless && wifi reload" >&2
  exit 1
fi

status=0
for s in $check; do
  ssid=$(uci -q get "wireless.$s.ssid")
  enc=$(iwinfo 2>/dev/null | grep -A 6 -F "ESSID: \"$ssid\"" | sed -n 's/^ *Encryption: *//p' | head -1)
  echo "$s: '$ssid' up, encryption: ${enc:-unknown}"
  case $enc in *WEP*) ;; *) echo "error: '$ssid' is up but not on WEP" >&2; status=1 ;; esac
done
[ $status -eq 0 ] || { echo "Restore with: cp $backup /etc/config/wireless && wifi reload" >&2; exit 1; }
echo "OK. Restore WPA2 with: cp $backup /etc/config/wireless && wifi reload"
