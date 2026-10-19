# Lab #10 — Tests and benchmarks

Working record for the WEP key-recovery lab (Laboratorio #10, "Captura el Router"). The submission asks for three parts: the commands used for the recovery, brief technical evidence of the tests, and the adversarial or physical actions. This file follows those three parts and records the decisions made along the way. Add a row for each run with its log file name, so every result can be checked against `logs/`.

**Status, 19 Oct 2026: WEP key recovered from the GL router.** The run is in section 2.7, and its capture is `logs/cap_nophone-01.cap`. Most of the IVs came from a phone downloading a file from the laptop over the WEP network. The replay added almost none: the access point sent only 24 broadcast frames in the whole run. The replay also stalled the Intel card's firmware 11 times in five minutes. Whether wired broadcasts can supply the IVs without a client is the next test (section 5).

> **Authorized targets only:** the GL router you own, and the class router only during its evaluation window. `injection-test.sh` probes every AP on its channel. Keep tests on channel 6, where only the GL router was heard at 21:18. Channel 1 has other networks in range.

---

## 1. Commands used

These are the commands used so far, on the Intel 3168NGW unless noted (`wlp2s0` is the card):

```bash
sudo ./scripts/managed-scan.sh -i wlp2s0                         # networks and channels; the card stays connected
sudo ./scripts/airodump.sh -i wlp2s0 -c 6                        # monitor scan locked to channel 6
sudo ./scripts/injection-test.sh -i wlp2s0 -c 6 -b <AP_BSSID>    # PASS when that AP answers
sudo ./scripts/wifi-recover.sh -i wlp2s0                         # managed mode back after a killed run
```

By hand, in the AC8260 failure tests (18 Oct):

```bash
sudo airmon-ng start wlp2s0
sudo ip link set wlp2s0mon up
sudo iw dev wlp2s0mon set channel 1
sudo aireplay-ng --test wlp2s0mon                                # "Injection is working!" means the card injects
sudo ip link set wlp2s0 down                                     # with NetworkManager stopped, before the 10:22 pass
```

PCIe power-saving test on the Realtek (18 Oct):

```bash
sudo setpci -s 02:00.0 CAP_EXP+0x10.b=2:3                        # L0s off on the card
sudo setpci -s 02:00.0 CAP_EXP+0x10.b=3:3                        # L0s on again
```

WEP recovery, 19 Oct (section 2.7). The router goes to WEP first. The capture runs while the phone downloads a file from the laptop over the WEP network:

```bash
ssh root@your-router-ip "SSID=Test AUTH=open sh -s -- '<13 ASCII characters>'" < router/wifi-wep-test.sh   # WEP on the GL router
sudo ./scripts/wep-crack.sh -i wlp2s0 -b <AP_BSSID> -c 6 -m active -o cap_nophone        # capture and crack
aircrack-ng -b <AP_BSSID> logs/cap_nophone-01.cap                                        # check the capture by hand
```

The download needs a file on the laptop. Make one and serve it on the laptop's LAN address, with port 8000 open to the LAN subnet only:

```bash
mkdir -p ~/wepfeed && cd ~/wepfeed && dd if=/dev/urandom of=big.bin bs=1M count=200
sudo firewall-cmd --add-rich-rule='rule family="ipv4" source address="192.168.8.0/24" port port="8000" protocol="tcp" accept'
python3 -m http.server 8000 --bind <laptop-LAN-address>
```

Not run yet (section 5, item 7): a passive capture with the phone's Wi-Fi off, while the laptop sends broadcasts to the LAN.

## 2. Test evidence

### 2.1 Cards

| Card | Driver, firmware | Antennas | Period (18 Oct unless noted) | Result |
| :--- | :--- | :--- | :--- | :--- |
| Realtek RTL8723BE | `rtl8723be`, single interface | not checked | 17 Oct ~23:05 to 18 Oct 06:43; 19:52 to 21:11 | PCIe RxErr errors logged, 65–120 a minute. Injection passed on channel 1 in some runs and failed in others. Channel 6 receives only if monitor mode starts on a channel 6 network |
| Intel AC8260 | `iwlwifi` with `iwlmvm`, firmware 36.c8e8e144.0 | 2x2 chips, one cable fitted | 06:43 to 19:45 (three boots) | No PCIe errors. Failed in every session from 08:47 to 09:59. Passed from 10:22 (see 2.4) |
| Intel 3168NGW (PCI 8086:24fb) | `iwlwifi` with `iwlmvm`, firmware 29.0bd893f3.0 | 1x1, one chain | from 21:16 | 8 of 8 runs passed |

The Realtek and the AC8260 both sat in PCI slot 02:00.0.

### 2.2 Injection runs

PASS means the target AP answered. `injection-test.sh` runs `aireplay-ng --test`. Logs are in `logs/` unless noted.

**Intel 3168NGW**

| Time | Channel | Result | Log |
| :--- | :---: | :--- | :--- |
| 21:21 | 1 | PASS. Home AP 30/30, extender 29/30, weakest AP (about -88 dBm) 0/30 | `injection-20261018-212105-ch1.txt` |
| 21:21 | 6 | PASS. Lab router 30/30, reply power -11.9 dBm | `injection-20261018-212130-ch6.txt` |
| 21:43–22:04 | 6 | PASS, six runs in a loop, all 30/30 | `injection-20261018-214318` to `220429-ch6.txt` |

**Intel AC8260**

| Time | Channel | Result | Log |
| :--- | :---: | :--- | :--- |
| 06:47 | 1 | FAIL. Manual test: 0/30 on every AP, no "Injection is working!" | Output outside `logs/` |
| 07:26 | 1 | PASS. Two APs 30/30 and 28/30; the AP at about -89 dBm 0/30 | `injection-20261018-072638-ch1.txt` |
| 08:05–08:15 | 1 | PASS, five runs. Strongest AP 30/30, the -70 dBm AP 29–30/30, the -89 dBm AP 0/30 | `injection-20261018-080805` to `081533-ch1.txt` |
| 08:18 | 6 | PASS, 30/30 | `injection-20261018-081821-ch6.txt` |
| 08:47 | 1 | FAIL. "Couldn't determine current channel … channel -1"; no probes sent | `injection-20261018-084727-ch1.txt` |
| 09:51 | 1 | FAIL. `-B` bitrate test: "No Answer", 0/30 | `bitrate-ch1.txt` |
| 09:57 | 1 | FAIL. "No Answer", 0/30 on every AP | `injection-20261018-095717-ch1.txt` |
| 09:59 | 1 | FAIL. Rate test: "No Answer", 0/30 on four APs | `rate-test-ch1.txt` |
| 10:22 | 1 | PASS, 30/30, after the manual link-down (see 2.4) | `injection-20261018-102201-ch1.txt` |
| 10:28 | 1 | PASS | `injection-20261018-102849-ch1.txt` |
| 10:30 | 6 | Not a card result: "Found 0 APs", the lab router was off | `injection-20261018-102958-ch6.txt`, `-103018-ch6.txt` |
| 10:30 | 36 | 3 APs found, no reply. Cause unknown (5 GHz) | `injection-20261018-103035-ch36.txt` |
| 10:54–11:21 | 1 | PASS, six runs five minutes apart. Target 30, 30, 29, 30, 30, 30 | `injection-20261018-105415` to `112036-ch1.txt` |
| 18:28 | 1 | PASS, 30/30 | `injection-20261018-182832-ch1.txt` |
| 19:08–19:35 | 1 | PASS, six runs on the other connector (chain 1). Target 29–30/30 | `injection-20261018-190806` to `193454-ch1.txt` |
| 19:41 | 6 | PASS, 30/30 | `injection-20261018-194145-ch6.txt` |

**Realtek RTL8723BE (17–18 Oct)**

| Time | Channel | Result | Log |
| :--- | :---: | :--- | :--- |
| 23:27 (17 Oct) | 1 | PASS | Output outside `logs/` |
| 04:51, 04:55 | 1 | No injection. No channel set after airodump | Output outside `logs/` |
| 05:03 | 1 | PASS, 30/30 on all four APs. 05:40, after the reseat: PASS | Output outside `logs/` |
| 19:55 | 6 | FAIL. "Found 0 APs": the receiver stayed on channel 1 | `injection-20261018-195540-ch6.txt` |
| 19:55 | 1 | PASS, 30/30 | `injection-20261018-195559-ch1.txt` |
| 20:22 | 6 | PASS, 30/30. Monitor mode started while connected to a channel 6 network | `injection-20261018-202215-ch6.txt` |

### 2.3 Finding channels

- A Realtek monitor scan shows only the channel the card started on. At 19:54 a hop scan listed five APs, all on channel 1. A managed-mode scan right after heard channel 6 APs at full strength.
- `managed-scan.sh` as root, 3168NGW at 21:18: networks on channels 1, 6, 11 and 36 (`logs/managed-scan-20261018-211858.txt`).

### 2.4 AC8260 failure, 08:31 to 10:22 (open)

The laptop booted at 08:31. Every session from 08:47 to 09:59 failed, and the 10:22 session passed. The earlier boot (06:43) failed once, at 06:47, and passed from 07:26 to 08:18. The cause is not found.

- **Failure signatures:**
  - "channel -1" (08:47). aireplay could not read the channel of the monitor interface. `iw set channel` had returned success, so `wifi_set_channel` did not catch it.
  - "No Answer" with 0/30 on every AP (06:47, 09:51, 09:57, 09:59). Managed connections to the home AP worked in the same minutes.
- **Ruled out:** firmware restart, driver, PCIe, cfg80211 and mac80211 errors (none in the journal of either boot); the station MAC (it toggles between two values in passing and failing runs); wpa_supplicant (it is stopped on every run); the home AP (it accepted the card's connections at 08:31, 08:47, 09:43, 09:52 and 09:57).
- **The one manual change before the 10:22 pass:** `ip link set wlp2s0 down` with NetworkManager stopped. The scripts do not do this on the AC8260. Their restore runs the link down and up only when the managed interface is itself in monitor mode, which is the single-vif case (the Realtek). It is a lead, not a proven cause: the 10:28 pass came after it, and a 5 GHz run at 10:30 failed after it.
- "After boot" is not a fixed window. The two boots above differ.

### 2.5 PCIe errors and power saving (Realtek)

- Realtek boots logged 65–120 correctable RxErr errors a minute on root port 00:1c.5 from 17 Oct. A reseat (boot at 05:33 on 18 Oct) did not stop them: about 85 a minute.
- L0s off on the card stopped the logged errors (none from 05:46:35 to 05:58:31). L0s back on brought them back within a minute.
- Benchmark, 06:18–06:26, three runs: L0s off showed no Wi-Fi gain, and the run-to-run spread was larger than the gap between the settings. Results are in `~/wifi-bench/results.csv`, outside the repo.
- The AC8260 boots (from 06:43) and the 3168NGW boot (from 21:16) logged no PCIe errors.

### 2.6 Signal

- Signal does not separate the two Intel cards. Readings for the same AP differ by 3–4 dB, in opposite directions for the two APs, and each card's readings move about as much between runs.
- AC8260 reply power for the channel 1 target: -30.6 dBm on chain 0, -34.1 dBm on chain 1.
- AC8260 `station dump`: one channel 1 AP read at -33 dBm on chain 0 and -86 dBm on chain 1, so the card runs on one chain.
- 3168NGW reply power on channel 6: -11.9 dBm (21:21 run), -18.6 to -19.7 dBm (loop).

### 2.7 WEP key recovery (19 Oct)

- **Target:** the GL router on WEP, SSID `Test`, channel 6, 13-character key (set with `router/wifi-wep-test.sh`). A phone was connected to it.
- **Run:** `logs/cap_nophone-01.cap` and `logs/cap_nophone-01.csv`, active mode (the default). Injection passed 30/30 at 04:45:05. The capture held 129,073 IVs when the key came out. The key file is `logs/cap_nophone.key`, 26 hex digits; its value is not copied here. The `-t` value is not in the logs.
- **Where the frames came from.** The capture holds 145,679 data frames, and 145,192 of them are WEP-encrypted, so each carries an IV:

| Direction | Frames | Retry-flagged (no ACK) |
| :--- | ---: | ---: |
| AP to phone (the download) | 83,112 | 7,542 |
| Phone to AP (its uplink) | 48,811 | 5,416 |
| Our replays to AP | 13,672 | 0 |
| AP broadcasts | 24 | 0 |

- The phone's traffic made up about 91% of the encrypted frames. The replays sent 13,672 frames, and the access point sent only 24 broadcast frames in the whole run. It did not accept the replayed frames as a client, so the replays added almost no new IVs.
- The Intel card's firmware restarted 11 times in those five minutes (kernel log, 04:44 to 04:49).
- Earlier runs without any download: 445 IVs in nine minutes (`logs/cap_gl-01.csv`), and 222 in two minutes (the 02:53 run; its file was overwritten).

### 2.8 IVs without a client (19 Oct, open)

- **Wired broadcasts reach the WEP network.** The laptop sent 600 UDP broadcasts to the LAN. The router's LAN port received 619 frames, and its radio transmitted 600, one per broadcast, with none dropped (router counters).
- **Pings to the phone get no replies.** The router sent 500 echo requests to the phone's address. Its radio sent 511 frames (including beacons) and received 18. None of the replies arrived.
- **The router's own tools cannot send broadcasts.** BusyBox `ping` has no `-b` option, and its `nc` sent nothing.
- **Open:** whether the broadcast frames carry IVs, and at what rate. That needs a passive capture with no client (section 5, item 7).

## 3. Decisions

1. **Keep the 3168NGW as the lab card, for now.** It passed all eight runs. The AC8260 failed from 08:47 to 09:59 with no cause found. Go back to the AC8260 only if the 3168NGW shows either failure signature in 2.4.
2. **Channel 6 is the test channel.** Only the GL router was heard there, so no other network gets probes. Channel 1 has other networks, and `injection-test.sh` probes every AP on its channel.
3. **One antenna cable.** With one cable fitted, the 2x2 AC8260 has no second antenna to gain from, so the 1x1 3168NGW does the same job here. The cable stays on the working connector: chain 0 for the 3168NGW, chain 1 for the AC8260 after 19:00.
4. **Find channels with `managed-scan.sh`.** On the Realtek, monitor mode keeps the channel it started on, so a monitor scan cannot find a network on another channel. Start monitor mode with the card connected to a network on the target channel.
5. **L0s stays on.** Turning it off stopped the logged errors but showed no Wi-Fi gain.
6. **Build wpad in GitHub Actions; run it from RAM once tested.** The SDK needs Ubuntu 22.04 with distutils, which this laptop lacks. The workflow builds from `lab10/router/wifi-wep-setup/` on each push to `main` that changes it. The router side, `try-on-router.sh`, mounts the build over `/usr/sbin/wpad` and writes nothing to flash. It is not tested yet, so it is held back from `main`.

7. **Crack in passive mode while the network carries traffic.** On the 3168NGW the replay added almost no IVs (2.7), and it stalled the firmware 11 times in five minutes. The traffic of a client sets the IV rate.
8. **Traffic for IVs: wired broadcasts first, no client needed.** An ESP32-C3 as a WEP client is the fallback if broadcasts do not supply IVs (section 5, item 8). Both stay on our own router.

## 4. Adversarial and physical actions

The guide asks for these in the submission. So far:

- **Physical (done):** card choice (1x1 3168NGW against 2x2 AC8260 with one cable) and antenna connector (chain 0 or chain 1). See 2.1, 2.6 and decisions 1 and 3.
- **Software (done):** a channel-locked injection test with a PASS/FAIL exit code (`injection-test.sh`); channel discovery without monitor mode (`managed-scan.sh`); recovery after a killed run (`wifi-recover.sh`).
- **Congestion, packet loss and availability attacks (not started):** the guide asks for research on these and on the tools that identify them. No test in this file covers them.
- **Other networks:** the channel 1 runs also sent probes to APs that were not the target. Keep tests on channel 6. No test against the class router is logged yet.

## 5. Open items

1. **WEP key recovered (19 Oct, section 2.7).** Keep `logs/cap_nophone-01.cap` as evidence. The `.key` file stays in `logs/` and is not in the repo.
2. **3168NGW watch.** Run the channel 6 loop again (planned for about 23:20) and check the new logs for "channel -1" and for "No Answer" with 0/30 on every AP.
3. **AC8260 cause.** The script restore does not explain the 10:22 pass (2.4), so the cause is still open. A test with the link-down done by hand, after a fresh boot, would show whether it matters.
4. **`wifi_set_channel`.** It checks only the exit status, so the 08:47 "channel -1" went unnoticed. Reading the channel back after `iw set channel` would catch it.
5. **Channel 36 at 10:30.** Three APs found on 5 GHz, no reply. An earlier note said 36 and 149 are not allowed in the GT domain. The 3168NGW showed both allowed at 21:33, so that note was wrong, and regulation does not explain this failure.
6. **Docs.** Fixed on 19 Oct: `README.md` and `COMMANDS.md` name the 3168NGW, say the 10:30 test found no AP because the router was off, and give channel 6 for the lab router.
7. **IVs with no client (section 2.8).** Turn the phone's Wi-Fi off. Capture passively on channel 6 (`wep-crack.sh -m passive`) while the laptop sends about 300 UDP broadcasts a second to the LAN. If the access point's broadcast frames carry IVs, the key can come out with no client at all. This is the next test.
8. **ESP32-C3 as WEP clients.** Check first that one board joins the Test network with its auth threshold set to WEP. The ESP-IDF docs describe this, but the v4.3 guide calls WEP deprecated, and nothing has been tested on the board: [ESP-IDF Wi-Fi API, v5.4.4](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32c3/api-reference/network/esp_wifi.html) and [Wi-Fi driver guide, v4.3](https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32c3/api-guides/wifi.html).
9. **Restore WPA2 on the GL router** after the lab, with the steps in [router/wifi-wep-setup/README.md](router/wifi-wep-setup/README.md) (section 4).
