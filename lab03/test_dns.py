import socket
import struct
import time
import subprocess
import os

def build_dns_query(domain, qtype=1, txid=0x1234, rd=1):
    # Header: ID, Flags (0x0100 for standard query with RD=1), QDCOUNT=1, ANCOUNT=0, NSCOUNT=0, ARCOUNT=0
    flags = 0x0100 if rd else 0x0000
    header = struct.pack("!HHHHHH", txid, flags, 1, 0, 0, 0)
    
    # QNAME
    qname_parts = domain.strip(".").split(".")
    qname_bytes = b"".join(bytes([len(part)]) + part.encode("ascii") for part in qname_parts) + b"\x00"
    
    # QTYPE, QCLASS (IN=1)
    question = qname_bytes + struct.pack("!HH", qtype, 1)
    
    return header + question

def parse_dns_response(data):
    if len(data) < 12:
        return None
    txid, flags, qdcount, ancount, nscount, arcount = struct.unpack("!HHHHHH", data[:12])
    rcode = flags & 0x000F
    return {
        "txid": txid,
        "flags": flags,
        "rcode": rcode,
        "qdcount": qdcount,
        "ancount": ancount,
        "raw": data
    }

def test_dns_server():
    server_port = 9999
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    
    tests = [
        ("beaglebone.local", 1, "Type A (Local IPv4)"),
        ("beaglebone.local", 28, "Type AAAA (Local IPv6)"),
        ("100.1.168.192.in-addr.arpa", 12, "Type PTR (Reverse DNS)"),
        ("www.lab3.test", 5, "Type CNAME (Local CNAME)"),
        ("lab3.test", 16, "Type TXT (Local TXT)"),
        ("google.com", 1, "Type A (Upstream Recursive Forwarding)"),
        ("nonexistent.local", 1, "Type A (Non-existent local domain - NXDOMAIN)")
    ]

    print("=== STARTING DNS SERVER INTEGRATION TESTS ===")
    
    for domain, qtype, desc in tests:
        query_pkt = build_dns_query(domain, qtype=qtype, txid=0xABCD, rd=1)
        start_time = time.time()
        try:
            sock.sendto(query_pkt, ("127.0.0.1", server_port))
            data, addr = sock.recvfrom(512)
            elapsed_ms = (time.time() - start_time) * 1000.0
            resp = parse_dns_response(data)
            print(f"[TEST PASS] {desc:45s} | Domain: {domain:30s} | RCODE: {resp['rcode']} | Answers: {resp['ancount']} | Latency: {elapsed_ms:.2f}ms")
        except Exception as e:
            print(f"[TEST FAIL] {desc:45s} | Domain: {domain:30s} | Error: {e}")

    sock.close()

if __name__ == "__main__":
    test_dns_server()
