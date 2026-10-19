#!/usr/bin/env bash
#
# wifi-lib.sh — shared Wi-Fi helpers for the lab10 scripts (sourced, not run)
#
# Monitor mode, and the WiFi recovery that runs on every exit, for both driver styles:
#   separate vif  Intel iwlwifi: airmon-ng adds <iface>mon; <iface> stays managed.
#   single vif    Realtek rtl8723be: <iface> itself switches to monitor mode.
#
# The sourcing script sets AIRMON_LOG (airmon-ng and NetworkManager output goes there)
# before it calls anything below.
#
# Typical use:
#   source "$SCRIPT_DIR/wifi-lib.sh"
#   wifi_preflight airmon-ng airodump-ng ...
#   wifi_guard "$IFACE"              # from here on, any exit restores WiFi
#   wifi_monitor_start "$IFACE"      # sets MON
#   wifi_set_channel 6
#   airodump_start "$LOGDIR/prefix" 6 "" bg
#
WIFI_PIDS=()         # background processes stopped before the card is restored
WIFI_KILLED=0        # 1 once airmon-ng check kill has stopped NetworkManager
MON=""               # monitor interface, set by wifi_monitor_start
TTY_STATE=""         # terminal settings saved by wifi_guard, put back on exit

die() { echo "[!] $*" >&2; exit 1; }

# Print the comment header of the calling script as help, then exit (1 unless given a code).
usage() { sed -n '2,${ /^#/!q; s/^# \{0,1\}//p }' "$0"; exit "${1:-1}"; }

# Type of an interface ("managed", "monitor", ...); empty if the interface doesn't exist.
iface_type() { iw dev "$1" info 2>/dev/null | awk '$1=="type"{print $2}'; }

# Stop before anything changes the card if the interface is not there. Lists the Wi-Fi
# interfaces that are, since a renamed card is the usual reason.
wifi_require_iface() {
  if [[ -z "$(iface_type "$1")" ]]; then
    local have
    have=$(iw dev | awk '/Interface/{print $2}' | paste -sd' ')
    die "$1 does not exist. Wi-Fi interfaces now: ${have:-none}. Check: iw dev"
  fi
}

wifi_preflight() {
  [[ $EUID -eq 0 ]] || die "Run with sudo."
  local t
  for t in "$@"; do
    command -v "$t" >/dev/null || die "Missing: $t"
  done
}

# Background processes are stopped in the order they were tracked.
track_pid() { WIFI_PIDS+=("$1"); }

stop_tracked() {
  local pid
  for pid in "${WIFI_PIDS[@]}"; do kill "$pid" 2>/dev/null; done
  # Wait, so airodump-ng finishes its files before the card leaves monitor mode.
  for pid in "${WIFI_PIDS[@]}"; do wait "$pid" 2>/dev/null; done
  WIFI_PIDS=()
}

# Call before anything changes the card. From then on, any exit (including Ctrl-C and
# TERM) stops the tracked processes and restores WiFi and the terminal.
wifi_guard() {
  IFACE=$1
  TTY_STATE=$(stty -g 2>/dev/null)
  trap wifi_cleanup EXIT
  # Ctrl-C and TERM become a real exit, which fires the EXIT trap. Trapping cleanup on
  # INT itself would run it and then carry on with the script.
  trap 'exit 130' INT TERM
}

# Runs on every exit. The background processes are stopped and waited for first, so their
# last output comes before ours. Then the terminal settings airodump-ng changed are put back.
wifi_cleanup() {
  # A second Ctrl-C must not cut the restore short, or the card stays in monitor mode.
  trap '' INT TERM
  stop_tracked
  if [[ -n "$TTY_STATE" ]]; then stty "$TTY_STATE" 2>/dev/null; fi
  if [[ -t 1 ]]; then tput cnorm 2>/dev/null; fi
  echo; echo "[*] Cleaning up (Ctrl-C will not stop this)..."
  wifi_restore "$IFACE" "$MON"
}

