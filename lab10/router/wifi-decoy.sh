#!/bin/sh
#
# wifi-decoy.sh — rogue / decoy AP farm for Lab #10 (GL-AR300M16), piped over SSH
#
# Adversarial Wi-Fi disruption for the "Captura el Router" lab competition.
# Runs ON the router (BusyBox ash), reconfiguring its radio (radio0) into adversarial
# postures that degrade rival captures while keeping your own crack clean:
#
#   --decoy-farm : N look-alike APs with the target's SSID on the same channel, each
#                  a different BSSID, one key. Rivals who grab a network by SSID crack
#                  an easy wrong key and waste time; the extra airtime also congests the
#                  channel. You still take the target by its real BSSID.
#   --evil-twin  : one rogue AP that spoofs the target's BSSID on a different channel,
#                  with a different key. Rivals doing a channel-hopping capture lock onto
#                  the wrong AP and crack a mixed-key capture that fails key recovery.
#   --restore    : restores original /etc/config/wireless from backup, stops any running
#                  flood, and reloads Wi-Fi.
#
# A single-radio router sits on one channel, so each mode fully takes over radio0
# (your original APs go down during the op and come back with --restore). Always backs
# up /etc/config/wireless to /root/wireless-decoy.bak; --restore rolls it back.
#
# Usage:
#   ssh root@<glinet-ip> "sh -s -- --decoy-farm -c <CHANNEL> [-k <KEY>] [-s <SSID>] [-n 3] [--hidden] [--flood]" < router/wifi-decoy.sh
#   ssh root@<glinet-ip> "sh -s -- --evil-twin -b <BSSID> -c <CHANNEL> [-k <KEY>] [-s <SSID>] [--hidden] [--my-channel 11]" < router/wifi-decoy.sh
#   ssh root@<glinet-ip> "sh -s -- --restore" < router/wifi-decoy.sh
#
#   -s  SSID to mimic (default: Test)
#   -c  the target AP's channel
#   -k  WEP key for the decoy or rogue APs (default: random "wrong_AP_<hex>", 13 ASCII chars)
#   -b  BSSID of the target AP (required for --evil-twin; optional for --decoy-farm)
#   -n  number of decoy APs to create, 1 to 8 (default: 3)
#   --hidden      hide the SSID in beacon frames (option hidden 1)
#   --my-channel  channel for the rogue AP under --evil-twin (default: 11 if target is not 11, else 1)
#   --flood       send broadcast packets on the LAN to pump IVs (needs an associated station)
#
# Broadcasts only become WEP IVs over the air if a station is associated to an AP. For
# heavy IV injection, run scripts/broadcast_feed.py from the laptop. Keep your crack
# locked to the real target's BSSID and channel.
#

set -eu

MODE=""
TARGET_BSSID=""
SSID="Test"
HIDDEN=0
CHANNEL=""
KEY=""
NDECOYS=3
FLOOD=0
MY_CHANNEL=""
ACTIVE_CH=""

die() { echo "wifi-decoy: error: $*" >&2; exit 1; }

usage() {
  cat <<'EOF'
wifi-decoy.sh — rogue / decoy AP farm for Lab #10 (GL-AR300M16), piped over SSH

Adversarial Wi-Fi disruption for the "Captura el Router" lab competition.
Runs ON the router (BusyBox ash), reconfiguring its radio (radio0) into adversarial
postures that degrade rival captures while keeping your own crack clean:

  --decoy-farm : N look-alike APs with the target's SSID on the same channel, each
                 a different BSSID, one key. Rivals who grab a network by SSID crack
                 an easy wrong key and waste time; the extra airtime also congests the
                 channel. You still take the target by its real BSSID.
  --evil-twin  : one rogue AP that spoofs the target's BSSID on a different channel,
                 with a different key. Rivals doing a channel-hopping capture lock onto
                 the wrong AP and crack a mixed-key capture that fails key recovery.
  --restore    : restores original /etc/config/wireless from backup, stops any running
                 flood, and reloads Wi-Fi.

A single-radio router sits on one channel, so each mode fully takes over radio0
(your original APs go down during the op and come back with --restore). Always backs
up /etc/config/wireless to /root/wireless-decoy.bak; --restore rolls it back.

Usage:
  ssh root@<glinet-ip> "sh -s -- --decoy-farm -c <CHANNEL> [-k <KEY>] [-s <SSID>] [-n 3] [--hidden] [--flood]" < router/wifi-decoy.sh
  ssh root@<glinet-ip> "sh -s -- --evil-twin -b <BSSID> -c <CHANNEL> [-k <KEY>] [-s <SSID>] [--hidden] [--my-channel 11]" < router/wifi-decoy.sh
  ssh root@<glinet-ip> "sh -s -- --restore" < router/wifi-decoy.sh

  -s  SSID to mimic (default: Test)
  -c  the target AP's channel
  -k  WEP key for the decoy or rogue APs (default: random "wrong_AP_<hex>", 13 ASCII chars)
  -b  BSSID of the target AP (required for --evil-twin; optional for --decoy-farm)
  -n  number of decoy APs to create, 1 to 8 (default: 3)
  --hidden      hide the SSID in beacon frames (option hidden 1)
  --my-channel  channel for the rogue AP under --evil-twin (default: 11 if target is not 11, else 1)
  --flood       send broadcast packets on the LAN to pump IVs (needs an associated station)

Broadcasts only become WEP IVs over the air if a station is associated to an AP. For
heavy IV injection, run scripts/broadcast_feed.py from the laptop. Keep your crack
locked to the real target's BSSID and channel.
EOF
  exit "${1:-0}"
}

