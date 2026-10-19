#!/usr/bin/env bash
#
# managed-scan.sh — list the Wi-Fi networks a card can hear, in managed mode
#
# A normal scan: the card stays in managed mode and keeps its connection. It covers the
# channels the card can use, so it finds a network's channel without knowing it. The table
# is sorted by channel. A hidden network shows <hidden>, but its beacons still carry its
# channel and BSSID. The network the card is connected to is marked with *.
#
# Usage:
#   sudo ./scripts/managed-scan.sh -i wlp2s0          # every channel the card supports
#   sudo ./scripts/managed-scan.sh -i wlp2s0 -c 6     # one channel only
#
# The table is also saved in a new folder under logs/, as scan.txt.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LAB_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
LOGDIR="$LAB_DIR/logs"
# shellcheck source=wifi-lib.sh
source "$SCRIPT_DIR/wifi-lib.sh"

IFACE=""
CHANNEL=""
while getopts ":i:c:h" opt; do
  case "$opt" in
    i) IFACE="$OPTARG" ;;
    c) CHANNEL="$OPTARG" ;;
    h) usage 0 ;;
    *) usage ;;
  esac
done

wifi_preflight iw awk sort
[[ -n "$IFACE" ]] || usage
[[ -z "$CHANNEL" || "$CHANNEL" =~ ^[0-9]+$ ]] || die "-c must be a channel number, e.g. 6 or 36"
wifi_require_iface "$IFACE"
[[ "$(iface_type "$IFACE")" == managed ]] || die "$IFACE is not in managed mode. Run ./scripts/wifi-recover.sh -i $IFACE first."

# iw scans by frequency: 2.4 GHz channels 1-14, 5 GHz channels from 36 on.
FREQ_ARG=()
if [[ -n "$CHANNEL" ]]; then
  if (( CHANNEL == 14 )); then
    FREQ=2484
  elif (( CHANNEL >= 1 && CHANNEL <= 13 )); then
    FREQ=$(( 2407 + 5 * CHANNEL ))
  else
    FREQ=$(( 5000 + 5 * CHANNEL ))
  fi
  FREQ_ARG=(freq "$FREQ")
fi

echo "[*] Scanning $IFACE${CHANNEL:+ on channel $CHANNEL} in managed mode (the connection stays up)..."
if ! RAW=$(iw dev "$IFACE" scan ${FREQ_ARG[@]+"${FREQ_ARG[@]}"} 2>&1); then
  die "scan failed (another scan may be running; try again): $(tail -n 1 <<<"$RAW")"
fi

# One line per network: channel, signal, BSSID, name, and * when connected.
PARSED=$(awk '
  function chan_of(f) {
    if (f == 2484) return 14
    if (f >= 2412 && f <= 2472) return int((f - 2407) / 5)
    if (f >= 5000 && f <= 5900) return int((f - 5000) / 5)
    return 0
  }
  function flush() {
    if (bssid == "") return
    name = (ssid == "" || ssid ~ /^(\\x00)*$/) ? "<hidden>" : ssid
    printf "%d\t%s\t%s\t%s\t%s\n", (dsch ? dsch : chan_of(freq)), signal, bssid, name, mark
    bssid = ""
  }
  /^BSS / {
    flush()
    bssid = $2; sub(/\(.*/, "", bssid)
    mark = ($0 ~ /associated/) ? "*" : ""
    freq = 0; signal = "?"; ssid = ""; dsch = 0
    next
  }
  /^[ \t]*freq:/ && freq == 0 { freq = $2 + 0; next }
  /^[ \t]*signal:/ && signal == "?" { signal = $2; next }
  /^[ \t]*SSID:/ && ssid == "" { ssid = substr($0, index($0, "SSID:") + 6); next }
  /DS Parameter set: channel/ && dsch == 0 { dsch = $NF + 0; next }
  END { flush() }
' <<<"$RAW")

# The kernel also returns networks from earlier scans, so keep only the asked-for channel.
if [[ -n "$CHANNEL" ]]; then
  PARSED=$(awk -F'\t' -v c="$CHANNEL" '$1 == c' <<<"$PARSED")
fi

RUNDIR=$(new_run_dir "$LOGDIR" "managed-scan") || die "Could not create a run folder in $LOGDIR"
OUT="$RUNDIR/scan.txt"

COUNT=$(grep -c . <<<"$PARSED")
{
  echo "# managed scan of $IFACE${CHANNEL:+, channel $CHANNEL} at $(date '+%F %T')"
  if (( COUNT == 0 )); then
    echo "No networks heard."
  else
    sort -t$'\t' -k1,1n -k2,2nr <<<"$PARSED" | awk -F'\t' '
      BEGIN { printf "%s %-4s %-10s  %-17s  %s\n", " ", "CHAN", "SIGNAL", "BSSID", "NAME" }
      { printf "%s %-4s %-10s  %-17s  %s\n", ($5 == "*" ? "*" : " "), $1, $2 " dBm", $3, $4 }'
    echo "Channels heard: $(cut -f1 <<<"$PARSED" | sort -un | paste -sd' ' -)"
  fi
} | tee "$OUT"
echo "[*] Saved to $OUT"
