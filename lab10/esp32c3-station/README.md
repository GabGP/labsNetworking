# Keyless WEP station (ESP32-C3 Super Mini)

Lab #10, open item 7 in `TESTS.md`.

The board joins the GL router's WEP network as an ordinary station, with a wrong key. Association does not check the key, so the access point can accept it. The board acknowledges frames in hardware. In the 09:26 run (TESTS.md 2.14), the laptop's monitor-mode card did not acknowledge the access point's replies, so the access point never kept it.

The test asks one question: does the access point send broadcast data frames to a keyless station that stays associated?

This is a station, not an injector. Espressif's raw-frame call cannot send encrypted frames, so the board cannot replay.

## Build and flash

PlatformIO Core is installed at `~/.platformio/penv/bin/pio`. Use the full path, or add `~/.platformio/penv/bin` to your PATH.

```bash
cd <repo>/lab10/esp32c3-station
pio run                 # build; the first build downloads ESP-IDF and the toolchain
pio run -t upload       # flash over USB
pio device monitor      # serial log at 115200
```

If the upload cannot connect, hold BOOT, tap RESET, release BOOT, and run the upload again.

## Connection check

Run this first, with no flood and no capture. Put the board within a metre of the router, flash it, and run `pio device monitor` for about a minute. The first lines show the board's MAC, its 802.11 protocol bitmap (1 = 11b, 2 = 11g, 4 = 11n) and its maximum TX power. Every 10 seconds the log shows the router's beacon signal, and whether the router has replied to the board. If the board still never connects, paste those lines.

## Test

Keep the phone's Wi-Fi off. Before you start, the router should show no station:

```bash
ssh root@your-router-ip 'iwinfo wlan0 assoclist'     # expect: No station connected
```

Run three terminals from the `lab10` folder. The laptop must be on the router's LAN port, because the flood goes out the wired port.

1. Capture (passive, needs sudo; the laptop's home Wi-Fi drops while it runs):
   ```bash
   sudo ./scripts/wep-crack.sh -i wlp2s0 -b <router BSSID> -c 6 -m passive -o esp
   ```
2. Flood, 300 broadcasts a second for 10 minutes:
   ```bash
   python3 scripts/broadcast_feed.py 192.168.8.255 300 600
   ```
3. Board log:
   ```bash
   cd esp32c3-station && pio device monitor
   ```

Start the flood and the capture before the board boots. The first minutes are then a control with no station.

While it runs, check the router. The board's MAC, shown in the first log line, should appear in `iwinfo wlan0 assoclist` and stay there.

To stop, press Ctrl-C in the flood and the board log, then Ctrl-C in the capture terminal. The script cleans up after itself.

## Reading the result

- Every 10 seconds the board log shows whether it is connected, the signal strength, and the data frames the access point sent since the last line, split into broadcast and unicast. The counts come from promiscuous mode, so they count frames heard on channel 6 from this BSSID.
- Broadcast counts in the thousands per 10 seconds mean the access point sends the flood to the keyless station. The capture's per-minute table should show AP broadcasts too.
- If the board never connects, its log shows the disconnect reason on each try. Paste those lines. WEP in station mode is deprecated, so the reason may show the driver refusing WEP.
- While the board is not connected, the log also shows the router's last beacon signal and any replies or deauthentications the router has sent to the board. Zero replies means the router never answered. A signal near -85 dBm or weaker means the board hears the router only faintly.
- If it connects and stays but gets no broadcasts, the access point needs more than a keyless association. That is a result too.

Record the run in `TESTS.md` with the capture folder name.
