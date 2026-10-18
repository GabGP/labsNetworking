#!/usr/bin/env bash
#
# wifi-recover.sh — put a Wi-Fi card back in managed mode and restart NetworkManager
#
# Run it when a capture was killed or crashed and WiFi stayed dark. It handles both driver
# styles (see wifi-lib.sh) and changes nothing on a card that is already fine.
#
# Usage:
#   sudo ./wifi-recover.sh [-i wlp2s0]
#
# Without -i it uses the first Wi-Fi interface that iw lists.
#
set -u

LAB_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LOGDIR="$LAB_DIR/logs"
AIRMON_LOG="$LOGDIR/airmon.log"
# shellcheck source=wifi-lib.sh
source "$LAB_DIR/wifi-lib.sh"

IFACE=""
while getopts ":i:h" opt; do
  case "$opt" in
    i) IFACE="$OPTARG" ;;
    h) usage 0 ;;
    *) usage ;;
  esac
done

wifi_preflight ip iw nmcli systemctl
IFACE=${IFACE:-$(iw dev | awk '/Interface/{print $2; exit}')}
[[ -n "$IFACE" ]] || die "No Wi-Fi interface found. Pass one with -i."
wifi_require_iface "$IFACE"

mkdir -p "$LOGDIR"
# A separate <iface>mon vif (Intel), or the interface itself in monitor mode (Realtek).
if [[ -n "$(iface_type "${IFACE}mon")" ]]; then
  MON="${IFACE}mon"
elif [[ "$(iface_type "$IFACE")" == monitor ]]; then
  MON="$IFACE"
fi

wifi_restore "$IFACE" "$MON"
