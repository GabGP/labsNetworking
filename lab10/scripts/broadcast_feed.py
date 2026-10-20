#!/usr/bin/env python3
# Sends UDP packets at a fixed rate, for the IV test on our own router.
# A broadcast address reaches every station on the LAN. A station's own IP sends to that station only.
# Usage: broadcast_feed.py <address> <packets per second> <seconds> [payload bytes, default 105]
# Keep the payload at 19 bytes or more. With 0 bytes the frames are 88 bytes, and aircrack-ng's default attack fails on them.
# Only one copy may run at a time. Two at once mix frame sizes in the same capture and share the air.
import argparse
import fcntl
import os
import socket
import sys
import tempfile
import time


class HelpOnErrorParser(argparse.ArgumentParser):
    # Show the full help, not only the usage line, when an argument is missing or wrong.
    def error(self, message):
        self.print_help(sys.stderr)
        self.exit(2, f"\n{self.prog}: error: {message}\n")


parser = HelpOnErrorParser(
    description="Sends UDP packets to port 9 at a fixed rate, for the IV test on our own router.\n"
    "Run one copy at a time: two at once mix frame sizes in the same capture and share the air.\n"
    "Ctrl+C stops it early and prints how many packets were sent.",
    epilog="""examples:
  python3 scripts/broadcast_feed.py 192.168.8.255 300 600   all stations on the LAN, 300 a second, 600 s
  python3 scripts/broadcast_feed.py <station IP> 2000 120   one station, 2000 a second, 120 s

Keep the payload at 19 bytes or more. With 0 bytes the frames are 88 bytes, and aircrack-ng's
default attack fails on them.""",
    formatter_class=argparse.RawDescriptionHelpFormatter,
)
parser.add_argument("address", help="a broadcast address such as 192.168.8.255 reaches every station "
                    "on the LAN; a station's own IP reaches only that station")
parser.add_argument("packets_per_second", type=float, help="packets sent each second; decimals allowed")
parser.add_argument("seconds", type=float, help="how long to keep sending, in seconds; decimals allowed")
parser.add_argument("payload_bytes", type=int, nargs="?", default=105,
                    help="zero bytes in each packet (default: %(default)s); see the note below")
args = parser.parse_args()
addr, pps, secs, size = args.address, args.packets_per_second, args.seconds, args.payload_bytes

lock = open(os.path.join(tempfile.gettempdir(), "broadcast_feed.lock"), "a")
try:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    sys.exit("another broadcast_feed.py is still running; stop it first")

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
payload = b"\0" * size

start = time.perf_counter()
sent = 0
interrupted = False
try:
    while True:
        now = time.perf_counter() - start
        if now >= secs:
            break
        if sent < now * pps:  # schedule from the start time, so the rate does not drift
            sock.sendto(payload, (addr, 9))
            sent += 1
        else:
            time.sleep(0.0005)
except KeyboardInterrupt:  # Ctrl+C: report what was sent so far instead of a traceback
    interrupted = True

elapsed = time.perf_counter() - start
if interrupted:
    print()  # the ^C leaves the cursor after it, so start the summary on a new line
print(f"sent {sent} packets to {addr} in {elapsed:.1f} s ({sent / elapsed:.0f} per second)"
      + (", stopped by Ctrl+C" if interrupted else ""))
sys.exit(130 if interrupted else 0)
