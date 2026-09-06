# One probe, byte by byte

Assume `sudo ./traceroute 8.8.8.8`, and that we are on the second probe of hop 5.
The source address is the one the kernel would pick for that destination, found
by `connect()`ing a throwaway UDP socket to the target and reading
`getsockname()`. The source port is fixed for the whole run,
`(getpid() & 0x7FFF) | 0x8000`; the destination port is `33434 + probe number`,
so probe number 13 uses 33447.

## What we send: 60 bytes, built by hand (`build_probe_packet`)

**IPv4 header, 20 bytes (RFC 791).** Version 4 and IHL 5 packed into one byte
(`0x45`); TOS 0; total length `htons(60)`; a per-probe id; flags and fragment
offset 0; **TTL = 5**, the field the whole technique rests on; protocol 17
(UDP); checksum computed over these 20 bytes with our RFC 1071 function
(`calculate_checksum`) after zeroing the field; then source and destination
addresses in network order.

**UDP header, 8 bytes (RFC 768).** Source port (fixed), destination port 33447,
length `htons(40)` = 8 header + 32 payload, and the checksum. The UDP checksum
covers a 12-byte pseudo-header — source IP, destination IP, a zero byte,
protocol 17, UDP length — followed by the whole UDP segment
(`calculate_udp_checksum`).

**Payload, 32 bytes of zeros.** Nothing but padding to reach the 60-byte size
the reference tool uses.

The datagram goes out on a `SOCK_RAW`/`IPPROTO_RAW` socket with `IP_HDRINCL`
set, so the kernel forwards our bytes as written instead of building a header of
its own. `clock_gettime(CLOCK_MONOTONIC)` is read immediately before `sendto`.

## What router 5 does

Every router decrements the TTL before forwarding. The first four routers hand
the packet on with TTL 4, 3, 2, 1. Router number 5 decrements it to **0**, which
by RFC 791 makes the datagram undeliverable: it must be discarded, and RFC 792
says the router should report it with an ICMP **Time Exceeded** message sent
back to the source address. That is why increasing the TTL one step at a time
walks the path — each TTL value forces a different router to identify itself.

## What comes back: ICMP Time Exceeded (`parse_icmp_reply`)

The packet arrives on the `SOCK_RAW`/`IPPROTO_ICMP` socket and looks like this:

```
| IP header of the router  | ICMP header | quoted IP header | quoted UDP header |
| 20 bytes (IHL*4)         |   8 bytes   | 20 bytes (IHL*4) |     8 bytes       |
| src = the router         | type 11     | our original     | our ports         |
|                          | code 0      | header, TTL 0    | 33447 <- this one |
```

The outer IP header is skipped using its own IHL field, never a hardcoded 20.
Type 11 / code 0 says "time exceeded in transit". RFC 792 requires the router to
quote the IP header of the offending datagram **plus its first 8 bytes** — and 8
bytes is exactly a full UDP header, which is why traceroute can use ports as
probe identifiers at all.

**Matching.** A reply is accepted only if the quoted inner IP header says
protocol 17 (UDP), the quoted source port equals our fixed source port, and the
quoted destination port equals the port of the probe currently outstanding.
Anything else — an echo reply from a `ping` running in parallel, an ICMP error
belonging to another process, or the late answer to a probe we already gave up
on — fails one of those three tests and is discarded, and the program keeps
waiting until its deadline expires.

**RTT.** A second `clock_gettime(CLOCK_MONOTONIC)` is taken right after
`recvfrom` returns, and the difference against the send timestamp is printed as
`%.3f ms`. `CLOCK_MONOTONIC` is used instead of the wall clock so an NTP
adjustment mid-run cannot produce a negative or inflated RTT. If `select` hits
the deadline first, the probe prints `*`.

## The last hop is different

When the TTL is finally large enough for the datagram to reach 8.8.8.8 itself,
no router discards it. The destination receives a UDP datagram on port 33447,
where nothing is listening — traceroute picks ports in the 33434+ range for
exactly that reason. The host answers with ICMP **type 3, code 3**
(Destination Unreachable / Port Unreachable), which quotes the same inner
headers, so it is matched to its probe in exactly the same way.

Type 3 is the signal that the destination has been reached: the program prints
that hop, finishes its remaining probes, and stops instead of walking to
`max_ttl`. A type 3 with any other code is a delivery failure rather than a
normal ending, so it is printed with ` !<code>` appended after the RTT.
