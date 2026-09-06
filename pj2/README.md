# traceroute — raw-socket implementation in C

Proyecto #02, Ciencias de la Computación VIII.

This is a from-scratch clone of Linux `traceroute` in its default UDP mode.
Every piece of protocol logic lives in this repository: the IPv4 header and the
UDP header are assembled byte by byte, the TTL is incremented hop by hop, the
RFC 1071 checksum is our own function, and each ICMP reply is matched back to
its probe by reading the UDP header the router quotes inside the ICMP message.

The program never calls the system `traceroute`, `tracepath`, `mtr` or `ping`,
and it links against nothing but libc — no libnet, no libpcap, no packet
library of any kind.

## Build

```sh
make            # gcc -Wall -Wextra -O2, zero warnings -> ./traceroute
make clean
```

Verify that nothing but libc got linked in:

```sh
ldd ./traceroute
# linux-vdso.so.1
# libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
# /lib64/ld-linux-x86-64.so.2
```

(`linux-vdso.so.1` is not a library on disk; it is a page the kernel maps into
every process.)

## Why it needs sudo

Traceroute has to write the IP header itself, because the TTL of every probe is
the whole point of the technique, and it has to read raw ICMP error messages
that are not addressed to any socket. Both require raw sockets:

* `socket(AF_INET, SOCK_RAW, IPPROTO_RAW)` with `IP_HDRINCL` — we supply the
  complete IP header instead of letting the kernel build it.
* `socket(AF_INET, SOCK_RAW, IPPROTO_ICMP)` — we receive every ICMP packet that
  arrives at the host.

Linux only allows raw sockets to processes with `CAP_NET_RAW`, which in practice
means root. The program checks `geteuid()` at startup and exits with a clear
message instead of failing later with a confusing `socket: Operation not
permitted`.

```sh
sudo ./traceroute galileo.edu
```

## Usage

```
usage: traceroute [-n] [-f first_ttl] [-m max_ttl] [-q nqueries] [-w waittime_s] [-z sendwait_ms] host
```

| Flag | Meaning | Default | Accepted range |
|------|---------|---------|----------------|
| `-f first_ttl` | TTL of the first probe, i.e. the first hop shown | `1` | 1–255 |
| `-m max_ttl` | Maximum number of hops before giving up | `64` | 1–255 |
| `-q nqueries` | Probes sent per hop | `3` | 1–10 |
| `-w waittime_s` | Seconds to wait for the reply to one probe | `3` | 1–60 |
| `-z sendwait_ms` | Pause between consecutive probes, in milliseconds | `100` | 0–10000 |
| `-n` | Do not resolve addresses to hostnames | off | — |

Invalid or out-of-range values print the offending flag and the usage line, and
exit with status 1. `-f` greater than `-m` is rejected the same way.

## Output

```
traceroute to galileo.edu (216.239.32.21), 64 hops max, 60 byte packets
 1  192.168.1.1 (192.168.1.1)  12.224 ms  1.962 ms  1.005 ms
 2  10.135.128.1 (10.135.128.1)  23.494 ms 10.135.4.1 (10.135.4.1)  13.168 ms  12.846 ms
 5  * * 10.192.16.113 (10.192.16.113)  17.873 ms
11  * * *
12  any-in-2015.1e100.net (216.239.32.21)  79.741 ms  76.326 ms  77.815 ms
```

* The hop number is right-aligned in two columns.
* The first reply of a hop prints `hostname (ip)`, resolved with `getnameinfo`.
  When the address has no PTR record the hostname falls back to the IP itself,
  so the line reads `ip (ip)`.
* A later probe of the same hop only prints its RTT, unless it came back from a
  *different* address (ECMP load balancing) — then it prints its own
  `hostname (ip)` inline before the RTT, as in hop 2 above.
* A probe with no reply before the deadline prints `*`.
* With `-n` the address is printed once as a bare IP, which is exactly what
  Linux `traceroute -n` does, so the two logs line up 1:1.
* Reaching the destination prints the last hop and stops. ICMP type 3 code 3
  (port unreachable) is the normal ending; any other type 3 code is a delivery
  error and is annotated as ` !<code>` after the RTT.

## Comparing against the system traceroute

`tests/compare.sh` runs both programs with the same arguments, saves each output
under `tests/logs/`, and prints a side-by-side diff.

```sh
tests/compare.sh                       # default: 8.8.8.8 with -n -m 20 -w 2
tests/compare.sh galileo.edu           # another host, same default arguments
tests/compare.sh 8.8.8.8 -n -q 2 -m 12 # host first, then the shared arguments
```

It writes `tests/logs/own_<host>.log` and `tests/logs/system_<host>.log`. Both
runs use `sudo`. The default arguments are bounded on purpose: with no `-m` a
host that never answers would make both tools walk all the way to hop 64. The system `traceroute` is only used as a reference for the
comparison; it is never called by our program. If it is not installed, install
it with `sudo apt-get install traceroute`.

## Capturing evidence on the wire

To prove that the datagrams really are hand-built UDP probes and that the
replies really are ICMP errors, capture them while a run is in progress:

```sh
sudo tcpdump -ni any 'icmp or udp portrange 33434-33534'
```

You will see one outgoing UDP datagram per probe with an increasing destination
port starting at 33434, and the matching ICMP `time exceeded in-transit`
messages coming back from each router — followed by `port unreachable` from the
destination. Add `-vv -X` to tcpdump to read the TTL field and the quoted
headers byte by byte.

## Differences vs system traceroute

Two runs never match line for line, not even two consecutive runs of the system
tool against itself. The expected causes:

* **ECMP load balancing.** Routers spread flows over parallel links by hashing
  the packet's addresses and ports. Our destination port changes on every probe,
  so consecutive probes can legitimately take different paths and a single hop
  can answer from two or three different routers.
* **Routers that do not answer.** ICMP generation is optional and many routers
  either drop the request or are configured to stay silent, which shows as
  `* * *` for that hop. The trace continues past it, because the next hop may
  well answer.
* **ICMP rate limiting.** A router that answered the first probe may drop the
  second one, so the same hop shows one RTT and two `*`.
* **RTT variance.** Queueing, interface load and CPU scheduling make the same
  hop differ by milliseconds between probes and by more between runs. Only the
  order of magnitude is meaningful.
* **Timing model.** Linux traceroute sends the probes of a hop in parallel and
  collects the answers; this implementation sends one probe, waits for its
  reply, then sends the next. The hops and addresses agree, the exact RTT values
  do not have to.
* **DNS.** `getnameinfo` results depend on the resolver and on PTR records that
  change over time, so hostnames may differ between runs. Use `-n` on both sides
  for a clean comparison.

## Files

| Path | Contents |
|------|----------|
| `src/traceroute.c` | CLI parsing, address resolution, hop loop, output format |
| `src/packet.c` / `src/packet.h` | IP/UDP/ICMP structs, checksums, probe builder, ICMP reply parser |
| `Makefile` | `make`, `make clean` |
| `tests/compare.sh` | side-by-side comparison against the system traceroute |
| `docs/PACKET_WALKTHROUGH.md` | byte-level story of one probe and its reply |
