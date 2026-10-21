# Lab #10 — "Captura el Router" (WEP key recovery)

Automation and notes for the authorized WEP exercise (CS VIII). The goal is to
capture traffic from an access point **you are authorized to test** and recover
its WEP key with a statistical/PTW attack on captured IVs — **no dictionary**.

Commands are in [COMMANDS.md](COMMANDS.md). This file covers the concepts, what
the scripts do, what their output means, and troubleshooting.

> ⚠️ **Authorized use only.** Run this against your own GL-AR300M16 while
> practicing, and against the class router **only** during the evaluation
> window. WEP attacks on networks you don't own are illegal.
>
> Deauthentication and traffic toward the class router are allowed only in the
> official window, as the course staff's written reply sets out (TESTS.md, section 4).

---

## Why WEP falls (the concept being graded)

WEP encrypts each packet with RC4 keyed by a 24-bit **Initialization Vector
(IV) + the shared key**. The IV space is tiny, so IVs repeat, and the RC4
keystream leaks information about the key bytes. Collect enough IVs (tens of
thousands) and `aircrack-ng`'s **PTW attack** solves the key algebraically —
**without trying any passwords**. That's why the lab says *"sin diccionarios."*

The whole game is therefore **IVs per second**:

- **Passive:** just listen. IVs come only from frames already on the air, so the rate follows the traffic on the network. A phone downloading a 200 MB file gave enough IVs for a key in minutes (19 Oct). With no traffic, passive capture is too slow.
- **Active:** replays a captured ARP packet so the AP answers with fresh IVs. It needs a card that can inject, and the AP must accept the card as a client. On the Intel 3168NGW the AP dropped the card within seconds, and the replays added almost no IVs (TESTS.md, section 2.7).

---

## Hardware

| Card | Monitor mode | Injection | Role in this lab |
|------|:---:|:---:|---|
| Intel **AC8260** (`iwlwifi`, built-in) | ✅ | ⚠️ partial | Passive capture and scanning. It recovered a key passively on 20 Oct (TESTS.md, section 2.19). It injected on channel 1 from 10:22 to 19:41 on 18 Oct. On 20 Oct it failed with the antenna cable on chain 0 and passed from 01:54 with the cable on chain 1 |
| Intel **3168NGW** (`iwlwifi`, 1x1; the card in the laptop as of 20 Oct) | ✅ | ✅ on channels 1 and 6 | Passive capture recovered the WEP key on 19 Oct and again on 20 Oct, both from the phone's download. Active replays: the AP drops the card within seconds, and the firmware stalls (TESTS.md, sections 2.7 and 2.20). |

On 18 Oct 2026 the Intel AC8260 failed in every test from the 08:30 reboot until 10:22,
and the cause is not known. From 10:22 every channel 1 test passed until the channel 6 run at 19:41.
The 10:30 channel 6 test found no AP because the lab router was off. On 20 Oct its runs failed with the
cable on chain 0 and passed with it on chain 1 (TESTS.md, section 2.19).
Test the target itself with `sudo ./scripts/injection-test.sh -i <iface> -c <channel> -b <AP_BSSID>`,
and repeat it before relying on a card. Adapters such as an Atheros AR9271 USB or a MediaTek
MT7921AU are often suggested for active mode. Neither has been tested in this lab. The Intel
3168NGW passed every injection test recorded here: 8 on channels 1 and 6 on 18 Oct, 3 on channel 6 on 19 Oct, and 4 on channel 6 on 20 Oct (TESTS.md, sections 2.2 and 2.20).

On 17 Oct 2026 the AC8260 was swapped for a Realtek **RTL8723BE** (`rtl8723be`),
which left the laptop after 21:11 on 18 Oct (TESTS.md, section 2.1). It is a
single-interface driver: `airmon-ng` switches `wlp2s0` itself
into monitor mode instead of creating a `wlp2s0mon` interface, so the script has to
put it back in managed mode on exit. Injection passed on channel 1 in several
tests on 17–18 Oct and failed in others. The cause is not known yet.