# ---- args (everything after "sh -s --" lands in "$@") -----------------------
while [ $# -gt 0 ]; do
  case "$1" in
    --evil-twin)  MODE="evil-twin"; shift ;;
    --decoy-farm) MODE="decoy-farm"; shift ;;
    --restore)    MODE="restore"; shift ;;
    -b)           [ $# -ge 2 ] || die "-b requires a BSSID argument"; TARGET_BSSID="$2"; shift 2 ;;
    -s)           [ $# -ge 2 ] || die "-s requires an SSID argument"; SSID="$2"; shift 2 ;;
    --hidden)     HIDDEN=1; shift ;;
    -c)           [ $# -ge 2 ] || die "-c requires a channel argument"; CHANNEL="$2"; shift 2 ;;
    -k)           [ $# -ge 2 ] || die "-k requires a key argument"; KEY="$2"; shift 2 ;;
    -n)           [ $# -ge 2 ] || die "-n requires a number argument"; NDECOYS="$2"; shift 2 ;;
    --flood)      FLOOD=1; shift ;;
    --my-channel) [ $# -ge 2 ] || die "--my-channel requires a channel argument"; MY_CHANNEL="$2"; shift 2 ;;
    -h|--help)    usage 0 ;;
    *)            die "unknown option: $1 (try --help)" ;;
  esac
done

is_mac() { case "$1" in *[!0-9A-Fa-f:]*) return 1;; esac; [ "$(printf '%s' "$1" | tr -d ':' | wc -c)" -eq 12 ]; }
is_chan() { case "$1" in ''|*[!0-9]*) return 1;; esac; [ "$1" -ge 1 ] && [ "$1" -le 14 ]; }
rand_mac() {
  b0=$(printf '%02x' $(( ($(head -c 1 /dev/urandom 2>/dev/null | hexdump -e '1/1 "%u"') & 0xfc) | 0x02 )))
  rest=$(head -c 5 /dev/urandom 2>/dev/null | hexdump -e '5/1 ":%02x"')
  echo "${b0}${rest}"
}
wep_key() {
  case ${#1} in
    5|13) echo "s:$1" ;;
    10|26)
      case $1 in *[!0-9A-Fa-f]*) die "a ${#1}-character WEP key must be hex digits" ;; esac
      echo "$1" ;;
    16|32) die "152-bit WEP keys are not supported by this router driver; use 13 ASCII characters or 26 hex digits" ;;
    *) die "WEP key must be 5 or 13 ASCII characters, or 10 or 26 hex digits (got ${#1})" ;;
  esac
}

stop_flood() {
  if [ -f /tmp/wifi-decoy_flood.pid ]; then
    kill "$(cat /tmp/wifi-decoy_flood.pid 2>/dev/null)" 2>/dev/null || true
    rm -f /tmp/wifi-decoy_flood.pid
  fi
  for p in $(ps 2>/dev/null | grep -E 'ping.*192\.168|wifi-decoy_flood' | grep -v grep | awk '{print $1}'); do
    kill "$p" 2>/dev/null || true
  done
}

# ---- restore ---------------------------------------------------------------
if [ "$MODE" = "restore" ]; then
  stop_flood
  [ -f /root/wireless-decoy.bak ] || die "no backup at /root/wireless-decoy.bak; nothing to restore"
  cp /root/wireless-decoy.bak /etc/config/wireless
  wifi reload
  echo "wifi-decoy: restored original /etc/config/wireless from /root/wireless-decoy.bak"
  exit 0
