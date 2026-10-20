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
#   ssh root@your-router-ip "sh -s -- '<key1>' ['<key2>' ...]" < router/wifi-wep-test.sh
#
# The redirect feeds this file to ssh, which passes it on stdin to 'sh -s'. Keys are the
# arguments after '--'. Without it, a key that starts with '-' is read by sh as an option.
#
# Environment overrides must come before 'sh' in the remote command string. The remote shell
# exports them to sh, and the script reads them from its environment. After 'sh' they are
# arguments instead, so "SSID=Lab" is taken as a key and rejected:
#   ssh root@your-router-ip "AUTH=open SSID=Test sh -s -- '12345'" < router/wifi-wep-test.sh
#   ssh root@your-router-ip "AUTH=shared SLOT=2 sh -s -- '12345' '67890'" < router/wifi-wep-test.sh
#
# Firmware: GL.iNet's stock hostapd (wpad-openssl 2022-01-16-cff80b4f-18.2) was built without
# WEP. With it, hostapd refuses the WEP config, the broadcast check fails, and the AP stays
# down. router/wifi-wep-setup/try-on-router.sh runs a WEP-enabled wpad from RAM, and a reboot
# removes it. The WEP settings are committed to flash, so after a reboot the AP stays down
# until the WEP wpad is installed again or WPA2 is restored. See router/wifi-wep-setup/README.md.
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
#   The script does not roll back. If the checks at the end fail, the router keeps the
#   WEP settings until you restore them.
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

# Ask netifd and hostapd, never iwinfo alone: iwinfo reads the SSID and the
# encryption from the config file netifd handed to hostapd, so it still reports
# "ESSID: ..." and "WEP" after hostapd refused that file and nothing broadcasts.
wireless_status() { ubus call network.wireless status 2>/dev/null || true; }
radio_of() { uci -q get "wireless.$1.device"; }
ifname_of() {
  wireless_status | jsonfilter -e "@.$(radio_of "$1").interfaces[@.section=\"$1\"].ifname" 2>/dev/null || true
}
hostapd_field() { ubus call "hostapd.$1" get_status 2>/dev/null | jsonfilter -e "@.$2" 2>/dev/null || true; }
broadcasting() {
  ifname=$(ifname_of "$1")
  [ -n "$ifname" ] && [ "$(hostapd_field "$ifname" status)" = ENABLED ]
}
# netifd gives up on a radio after hostapd refuses its config; no point waiting longer.
setup_failed() {
  [ "$(wireless_status | jsonfilter -e "@.$(radio_of "$1").retry_setup_failed" 2>/dev/null)" = true ]
}

pending=$check
gave_up=0
i=0
while [ -n "$pending" ] && [ $gave_up = 0 ] && [ $i -lt 20 ]; do
  sleep 1
  i=$((i + 1))
  still=
  for s in $pending; do
    if ! broadcasting "$s"; then
      still="$still $s"
      if setup_failed "$s"; then gave_up=1; fi
    fi
  done
  pending=$still
done

if [ -n "$pending" ]; then
  for s in $pending; do
    echo "error: wireless.$s ('$(uci -q get "wireless.$s.ssid")') is not broadcasting after $i s." >&2
  done
  # "unknown configuration item 'wep_key0'" means hostapd was compiled without WEP.
  logread 2>/dev/null | grep -E "hostapd: .*(unknown configuration item|errors found)" | tail -4 >&2 || true
  echo "Likely cause: this firmware's hostapd was built without WEP, or does not accept '$encryption'." >&2
  echo "Check 'logread | grep -i hostapd'. Restore with: cp $backup /etc/config/wireless && wifi reload" >&2
  exit 1
fi

status=0
for s in $check; do
  ifname=$(ifname_of "$s")
  ssid=$(hostapd_field "$ifname" ssid)
  # hostapd is running this config now, so iwinfo's reading of it is accurate.
  enc=$(iwinfo "$ifname" info 2>/dev/null | sed -n 's/^ *Encryption: *//p')
  echo "$s ($ifname): '$ssid' broadcasting, encryption: ${enc:-unknown}"
  [ "$ssid" = "$(uci -q get "wireless.$s.ssid")" ] || { echo "error: $ifname broadcasts '$ssid', not the configured SSID" >&2; status=1; }
  case $enc in *WEP*) ;; *) echo "error: '$ssid' is up but not on WEP" >&2; status=1 ;; esac
done
[ $status -eq 0 ] || { echo "Restore with: cp $backup /etc/config/wireless && wifi reload" >&2; exit 1; }
echo "OK. Restore WPA2 with: cp $backup /etc/config/wireless && wifi reload"
