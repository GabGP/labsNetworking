#!/usr/bin/env python3
# Sends Ethernet frames addressed to one station's MAC address, for the IV test on our own router.
# The router floods a frame for a MAC it has not learned to its WLAN side, and the access point encrypts it to
# that station with a new IV. The station needs no IP address and no ARP reply, unlike a unicast UDP flood to its IP.
# Each frame carries the UDP packet that broadcast_feed.py sends, to port 9, with zero bytes.
# Usage (as root): sudo python3 scripts/l2_feed.py <interface> <station MAC> <packets per second> <seconds> [payload bytes]
# Keep the payload at 19 bytes or more, as in broadcast_feed.py.
# Only one copy may run at a time. It uses the same lock as broadcast_feed.py.
import argparse
import fcntl
import os
import socket
import struct
import sys
import tempfile
import time

PROGRESS_SECONDS = 5  # a progress line this often while sending


def checksum(data):
    """The Internet checksum (RFC 1071) of the bytes."""
    if len(data) % 2:
        data += b"\0"
    total = sum(struct.unpack(f"!{len(data) // 2}H", data))
    while total >> 16:
        total = (total & 0xFFFF) + (total >> 16)
    return ~total & 0xFFFF


def build_frame(station_mac, own_mac, size):
    """An Ethernet frame to station_mac, carrying an IPv4 packet with a UDP packet to port 9 and size zero bytes."""
    udp_len = 8 + size
    ip = struct.pack("!BBHHHBBH4s4s", 0x45, 0, 20 + udp_len, 0, 0x4000, 64, 17, 0,
                     bytes([192, 168, 8, 2]), bytes([192, 168, 8, 255]))
    ip = ip[:10] + struct.pack("!H", checksum(ip)) + ip[12:]
    udp = struct.pack("!HHHH", 9, 9, udp_len, 0) + b"\0" * size
    return station_mac + own_mac + b"\x08\x00" + ip + udp


def open_lock(path):
    # Under sudo, /tmp refuses to create or open a file that another user owns, even for root
    # (fs.protected_regular). So an existing lock is opened without creating it. A new lock goes to the
    # user who ran sudo, so a normal run of broadcast_feed.py can still open it.
    try:
        return open(path, "r+")
    except FileNotFoundError:
        lock = open(path, "w")
        if os.geteuid() == 0 and "SUDO_UID" in os.environ:
            os.chown(path, int(os.environ["SUDO_UID"]), int(os.environ["SUDO_GID"]))
        return lock


class HelpOnErrorParser(argparse.ArgumentParser):
    # Show the full help, not only the usage line, when an argument is missing or wrong.
    def error(self, message):
        self.print_help(sys.stderr)
        self.exit(2, f"\n{self.prog}: error: {message}\n")


def main():
    parser = HelpOnErrorParser(
        description="Sends Ethernet frames to one station's MAC address at a fixed rate, for the IV test on our own router.\n"
        "The router floods them to its WLAN side, and the access point encrypts them to that station.\n"
        "Run one copy at a time. Prints a progress line every 5 s. Ctrl+C stops it early and prints how many frames were sent.",
        epilog="""examples:
  sudo python3 scripts/l2_feed.py enp1s0 7c:4f:ad:79:9d:d8 2000 30   the station's MAC, 2000 a second, 30 s

Keep the payload at 19 bytes or more.""",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("interface", help="the wired interface on the router's LAN port, such as enp1s0")
    parser.add_argument("station", help="the station's MAC address, such as 7c:4f:ad:79:9d:d8")
    parser.add_argument("packets_per_second", type=float, help="frames sent each second; decimals allowed")
    parser.add_argument("seconds", type=float, help="how long to keep sending, in seconds; decimals allowed")
    parser.add_argument("payload_bytes", type=int, nargs="?", default=105,
                        help="zero bytes in each frame (default: %(default)s)")
    args = parser.parse_args()
    if args.payload_bytes < 19:
        parser.error("keep the payload at 19 bytes or more")
    station_mac = bytes.fromhex(args.station.replace(":", ""))
    if len(station_mac) != 6:
        parser.error("a MAC address has six bytes, such as 7c:4f:ad:79:9d:d8")
    try:
        with open(f"/sys/class/net/{args.interface}/address") as f:
            own_mac = bytes.fromhex(f.read().strip().replace(":", ""))
    except OSError:
        parser.error(f"no interface named {args.interface}")

    lock = open_lock(os.path.join(tempfile.gettempdir(), "broadcast_feed.lock"))
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        sys.exit("another broadcast_feed.py or l2_feed.py is still running; stop it first")

    sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
    sock.bind((args.interface, 0))
    frame = build_frame(station_mac, own_mac, args.payload_bytes)
    pps, secs = args.packets_per_second, args.seconds

    start = time.perf_counter()
    sent = 0
    interrupted = False
    next_report = PROGRESS_SECONDS
    print(f"sending {pps:g} frames a second to {args.station} on {args.interface} for {secs:g} s "
          "(Ctrl+C stops early)", file=sys.stderr, flush=True)
    try:
        while True:
            now = time.perf_counter() - start
            if now >= secs:
                break
            if now >= next_report:
                print(f"{now:6.1f} s  sent {sent} of {round(pps * secs)} frames ({sent / now:.0f} per second)",
                      file=sys.stderr, flush=True)
                next_report = (int(now // PROGRESS_SECONDS) + 1) * PROGRESS_SECONDS
            if sent < now * pps:  # schedule from the start time, so the rate does not drift
                sock.send(frame)
                sent += 1
            else:
                time.sleep(0.0005)
    except KeyboardInterrupt:  # Ctrl+C: report what was sent so far instead of a traceback
        interrupted = True

    elapsed = time.perf_counter() - start
    if interrupted:
        print()  # the ^C leaves the cursor after it, so start the summary on a new line
    print(f"sent {sent} frames to {args.station} on {args.interface} in {elapsed:.1f} s "
          f"({sent / elapsed:.0f} per second)" + (", stopped by Ctrl+C" if interrupted else ""))
    sys.exit(130 if interrupted else 0)


if __name__ == "__main__":
    main()