# Stop NetworkManager and wpa_supplicant, then put $1 into monitor mode. Sets MON to the
# monitor interface: $1 itself on single-vif drivers, <iface>mon on separate-vif ones.
wifi_monitor_start() {
  echo "[*] Freeing the radio (stopping NetworkManager/wpa_supplicant)..."
  airmon-ng check kill >>"$AIRMON_LOG" 2>&1
  WIFI_KILLED=1
  echo "[*] Enabling monitor mode on $1..."
  airmon-ng start "$1" >>"$AIRMON_LOG" 2>&1
  MON=$(iw dev | awk '/Interface/{n=$2} /type monitor/{print n; exit}')
  [[ -n "$MON" ]] || die "No monitor interface after airmon-ng start, see $AIRMON_LOG"
  echo "[*] Monitor interface: $MON"
}

wifi_set_channel() {
  iw dev "$MON" set channel "$1" || die "Could not set channel $1 on $MON"
}

# airodump-ng on $MON, writing pcap and csv to prefix $1. $2 locks the channel (empty hops
# all channels); $3 keeps one BSSID (empty shows every AP). The bands hopped come from
# AIRODUMP_BAND (default bg). With "bg" as $4 the AP table is hidden and airodump-ng runs in
# the background. Otherwise the table shows until Ctrl-C. Either way it is tracked, so a
# signal to this script reaches cleanup at once.
airodump_start() {
  local prefix=$1 channel=$2 bssid=$3
  local args=(--output-format pcap,csv --write-interval 1 --band "${AIRODUMP_BAND:-bg}" -w "$prefix")
  [[ -n "$channel" ]] && args+=(-c "$channel")
  [[ -n "$bssid" ]] && args+=(--bssid "$bssid")
  if [[ "${4:-}" == bg ]]; then
    airodump-ng "${args[@]}" "$MON" >/dev/null 2>&1 &
    track_pid $!
  else
    # A background job gets /dev/null as stdin, but airodump-ng reads the terminal width from
    # stdin (an ioctl on fd 0), so the table would be cut short. Pass the terminal in as fd 0.
    exec 3<&0
    airodump-ng "${args[@]}" "$MON" 0<&3 &
    track_pid $!
    wait $!
  fi
}

# aireplay-ng --test on monitor interface $1. Output is shown and saved to $2. With a BSSID
# in $3 the test passes only if that AP answered; without one, if any AP answered. aireplay-ng
# is not given -a: on the AC8260 its -a mode stops with "Couldn't determine current channel".
inject_test() {
  local mon=$1 out=$2 bssid=${3:-} ratio
  aireplay-ng --test "$mon" 2>&1 | tee "$out"
  if ! grep -qE "Found [0-9]+ APs?" "$out"; then
    echo "[!] aireplay-ng stopped before scanning for APs: $(tail -n 1 "$out")" >&2
    return 1
  fi
  if [[ -z "$bssid" ]]; then
    grep -q "Injection is working" "$out"
    return
  fi
  ratio=$(ap_ratio "$out" "$bssid")
  [[ "$ratio" =~ ^[1-9] ]]
}

# Last reply count ("x/y") for one AP in an aireplay-ng --test output file. $2 is the BSSID
# in any case. Prints nothing if the AP was not tested.
ap_ratio() {
  tr '\r' '\n' < "$1" | awk -v bssid="${2^^}" '
    /- channel: [0-9]+ - / { on = index(toupper($0), bssid) > 0; next }
    on && match($0, /[0-9]+\/ *[0-9]+: *[0-9]+%/) { ratio = substr($0, RSTART, RLENGTH) }
    END { sub(/:.*/, "", ratio); gsub(/ /, "", ratio); print ratio }'
}

