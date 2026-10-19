#!/usr/bin/env python3
# Sends UDP packets at a fixed rate, for the IV test on our own router.
# A broadcast address reaches every station on the LAN. A station's own IP sends to that station only.
# Usage: broadcast_feed.py <address> <packets per second> <seconds> [payload bytes, default 105]
# Keep the payload at 19 bytes or more. With 0 bytes the frames are 88 bytes, and aircrack-ng's default attack fails on them.
# Only one copy may run at a time. Two at once mix frame sizes in the same capture and share the air.
import fcntl
import os
import socket
import sys
import tempfile
import time

addr, pps, secs = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
size = int(sys.argv[4]) if len(sys.argv) > 4 else 105

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
while True:
    now = time.perf_counter() - start
    if now >= secs:
        break
    if sent < now * pps:  # schedule from the start time, so the rate does not drift
        sock.sendto(payload, (addr, 9))
        sent += 1
    else:
        time.sleep(0.0005)

elapsed = time.perf_counter() - start
print(f"sent {sent} packets to {addr} in {elapsed:.1f} s ({sent / elapsed:.0f} per second)")
