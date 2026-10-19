# Lab #10 — "Captura el Router" (WEP key recovery)

Automation and notes for the authorized WEP exercise (CS VIII). The goal is to
capture traffic from an access point **you are authorized to test** and recover
its WEP key with a statistical/PTW attack on captured IVs — **no dictionary**.

Commands are in [COMMANDS.md](COMMANDS.md). This file covers the concepts, what
the scripts do, what their output means, and troubleshooting.

> ⚠️ **Authorized use only.** Run this against your own GL-AR300M16 while
> practicing, and against the class router **only** during the evaluation
> window. WEP attacks on networks you don't own are illegal.

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
| Intel **AC8260** (`iwlwifi`, built-in) | ✅ | ⚠️ partial | Passive capture and scanning. Active on channel 1 since 10:22 on 18 Oct; failed for two hours before that (see below) |
| Intel **3168NGW** (`iwlwifi`, 1x1, in the laptop from 18 Oct) | ✅ | ✅ on channels 1 and 6 | Passive capture recovered the WEP key on 19 Oct. Active replays: the AP drops the card within seconds, and the firmware stalls (TESTS.md, section 2.7). |

On 18 Oct 2026 the Intel AC8260 failed in every test from the 08:30 reboot until 10:22,
and the cause is not known. Since 10:22 it has injected on channel 1 in every test (15 runs).
It injected on channel 6 at 08:18 and 19:41. The 10:30 channel 6 test found no AP because the lab router was off.
Test the target itself with `sudo ./scripts/injection-test.sh -i <iface> -c <channel> -b <AP_BSSID>`,
and repeat it before relying on a card. Adapters such as an Atheros AR9271 USB or a MediaTek
MT7921AU are often suggested for active mode. Neither has been tested in this lab. The spare
Intel 3168NGW, the card in the laptop from 18 Oct, injected on channels 1 and 6 in 8 of 8 runs.

On 17 Oct 2026 the Intel card above was replaced by a Realtek **RTL8723BE**
(`rtl8723be`). It is a single-interface driver: `airmon-ng` switches `wlp2s0` itself
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
3. **Active mode only:** runs the injection test against the target BSSID and
   saves the output to `<prefix>-injection.txt` in the run folder. If the target does not
   answer, it switches to passive mode, so it never silently does nothing. If the
   target answers, it starts fake-auth and ARP replay.
4. Every 3 s it prints the IV count, the IV rate and, in active mode, the ARP replay counters. Once there are at least 5000 IVs it runs `aircrack-ng`, then runs it again each time the count grows by `-t` (default 20000). A try can take up to a minute, which pauses the progress lines, but the IV count keeps rising.
5. When the key is found, it prints the key in ASCII and hex, writes `<prefix>.key` (26 hex digits, as aircrack-ng writes it), and cleans up.

Notes:

- Each run gets its own folder under `logs/` (gitignored), named by the start time and the prefix, for example `2026-10-19_044700_cap_gl/`. It holds the capture, the CSV, the key, the attack output and the airmon log, so one run's files stay together.
- On exit, including Ctrl-C, the script stops the monitor interface and restarts
  NetworkManager.

### `airodump.sh`

Starts monitor mode and runs `airodump-ng` in the foreground, so the access
points show in the terminal. Use it to find the BSSID and channel before
`wep-crack.sh`. Ctrl-C stops it and restores WiFi. Its capture goes to a new folder
under `logs/`. It hops 2.4 GHz channels by default; `-B abg` adds 5 GHz, which only the
AC8260 can see.

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
the injection test, the airodump start, and the recovery that runs on every exit.
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
| `aireplay-ng --test` → "no answer / 0 APs" | Lock the card to the AP's channel first. `sudo ./scripts/injection-test.sh -i wlp2s0 -c N -b <AP_BSSID>` does that and prints the replies per AP. The AC8260 may not answer on some runs (see Hardware). |
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

---

## Adversarial round (ESP32-C3)

The second half of the grade is researching how **congestion / packet loss /
availability attacks** degrade a rival's capture, **and which tools identify
them**. The three ESP32-C3 Super Minis are the right tool for this (frame-based
congestion + a packet-monitor/waterfall as a sensor). See separate notes.
*(Frame-based congestion only — not RF noise jamming, which is illegal.)*
