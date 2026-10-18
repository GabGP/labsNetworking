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
| Intel **AC8260** (`iwlwifi`, built-in) | ✅ | ❌ | Passive capture + analysis only |
| **Atheros AR9271** USB (e.g. Alfa AWUS036NHA) | ✅ | ✅ | **The active attack / key recovery** |
| MediaTek **MT7921AU** (many "AX1800" USB) | ✅ | ✅ (modern kernel) | Viable alternative injector |

The Intel AC8260 **cannot inject** (firmware-blocked) — confirm with
`sudo aireplay-ng --test <iface>mon`. Use it as your passive listener; use an
AR9271 (or confirmed MT7921) for the active attack.

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
3. **Active mode only:** runs `aireplay-ng --test`. If injection fails, it
   switches to passive mode, so it never silently does nothing. If injection
   works, it starts fake-auth and ARP replay.
4. Every 15 s it prints the IV count from the CSV. Once there are at least 5000
   IVs it runs `aircrack-ng`, then runs it again each time the count grows by
   `-t` (default 20000).
5. When the key is found, it writes `logs/<prefix>.key` and exits.

Notes:

- Output goes to `logs/`, which is gitignored.
- On exit, including Ctrl-C, the script stops the monitor interface and restarts
  NetworkManager.

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

The script cleans up on exit. If a run was killed and WiFi stays dark, follow
the recovery steps in [COMMANDS.md](COMMANDS.md#8-wifi-disappeared).

> `airmon-ng check kill` only **stops** NetworkManager and wpa_supplicant for
> the capture, so it's reversible. Put the card back in managed mode **before**
> restarting NetworkManager, or NM won't re-adopt the interface.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `aireplay-ng --test` → "no answer / 0 APs" | Lock the monitor iface to the AP's channel first (`iw dev <mon> set channel N`), then retest. On AC8260 it will still fail — that's expected. |
| Script says injection failed and went passive | Expected on the AC8260. Use an AR9271 with `-m active`. |
| Fake-auth keeps failing | Set the GL AP to **Open** WEP (not Shared) for first practice; Shared-key needs a PRGA xorfile. |
| `#Data` not climbing in active mode | You're probably not injecting — check the card, antenna, and that you're close to the AP. |
| Only `lo` after the script exits | The card stayed in monitor mode. The script restores it and prints a warning if it can't; otherwise follow COMMANDS.md §8. |
| No `wlan0` | Fedora names it `wlp…`; run `iw dev`. |
| Router AP not broadcasting after `wifi-wep-test.sh` | Read the hostapd log on the router (COMMANDS.md §7). The firmware may have been built without WEP. |

---

## Adversarial round (ESP32-C3)

The second half of the grade is researching how **congestion / packet loss /
availability attacks** degrade a rival's capture, **and which tools identify
them**. The three ESP32-C3 Super Minis are the right tool for this (frame-based
congestion + a packet-monitor/waterfall as a sensor). See separate notes.
*(Frame-based congestion only — not RF noise jamming, which is illegal.)*