On 18 Oct the Realtek heard channel 6 APs normally in managed mode, but in monitor mode its
receiver stayed on the channel it started on. Changing the channel after monitor mode starts
did not move it. Channel 1 works because the card is connected to a channel 1 network when
monitor mode starts. Starting monitor mode while connected to a channel 6 network worked once,
at 30/30. A single-frequency scan and an IBSS network on channel 6 did not keep the receiver
there. A monitor scan therefore shows only the channel the card started on. Use
`managed-scan.sh` to find a network's channel.

---

## How the scripts work

The laptop scripts are in `scripts/`. The router script is in `router/`.

### `wep-crack.sh`

1. Stops NetworkManager and wpa_supplicant (`airmon-ng check kill`) and puts the
   card into monitor mode.
2. Locks the monitor interface to the AP's channel and starts `airodump-ng` on
   the BSSID, writing `<prefix>-01.cap` and `<prefix>-01.csv` in the run folder.
3. **Active, forge and caffe modes:** runs the injection test against the target BSSID and
   saves the output to `<prefix>-injection.txt` in the run folder. If the target does not
   answer, it switches to passive mode, so it never silently does nothing. If the
   target answers, it starts fake-auth and the replay: ARP replay (active), a forged ARP
   request (forge), or caffe-latte (caffe).
