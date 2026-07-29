# System Prompt & Specification: C-Based DNS Server for BeagleBone Black (150% Extra Credit Target)

## ?“Œ Project Overview

The goal of this project is to build a fully functional, high-performance **DNS Server over UDP** in **C** running on **Debian Linux on a BeagleBone Black (BBB)**.

By implementing the entire solution in **C** (rather than Java) and deploying it on the **BeagleBone Black** connected via **Ethernet**, the project qualifies for the **150% bonus grade (+50% extra credit)**.

---

## ?Ž¯ Primary Goal & Requirements

### 1. Base Architecture & Language Requirements

* **Implementation Language:** C (`gcc`, POSIX standards).
* **Architecture Inspiration:** Base the application structure and thread/socket handling on the concurrency and architecture model from **Lab #1**, but implemented **strictly in pure C (NOT in Rust)**.
* **Concurrency:** Use a robust C thread pool (using `pthread`) based on previous lab architectures to efficiently manage incoming UDP requests across multiple worker threads.

### 2. Hardware & Network Target (BeagleBone Black)

* **Target OS:** Debian Linux running on BeagleBone Black (BBB).
* **Network Interface:** Ethernet interface listening on **`UDP port 53`** (requires `root` / `sudo` privileges).
* **Network Setup:** Must function seamlessly on a local isolated network via a router (with or without internet connection).

### 3. Core DNS Protocol Capabilities (RFC 1035 & RFC 5395)

* **UDP Socket Engine:** Listen on `0.0.0.0:53` using non-blocking or multi-threaded `DatagramSocket`-equivalent POSIX UDP sockets (`SOCK_DGRAM`).
* **DNS Header Handling:**
  * Parse 12-byte DNS headers (`Transaction ID`, `Flags`, `QDCOUNT`, `ANCOUNT`, `NSCOUNT`, `ARCOUNT`).
  * Construct valid DNS response headers setting proper flags (Query/Response `QR`, Opcode, `AA`, `RD`, `RA`, `RCODE`).
* **Question Section Parsing:**
  * Parse `QNAME` (variable length domain names with length-octet encoding), `QTYPE` (2 bytes), and `QCLASS` (2 bytes).
* **DNS Record Types Support (Minimum 3 complete implementations):**
  * **Type A (0x0001):** IPv4 address resolution.
  * **Type AAAA (0x001C):** IPv6 address resolution.
  * **Type PTR (0x000C):** Reverse DNS lookup.
  * *(Optional extensions: `SOA`, `CNAME`, `TXT`)*.
* **DNS Name Compression:**
  * Support pointer-based domain name compression (`0xC0` byte prefix with offset) when generating DNS response packages as outlined in `Laboratorio DNS.xlsx`.
* **Recursion & Forwarding:**
  * Implement query forwarding to upstream DNS servers (e.g., Google `8.8.8.8` or Cloudflare `1.1.1.1`) for external domain resolution when not found in local cache/records.

### 4. Code Structure & Quality Standards

* **Modular Code Structure:**
  * `main.c` / `server.c`: Socket initialization and event/threadpool loop.
  * `dns_parser.c`: DNS packet decoding, header/question/record binary serializing and deserializing.
  * `threadpool.c`: C thread pool manager using `pthread_mutex` and `pthread_cond`.
  * `dns_records.c`: Record lookup database and recursive upstream forwarder.
* **Structured Logging:**
  * Clean, formatted log outputs displaying incoming timestamp, client IP:Port, queried domain name, QTYPE, response RCODE, and execution time.
* **Error Resilience:**
  * Gracefully handle malformed DNS queries, buffer boundaries, and socket timeouts without server crashes or memory leaks.

---

## ? Build & Deployment Instructions

### `Makefile` Specification

Provide a clean `Makefile` with the following targets:

* `make`: Compiles the C binary (`dns_server`) using `gcc` with standard flags (`-Wall -Wextra -O2 -pthread`).
* `make run`: Compiles and executes the server with `sudo ./dns_server`.
* `make clean`: Removes binary artifacts and object files.

### BeagleBone Black (Debian) Deployment Checklist

1. Connect BeagleBone Black to PC/Router via Ethernet cable and power up.
2. SSH into BeagleBone: `ssh debian@beaglebone.local` (or via assigned LAN IP).
3. Transfer codebase to BeagleBone Black (`scp` or `git clone`).
4. Compile natively on BBB: `make`.
5. Run server with elevated privileges: `sudo ./dns_server`.
6. Verify from host PC using `nslookup`, `dig`, or `Wireshark`:
   
   ```bash
   dig @<BEAGLEBONE_IP> google.com A
   ```

---

## ? Deliverables & Verification

1. Full C source code (`.c` and `.h` files).
2. Clean `Makefile`.
3. Updated `README.md` with BBB setup and execution steps.
4. Empirical verification using `dig` or `nslookup` targetting the BeagleBone Black IP address.
