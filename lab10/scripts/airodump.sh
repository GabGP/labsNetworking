#!/usr/bin/env bash
#
# airodump.sh — watch or capture Wi-Fi traffic with airodump-ng, then restore WiFi
#
# Starts monitor mode and runs airodump-ng in the foreground, so the AP table shows here.
# Use it to find a target's BSSID and channel. Ctrl-C stops it and restores WiFi.
# Files go to logs/<prefix>-01.cap and logs/<prefix>-01.csv.
#
# airodump-ng hops 2.4 GHz channels unless told otherwise. The AC8260 can also see 5 GHz
# (-B abg); the RTL8723BE cannot.
#
# Usage:
#   sudo ./scripts/airodump.sh -i wlp2s0                # hop 2.4 GHz channels, list APs
#   sudo ./scripts/airodump.sh -i wlp2s0 -B abg          # hop 2.4 and 5 GHz channels
#   sudo ./scripts/airodump.sh -i wlp2s0 -c 6 [-b <AP_BSSID>] [-w prefix]
#
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LAB_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
LOGDIR="$LAB_DIR/logs"
AIRMON_LOG="$LOGDIR/airmon.log"
# shellcheck source=wifi-lib.sh
source "$SCRIPT_DIR/wifi-lib.sh"

IFACE=""
CHANNEL=""
BSSID=""
AIRODUMP_BAND="bg"
PREFIX="scan-$(date +%Y%m%d-%H%M%S)"
while getopts ":i:c:b:B:w:h" opt; do
  case "$opt" in
    i) IFACE="$OPTARG" ;;
    c) CHANNEL="$OPTARG" ;;
    b) BSSID="$OPTARG" ;;
    B) AIRODUMP_BAND="$OPTARG" ;;
    w) PREFIX="$OPTARG" ;;
    h) usage 0 ;;
    *) usage ;;
  esac
done

wifi_preflight airmon-ng airodump-ng iw ip nmcli systemctl
[[ -n "$IFACE" ]] || usage
[[ -z "$CHANNEL" || "$CHANNEL" =~ ^[0-9]+$ ]] || die "-c must be a channel number"
[[ "$AIRODUMP_BAND" =~ ^[abg]+$ ]] || die "-B takes a combination of a, b and g, e.g. bg or abg"
wifi_require_iface "$IFACE"

mkdir -p "$LOGDIR"
: > "$AIRMON_LOG"

wifi_guard "$IFACE"
wifi_monitor_start "$IFACE"

echo "[*] Capturing to $LOGDIR/$PREFIX-01.cap and .csv. Ctrl-C stops and restores WiFi."
airodump_start "$LOGDIR/$PREFIX" "$CHANNEL" "$BSSID"
