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

- **Passive:** just listen. IVs trickle in only as fast as real clients talk. Slow.
- **Active:** capture one ARP packet and replay it thousands of times. The AP
  answers each replay with a fresh IV → IVs pour in → key in minutes.
  **Active needs a WiFi card that supports packet injection.**

---

## Hardware

| Card | Monitor mode | Injection | Role in this lab |
|------|:---:|:---:|---|
| Intel **AC8260** (`iwlwifi`, built-in) | ✅ | ⚠️ partial | Passive capture and scanning. Injected for some APs only (see below) |
| **Atheros AR9271** USB (e.g. Alfa AWUS036NHA) | ✅ | ✅ | **The active attack / key recovery** |
| MediaTek **MT7921AU** (many "AX1800" USB) | ✅ | ✅ (modern kernel) | Viable alternative injector |

The Intel AC8260 injected only part of the time on 18 Oct 2026. At 06:47 on channel 1
no AP answered. At 07:26 on channel 1, one nearby AP answered 30/30, a second 28/30,
and a third did not answer. Test the target itself with
`sudo ./injection-test.sh -i <iface> -c <channel> -b <AP_BSSID>`, and repeat it before
relying on a card. Use an AR9271 (or a confirmed MT7921) for the active attack.

On 17 Oct 2026 the Intel card above was replaced by a Realtek **RTL8723BE**
(`rtl8723be`). It is a single-interface driver: `airmon-ng` switches `wlp2s0` itself
into monitor mode instead of creating a `wlp2s0mon` interface, so the script has to
put it back in managed mode on exit. Injection has passed once and failed in later
tests. The cause is not known yet.

---

## How the scripts work

### `wep-crack.sh`

1. Stops NetworkManager and wpa_supplicant (`airmon-ng check kill`) and puts the
   card into monitor mode.
2. Locks the monitor interface to the AP's channel and starts `airodump-ng` on
   the BSSID, writing `logs/<prefix>-01.cap` and `logs/<prefix>-01.csv`.
3. **Active mode only:** runs the injection test against the target BSSID and
   saves the output to `logs/<prefix>-injection.txt`. If the target does not
   answer, it switches to passive mode, so it never silently does nothing. If the
   target answers, it starts fake-auth and ARP replay.
4. Every 15 s it prints the IV count from the CSV. Once there are at least 5000
   IVs it runs `aircrack-ng`, then runs it again each time the count grows by
   `-t` (default 20000).
5. When the key is found, it writes `logs/<prefix>.key` and exits.

Notes:

- Output goes to `logs/`, which is gitignored.
- On exit, including Ctrl-C, the script stops the monitor interface and restarts
  NetworkManager.

### `airodump.sh`

Starts monitor mode and runs `airodump-ng` in the foreground, so the access
points show in the terminal. Use it to find the BSSID and channel before
`wep-crack.sh`. Ctrl-C stops it and restores WiFi. It hops 2.4 GHz channels by
default; `-B abg` adds 5 GHz, which only the AC8260 can see.

### `injection-test.sh`

Locks the card to a channel, runs `aireplay-ng --test`, and saves the output to
`logs/injection-<time>-ch<N>.txt`. It prints the replies per AP. Exit 0 means
injection works. With `-b <AP_BSSID>` it passes only if that AP answers.
aireplay-ng still probes every AP on the channel, because its `-a` mode does not
work on the AC8260. The channel is required, because the test only hears APs on
the card's channel.

### `wifi-recover.sh`

Puts the card back in managed mode and restarts NetworkManager. Use it when a
run was killed and WiFi stayed dark. It changes nothing on a card that is fine.

### `wifi-lib.sh`

The code that the scripts above and `wep-crack.sh` share: starting monitor mode,
the injection test, the airodump start, and the recovery that runs on every exit.
Recovery handles both driver styles (a separate `<iface>mon` vif, or single-vif).
A fix there applies to all four scripts.

### `wifi-wep-test.sh`

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

- `[#Data ~ N IVs]` printed every ~15s — this is your IV count climbing.
- Once `#Data` is high enough the script runs `aircrack-ng`; on success:

```
============================================================
 KEY RECOVERED: <WEP-key-redacted>
 Saved to: cap_gl.key   Capture: cap_gl-01.cap
============================================================
```

The `.cap` file and recovered key are your **evidence for the GES report**.

---

## Cleanup

The scripts clean up on exit. If a run was killed and WiFi stays dark, run
`sudo ./wifi-recover.sh -i wlp2s0`, or follow the steps in
[COMMANDS.md](COMMANDS.md#8-wifi-disappeared).

> `airmon-ng check kill` only **stops** NetworkManager and wpa_supplicant for
> the capture, so it's reversible. Put the card back in managed mode **before**
> restarting NetworkManager, or NM won't re-adopt the interface.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `aireplay-ng --test` → "no answer / 0 APs" | Lock the card to the AP's channel first. `sudo ./injection-test.sh -i wlp2s0 -c N -b <AP_BSSID>` does that and prints the replies per AP. The AC8260 may not answer on some runs (see Hardware). |
| Script says injection failed and went passive | Expected on the AC8260. Use an AR9271 with `-m active`. |
| Fake-auth keeps failing | Set the GL AP to **Open** WEP (not Shared) for first practice; Shared-key needs a PRGA xorfile. |
| `#Data` not climbing in active mode | You're probably not injecting — check the card, antenna, and that you're close to the AP. |
| Only `lo` after the script exits | The card stayed in monitor mode. Run `sudo ./wifi-recover.sh -i wlp2s0`; COMMANDS.md §8 has the manual steps. |
| No `wlan0` | Fedora names it `wlp…`; run `iw dev`. |
| Router AP not broadcasting after `wifi-wep-test.sh` | Read the hostapd log on the router (COMMANDS.md §7). The firmware may have been built without WEP. |

---

## Adversarial round (ESP32-C3)

The second half of the grade is researching how **congestion / packet loss /
availability attacks** degrade a rival's capture, **and which tools identify
them**. The three ESP32-C3 Super Minis are the right tool for this (frame-based
congestion + a packet-monitor/waterfall as a sensor). See separate notes.
*(Frame-based congestion only — not RF noise jamming, which is illegal.)*
