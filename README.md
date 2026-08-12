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

---

## ⚙️ Prerequisites

To run these labs, ensure you have the following tools installed:

* **Rust and Cargo**: Required for Lab 001.
* **Java Development Kit**: JDK 21 or newer for Lab 002.
* **C compiler and POSIX development tools**: `gcc`, `make`, and `pthread` support for Labs 003 and 004.
* **Linux or Debian environment**: Recommended for the C networking labs; Lab 003 targets Debian on BeagleBone Black.
* **Network utilities**: DNS tools such as `dig` or `nslookup` are useful for testing Lab 003.