fi

[ -n "$MODE" ] || die "mode required: --decoy-farm or --evil-twin (try --help)"
[ -n "$CHANNEL" ] || die "-c <target-channel> is required (channel 1-14)"
is_chan "$CHANNEL" || die "-c must be a channel number between 1 and 14 (got '$CHANNEL')"

if [ -z "$SSID" ]; then
  SSID="Test"
  HIDDEN=1
fi

if [ -z "$KEY" ]; then
  rand_hex=$(head -c 16 /dev/urandom 2>/dev/null | md5sum | cut -c 1-4)
  [ -n "$rand_hex" ] || rand_hex=$(printf '%04x' "$$")
  KEY="wrong_AP_${rand_hex}"
  echo "wifi-decoy: no -k given; autogenerated random WEP key '$KEY'"
fi
WEPKEY="$(wep_key "$KEY")"

case "$MODE" in
  evil-twin)
    [ -n "$TARGET_BSSID" ] || die "-b <TARGET_BSSID> is required for --evil-twin"
    is_mac "$TARGET_BSSID" || die "-b is not a valid BSSID: $TARGET_BSSID"
    if [ -n "$MY_CHANNEL" ]; then
      is_chan "$MY_CHANNEL" || die "--my-channel must be a channel number between 1 and 14 (got '$MY_CHANNEL')"
      EVIL_CH="$MY_CHANNEL"
    else
      if [ "$CHANNEL" != "11" ]; then EVIL_CH=11; else EVIL_CH=1; fi
    fi
    ACTIVE_CH="$EVIL_CH"
    ;;
  decoy-farm)
    if [ -n "$TARGET_BSSID" ]; then
      is_mac "$TARGET_BSSID" || die "-b is not a valid BSSID: $TARGET_BSSID"
    fi
    [ "$NDECOYS" -ge 1 ] 2>/dev/null || die "-n must be >= 1"
    [ "$NDECOYS" -le 8 ] 2>/dev/null || die "-n must be <= 8 (hardware limit: #{ AP } <= 8)"
    ACTIVE_CH="$CHANNEL"
    ;;
esac

# ---- radio detection -------------------------------------------------------
RADIO=${RADIO:-}
if [ -z "$RADIO" ]; then
  RADIO=$(uci -q show wireless | sed -n 's/^wireless\.\([^.=]*\)=wifi-device$/\1/p' | head -n1)
fi
RADIO=${RADIO:-radio0}
[ "$(uci -q get "wireless.$RADIO")" = "wifi-device" ] || die "radio '$RADIO' not found in /etc/config/wireless"

# ---- backup original wireless config ---------------------------------------
BACKUP=/root/wireless-decoy.bak
[ -f "$BACKUP" ] || cp /etc/config/wireless "$BACKUP"

# Delete all wifi-iface sections before creating rogue/decoy interfaces
remove_all_wifi() {
  for s in $(uci -q show wireless | sed -n 's/^wireless\.\([^.=]*\)=wifi-iface$/\1/p'); do
    uci -q delete "wireless.$s" || true
  done
  uci commit wireless 2>/dev/null || true
}

# ---- apply config ----------------------------------------------------------
case "$MODE" in
  evil-twin)
    remove_all_wifi
    uci set "wireless.$RADIO.disabled=0"
    uci set "wireless.$RADIO.channel=$EVIL_CH"

    sec="evil_twin"
    uci set "wireless.$sec=wifi-iface"
    uci set "wireless.$sec.device=$RADIO"
    uci set "wireless.$sec.network=lan"
    uci set "wireless.$sec.mode=ap"
    uci set "wireless.$sec.disabled=0"
    uci set "wireless.$sec.hidden=$HIDDEN"
    uci set "wireless.$sec.ssid=$SSID"
    uci set "wireless.$sec.macaddr=$TARGET_BSSID"
    uci set "wireless.$sec.encryption=wep-open"
    uci set "wireless.$sec.key=1"
    uci set "wireless.$sec.key1=$WEPKEY"
    uci commit wireless
    echo "wifi-decoy: evil twin configured on $RADIO ch $EVIL_CH, spoofed BSSID $TARGET_BSSID, SSID '$SSID' (hidden=$HIDDEN), key '$KEY'"
    ;;
  decoy-farm)
    remove_all_wifi
    uci set "wireless.$RADIO.disabled=0"
    uci set "wireless.$RADIO.channel=$CHANNEL"

    # Generate distinct, randomized BSSIDs for each decoy AP so they do not share
    # the hardware base MAC or look like an obvious clone cluster.
    d=1
    while [ "$d" -le "$NDECOYS" ]; do
      sec="decoy${d}"
      mac=$(rand_mac)
      uci set "wireless.$sec=wifi-iface"
      uci set "wireless.$sec.device=$RADIO"
      uci set "wireless.$sec.network=lan"
      uci set "wireless.$sec.mode=ap"
      uci set "wireless.$sec.disabled=0"
      uci set "wireless.$sec.hidden=$HIDDEN"
      uci set "wireless.$sec.ssid=$SSID"
      uci set "wireless.$sec.macaddr=$mac"
      uci set "wireless.$sec.encryption=wep-open"
      uci set "wireless.$sec.key=1"
      uci set "wireless.$sec.key1=$WEPKEY"
      d=$((d + 1))
    done
    uci commit wireless
    echo "wifi-decoy: $NDECOYS decoy APs configured on $RADIO ch $CHANNEL with randomized BSSIDs, SSID '$SSID' (hidden=$HIDDEN), key '$KEY'"
    ;;
