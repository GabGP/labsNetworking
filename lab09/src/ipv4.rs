//! Minimal reader of the IPv4 packets delivered by the raw socket (RFC 791).
//!
//! On Linux a raw `IPPROTO_TCP` socket returns every packet *with* its IPv4
//! header, so we need to skip it (its length is IHL * 4 bytes) to reach TCP.

use std::net::Ipv4Addr;

/// Length of an IPv4 header without options.
pub const IPV4_MIN_HEADER_LEN: usize = 20;

#[derive(Debug, PartialEq, Eq)]
pub struct Ipv4Packet<'a> {
    pub src: Ipv4Addr,
    pub dst: Ipv4Addr,
    pub protocol: u8,
    pub ttl: u8,
    /// Contents after the IPv4 header (here: the TCP segment).
    pub payload: &'a [u8],
}

pub fn parse(buf: &[u8]) -> Option<Ipv4Packet<'_>> {
    if buf.len() < IPV4_MIN_HEADER_LEN {
        return None;
    }
    let version = buf[0] >> 4;
    let header_len = (buf[0] & 0x0f) as usize * 4; // IHL is counted in 32-bit words
    if version != 4 || header_len < IPV4_MIN_HEADER_LEN || buf.len() < header_len {
        return None;
    }
    // Fragments other than a complete packet do not start with a TCP header.
    let fragment = u16::from_be_bytes([buf[6], buf[7]]);
    if fragment & 0x3fff != 0 {
        return None;
    }
    // Trust Total Length only when it is consistent with what was received.
    let total_len = u16::from_be_bytes([buf[2], buf[3]]) as usize;
    let end = if (header_len..=buf.len()).contains(&total_len) {
        total_len
    } else {
        buf.len()
    };
    Some(Ipv4Packet {
        src: Ipv4Addr::new(buf[12], buf[13], buf[14], buf[15]),
        dst: Ipv4Addr::new(buf[16], buf[17], buf[18], buf[19]),
        protocol: buf[9],
        ttl: buf[8],
        payload: &buf[header_len..end],
    })
}

/// Builds an IPv4 packet like the ones the kernel hands to the raw socket (used by tests).
#[cfg(test)]
pub(crate) fn build_packet(src: Ipv4Addr, dst: Ipv4Addr, payload: &[u8]) -> Vec<u8> {
    let total_len = (IPV4_MIN_HEADER_LEN + payload.len()) as u16;
    let mut packet = vec![0u8; IPV4_MIN_HEADER_LEN];
    packet[0] = 0x45; // version 4, IHL 5
    packet[2..4].copy_from_slice(&total_len.to_be_bytes());
    packet[6] = 0x40; // Don't Fragment
    packet[8] = 64; // TTL
    packet[9] = crate::tcp::IPPROTO_TCP;
    packet[12..16].copy_from_slice(&src.octets());
    packet[16..20].copy_from_slice(&dst.octets());
    let checksum = crate::tcp::internet_checksum(&packet);
    packet[10..12].copy_from_slice(&checksum.to_be_bytes());
    packet.extend_from_slice(payload);
    packet
}

#[cfg(test)]
mod tests {
    use super::*;

    const A: Ipv4Addr = Ipv4Addr::new(192, 168, 1, 5);
    const B: Ipv4Addr = Ipv4Addr::new(192, 168, 1, 2);

    #[test]
    fn parses_header_fields() {
        let packet = build_packet(A, B, b"tcp-segment");
        let parsed = parse(&packet).unwrap();
        assert_eq!(parsed.src, A);
        assert_eq!(parsed.dst, B);
        assert_eq!(parsed.protocol, 6);
        assert_eq!(parsed.ttl, 64);
        assert_eq!(parsed.payload, b"tcp-segment");
        // A correct IPv4 header checksum verifies to zero.
        assert_eq!(
            crate::tcp::internet_checksum(&packet[..IPV4_MIN_HEADER_LEN]),
            0
        );
    }

    #[test]
    fn skips_ip_options() {
        let mut packet = build_packet(A, B, b"data");
        packet[0] = 0x46; // IHL 6: 4 bytes of options
        packet.splice(20..20, [1, 1, 1, 0]);
        let total = packet.len() as u16;
        packet[2..4].copy_from_slice(&total.to_be_bytes());
        assert_eq!(parse(&packet).unwrap().payload, b"data");
    }

    #[test]
    fn ignores_trailing_bytes_after_total_length() {
        let mut packet = build_packet(A, B, b"data");
        packet.extend_from_slice(&[0xee; 6]); // Ethernet padding, for example
        assert_eq!(parse(&packet).unwrap().payload, b"data");
    }

    #[test]
    fn rejects_invalid_packets() {
        assert_eq!(parse(&[0x45; 10]), None);
        let mut v6 = build_packet(A, B, b"data");
        v6[0] = 0x65;
        assert_eq!(parse(&v6), None);
        let mut fragment = build_packet(A, B, b"data");
        fragment[6] = 0x20; // More Fragments
        assert_eq!(parse(&fragment), None);
    }
}
