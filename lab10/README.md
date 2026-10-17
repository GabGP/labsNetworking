# Lab #10 — "Captura el Router" (WEP key recovery)

Automation and notes for the authorized WEP exercise (CS VIII). The goal is to
capture traffic from an access point **you are authorized to test** and recover
its WEP key with a statistical/PTW attack on captured IVs — **no dictionary**.

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

---

## Prerequisites (Fedora)

```bash
sudo dnf install aircrack-ng
```

Find your WiFi interface name (Fedora uses `wlp…`, not `wlan0`):

```bash
iw dev        # look for the wlp… interface, e.g. wlp2s0
```

---

## Usage

```bash
chmod +x wep-crack.sh
sudo ./wep-crack.sh -i <iface> -b <AP_BSSID> -c <CHANNEL> [options]
```

### Options

| Flag | Meaning | Default |
|------|---------|---------|
| `-i` | WiFi interface (e.g. `wlp2s0`, `wlan0`) | *(required)* |
| `-b` | Target AP BSSID (MAC) | *(required)* |
| `-c` | Channel the AP is on | *(required)* |
| `-e` | AP ESSID (name) — helps fake-auth on some APs | *(none)* |
| `-m` | `active` (inject) or `passive` (listen only) | `active` |
| `-o` | Output file prefix | `wepcap` |
| `-t` | IV re-check threshold | `20000` |

### First, find the target's BSSID and channel

```bash
sudo airmon-ng check kill
sudo airmon-ng start wlp2s0
sudo airodump-ng wlp2s0mon          # note the BSSID + CH of your AP, then Ctrl-C
sudo airmon-ng stop wlp2s0mon
sudo systemctl start NetworkManager
```

### Passive practice run (works on the AC8260)

```bash
sudo ./wep-crack.sh -i wlp2s0 -b 11:22:33:44:55:66 -c 6 -m passive -o practice
```

### Active attack (needs AR9271 / injecting card)

```bash
sudo ./wep-crack.sh -i wlan0 -b 11:22:33:44:55:66 -c 6 -m active -o cap_gl
```

The script auto-runs `aireplay-ng --test` first; **if injection fails it falls
back to passive automatically** so it never silently does nothing.

---

## What you'll see

- `[#Data ~ N IVs]` printed every ~15s — this is your IV count climbing.
- Once `#Data` is high enough the script runs `aircrack-ng`; on success:

```
============================================================
 KEY RECOVERED: AA:BB:CC:DD:EE
 Saved to: cap_gl.key   Capture: cap_gl-01.cap
============================================================
```

The `.cap` file and recovered key are your **evidence for the GES report**.

---

## Cleanup / "my WiFi disappeared"

The script restores networking automatically on exit (even Ctrl-C). If a run is
ever killed uncleanly and WiFi stays dark, run the restore ritual by hand:

```bash
sudo airmon-ng stop wlp2s0mon          # return the card to managed mode FIRST
sudo systemctl start NetworkManager    # then bring networking back
```

> `airmon-ng check kill` only **stops** NetworkManager/wpa_supplicant for the
> capture — it's reversible. Always `airmon-ng stop …mon` **before** restarting
> NetworkManager, or NM won't re-adopt the interface.

---

## Troubleshooting

| Symptom | Fix |
|---|---|
| `aireplay-ng --test` → "no answer / 0 APs" | Lock the monitor iface to the AP's channel first (`iw dev <mon> set channel N`), then retest. On AC8260 it will still fail — that's expected. |
| Fake-auth keeps failing | Set the GL AP to **Open** WEP (not Shared) for first practice; Shared-key needs a PRGA xorfile. |
| `#Data` not climbing in active mode | You're probably not injecting — check the card, antenna, and that you're close to the AP. |
| No `wlan0` | Fedora names it `wlp…`; run `iw dev`. |

---

## Adversarial round (ESP32-C3)

The second half of the grade is researching how **congestion / packet loss /
availability attacks** degrade a rival's capture, **and which tools identify
them**. The three ESP32-C3 Super Minis are the right tool for this (frame-based
congestion + a packet-monitor/waterfall as a sensor). See separate notes.
*(Frame-based congestion only — not RF noise jamming, which is illegal.)*