4. Every 3 s it prints the IV count, the IV rate and, in the active modes, the replay counters. Once there are at least 40,000 IVs it runs `aircrack-ng` in the background, then again each time the count grows by `-t` (default 10,000). A try that finds the key takes under a second; a failed one stops at its 10 s cap, and the progress lines keep running meanwhile.
5. When the key is found, it prints the key in ASCII and hex, writes `<prefix>.key` (26 hex digits, as aircrack-ng writes it), and cleans up.
6. On exit, after WiFi is back, it prints the capture-health table for its capture and saves it as `<prefix>-01-health.txt` (see [Capture health](#capture-health)).

Notes:

- Each run gets its own folder under `logs/` (gitignored), named by the start time and the prefix, for example `2026-10-19_044700_cap_gl/`. It holds the capture, the CSV, the key, the attack output and the airmon log, so one run's files stay together.
- On exit, including Ctrl-C, the script stops the monitor interface and restarts
  NetworkManager.
- `-M <monitor>` (instead of `-i`) uses a monitor interface that is already up, such as the one `airodump.sh -i wlp2s0 -c <CH>` started. The card must already be on `-c`, or the run refuses to start. It neither starts nor stops that monitor and leaves it up on exit, so WiFi is restored by the run that owns the monitor. See [COMMANDS.md section 12.9](COMMANDS.md#129-a-crack-on-a-monitor-that-is-already-up-listens-only).
- `-m forge -C <client MAC>` is active mode with a forged ARP request, for a network that sends no ARP request of its own. A fragment attack takes a keystream from a frame the AP relays to that client, `packetforge-ng` builds an ARP request from it, and `aireplay-ng --interactive` replays that. It waits up to 120 s. See [COMMANDS.md section 12.11](COMMANDS.md#1211-forged-arp-request-mode-forge-sends-frames).
- `-m caffe` runs aireplay-ng's caffe-latte. It needs no client address and sends no deauthentication: it listens, and it replays a broadcast ARP frame when a client sends one. Until then, the IVs come from the client's traffic, as in passive mode. See [COMMANDS.md section 12.12](COMMANDS.md#1212-caffe-latte-mode-caffe-listens-replays-an-arp).

### `airodump.sh`

Starts monitor mode and runs `airodump-ng` in the foreground, so the access
points show in the terminal. Use it to find the BSSID and channel before
`wep-crack.sh`. Ctrl-C stops it and restores WiFi. If another run uses its monitor (`wep-crack.sh -M`, `competition-deauth.sh -M`), stop it last, because its Ctrl-C ends that monitor for them too. Its capture goes to a new folder
under `logs/`. It hops 2.4 GHz channels by default; `-B abg` adds 5 GHz. The 3168NGW's managed
scan saw channel 36 on 18 Oct, so it sees 5 GHz too; its monitor-mode 5 GHz hop is
untested (TESTS.md, section 2.3).

### `managed-scan.sh`

Runs a normal managed-mode scan. The card stays in managed mode and keeps its connection. It
lists each network's channel, signal, BSSID and name, sorted by channel, and marks the
connected one with `*`. A hidden network shows as `<hidden>`, but its beacons still carry its
channel and BSSID. Use it to find the channel of a network you don't know. `-c 6` shows one
channel only. The table is also saved to a new folder under `logs/`, as `scan.txt`. The kernel keeps
earlier scan results for a few minutes, so an entry can be slightly old.

### `injection-test.sh`

Locks the card to a channel, runs `aireplay-ng --test`, and saves the output to a new folder
under `logs/`, as `injection.txt`. It prints the replies per AP. Exit 0 means
injection works. With `-b <AP_BSSID>` it passes only if that AP answers.
aireplay-ng still probes every AP on the channel, because its `-a` mode does not
work on the AC8260. The channel is required, because the test only hears APs on
the card's channel.

### `wifi-recover.sh`

Puts the card back in managed mode and restarts NetworkManager. Use it when a
run was killed and WiFi stayed dark. It changes nothing on a card that is fine. Its
log goes to a new folder under `logs/`.

### `wifi-lib.sh`

The code that the scripts above and `wep-crack.sh` share: starting monitor mode,
the injection test, the airodump start, the capture-health table, and the recovery that
runs on every exit.
Recovery handles both driver styles (a separate `<iface>mon` vif, or single-vif).
A fix there applies to all four scripts.

### `router/wifi-wep-test.sh`

Runs **on the router**, not the laptop. It backs up `/etc/config/wireless`,
puts the main AP on WEP with the keys you give it, and checks netifd and
hostapd that the AP is really broadcasting. It doesn't rely on `iwinfo` alone,
because `iwinfo` still reports the WEP settings after hostapd has refused them.

- Give it 1–4 keys. Each is 5 or 13 ASCII characters, or 10 or 26 hex digits.
  152-bit keys are not supported by the router's driver.
- Leave the router on this config only for the lab. Restore WPA2 from the
  backup it made.

### Capture health

`wep-crack.sh` and `competition-deauth.sh` print this table when they exit, after WiFi is
back, and save it as `<prefix>-01-health.txt` beside the capture. It reads the capture
file only, so the card is not touched. It needs `tshark` (package `wireshark-cli`). One row
per minute, and a total row:

- `WEP_frames`: data frames with the WEP bit set. Each carries an IV.
- `AP_bcast`: of those, the frames the AP sent to the broadcast address.
- `retry%`: the share of those flagged as retries.
- `deauth`, `disassoc`, `auth`, `assoc_req`: management frames of those types. They carry
  the AP's address whoever sent them, so they show what reached the air, not who sent it.

Reading the table took 6 to 7 s on the 132 to 139 MB captures of 19 Oct. The cost is paid once, at exit. To read an
older capture by hand:

```bash
bash -c 'source scripts/wifi-lib.sh && capture_health logs/<run>/cap-01.cap <BSSID>'
```

The counts were checked against tshark's own counts on six real captures (20 Oct).

### `competition-deauth.sh`

Deauthenticates clients on one network, during the official evaluation window only. It has three ways to choose the clients:

- **Without `-f`:** each round is one `aireplay-ng --deauth 5 -a <BSSID>` call with no destination.
  The frames go to the broadcast address and reach every client of the AP. No client list is needed.
- **With `-f clients.txt`:** each round is one call per MAC in the file, aimed at that client. Use
  it to leave the other clients alone. The list is a snapshot, so a client that joins later is missed.
- **With `-x logs/own-devices.txt`:** each round reads the run's own capture and makes one call per client it lists on the AP, except the MACs in that file (your own devices). The list is read again every round, so a client that joins later is reached on the next round. Nothing is broadcast. See [COMMANDS.md section 12.10](COMMANDS.md#1210-deauth-to-the-other-clients-only-sends-frames).

`-M <monitor>` takes the place of `-i`, for a monitor interface that a crack already started (`wlp2s0mon` for `wep-crack.sh`), so a round can run during the crack. This run neither starts nor stops that monitor, nor changes its channel, and leaves it up on exit. Use `-i` otherwise: it starts monitor mode and restores WiFi on exit. See [COMMANDS.md section 12.7](COMMANDS.md#127-deauth-during-a-crack-sends-frames).

`--deauth-rc N` sets the reason code of each deauthentication frame, 0 to 254. Without it, aireplay-ng uses 7. See [COMMANDS.md section 12.13](COMMANDS.md#1213-deauth-reason-code-dry-run-then-sends-frames).

Required: `-i` or `-M`, `-b`, `-c` and `--in-window`. `-u HH:MM` is optional: with it, the run stops when the window
closes. You type START at the prompt. `--dry-run`
prints the plan and sends nothing. Each run folder holds `deauth.log`, the raw `aireplay.txt`,
`capture-01.cap` and `capture-01-health.txt`. The commands are in
[COMMANDS.md](COMMANDS.md#11-the-evaluation-window-capture-health-and-deauth), the checks in TESTS.md
section 4.2. It has not been run on air yet.

---

## What you'll see

- A progress line every 3 s, for example `    04:12:15  IVs 16000 (+1987 IV/s)  ARP req 7, ACK 140, TX 1750`. The rate is per real second. In passive mode the replay columns are empty.
- Once there are enough IVs the script runs `aircrack-ng`; on success:

```
============================================================
 KEY RECOVERED
 ASCII:    <13 characters>
 Hex:      <13 pairs of hex, colon separated>
 Saved to: logs/<run>/cap_gl.key
 Capture:  logs/<run>/cap_gl-01.cap
============================================================
```

The ASCII line appears only when every byte of the key is printable.

The `.cap` file and recovered key are your **evidence for the GES report**.

The [capture-health table](#capture-health) gives the same kind of count per minute. The 20 Oct
04:00 run, AP only, looks like this (the 04:00 row is the start of the run, when the deauth
burst in TESTS.md 2.20 happened):

```
minute WEP_frames  AP_bcast  retry%  deauth  disassoc   auth assoc_req
04:00           0         0    0.0%     210         0    227         1
04:01        3925         8    0.1%       0     13036     84         7
04:02        5618        12    0.0%       0     23511     79         5
04:03       71363        16    1.2%       0     25295     49         2
total       80906        36    1.1%     210     61842    439        15
```

---

## Cleanup

The scripts clean up on exit. If a run was killed and WiFi stays dark, run
`sudo ./scripts/wifi-recover.sh -i wlp2s0`, or follow the steps in
[COMMANDS.md](COMMANDS.md#8-wifi-disappeared).

On the Intel card the cleanup waits 12 s before it removes the monitor interface, so the firmware can settle. A second Ctrl-C does not stop the restore. If the firmware did not finish its last restart, the script says so, and a reboot brings WiFi back.

> `airmon-ng check kill` only **stops** NetworkManager and wpa_supplicant for
> the capture, so it's reversible. Put the card back in managed mode **before**
> restarting NetworkManager, or NM won't re-adopt the interface.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `aireplay-ng --test` → "no answer / 0 APs" | Lock the card to the AP's channel first. `sudo ./scripts/injection-test.sh -i wlp2s0 -c N -b <AP_BSSID>` does that and prints the replies per AP. The AC8260 failed most runs on 19–20 Oct until its cable moved to the other connector (see Hardware). |
| Script says injection failed and went passive | Expected when the card can't inject against the target. The AC8260 does this some of the time; passive capture still works. |
| Fake-auth keeps failing | Set the GL AP to **Open** WEP (not Shared) for first practice; Shared-key needs a PRGA xorfile. |
| `#Data` not climbing in active mode | You're probably not injecting — check the card, antenna, and that you're close to the AP. |
| Only `lo` after the script exits | The card stayed in monitor mode. Run `sudo ./scripts/wifi-recover.sh -i wlp2s0`; COMMANDS.md §8 has the manual steps. |
| No `wlan0` | Fedora names it `wlp…`; run `iw dev`. |
| Router AP not broadcasting after `wifi-wep-test.sh` | Read the hostapd log on the router (COMMANDS.md §7). The firmware may have been built without WEP. |
| Target channel unknown | `sudo ./scripts/managed-scan.sh -i wlp2s0` lists each network's channel, BSSID and name. Hidden names show as `<hidden>`. |
| Target is on channel 6 and the card is the Realtek | The Realtek receives only on the channel it started on. Use the AC8260 on that channel, or connect the card to a network on that channel before monitor mode starts (see Hardware). |
| Hidden network, injection test shows 0/30 | Most hidden networks ignore probes that don't name them, so the test can't see them even when injection works. Get the name first; airodump shows it once a client connects. Untested in this lab. |
| Few IVs a second | The network carries little traffic, and the replay adds almost none on the Intel 3168NGW. Start a large download from a device on the network. A phone downloading from the laptop worked (TESTS.md, section 2.7). |
| `competition-deauth.sh` says the `-u` time has passed | Give a time later today, on this machine's clock (`date` shows it). |
| `competition-deauth.sh` says there is no capture | airodump-ng stopped at once. Read `airmon.log` in the run folder and try again. Nothing was sent. |
| `competition-deauth.sh` prints only its usage | A real run needs `-i` or `-M`, `-b`, `-c` and `--in-window`. `-u`, `-f` and `-x` are optional. COMMANDS.md §11 has the full line. |
| A call stops with "stopped after 30 s" and no reply | aireplay-ng waited for a beacon from the AP and did not hear one on that channel. Check the channel with `managed-scan.sh`. |
| A run stops with "Couldn't determine current channel" | aireplay-ng cannot read the card's channel, so it exits before sending. The tool passes `--ignore-negative-one` only after it has read the channel back and seen the AP in its own capture. If one of those checks refuses, the message names it. The AC8260 stopped this way before (TESTS.md, section 2.19). |
| The end of a run says "No health table" | The capture could not be read, or tshark is missing (package `wireshark-cli`). The run's files are still in its folder; the reason is printed above that line. |
| "Ignoring N line(s)" from the deauth tool | The client file given with `-f` has a typo. Only the MACs the tool prints before START are targeted. |

---

## Adversarial round

The grade's second part asks how **congestion, packet loss and availability attacks**
change a capture, and which tools identify them. The research, the checks and the
window rules are in [TESTS.md, section 4](TESTS.md#4-adversarial-and-physical-actions).
The commands are in [COMMANDS.md, section 11](COMMANDS.md#11-the-evaluation-window-capture-health-and-deauth).

- **Our own equipment:** the congestion tests may run at any time on our own router and devices.
- **The window:** deauthentication of clients on the evaluation network, and traffic toward
  the class router's channel, only during the official window, as the course staff's written
  reply allows.
- **Tools:** the [capture-health table](#capture-health) counts the frames behind each condition,
  and every run prints it. `scripts/competition-deauth.sh` deauthenticates the clients of the network
  named with `-b`. It runs only with `--in-window`, and after you type START.
- **The ESP32-C3 boards** are used for the keyless-station test and as a packet monitor
  (TESTS.md, sections 2.21 and 5).

RF noise jamming is out of scope: the lab rules say it is illegal.
