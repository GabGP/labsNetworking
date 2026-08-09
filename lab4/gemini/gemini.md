# CLAUDE.md — Lab 4: NTP Client (RFC 5905)

Guidance for Claude Code when working in this directory. This lab implements a
manual NTP client in C for the course *Ciencias de la Computación VIII*.
Source of truth: `Laboratorio_NTP_Client__1_.pdf` (in this folder).

## What must be built

A C program (no external NTP libraries) that:

1. Manually constructs a 48-byte NTP packet.
2. Sends it over UDP (port 123) to **at least 7 NTP servers simultaneously**.
3. Captures the four timestamps T1–T4 and runs the clock synchronization
   algorithm.
4. Computes **offset (θ)** and **delay (δ)**, picks the best server response
   (the one with the **smallest delay**), and adjusts a **simulated internal
   clock**.
5. Repeats the sync cycle until the offset is effectively zero (threshold in
   milliseconds, configurable), then keeps verifying on a configurable
   interval (default: 1 minute).
6. Prints a **clear, detailed console log** of every calculation in every
   iteration (a zero-grade risk if missing).

## Hard requirements (grading — any miss can mean a zero)

- Must be written in **C** (`.c` files) and **must compile** with a
  **Makefile** on a Unix/UNIX-like system (Solaris, Ubuntu, BeagleBone).
- Must produce a clear console LOG of the whole algorithm: T1–T4, the
  conversions, offset and delay per server per iteration, chosen server,
  clock adjustment, until synchronized.
- Configurable **before execution** (CLI arguments and/or config values):
  - Initial internal local date/time (Guatemala format, UTC-6) that will be
    synchronized against the servers.
  - Offset threshold in milliseconds to consider the clock synchronized.
  - Verification interval (default 60 seconds).
- Deliverable is a ZIP for GES: `.c` files, `Makefile`, and a 5–10 minute
  explanation video (video is the student's task, not Claude's).

## NTP packet format (48 bytes, big-endian / network byte order)

### First 12 bytes — common fields
| Field | Size | Notes |
|---|---|---|
| Leap Indicator (LI) | 2 bits | Use 0 or 3 (unsynchronized) |
| Version Number (VN) | 3 bits | 3 (`011`) or 4 |
| Mode | 3 bits | **3 = client request** |
| Stratum | 8 bits | 0 in request |
| Poll Interval | 8 bits | Power of two, seconds |
| Precision | 8 bits | Power of two (signed) |
| Root Delay | 32 bits | Fixed-point 16.16 |
| Root Dispersion | 32 bits | Fixed-point 16.16 |
| Reference ID | 32 bits | Server/clock identifier |

First byte of a v3 client request: `LI=0, VN=3, Mode=3` → `0b00011011` = `0x1B`.

### Timestamps (8 bytes each, 64-bit NTP format)
| Field | Meaning |
|---|---|
| Reference Timestamp | Server's last sync time |
| Originate Timestamp | **T1** — client send time |
| Receive Timestamp | **T2** — server receives request |
| Transmit Timestamp | **T3** — server sends reply |

**T4** is captured locally by the client immediately upon receiving the reply.
Reference: https://datatracker.ietf.org/doc/html/rfc5905

## NTP 64-bit timestamp format

- High 32 bits: seconds since **1900-01-01 00:00:00 UTC** (NTP epoch).
- Low 32 bits: fraction of a second, `frac32 = round(frac_seconds * 2^32)`
  (watch for carry into the seconds field when rounding).
- Unix→NTP epoch difference: **2,208,988,800 seconds**
  (`ntp_secs = unix_secs + 2208988800`).
- Guatemala is **UTC-6** with no DST: `utc = local + 6*60*60` seconds.

Worked example from the PDF (use as a unit-test vector):
local `Tue 2020-09-01 21:00:17.123` Guatemala → `2020-09-02 03:00:17.123 UTC`
→ Unix `1,599,015,617` → NTP seconds `3,808,004,417`,
fraction `round(0.123 × 4,294,967,296) = 528,280,977`.

## Synchronization algorithm

```
offset (θ) = ((T2 - T1) + (T3 - T4)) / 2
delay  (δ) = (T4 - T1) - (T3 - T2)
```

- T1: client sends request (fill Originate/Transmit before sending)
- T2: server receives request — from reply's Receive Timestamp
- T3: server sends reply — from reply's Transmit Timestamp
- T4: client receives reply — captured locally on arrival
- With multiple server responses, choose the offset from the response with
  the **smallest delay**.
- Apply the chosen offset to the simulated clock; iterate until
  `|offset| <= threshold_ms`, logging each iteration; then re-verify every
  interval.

PDF example result (test vector): T1=0xE33A16911F9843A4, T2=0xE33A18CBBE25E755,
T3=0xE33A18CC05A4D480, T4=0xE33A169200C3F6D6 → delay ≈ 601 ms,
offset ≈ 586 s + 319.5 ms (client behind, must move forward).

## NTP servers to use (UDP port 123, minimum 7 concurrently)

`time.google.com`, `time.apple.com`, `time.windows.com`,
`time.cloudflare.com`, `cern.ch` (ntp.cern.ch), `time.nist.gov`,
`time-a.nist.gov`, `time-b.nist.gov`, `pool.ntp.org` and regional pools
(`north-america.pool.ntp.org`, `south-america.pool.ntp.org`,
`europe.pool.ntp.org`, `asia.pool.ntp.org`, `oceania.pool.ntp.org`,
`africa.pool.ntp.org`).

## C coding conventions for this lab

- Target **POSIX** (Ubuntu/Solaris/BeagleBone): `sys/socket.h`,
  `netinet/in.h`, `netdb.h` (`getaddrinfo`), `arpa/inet.h`, `time.h`,
  `sys/time.h`. No Windows-only APIs (dev machine is Windows — build/test via
  WSL or a Linux VM; do not add winsock code).
- `-Wall -Wextra` clean; C99 or later (`-std=c99` with `-D_POSIX_C_SOURCE=200809L`
  or `-std=gnu99`).
- Use fixed-width types from `<stdint.h>` (`uint32_t`, `uint64_t`, `int64_t`).
- Convert multi-byte fields with `htonl`/`ntohl`; never assume host endianness.
- Do all timestamp math in 64-bit integers (or `double` only for display);
  handle the signed subtraction of NTP timestamps carefully (use `int64_t`
  on the 32.32 fixed-point values).
- UDP sockets need timeouts (`SO_RCVTIMEO` or `select`/`poll`) — servers may
  not respond; querying 7+ servers should not block on one dead server.
- Check every syscall return value; on DNS failure of one server, log it and
  continue with the rest.
- Keep modules small and separated: packet build/parse, time conversion,
  network I/O, sync algorithm/loop, logging, `main` + config parsing.
- Log to stdout with clear labels (Spanish is fine — the course is in
  Spanish): show hex timestamps, seconds/fraction breakdowns, offset and
  delay per server, chosen server, and clock adjustment per iteration.

## Build

`Makefile` targets expected: `all` (default build, `-Wall -Wextra -O2`),
`clean`. Binary name suggestion: `ntp_client`. Run example:

```
./ntp_client --fecha "2020-09-01 21:00:17.123" --umbral-ms 50 --intervalo 60
```

(Exact flag names may differ; keep them documented in the log/usage output.)
