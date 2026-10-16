# Networking Labs

Welcome to the **Networking Labs** repository! This collection of assignments explores network communication from low-level TCP and UDP sockets to application-layer protocols such as HTTP, DNS, and NTP.

---

## 🔬 Lab Overview

| Lab | Title | Core Concepts | Target Environment |
| :--- | :--- | :--- | :--- |
| **Lab 001** | TCP and UDP Sockets | Client/server communication, TCP, UDP, expression evaluation, structured logging | Rust / Cargo |
| **Lab 002** | Java Web Server | HTTP requests and responses, static files, thread pools, server configuration | Java 21+ / Make |
| **Lab 003** | DNS Server over UDP | RFC 1035 messages, recursive forwarding, zones, caching, name compression, POSIX threads | C / Debian / BeagleBone Black |
| **Lab 004** | NTP Client | RFC 5905, UDP packets, timestamps T1–T4, offset and delay, clock synchronization | C / Linux / POSIX |
| **Lab 005** | TCP Client with Raw Sockets | Hand-built TCP segments, 3-way handshake, SEQ/ACK numbers, checksums, connection teardown, Wireshark verification | C / Linux (root) / Java 21+ (test server) |
| **Lab 006** | ICMP Analysis with Wireshark | RFC 792, Echo Request/Reply, Time Exceeded, host/port unreachable, TCP and UDP captures | Wireshark / ping / traceroute |
| **Lab 008** | Link-State Routing | Dijkstra's algorithm, shortest-path trees, routing tables, step-by-step `D(v)`, `p(v)`, `N'` output | Python 3 |

---

## 🛠️ Detailed Lab Descriptions

### Lab 001: TCP and UDP Sockets

Implements a client/server mathematical calculator in **Rust**. The client and server communicate using either TCP streams or UDP datagrams, evaluate basic expressions, handle errors such as division by zero, and print structured logs containing the communication direction, host, protocol, and timestamp.

### Lab 002: Java Web Server

Builds a web server in **Java** that parses incoming HTTP requests and produces HTTP responses for web resources. The lab separates request and response handling from the server runtime, supports configurable ports and delays, and uses a thread pool to handle multiple connections.

### Lab 003: DNS Server over UDP

Implements a DNS server in **C** for POSIX systems. The server parses and serializes RFC 1035 messages, supports local zones and multiple record types, forwards unresolved queries to upstream DNS servers, caches responses according to their TTL, and uses a bounded POSIX thread pool for concurrent requests. It also implements DNS name compression and structured logging.

### Lab 004: NTP Client

Implements an **NTP client in C** without external NTP libraries. The client builds and parses 48-byte UDP packets, queries multiple public time servers, records the four NTP timestamps (T1–T4), calculates network delay and clock offset, and applies corrections to a simulated clock configured for Guatemala time (UTC-6). The project includes unit tests for timestamp conversion and synchronization calculations.

### Lab 005: TCP Client with Raw Sockets

Extends a **C** TCP client that builds every segment by hand over raw sockets. The client performs the three-way handshake once and then keeps a single connection open for an interactive loop of user-supplied messages, closing cleanly when the server answers `EXIT` or the user sends end-of-input. The session is logged in detail (flags, SEQ/ACK numbers, checksums, payloads) and verified against a Wireshark capture. The provided `ServerTCP.java` is used unmodified. See [`lab05/README_Lab.md`](lab05/README_Lab.md) for the report and [`lab05/CONCEPTOS_RED.md`](lab05/CONCEPTOS_RED.md) for the supporting networking concepts.

### Lab 006: ICMP Analysis with Wireshark

Packet capture and analysis of the network layer (ICMP, RFC 792). The lab captures the TCP and UDP sessions from Lab 001, then generates ICMP traffic with `ping` (Echo Request/Reply), `traceroute`/`tracert` (Time Exceeded), and the UDP client pointed at a wrong port or IP (port and host unreachable). This folder currently contains only the assignment handout.

### Lab 008: Link-State Routing

Implements **Dijkstra's algorithm** in **Python** to simulate link-state routing from a chosen starting node. The program reads a topology file (`test/topo1.txt` … `topo7.txt`), prints the node-to-node cost table, and then shows every step of the algorithm (`D(v)`, `p(v)` per node and the growing set `N'`). Ties are resolved as specified in the handout: the next node is chosen alphabetically, and an equal-cost path updates `p(v)` to the newly added node. Run it with `python LinkState.py test/topo1.txt`. See [`Lab08/README.md`](Lab08/README.md).

---

## ⚙️ Prerequisites

To run these labs, ensure you have the following tools installed:

* **Rust and Cargo**: Required for Lab 001.
* **Java Development Kit**: JDK 21 or newer for Lab 002 (and for the Lab 005 test server).
* **C compiler and POSIX development tools**: `gcc`, `make`, and `pthread` support for Labs 003, 004 and 005.
* **Linux or Debian environment**: Recommended for the C networking labs; Lab 003 targets Debian on BeagleBone Black.
* **Network utilities**: DNS tools such as `dig` or `nslookup` are useful for testing Lab 003.
* **Root privileges**: Lab 005 opens raw sockets, which require `sudo` (or `CAP_NET_RAW`) on Linux.
* **Wireshark**: Required for Lab 006 and used to verify Lab 005.
* **Python 3**: Required for Lab 008.
