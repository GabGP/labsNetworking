# Lab #10 — Tests and benchmarks

Working record for the WEP key-recovery lab (Laboratorio #10, "Captura el Router"). The submission asks for three parts: the commands used for the recovery, brief technical evidence of the tests, and the adversarial or physical actions. This file follows those three parts and records the decisions made along the way. Add a row for each run with its log file name, so every result can be checked against `logs/`.

**Status, 19 Oct 2026: WEP key recovered from the GL router.** The run is in section 2.7, and its capture is `logs/cap_nophone-01.cap`. Most of the IVs came from a phone downloading a file from the laptop over the WEP network. The replay added almost none: the access point sent only 24 broadcast frames in the whole run. The replay also stalled the Intel card's firmware 11 times in five minutes. A second run on 19 Oct got 99% of its IVs from wired broadcasts, with one client present for part of it (section 2.9). With no station associated, the same broadcasts gave no IVs (section 2.10). Corrections from 19 Oct: in the 06:32 run the 82,000 frames went to the phone's download, not to the replay card, which got no data frames (section 2.12). The 08:13 and 08:21 passive runs cracked from that same download (section 2.13). The fastest run so far was a unicast flood to the phone, which cracked the key 23.5 seconds after the phone's first frame (section 2.18).

**Status, 20 Oct 2026:** the key came out again on the 3168NGW from the phone's download, in two runs (section 2.20). Passive capture on the AC8260 recovered a key at 00:32 and at 00:46, before the cable moved. The AC8260 injects with the cable on the other connector (section 2.19). The board's notes (section 2.21) show it joined with a wrong key at 06:06 and 06:09 and got no address, and with the real key at 06:12 and got one. So the 06:14 run, which got 72,149 broadcast IVs, had a keyed station and does not test the keyless question.

> **Authorized targets only:** the GL router you own, and the class router only during its evaluation window. `injection-test.sh` probes every AP on its channel. Keep tests on channel 6, where only the GL router was heard at 21:18. Channel 1 has other networks in range.

**How to read this file.** A row with a log folder can be checked against `logs/`. *Session notes* marks a result that came from the lab session and has no file in `logs/`; rerun it with logs before citing it as evidence. Several log files cited before 19 Oct 05:26 were not in `logs/` on 20 Oct. They are marked *missing* where they are cited, and section 5, item 20 has the list. Other outside sources are named where they are used: the kernel log (`journalctl -k`), the board's notes, and the router's log.

**Results at a glance (20 Oct 2026).**

| Question | Answer so far | Where |
| :--- | :--- | :--- |
| Can the key be recovered without a dictionary? | Yes: on 19 Oct (3168NGW, from a phone's download), and on 20 Oct (3168NGW in two runs; the AC8260 passively in two runs). | 2.7, 2.13, 2.19, 2.20 |
| What supplies the IVs? | A client's traffic. A phone download gave about 1,400 frames a second, and a unicast flood about 2,000 IVs a second. Broadcasts give IVs only when a station is associated. | 2.9, 2.10, 2.13, 2.16–2.18 |
| Does replaying add IVs on the 3168NGW? | Almost none: the AP does not keep the replaying card as a client. | 2.7, 2.20 |
| Which cards inject, and when? | The 3168NGW passed every injection test recorded here: 8 on 18 Oct, 3 on 19 Oct, 4 on 20 Oct. The AC8260 failed with its cable on chain 0 and passed on chain 1 (20 Oct). | 2.2, 2.19, 2.20 |
| Does a keyless station get broadcasts? | Open. The 06:14 run had a keyed station, and the wrong-key joins on 20 Oct got no DHCP address. | 2.21, section 5, item 7 |
| What do congestion, packet loss and availability attacks do to a capture, and what shows it? | Research in 4.1. The capture-health table counts the frames behind each condition, and its totals match tshark on six real captures. `competition-deauth.sh` runs only in the window and has not yet been run on air. | 4.1, 4.2 |

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

Run on 19 Oct with one client present (section 2.9). The repeat with no client gave no IVs (section 2.10).

Flood from the laptop for the IVs (sections 2.9, 2.18 and 2.21). Run one copy at a time, and start the capture first:

```bash
python3 scripts/broadcast_feed.py 192.168.8.255 300 600     # broadcasts to the LAN, 300 a second for 10 minutes
python3 scripts/broadcast_feed.py <station-IP> 2000 120     # unicast to one station's IP, 2,000 a second for 2 minutes (2.18)
```

The 06:14 board run (2.21) used a broadcast flood. Its rate is not recorded in the run folder.

Manual checks (20 Oct). None of these sends a frame. COMMANDS.md, section 12, has the full list and what each should print:

```bash
bash -c 'source scripts/wifi-lib.sh && capture_health logs/<run>/cap_gl-01.cap <BSSID>'   # per-minute counts for one AP; reads the file only
./scripts/competition-deauth.sh -b <BSSID> -c 6 -u <HH:MM> --dry-run                      # the deauth plan only; no root, no card
```

## 2. Test evidence

### 2.1 Cards

| Card | Driver, firmware | Antennas | Period (18 Oct unless noted) | Result |
| :--- | :--- | :--- | :--- | :--- |
| Realtek RTL8723BE | `rtl8723be`, single interface | not checked | 17 Oct ~23:05 to 18 Oct 06:43; 19:52 to 21:11 | PCIe RxErr errors logged, 65–120 a minute. Injection passed on channel 1 in some runs and failed in others. Channel 6 receives only if monitor mode starts on a channel 6 network |
| Intel AC8260 | `iwlwifi` with `iwlmvm`, firmware 36.c8e8e144.0 | 2x2 chips, one cable fitted | 06:43 to 19:45 on 18 Oct (three boots); 23:24 on 19 Oct to 03:51 on 20 Oct | No PCIe errors. Failed in every session from 08:47 to 09:59 on 18 Oct. Passed from 10:22 (see 2.4). On 20 Oct it failed with the cable on chain 0 and passed on chain 1 (see 2.19) |
| Intel 3168NGW (PCI 8086:24fb) | `iwlwifi` with `iwlmvm`, firmware 29.0bd893f3.0 | 1x1, one chain | 21:16 on 18 Oct to the AC8260's load at 23:24 on 19 Oct; again from the firmware load at 03:51 on 20 Oct (kernel log) | Every injection test recorded here passed: 8 on 18 Oct, 3 on 19 Oct, 4 on 20 Oct (see 2.2, 2.20) |

The Realtek and the AC8260 both sat in PCI slot 02:00.0.

### 2.2 Injection runs

PASS means the target AP answered. `injection-test.sh` runs `aireplay-ng --test`. Logs are in `logs/` unless noted. The 18 Oct log files named in the tables below are *missing* from `logs/` on 20 Oct (section 5, item 20). The results stay as recorded here.

**Intel 3168NGW**

| Time | Channel | Result | Log |
| :--- | :---: | :--- | :--- |
| 21:21 | 1 | PASS. Home AP 30/30, extender 29/30, weakest AP (about -88 dBm) 0/30 | `injection-20261018-212105-ch1.txt` |
| 21:21 | 6 | PASS. Lab router 30/30, reply power -11.9 dBm | `injection-20261018-212130-ch6.txt` |
| 21:43–22:04 | 6 | PASS, six runs in a loop, all 30/30 | `injection-20261018-214318` to `220429-ch6.txt` |
| 04:45 (19 Oct) | 6 | PASS, 30/30 (the 2.7 run; session notes, no injection file in `logs/`) | not in `logs/` |
| 06:28 (19 Oct) | 6 | PASS, 29/30 on "Test" | `2026-10-19_062859_iso_off/iso_off-injection.txt` |
| 06:32 (19 Oct) | 6 | PASS, 29/30 on "Test"; other AP D 1/30 | `2026-10-19_063243_iso_off/iso_off-injection.txt` |
| 08:10 (19 Oct) | 6 | PASS, 29/30 on "Test" | `2026-10-19_080955_cap/cap-injection.txt` |
| 04:00–04:53 (20 Oct) | 6 | PASS in all four runs, 29 or 30 of 30 | see 2.20 |

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
| 00:32–02:34 (20 Oct) | 1 and 6 | FAIL with the cable on chain 0 (00:32–01:51); PASS on chain 1 (01:54–02:34) | see 2.19 |

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
- `managed-scan.sh` as root, 3168NGW at 21:18: networks on channels 1, 6, 11 and 36 (`logs/managed-scan-20261018-211858.txt`, *missing*, item 20).

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
- **Run:** `logs/cap_nophone-01.cap` and `logs/cap_nophone-01.csv`, active mode (the default). *Missing* from `logs/` on 20 Oct (item 20). The same kind of key recovery from the download alone is in the per-run folders `2026-10-19_081325_cap` (key at 08:14) and `2026-10-19_082150_cap` (key at 08:37), section 2.13. Injection passed 30/30 at 04:45:05 (session notes; no injection file for this run is in `logs/`). The capture held 129,073 IVs when the key came out. The key file is `logs/cap_nophone.key`, 26 hex digits; its value is not copied here. The `-t` value is not in the logs.
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
- **Answered in 2.9:** the broadcast frames carry IVs, at close to the send rate.

### 2.9 Broadcasts supply the IVs (19 Oct, passive)

- **Run:** `logs/2026-10-19_052643_cap_bcast/`, passive, channel 6. The laptop sent 300 UDP broadcasts a second to the LAN from about 05:26:22, and stopped them at about 05:34. The router's radio sent 143,898 frames in that window (router counter). The capture started at 05:26:43 and stopped at 05:32:00, with the key recovered before it stopped. The sender was a short Python script kept outside the repo. The repo's `scripts/broadcast_feed.py` does the same job, and 2.18 used it. The capture-health table gives the same AP-broadcast count, 47,041.
- **Where the IVs came from.** The capture holds 47,535 WEP data frames to or from the AP (the CSV says 47,529 IVs):

| Direction | Frames |
| :--- | ---: |
| AP broadcasts | 47,041 |
| AP to client | 137 |
| Client to AP | 357 |

- One client was associated from 05:27:43 to 05:30:53 (last seen). Its 494 frames are about 1% of the IVs. This breaks the phone-off condition for those three minutes.
- In 05:31, with no client data frames, the capture logged 17,816 broadcast IVs, about 297 a second, close to the send rate. The minutes with the client were lower, 4,300 to 11,600. That may be the access point holding broadcasts for a client in power-save mode. This is not tested.
- The laptop's Wi-Fi dropped when the capture started (05:26:44) and came back when the restore finished (05:32:17).

### 2.10 No station, broadcasts sent (19 Oct, passive)

- **Run:** `logs/2026-10-19_070743_nostation/`, channel 6, with the phone off. The router showed no associated station at 07:05:28. The laptop sent 90,000 UDP broadcasts to 192.168.8.255 in 300 seconds, from about 07:05 to 07:10 (300 a second). The capture started at 07:07:43, about two minutes into the broadcasts, so it covers roughly the last three minutes. The sender reported no errors. The router's counters were not read in this run.
- **Result:** no WEP data frame to or from the AP in the whole capture (the capture-health table counts 0 WEP frames). No client was seen. The CSV shows `#IV` 0, and the capture holds only 3,029 beacons. No key.
- **Control:** with a station associated, the same sender gave 47,041 AP broadcasts and 17,816 broadcast IVs in one minute (2.9).

### 2.11 Time for each aircrack-ng try (19 Oct)

- **Successes took under a second.** Copies of the broadcast capture (2.9) gave the key in 0.3 s at 47,535 frames, with the default options and with `-1`, `-x0` and `-f 1`. At 42,706 frames the default took 0.6 s, and at 37,877 frames 0.8 s.
- **Failures never ended on their own.** A copy cut at 40,293 frames found no key in 10 s or in 60 s (default options). Partial copies of the 19 Oct capture, kept outside the repo, at about 5,400, 19,500 and 38,700 IVs each ran the full 60 s with the default options. With `-x0` or `-f 1`, the 19,500 and 38,700 IV copies ran past 30 s. With `-1` they ran past 351 s, and only KILL stopped them, because TERM did not.
- None of the options made a failed try end sooner, so the script keeps the defaults and caps each try (decision 10).

### 2.12 The 06:32 run: who sent what (19 Oct, active)

- **Run:** `logs/2026-10-19_063243_iso_off/`, the second isolation attempt, channel 6. Fake auth and ARP replay ran from our card, and the phone was downloading. The user confirmed the phone was on the network in these runs.
- **The phone** has a randomized MAC (the locally administered bit is set), and the router gave it a DHCP lease. It is the phone. The AP sent it 82,132 data frames, 99.9% of them 1,542 bytes. It sent back 45,540 frames, 96% of them 94 bytes. That is a TCP download with ACKs.
- **The card's hardware MAC** is not written here. Its 9,035 frames to the AP are 68 to 70 bytes, the size of the ARP replays. The AP sent it no data frames at all.
- **Association without the key worked.** The fake-auth log shows one association and no deauthentication for the run.
- **Broadcasts:** 33 went to all stations, each 68 bytes (ARP size). The phone was associated, so these don't show whether the card alone gets broadcasts.
- **Correction:** an earlier reading said the AP answered the card with 82,000 frames and that the key came from that. Those frames went to the phone, and the replays got no answer in this run.
- **The 08:10 active run** (`logs/2026-10-19_080955_cap/`) passed the injection test (29 of 30 probes on the lab AP, the last count in its log). The card was disassociated twice after fake auth, and about 10 s after it started the monitor interface went down ("Network is down"). The run ended with 509 frames and no key.

### 2.13 Passive runs, 08:13 and 08:21 (19 Oct)

- **`logs/2026-10-19_081325_cap/`:** 55,342 WEP frames. The key came out in about 40 seconds (08:13:25 to 08:14:05). 99.9% of the AP's frames went to the phone's MAC and were 1,542 bytes.
- **`logs/2026-10-19_082150_cap/`:** 144,518 WEP frames. The key came out at 08:37:27, after 16 minutes. The download paused from 08:25 to 08:34 (about 700 frames in those ten minutes) and resumed at 08:35.
- **The 10 s cap was not the cause.** Cut copies of this capture at 40,000 to 130,000 frames gave no key in 60 seconds. The full 144,518 frames gave the key in 2.9 seconds.
- **Retransmissions were not the cause.** Repeated IVs were all retries, 0.7% to 2.3% of the frames in both runs.
- **PTW is probabilistic.** The 08:13 capture cracked at 55,000 frames, and the 08:21 capture did not crack at 130,000. The 08:21 wait was the download pausing, and the last frames it needed came after it resumed.

### 2.14 Keyless card with the flood, phone off (19 Oct, passive)

- **Run:** `logs/2026-10-19_092546_keyless/`, channel 6, passive capture. The flood sent 300 broadcasts a second to 192.168.8.255 from 09:25:27, and the router was logged every 30 seconds. The run was stopped after about two minutes.
- **Result:** no WEP frames, so no IVs. The router's association list showed "No station connected" at every sample from 09:25:27 to 09:29:31.
- **The AP never kept the card.** The fake-auth output shows "Authentication successful" and "Association successful" every 10 seconds from 09:26:09 to 09:27:19, and each request got an [ACK] from the AP. So the card received the AP's replies. The router's log shows the AP recorded those replies as "not acknowledged" about every 10 seconds, and then "deauthenticated due to inactivity". So the AP never got an acknowledgement for its replies. The router's clock reads 2023, so its timestamps show order only. The capture saw the card's MAC (20 packets, 09:26:09 to 09:27:19).
- **The flood reached the AP's driver.** The router's LAN counter rose by about 301 frames a second, and its wireless transmit counter rose by about 301 a second. That counter counts frames handed to the driver, not frames on the air. The capture shows no frames on the air, which matches the no-station result (2.10).
- **Not a test of a keyless station.** The question is whether the AP sends broadcasts to a keyless card that stays associated. This card never stayed associated, so this run doesn't answer it.

### 2.15 Keyless board and the phone, flood (19 Oct, passive)

- **The board never associated.** No connection or successful authentication was seen, and the capture shows no frames from the board in either run below. Its disconnect reasons are not recorded here.
- **Run `logs/2026-10-19_102957_keyless/`:** channel 6, 10:29:59 to 10:30:49, no station. No WEP data frames (CSV `#IV` 0, 490 beacons).
- **Run `logs/2026-10-19_103924_keyless/`:** channel 6, 10:39:26 to 10:43:44. The flood sent 300 broadcasts a second. The phone was first seen at 10:40:11. The capture holds 62,952 WEP frames, 62,852 of them AP broadcasts: 14,245 in 10:40 (from 10:40:11), 17,803 in 10:41, 17,729 in 10:42 and 13,075 in 10:43. The key came out in this run too: aircrack wrote `keyless.key` at 10:43. Its value is not copied here.
- **Reading:** with a station associated, the flood gives about 295 IVs a second, so 40,000 IVs take about 2.3 minutes. The phone download gave about 1,400 frames a second in 2.13 (55,342 frames in 40 seconds), so 40,000 IVs take about 30 seconds of download. On 19 Oct the router's link to the phone ran at 54 Mbit/s, which `iwinfo` expects to give 28.5 Mbit/s of throughput.
- **Broadcast rate (not measured):** the router's hostapd config sets basic rates of 6, 12 and 24 Mbit/s (`basic_rates=60 120 240`), so broadcasts most likely go out at 6 Mbit/s. An earlier note said 1 Mbit/s, which was wrong. The flood's ceiling is therefore much higher than the 300 a second tested here, but a faster sender has not been tested.

### 2.16 Unicast flood to the phone, 0-byte payload (19 Oct, passive)

- **Run `logs/2026-10-19_111119_keyless/`:** channel 6, 11:11:21 to 11:18:00. The flood was unicast to the phone (its DHCP address), 2,000 packets a second, with a 0-byte payload.
- **IVs:** 430,492 WEP data frames: 427,978 to the phone, 2,450 from it, and 64 broadcasts. The AP's frames were 88 bytes (QoS data). They carried one sequential IV counter: 428,097 distinct IVs, with a +1 step in almost every pair, and 4,241 gaps of +2 to +10 (missed frames). 2,746 frames had the retry bit. The busiest minutes had about 120,000 frames (2,000 a second). The 11:14 minute had no frames at all.
- **The default attack failed.** `aircrack-ng -n 128 -b <BSSID>` on the full capture, with the same options as `wep-crack.sh`, ran the full 170 s and found no key. The 10 s cap is not the cause.
- **The old KoreK attack found it.** `aircrack-ng -K` on the same file found the key after 75 keys. The key file was kept outside the repo, and its value is not copied here.
- **Reading:** the IVs are fine. The cause is the 0-byte payload: it makes 88-byte frames, the length of a QoS ARP reply, and the default PTW attack most likely reads frames of that length as ARP with known contents. Section 2.17 shows that removing the 88-byte frames makes the default attack crack. The 2.15 run used a 105-byte payload (173-byte frames) and cracked with the default attack at 62,952 IVs.
- **Change:** `scripts/broadcast_feed.py` keeps 105 bytes as its default, and its header says to keep payloads at 19 bytes or more.

### 2.17 Mixed frames in one capture, 11:31 (19 Oct, passive)

- **Run `logs/2026-10-19_113105_keyless/`:** channel 6, 11:31:07 to 11:33:53, 317,562 IVs. The capture began with a flood using 0-byte payloads (88-byte frames) from about 11:31:10. That flood ended by itself at about 11:33:00. A flood with the default 105-byte payload (175-byte frames) started at 11:32:58, so the two overlapped for about two seconds. By length, 206,825 frames were 88 bytes and 110,845 were 175 bytes.
- **The air set the rate:** the frames came at about 1,800 to 1,900 a second, with one flood or two. The 175-byte flood alone gave about 107,800 frames in 11:33.
- **The default attack failed on the mixed capture:** the first 40,000 IVs, the first 60,000 IVs and all 317,562 IVs each ran to the 10 s cap with no key.
- **The copy without the 88-byte frames cracked in 0.8 s** with the same default options (aircrack-ng 1.7). The key file was kept outside the repo, and its value is not copied here.
- **The tries did not overlap.** `wep-crack.sh` runs one aircrack-ng try at a time, and each try ends at its cap or when it finds the key.
- **Change:** `scripts/broadcast_feed.py` takes a file lock and refuses to start while another copy runs. A test with two copies ran one and refused the other.

### 2.18 Unicast flood to the phone, clean run (19 Oct, passive)

- **Run `logs/2026-10-19_114728_keyless/`:** channel 6, 11:47:30 to 11:48:01. The phone was first seen at 11:47:36. The flood was `broadcast_feed.py <phone IP> 2000 120` with the default 105-byte payload, and nothing else was running.
- **The key came out at 11:47:59,** 23.5 seconds after the phone's first frame, after 47,494 WEP frames. The key file `keyless.key` is in the run folder, and its value is not copied here.
- **Frames:** 51,224 AP-to-phone frames were 175 bytes. Nine other frames had other lengths.
- **Reading:** the unicast flood gave about 2,000 IVs a second, so 40,000 IVs took about 20 seconds. The 0-byte run (2.16) and the mixed run (2.17) did not crack, because their 88-byte frames confused the default attack.

### 2.19 AC8260 injection on 20 Oct (cable on the other connector)

The cable was on chain 0 for the runs up to 01:51 and on chain 1 from the 01:54 run. The move itself comes from the session notes and is not logged.

| Time | Channel | Result | Log folder (`logs/`) |
| :--- | :---: | :--- | :--- |
| 00:32 | 6 | FAIL, 0/30 on the lab AP "Test". The script went passive: 48,827 IVs, key recovered | `2026-10-20_003247_cap_gl` |
| 00:46 | 6 | FAIL, 0/30 on "Test". The script went passive: 51,418 IVs, key recovered | `2026-10-20_004631_cap_gl` |
| 00:53 | 6 | FAIL, 0/30 on "Test". The script went passive: 0 IVs, no key | `2026-10-20_005341_cap_gl` |
| 00:57 | 1 | FAIL, 0/30 on the home AP, its extender and other AP A | `2026-10-20_005657_injection-ch1` |
| 01:47 | 1 | FAIL, 0/30 on the same three APs | `2026-10-20_014734_injection-ch1` |
| 01:48 | 6 | 3/30 on "Test"; aireplay printed "Injection is working!" | `2026-10-20_014839_injection-ch6` |
| 01:49 | 1 | FAIL, 0/30 on the same three APs | `2026-10-20_014911_injection-ch1` |
| 01:50 | 1 | FAIL, 0/30 on the same three APs | `2026-10-20_015000_injection-ch1` |
| 01:51 | 6 | FAIL, 0/30 on "Test" and other AP C | `2026-10-20_015107_injection-ch6` |
| 01:54 | 6 | PASS, 30/30 on "Test" | `2026-10-20_015453_injection-ch6` |
| 01:55 | 1 | PASS: home AP 30/30, other AP A 15/30, extender 14/30, other AP B 3/30 | `2026-10-20_015521_injection-ch1` |
| 01:56 | 6 | PASS, 30/30 (replay run, below) | `2026-10-20_015626_cap_gl` |
| 01:57 | 6 | PASS, 30/30 (replay run) | `2026-10-20_015703_cap_gl` |
| 02:33 | 6 | PASS, 30/30 | `2026-10-20_023312_injection-ch6` |
| 02:34 | 6 | PASS, 30/30 (replay run) | `2026-10-20_023435_cap_gl` |

- **Chain 0 (00:32 to 01:51):** nine runs. Eight got 0/30, and the 01:48 run got 3/30, which aireplay reports as "Injection is working!", so it is a weak pass. The failed test sends the script to passive mode, and two of the three runs at 00:32 to 00:53 recovered a key that way (48,827 and 51,418 IVs). Passive capture worked on this card, with the cable on chain 0. The source of those IVs is not analysed yet.
- **Chain 1 (01:54 to 02:34):** six runs, all passed. In the 01:55 run every AP replied, but only the home AP answered 30/30.
- **Replay runs on 20 Oct:** the runs at 01:56, 01:57 and 02:34 recovered no key (no `.key` file in their folders). Their fake-auth and ARP output printed "channel -1" while waiting for a beacon. The injection outputs in the same folders do not, and the attack went on after the line (2.20).

Session notes, not in `logs/` (02:26 to 03:32). They cannot be checked against logs until they are rerun with the output saved:

- **02:26:** `iw phy phy2 set antenna 2 2` failed with -95 (EOPNOTSUPP). The driver source, as read on 20 Oct, accepts that call only on the 9000, 22000 and AX210 families, so the AC8260 cannot pin its antenna.
- **02:49 to 03:27:** a per-frame antenna field in the injected frame. At 1 Mbit/s the field was ignored. At 6 Mbit/s with the cable on chain 0, antenna 0 answered 30 of 30 in both rounds, antenna 1 (the empty chain) answered 0 of 30, and the default answered 0 of 30 (03:23). A null-frame test (03:27) gave 200 to 240 ACKs per 30 frames in every mode, so ACK counts do not show which chain sent a frame.
- **03:32:** fake auth alone crashed the AC8260 firmware twice in 2 s. The kernel log has 10 "Microcode SW error" restarts for that card in the same boot.

### 2.20 3168NGW on 20 Oct: the keys came from the phone's download

- The kernel log shows the 3168's firmware loaded at 03:51:43 on 20 Oct. An earlier note put its return at 02:00, which the log does not support.
- All four runs are in `logs/`, on channel 6 against the lab AP "Test", in active mode.

| Time | Injection | Key file | IVs (CSV) | Log folder (`logs/`) |
| :--- | :---: | :---: | ---: | :--- |
| 04:00:47 | 30/30 | none | 63,572 | `2026-10-20_040047_cap_3168` |
| 04:04:20 | 29/30 | yes | 46,664 | `2026-10-20_040420_cap_3168` |
| 04:20:19 | 30/30 | yes | 68,387 | `2026-10-20_042019_cap_3168` |
| 04:53:00 | 29/30 | none | 172 | `2026-10-20_045300_cap_3168` |

- **The IVs came from the phone's download.** The AP sent frames to the phone's MAC (the phone from 19 Oct) in numbers that match the IV count: 46,852 frames against 46,664 IVs (04:04), 68,520 against 68,387 (04:20), and 63,730 against 63,572 (04:00). The replays added almost nothing, as in 2.7.
- **The AP dropped the card.** In the 04:00 run, 61,842 disassociation frames addressed to the card carried the AP's address. No deauthentication frame was addressed to the card. The router's log would show whether hostapd sent them. Its clock is about 3 h 04 min behind the laptop's (the board's notes).
- **Deauth bursts to other stations.** At 5.7 s into the 04:00 run and 5.8 s into the 04:20 run, the AP's address sent 210 and 235 deauthentication frames to 22 and 24 other stations, all within about 0.4 s. None were addressed to the card. The capture does not say who sent them. The router's log would show whether hostapd did.
- **Firmware restarts.** The kernel log shows 28 "Microcode SW error" restarts after the 03:51:43 load.
- **"channel -1" in the attack output.** The fake-auth or ARP replay output of the 04:00, 04:04, 04:20 and 04:53 runs prints "Waiting for beacon frame ... on channel -1". Their injection tests do not. The line is aireplay-ng waiting for a beacon, and the same text is one of the 2.4 failure signatures. Here it stopped nothing: the attack went on in all four runs, with 14 successful fake authentications in 04:00, 4 in 04:20, and ARP-replay reads in 04:04 and 04:53.
- The 04:53 run got only 172 IVs. The log does not say why; the download may not have been running.

### 2.21 ESP32-C3 board, 20 Oct: wrong-key joins and a keyed run

Channel 6, passive capture, folders in `logs/`.

| Time | Log folder | Station seen | AP data frames | IVs | Key file |
| :--- | :--- | :--- | ---: | ---: | :--- |
| 04:36 | `2026-10-20_043649_esp` | none | 0 | 0 | no |
| 05:24 | `2026-10-20_052444_board_near` | none | 0 | 0 | no |
| 05:29 | `2026-10-20_052911_board_near` | none | 0 | 0 | no |
| 05:37 | `2026-10-20_053755_board_near` | none | 0 | 0 | no |
| 06:14 | `2026-10-20_061421_board_near` | the board's MAC, first seen 06:15:18 | 72,149 | 72,149 | yes, 26 bytes (value not copied) |

- **06:14 run.** All 72,149 AP data frames went to the broadcast address, and the CSV's IV count is the same number. The AP's broadcasts started at about 06:14:30. Before the station first appeared at 06:15:18 there were 8,388 broadcast IVs. After it, there were 63,761 in 54 s, about 1,180 a second. The sender's rate is not recorded in the folder.
- **Matched by timing, not by MAC.** The board's notes give three joins on the laptop clock: 06:06 and 06:09 with a wrong key, and 06:12 with the real key. Only the real-key join got an address (its DHCP address). The board's MAC is not in the repo, so the station is matched to the board only by timing. The station sent one 24-byte null data frame at 06:15:18. No authentication or association frame from it is in the capture, so the capture does not show when it associated.
- **Effect on 2.10.** The broadcasts started at 06:14:30, after the 06:12 join. So the 8,388 broadcast IVs sent before airodump first saw the station went to an associated station, and 2.10 stands. This run had a keyed station, so it does not answer the keyless question (section 5, item 7).
- **Wrong-key joins (board notes).** The wrong-key joins at 06:06 and 06:09 associated on the first retry after boot, but the board logged no `sta ip`, so it never got DHCP. The router dropped it for inactivity at 06:22. Its requests reached the router only between 06:29 and 06:54, where the router answered but never got an ACK, and not at all after 06:55. The board was recovered at 07:30 with `txpower 8` and `apply` (the board's notes).
- **Not in the repo.** The board's serial log and the router's log are not in `logs/`. The joins and the 06:22 drop above come from the board's notes and cannot be checked here (section 5, item 16).

## 3. Decisions

1. **Keep the 3168NGW as the lab card, for now.** It passed every injection test recorded here: 8 on 18 Oct, 3 on 19 Oct and 4 on 20 Oct (2.2, 2.20). The AC8260 failed from 08:47 to 09:59 on 18 Oct with no cause found, and failed from 00:32 to 01:51 on 20 Oct until its cable moved (2.19). On 20 Oct the 3168NGW's replay output printed "channel -1" while it waited for a beacon, and the attack went on (2.20).
2. **Channel 6 is the test channel.** Only the GL router was heard there, so no other network gets probes. Channel 1 has other networks, and `injection-test.sh` probes every AP on its channel.
3. **One antenna cable.** With one cable fitted, the 2x2 AC8260 has no second antenna to gain from, so the 1x1 3168NGW does the same job here. The cable stays on the working connector: chain 0 for the 3168NGW, chain 1 for the AC8260 after 19:00 on 18 Oct. On 20 Oct the AC8260 failed with the cable on chain 0 and passed on chain 1 (2.19). The connector used for the 3168NGW runs on 20 Oct is not recorded, so note it with each run.
4. **Find channels with `managed-scan.sh`.** On the Realtek, monitor mode keeps the channel it started on, so a monitor scan cannot find a network on another channel. Start monitor mode with the card connected to a network on the target channel.
5. **L0s stays on.** Turning it off stopped the logged errors but showed no Wi-Fi gain.
6. **Build wpad in GitHub Actions; run it from RAM.** The SDK needs Ubuntu 22.04 with distutils, which this laptop lacks. The workflow (`.github/workflows/build-wpad-wep.yml`, in the repo) builds from `lab10/router/wifi-wep-setup/` on each push that changes it. Whether its runs pass has not been checked here, because `gh` is not installed on this laptop. The router side, `try-on-router.sh`, mounts the build over `/usr/sbin/wpad` and writes nothing to flash. On 20 Oct it brought WEP back after a reboot, with a wpad built outside the repo (session notes).

7. **Crack in passive mode while the network carries traffic.** On the 3168NGW the replay added almost no IVs (2.7), and it stalled the firmware 11 times in five minutes. The traffic of a client sets the IV rate. The 08:13 and 08:21 runs cracked from a download alone (section 2.13).
8. **Traffic for IVs: wired broadcasts, with a station associated.** With no station they gave no IVs (2.10). A unicast flood to the phone's IP gave about 1,800 to 2,000 IVs a second (2.16, 2.17), with payloads of 19 bytes or more and one flood per capture. The first clean run cracked the key 23.5 s after the phone's first frame (2.18). Whether a station that never had the key can serve is the open test (section 5, item 7). The ESP32-C3 cannot send encrypted frames, so it cannot inject (section 5, item 8). Both stay on our own router. The class router is used only in the window (section 4).
9. **One folder per run in `logs/`.** From 19 Oct 05:26, each run writes into `logs/<time>_<prefix>/`, so its capture, key and output stay together and an earlier run is never overwritten. The runs before that were flat in `logs/`. Those files are missing now (section 5, item 20).
10. **Each aircrack-ng try stops at 10 s.** A try that finds the key takes under a second (2.11), so 10 s is more than three times the slowest success measured (2.9 s, section 2.13). A failed try runs until the cap, so the cap is its wait. The old 60 s was not based on a measurement or a source. A key that a try misses is tried again 10,000 IVs later.
11. **The analysis numbers come from the capture-health table** (`capture_health` in `scripts/wifi-lib.sh`, printed at the end of each run). It counts the WEP frames, the AP's broadcasts, the retries, and the deauth, disassoc, auth and association frames per minute. Its totals match tshark's own counts on six real captures (section 4.2). The counts in 2.9 and 2.10 match it.
12. **The deauth tool sends nothing without its checks.** `--in-window` and START come before the card is touched. The channel read-back and the capture check come before the first frame. `-u`, when given, stops the run at the window's end. Each run writes its own capture and health table. Its checks are in section 4.2. The first live run is still to do (section 5, item 14).
13. **Deauth targets (20 Oct).** Without a client file (`-f`), the tool sends to the broadcast address, so no client list is needed. The earlier version sent only to listed clients. A list is a snapshot: it misses clients that join later. Both forms stay in the tool, and the operator chooses. Either form reaches only the BSSID named with `-b`.

## 4. Adversarial and physical actions

The guide asks for these in the submission. So far:

- **Authorization for the evaluation window (20 Oct).** A written reply from the course staff allows deauthentication of clients on the evaluation network during the official window, and traffic toward the class router's channel for IV collection. It says that no outside users are on that router. It allows joint work on the document, with both names and each person's part. The reply covers the document, not the competition, which the guide makes individual. The reply is kept with the course email, not in this repo.

- **Physical (done):** card choice (1x1 3168NGW against 2x2 AC8260 with one cable) and antenna connector. On 20 Oct the AC8260 injected only with the cable on chain 1 (2.19). See 2.1, 2.6 and decisions 1 and 3.
- **Software, done and in use:** a channel-locked injection test with a PASS/FAIL exit code (`injection-test.sh`); channel discovery without monitor mode (`managed-scan.sh`); recovery after a killed run (`wifi-recover.sh`); the per-minute capture check (the capture-health table in `wifi-lib.sh`, section 4.2).
- **Software, built and checked, not yet run on air:** the deauth tool (`competition-deauth.sh`, section 4.2).
- **Per-frame antenna (session notes only):** a radiotap antenna field on an injected frame changed which chain carried 6 Mbit/s probes on 20 Oct. It did not change 1 Mbit/s probes (2.19). No log is saved for it yet.
- **Congestion, packet loss and availability (research and tools, no test run yet):** section 4.1 covers how each condition affects a capture and which tools show it. The capture-health table counts those frames in any capture, minute by minute. `scripts/competition-deauth.sh` deauthenticates the clients of one network, either all of them (broadcast) or the ones listed with `-f`. It runs only with `--in-window`, only against the BSSID given with `-b`, and only after you type START. Neither has been run against the class router, and the deauth tool has not yet been run on air.
- **Other networks:** the channel 1 runs also probed APs that were not the target, for example other AP A and other AP B on 20 Oct. Keep tests on channel 6. No test against the class router is logged yet.

### 4.1 Congestion, packet loss and availability: what they do to a capture

These are research notes, from the 802.11 mechanisms and the tools' documentation. None of these conditions has been run against anyone.

| Condition | Effect on a passive WEP capture | What shows it |
| :--- | :--- | :--- |
| Airtime congestion (heavy traffic from any sender on the channel) | The AP's frames wait for the channel. The monitor card misses frames it cannot hear cleanly, and senders retry. Retries repeat IVs and leave gaps in the frame count, so the IV rate falls. | Channel busy time, if the driver reports it. `iw dev <iface> survey dump` printed nothing on the 3168NGW in managed mode (20 Oct), so another source is needed; not tested. The retry share per minute from the capture-health table, and IVs per minute in the CSV. |
| Deauthentication or disassociation of a client | The client loses its session, so its data stops until it reconnects. WEP networks have no protected management frames (802.11w needs WPA2 or later), so any station can send these. The IV source goes quiet during the reconnect. | The capture-health table counts deauth and disassoc frames per minute. Kismet has alerts for deauth and disassociation floods (from its documentation; not tried here). |
| Authentication or association floods at the AP | The AP's table of stations fills, so real clients may not be able to join. | Authentication (0x0b) and association request frames by source. The AP's log (`logread` on the router). |
| Packet loss from weak signal or interference | Frames fail their checksum, the capture has gaps, and retries rise. | Retry share per AP. Signal per AP in `airodump-ng` or `iw dev <iface> station dump`. The ESP32's packet count against the laptop's. |
| RF noise jamming | Out of scope. The lab rules say it is illegal. | Not tested. |

Our own runs already show two of these. Repeated IVs were all retries, 0.7% to 2.3% of frames (2.13). The 04:00 run on 20 Oct had 883 retries in 80,906 WEP frames (1.1%, from the capture-health table), 61,842 disassociation frames addressed to our card with the AP's address, and a burst of 210 deauthentication frames to 22 stations within 0.2 s (2.20). A burst like that is what a broadcast-style deauth looks like, whether the AP or a spoofer sent it.

Planned, not run. On our own phone and router, at any time:

- The IV rate in the CSV with and without a disconnect of our own phone during the capture.
- Channel busy time during our own flood, from a source that reports it (`survey dump` printed nothing on the 3168NGW), set against the IV rate in the same minutes.
- Counting deauth and disassociation frames per minute during our own capture, and logging the reconnect time, so a run shows the drop and the recovery.

In the official window, on the class router, and only as the course staff's reply allows: deauthentication of the clients of that network with `scripts/competition-deauth.sh`. Each run prints the capture-health table of its own capture, which measures the effect.

### 4.2 Tool checks (20 Oct)

Both tools were checked on 20 Oct without sending a frame, using stub Wi-Fi commands and a synthetic capture. The check scripts are not in the repo. Section 1 and COMMANDS.md, section 12, have the commands to check the tools by hand.

- **The capture-health table** (`capture_health` in `scripts/wifi-lib.sh`): on a synthetic capture with known counts, both minutes and the total matched, with and without a BSSID, and the four refusals exited 1 (missing file, not a capture, bad BSSID, BSSID with no frames). Every total of six real captures was recounted with tshark's own filters: the 04:00, 04:20 and 06:14 captures of 20 Oct, the 19 Oct broadcast and no-station captures, and the 04:00 run with every AP. Result: 12 of 12 checks passed.
- **`competition-deauth.sh`:** every Wi-Fi command was replaced with a stub that writes one line to a log, and the script ran as root inside a user namespace (`unshare -r`). The airodump stub copied a synthetic capture, so the table at the end had known numbers. The checks covered the refusals (no window flag, BSSID not allowed, no root, removed or bad options, missing or empty client file, no allowlist, no arguments, no `-u`, a `-u` time already passed or badly formed), the dry run with and without `-f` (the plan, the card and AP skipped, nothing called), and the run: START gating, the call form (one per client with `-f`, one broadcast call without it), the order of the calls and the WiFi changes, the run's capture started after the channel and before the first call, the table printed and saved, a hung call stopped at its limit, Ctrl-C with WiFi restored, a capture that dies at once stopping the run before any send, and `-u` stopping the run at the window end. Result: 31 of 31 checks passed, before the BSSID allowlist and the required `-u` were removed on 20 Oct. The stub suite is no longer in the repo, so that result does not cover the current script.
- **The end of `wep-crack.sh`:** a passive run with stubbed Wi-Fi commands and a stub `aircrack-ng` that wrote a key. The key came out, the table was saved, and it was printed after WiFi was restored. Result: 4 of 4 checks passed.
- **`-m` and the shared monitor (21 Oct):** the same method: copies of the scripts in a scratch folder, stub commands that log their arguments and send nothing, and `unshare -r`. Nine cases: the two dry runs; the refusals (both `-i` and `-m`, neither, a monitor that is not in monitor mode, and a channel other than `-c`, which stops before the capture starts); a broadcast round and a round to a listed client under `-m`, with no monitor start or stop, no channel change and no WiFi restore on exit; and the `-i` run, which made the same Wi-Fi calls as the 20 Oct script. Result: 9 of 9 behaved as expected. Not run on air: section 5, items 21 and 22.

Not yet checked: the tool on air. The real output of `aireplay-ng --deauth` is not recorded yet, and neither is the effect of the broadcast form on a real client. On the AC8260, `-a` can stop with "Couldn't determine current channel" (2.19). aireplay-ng has `--ignore-negative-one` for that case. The tool now passes it, after reading the channel back and checking that its capture hears the BSSID. The first real run (21:12, 20 Oct, `logs/2026-10-20_211235_deauth-ch6`) sent nothing: aireplay-ng exited on the channel -1 message. The fixed tool is not yet run on air. The next live run needs your own phone and the GL router (section 5, item 14).

## 5. Open items

1. **WEP key recovered (19 Oct, section 2.7).** The capture named in 2.7 is missing from `logs/` (item 20). Use `2026-10-19_081325_cap` or `2026-10-19_082150_cap` as the evidence instead: both recovered the key from the download alone (2.13). Their `.key` files stay in `logs/` and are not in the repo.
2. **3168NGW watch.** The 20 Oct injection tests passed four times on channel 6 (2.20), and none printed "channel -1" or "No Answer". In the earlier fake-auth output, the "channel -1" line was followed by sends (2.20). In the deauth tool on 20 Oct at 21:12 it was followed by an exit, so it is not always harmless (section 4.2). Repeat the channel 6 loop.
3. **AC8260 cause.** The script restore does not explain the 10:22 pass (2.4). On 20 Oct the failures stopped when the cable moved to chain 1 (2.19). The cable move is the change the notes record between the failing and passing runs. A run with the cable on chain 0 and the link-down done by hand would test whether the connector explains it.
4. **`wifi_set_channel`.** It checks only the exit status, so the 08:47 "channel -1" went unnoticed. The deauth tool now reads the channel back itself (section 4.2); `wifi_set_channel` still does not.
5. **Channel 36 at 10:30.** Three APs found on 5 GHz, no reply. An earlier note said 36 and 149 are not allowed in the GT domain. The 3168NGW showed both allowed at 21:33, so that note was wrong, and regulation does not explain this failure.
6. **Docs.** Done (19 Oct).
7. **Keyless station and broadcasts (sections 2.8 to 2.15).** On 19 Oct, the broadcasts gave 99% of the IVs in a five-minute capture, with one client present for three minutes. With no station associated, the same broadcasts gave no IVs (2.10). In the 06:32 run the replay card stayed associated without the key but received no data frames (2.12). At 09:25 the AP never kept the card (2.14). The ESP32-C3 board never associated on 19 Oct (2.15). On 20 Oct it associated with a wrong key and got no address (2.21), so the 06:14 run had a keyed station. Still open: whether the AP sends broadcasts to a wrong-key station that stays associated.
8. **ESP32-C3 as a keyless WEP station.** Espressif's raw-frame call ["cannot be used for sending encrypted or QoS frames"](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32c3/api-guides/wifi-driver/wifi-vendor-features.html), so the board cannot send WEP replays. It could still be the station in item 7. Nothing had been tested on the board before 19 Oct: [ESP-IDF Wi-Fi API, v5.4.4](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32c3/api-reference/network/esp_wifi.html) and [Wi-Fi driver guide, v4.3](https://docs.espressif.com/projects/esp-idf/en/v4.3/esp32c3/api-guides/wifi.html). On 19 Oct the board never associated (2.15). On 20 Oct it associated with a wrong key but got no DHCP address, and the router dropped it for inactivity at 06:22 (2.21, from the board's notes). Its disconnect reasons are still to be recorded.
9. **Restore WPA2 on the GL router** after the lab, with the steps in [router/wifi-wep-setup/README.md](router/wifi-wep-setup/README.md) (section 4).
10. **Injector hardware.** The 3168NGW passed the 08:10 injection test (29 of 30 probes), but the AP never answered its replays, and its monitor interface dropped ("Network is down") about 10 s into the replay. An AR9271 USB adapter is the common alternative. It is not tested here. The router's single radio cannot inject into its own AP.
11. **Monitor-card acknowledgements.** At 09:25 the card received the AP's authentication and association replies, and the AP acknowledged the card's requests, but the AP never got an acknowledgement for its replies, so it never kept the card (2.14). The card did associate in the 06:32 run. The cause is unknown; it may be how the monitor-mode card acknowledges frames.
12. **Antenna probe output.** The probes from 02:49 to 03:27 on 20 Oct (2.19) have no output in `logs/`, and the probe script is not in the repo. Add both, then rerun the probes with logs before their numbers are cited.
13. **Board MAC.** The board's MAC is not in the repo. Add it from the board's first log line, so the station in 2.21 can be matched.
14. **Live run of the deauth tool.** Not done. The first real run (21:12, 20 Oct) sent nothing (section 4.2), and the fix is in the script. The plan: a dry run first; then one round (`-r 1`) with `-f` set to the phone's MAC, on the GL router (channel 6). The run's capture and health table show the effect on the phone. A broadcast run would reach every client of our own router, so it comes after that, and only on our own router. It needs your go-ahead and the phone's MAC.
15. **Class router.** Its BSSID and channel are not in the repo. Pass the BSSID with `-b` when the course staff confirm it, and write the window times here.
16. **Board log.** Save the board's serial log for 20 Oct, 06:00 to 07:31, into `logs/`. Section 2.21 relies on the board's notes until then.
17. **Router log for the 20 Oct deauth bursts (2.20).** Check hostapd's log on the router for 04:00 and 04:20. Its clock is about 3 h 04 min behind the laptop's.
18. **Monitor-mode 5 GHz on the 3168NGW.** Not tested. Section 2.3 saw channel 36 in managed mode only.
19. **Channel check in the deauth tool.** The tool now reads the channel back, and its own capture must hear the BSSID before the first call (section 4.2). Not yet run on air.
20. **Missing logs.** On 20 Oct these were not in `logs/`, and none was found elsewhere under the home folder or in `/tmp`: `cap_nophone-01.cap`, `cap_nophone-01.csv` and `cap_nophone.key` (2.7); `cap_gl-01.csv` (the earlier 19 Oct runs in 2.7); `managed-scan-20261018-211858.txt` (2.3); and the 18 Oct injection outputs `injection-20261018-*` (2.2). The `logs/` folder last changed at 07:42 on 20 Oct, and the cause is not known. The 20 Oct commands here did not delete them: they wrote only to temporary folders. The per-run folders from 19 Oct 05:26 onward are present. If you have copies, put them back in `logs/`; the rows that cite them then need no change.
21. **A round during a crack (`-m`).** Not run. COMMANDS.md section 12.7: one round to your phone while `wep-crack.sh` runs on the GL router. The crack's IV count should keep rising, and none of its restart lines should appear. Until that is seen, a round during a crack is untested.
22. **Fake auth after a deauth to its own card.** Not run. The code says aireplay-ng re-authenticates 3 s after a deauth addressed to the card. No such deauth is in any fake-auth log (19 and 20 Oct). Disassoc re-authentication is in the logs (13 events), but its "3 seconds" is not a real pause. COMMANDS.md section 12.8 sends one frame to the crack's card and reads the log.