# One line per AP that aireplay-ng tested: name, BSSID and reply count ("x/y").
ap_summary() {
  tr '\r' '\n' < "$1" | awk -v q="'" '
    function flush() { if (mac != "") printf "  %-20s %s  %s\n", essid, mac, ratio }
    match($0, /[0-9A-F][0-9A-F](:[0-9A-F][0-9A-F]){5} - channel: [0-9]+ - .*/) {
      flush()
      line = substr($0, RSTART, RLENGTH)
      mac = substr(line, 1, 17)
      essid = substr(line, index(line, "- " q) + 3)
      sub(q "$", "", essid)
      ratio = "?"
      next
    }
    mac != "" && match($0, /[0-9]+\/ *[0-9]+: *[0-9]+%/) {
      ratio = substr($0, RSTART, RLENGTH); sub(/:.*/, "", ratio); gsub(/ /, "", ratio)
    }
    END { flush() }'
}

# Put the card back in managed mode and let NetworkManager adopt it again. Safe to run on a
# card that is already fine: NetworkManager is restarted only when something changed, when
# airmon-ng check kill stopped it, or when it is not running.
wifi_restore() {
  local iface=$1 mon=${2:-} changed=0
  if [[ -n "$mon" && "$mon" != "$iface" && -n "$(iface_type "$mon")" ]]; then
    # Removing the monitor fails if the Intel firmware is stalled, and in the 03:32 run that
    # left the card dead until a reboot. The driver resets a stalled queue after 10 s, so wait.
    echo "[*] Waiting 12 s for the card's firmware to settle..."
    sleep 12
    echo "[*] Stopping monitor interface $mon..."
    airmon-ng stop "$mon" >>"$AIRMON_LOG" 2>&1
    changed=1
  fi
  # Single-vif drivers keep the managed name in monitor mode, and the mode only changes
  # while the link is down.
  if [[ "$(iface_type "$iface")" == monitor ]]; then
    echo "[*] Switching $iface back to managed mode..."
    ip link set "$iface" down
    iw dev "$iface" set type managed 2>>"$AIRMON_LOG"
    ip link set "$iface" up
    changed=1
  fi
  if (( changed || WIFI_KILLED )) || ! systemctl is-active --quiet NetworkManager; then
    systemctl restart NetworkManager 2>/dev/null
  fi
  wifi_verify "$iface"
}

# True when the newest Intel firmware restart in the kernel log never completed. Only a reboot
# recovers the card then.
iwl_restart_unfinished() {
  journalctl -k --since "-3 min" --no-pager 2>/dev/null |
    grep -E 'Hardware restart was requested|restart completed' | tail -n 1 | grep -q 'was requested'
}

# Check that the card is managed and NetworkManager lists it. A card that is up but not
# connected yet only gets a warning. Returns 1 when WiFi will stay down.
wifi_verify() {
  local iface=$1 now state i
  now=$(iface_type "$iface")
  if [[ "$now" != managed ]]; then
    echo "[!] $iface is in '${now:-missing}' mode, so WiFi will stay down." >&2
    echo "    Restore it by hand: see COMMANDS.md section 8." >&2
    return 1
  fi
  for i in $(seq 10); do
    nmcli -t -f DEVICE device 2>/dev/null | grep -qx "$iface" && break
    sleep 1
  done
  nmcli -t -f DEVICE device 2>/dev/null | grep -qx "$iface" || {
    echo "[!] NetworkManager does not list $iface. Check: nmcli device status" >&2
    return 1
  }
  for i in $(seq 15); do
    state=$(nmcli -g GENERAL.STATE device show "$iface" 2>/dev/null)
    [[ "$state" == *"(connected)"* ]] && break
    sleep 1
  done
  if [[ "$state" == *"(connected)"* ]]; then
    echo "[*] $iface is managed and connected."
  else
    echo "[!] $iface is managed but not connected yet (${state:-no state}). Check: nmcli device status" >&2
    if iwl_restart_unfinished; then
      echo "[!] The Intel firmware did not finish its last restart. Reboot to recover the card." >&2
    fi
  fi
}