esac

wifi reload

# ---- verify broadcast / hostapd --------------------------------------------
echo "wifi-decoy: waiting for interfaces to come up..."
i=0
up_count=0
while [ "$i" -lt 20 ]; do
  sleep 1
  i=$((i + 1))
  if command -v ubus >/dev/null 2>&1; then
    status=$(ubus call network.wireless status 2>/dev/null || true)
    if echo "$status" | grep -q '"retry_setup_failed": true'; then
      break
    fi
    up_count=$(echo "$status" | grep -c '"up": true' || true)
    [ "$up_count" -gt 0 ] && break
  fi
done

if [ "$up_count" -gt 0 ]; then
  echo "wifi-decoy: OK — radio and AP interfaces are up on channel $ACTIVE_CH."
  if command -v ubus >/dev/null 2>&1; then
    for obj in $(ubus list "hostapd.*" 2>/dev/null); do
      ifname="${obj#hostapd.}"
      ssid=$(ubus call "$obj" get_status 2>/dev/null | jsonfilter -e '@.ssid' 2>/dev/null || echo "$SSID")
      bssid=$(ubus call "$obj" get_status 2>/dev/null | jsonfilter -e '@.bssid' 2>/dev/null || echo "")
      echo "  - $ifname: SSID '${ssid:-$SSID}' BSSID ${bssid:-unknown}"
    done
  fi
else
  echo "wifi-decoy: WARNING — AP interfaces may not be broadcasting after ${i}s." >&2
  if logread 2>/dev/null | grep -E "hostapd: .*(unknown configuration item|errors found)" | tail -n 2 >&2; then
    echo "wifi-decoy: Check: hostapd lacks WEP support or configuration error." >&2
  fi
fi

# ---- optional fake-IV flood (router LAN -> APs over WiFi) ------------------
# NOTE: broadcasts only become WEP IVs over the air if a station is associated to
# an AP. Connect a test station (phone/ESP32) to see IVs appear.
if [ "$FLOOD" = "1" ]; then
  lan_ip=$(ifconfig br-lan 2>/dev/null | grep -o 'inet addr:[0-9.]*' | cut -d: -f2 || echo "")
  if [ -n "$lan_ip" ]; then
    bcast=$(echo "$lan_ip" | sed 's/\.[0-9]*$/\.255/')
    stop_flood
    nohup sh -c "while true; do ping -c 1 -s 100 -W 1 '$bcast' >/dev/null 2>&1; sleep 0.05; done" >/dev/null 2>&1 &
    echo $! > /tmp/wifi-decoy_flood.pid
    echo "wifi-decoy: flooding LAN broadcast $bcast with 100-byte packets (connect a client to decoy to emit IVs)"
    echo "            (Tip: for higher IV rates, run 'python3 scripts/broadcast_feed.py $bcast 300 600' from laptop)"
  else
    echo "wifi-decoy: could not detect LAN subnet; skipping --flood."
  fi
else
  echo "wifi-decoy: no flood. Add --flood to pump IVs (needs associated client), or run broadcast_feed.py from laptop."
fi

if [ -n "$TARGET_BSSID" ]; then
  echo "wifi-decoy: done. Keep your own crack locked to target BSSID $TARGET_BSSID ch $CHANNEL."
else
  echo "wifi-decoy: done. Keep your own crack locked to the real target BSSID on ch $CHANNEL."
fi
echo "wifi-decoy: restore with: ssh root@<glinet-ip> \"sh -s -- --restore\" < router/wifi-decoy.sh"
