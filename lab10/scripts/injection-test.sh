#!/usr/bin/env bash
#
# injection-test.sh — check whether a Wi-Fi card can inject frames (aireplay-ng --test)
#
# Starts monitor mode, locks the card to the channel, runs the test, saves the output to
# logs/, and restores WiFi on exit. The channel is required: aireplay-ng only hears APs on
# the channel the card is on, so a test without one fails for no real reason.
#
# With -b, the test passes only if that AP answers. aireplay-ng still probes every AP on the
# channel, because its -a mode does not work on the AC8260. Without -b, one answering AP is
# enough. Injection can work for some APs and not others, so test the target itself and
# repeat the test before relying on a card.
#
# Usage:
#   sudo ./scripts/injection-test.sh -i wlp2s0 -c 1 [-b <AP_BSSID>]
#
# Exit status: 0 = injection works, 1 = it does not, or the setup failed.
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
while getopts ":i:c:b:h" opt; do
  case "$opt" in
    i) IFACE="$OPTARG" ;;
    c) CHANNEL="$OPTARG" ;;
    b) BSSID="$OPTARG" ;;
    h) usage 0 ;;
    *) usage ;;
  esac
done

wifi_preflight airmon-ng aireplay-ng iw ip nmcli systemctl
[[ -n "$IFACE" && -n "$CHANNEL" ]] || usage
[[ "$CHANNEL" =~ ^[0-9]+$ ]] || die "-c must be a channel number, e.g. 1 or 36"
wifi_require_iface "$IFACE"

mkdir -p "$LOGDIR"
: > "$AIRMON_LOG"
OUT="$LOGDIR/injection-$(date +%Y%m%d-%H%M%S)-ch$CHANNEL.txt"

wifi_guard "$IFACE"
wifi_monitor_start "$IFACE"
wifi_set_channel "$CHANNEL"

echo "[*] Testing injection on $MON, channel $CHANNEL (saving to $OUT)..."
[[ -n "$BSSID" ]] && echo "[*] $BSSID decides the result; aireplay-ng still probes every AP on the channel."
inject_test "$MON" "$OUT" "$BSSID"
rc=$?
echo "[*] Replies per AP:"
ap_summary "$OUT"
found=$(grep -oE 'Found [0-9]+ APs?' "$OUT" | tail -1)
found=${found:-aireplay-ng stopped before scanning}

if [[ -n "$BSSID" ]]; then
  ratio=$(ap_ratio "$OUT" "$BSSID")
  if (( rc == 0 )); then
    echo "[*] PASS: $BSSID answered $ratio probes on channel $CHANNEL."
    exit 0
  fi
  echo "[!] FAIL: $BSSID did not answer on channel $CHANNEL (${ratio:-not heard}, ${found:-no AP count})." >&2
else
  if (( rc == 0 )); then
    echo "[*] PASS: at least one AP answered on channel $CHANNEL, so $IFACE can inject."
    exit 0
  fi
  echo "[!] FAIL: no AP answered on channel $CHANNEL (${found:-no AP count})." >&2
fi
echo "    Check the channel and that the AP is in range. Output: $OUT" >&2
exit 1
