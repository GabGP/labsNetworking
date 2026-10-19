//! Manual construction of the 20-byte TCP header (RFC 9293, formerly RFC 793)
//! and of its 16-bit checksum, computed over the IPv4 pseudo-header.
//!
//! ```text
//!  0                   1                   2                   3
//!  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//! +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//! |          Source Port          |       Destination Port        |
//! +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//! |                        Sequence Number                        |
//! +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//! |                    Acknowledgment Number                      |
//! +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//! |  Data |       |C|E|U|A|P|R|S|F|                               |
//! | Offset| Rsrvd |W|C|R|C|S|S|Y|I|            Window             |
//! |       |       |R|E|G|K|H|T|N|N|                               |
//! +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//! |           Checksum            |         Urgent Pointer        |
//! +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//! ```

use std::net::Ipv4Addr;

/// Length of a TCP header without options (data offset = 5 words of 32 bits).
pub const TCP_HEADER_LEN: usize = 20;

/// IP protocol number of TCP, used in the IPv4 header and in the pseudo-header.
pub const IPPROTO_TCP: u8 = 6;

// Control flags (byte 13 of the header).
pub const FIN: u8 = 0x01;
pub const SYN: u8 = 0x02;
pub const RST: u8 = 0x04;
pub const PSH: u8 = 0x08;
pub const ACK: u8 = 0x10;
pub const URG: u8 = 0x20;
pub const ECE: u8 = 0x40;
pub const CWR: u8 = 0x80;

/// Flag names in the order Wireshark prints them, e.g. `[SYN, ACK]`.
const FLAG_NAMES: [(u8, &str); 8] = [
    (FIN, "FIN"),
    (SYN, "SYN"),
    (RST, "RST"),
    (PSH, "PSH"),
    (ACK, "ACK"),
    (URG, "URG"),
    (ECE, "ECE"),
    (CWR, "CWR"),
];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TcpHeader {
    pub src_port: u16,
    pub dst_port: u16,
    /// Sequence number of the first data byte (or of the SYN/FIN).
    pub seq: u32,
    /// Next sequence number expected from the other side (valid when ACK is set).
    pub ack: u32,
    /// Header length in 32-bit words: 5 means 20 bytes, no options.
    pub data_offset: u8,
    pub flags: u8,
    /// Number of bytes the sender of this segment is willing to receive.
    pub window: u16,
    pub checksum: u16,
    pub urgent_ptr: u16,
}

impl TcpHeader {
    /// Creates a 20-byte header (no options). The checksum is filled in by `build_segment`.
    pub fn new(src_port: u16, dst_port: u16, seq: u32, ack: u32, flags: u8, window: u16) -> Self {
        TcpHeader {
            src_port,
            dst_port,
            seq,
            ack,
            data_offset: 5,
            flags,
            window,
            checksum: 0,
            urgent_ptr: 0,
        }
    }

    /// Serializes the header in network byte order (big endian).
    pub fn to_bytes(&self) -> [u8; TCP_HEADER_LEN] {
        let mut bytes = [0u8; TCP_HEADER_LEN];
        bytes[0..2].copy_from_slice(&self.src_port.to_be_bytes());
        bytes[2..4].copy_from_slice(&self.dst_port.to_be_bytes());
        bytes[4..8].copy_from_slice(&self.seq.to_be_bytes());
        bytes[8..12].copy_from_slice(&self.ack.to_be_bytes());
        bytes[12] = self.data_offset << 4; // high nibble: data offset, low nibble: reserved
        bytes[13] = self.flags;
        bytes[14..16].copy_from_slice(&self.window.to_be_bytes());
        bytes[16..18].copy_from_slice(&self.checksum.to_be_bytes());
        bytes[18..20].copy_from_slice(&self.urgent_ptr.to_be_bytes());
        bytes
    }

    /// Reads a header from the beginning of a TCP segment.
    pub fn parse(segment: &[u8]) -> Option<TcpHeader> {
        if segment.len() < TCP_HEADER_LEN {
            return None;
        }
        let data_offset = segment[12] >> 4;
        if data_offset < 5 || segment.len() < data_offset as usize * 4 {
            return None;
        }
        Some(TcpHeader {
            src_port: u16::from_be_bytes([segment[0], segment[1]]),
            dst_port: u16::from_be_bytes([segment[2], segment[3]]),
            seq: u32::from_be_bytes([segment[4], segment[5], segment[6], segment[7]]),
            ack: u32::from_be_bytes([segment[8], segment[9], segment[10], segment[11]]),
            data_offset,
            flags: segment[13],
            window: u16::from_be_bytes([segment[14], segment[15]]),
            checksum: u16::from_be_bytes([segment[16], segment[17]]),
            urgent_ptr: u16::from_be_bytes([segment[18], segment[19]]),
        })
    }

    /// Header length in bytes (including options).
    pub fn header_len(&self) -> usize {
        self.data_offset as usize * 4
    }
}

/// Builds a complete segment (20-byte header + data) with a valid checksum,
/// ready to be written to the raw socket.
pub fn build_segment(
    src_ip: Ipv4Addr,
    dst_ip: Ipv4Addr,
    header: &TcpHeader,
    payload: &[u8],
) -> Vec<u8> {
    let mut header = *header;
    header.data_offset = 5;
    header.checksum = 0; // the checksum field counts as zero while it is being computed

    let mut segment = Vec::with_capacity(TCP_HEADER_LEN + payload.len());
    segment.extend_from_slice(&header.to_bytes());
    segment.extend_from_slice(payload);

    let checksum = tcp_checksum(src_ip, dst_ip, &segment);
    segment[16..18].copy_from_slice(&checksum.to_be_bytes());
    segment
}

/// Splits a received segment into header, options and data.
pub fn split_segment(segment: &[u8]) -> Option<(TcpHeader, &[u8], &[u8])> {
    let header = TcpHeader::parse(segment)?;
    let options = &segment[TCP_HEADER_LEN..header.header_len()];
    let payload = &segment[header.header_len()..];
    Some((header, options, payload))
}

/// TCP checksum (RFC 9293 section 3.1): one's complement of the one's complement
/// sum of every 16-bit word of the pseudo-header followed by the segment.
///
/// ```text
/// IPv4 pseudo-header (12 bytes, never transmitted):
/// +--------+--------+--------+--------+
/// |           Source Address          |
/// +--------+--------+--------+--------+
/// |         Destination Address       |
/// +--------+--------+--------+--------+
/// |  zero  |  PTCL  |    TCP Length   |
/// +--------+--------+--------+--------+
/// ```
///
/// Applied to a segment whose checksum field is already filled in, the result
/// is 0 when the checksum is correct (this is how the receiver verifies it).
pub fn tcp_checksum(src_ip: Ipv4Addr, dst_ip: Ipv4Addr, segment: &[u8]) -> u16 {
    let mut pseudo = [0u8; 12];
    pseudo[0..4].copy_from_slice(&src_ip.octets());
    pseudo[4..8].copy_from_slice(&dst_ip.octets());
    pseudo[8] = 0;
    pseudo[9] = IPPROTO_TCP;
    pseudo[10..12].copy_from_slice(&(segment.len() as u16).to_be_bytes());

    // The pseudo-header has an even length, so both parts can be summed separately.
    !fold(sum_words(&pseudo) + sum_words(segment))
}

/// Internet checksum (RFC 1071) of a buffer, the same algorithm as the IPv4 header checksum.
pub fn internet_checksum(data: &[u8]) -> u16 {
    !fold(sum_words(data))
}

/// Adds the buffer as big endian 16-bit words; an odd final byte is padded with a zero.
/// A u32 accumulator cannot overflow for buffers of up to 128 KB.
fn sum_words(data: &[u8]) -> u32 {
    let (words, rest) = data.as_chunks::<2>();
    let mut sum: u32 = words
        .iter()
        .map(|&word| u16::from_be_bytes(word) as u32)
        .sum();
    if let [last] = rest {
        sum += u16::from_be_bytes([*last, 0]) as u32;
    }
    sum
}

/// Folds the carries of the high 16 bits back into the low 16 bits (end-around carry).
fn fold(mut sum: u32) -> u16 {
    while sum > 0xffff {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    sum as u16
}

/// Flag names as Wireshark shows them, e.g. `SYN, ACK`.
pub fn flags_to_string(flags: u8) -> String {
    let names: Vec<&str> = FLAG_NAMES
        .iter()
        .filter(|(bit, _)| flags & bit != 0)
        .map(|(_, name)| *name)
        .collect();
    if names.is_empty() {
        "<ninguna>".to_string()
    } else {
        names.join(", ")
    }
}

/// Looks for the MSS option (kind 2, length 4) in the options area of a header.
pub fn parse_mss(options: &[u8]) -> Option<u16> {
    let mut i = 0;
    while i < options.len() {
        match options[i] {
            0 => return None, // End of Option List
            1 => i += 1,      // No-Operation (padding)
            kind => {
                let len = *options.get(i + 1)? as usize;
                if len < 2 || i + len > options.len() {
                    return None;
                }
                if kind == 2 && len == 4 {
                    return Some(u16::from_be_bytes([options[i + 2], options[i + 3]]));
                }
                i += len;
            }
        }
    }
    None
}

// Sequence numbers live in a 32-bit circular space (they wrap around after
// 2^32 - 1), so they are compared with modular arithmetic (RFC 9293 section 3.4).

/// `a < b` in sequence space.
pub fn seq_lt(a: u32, b: u32) -> bool {
    (a.wrapping_sub(b) as i32) < 0
}

/// `a <= b` in sequence space.
pub fn seq_le(a: u32, b: u32) -> bool {
    (a.wrapping_sub(b) as i32) <= 0
}

/// `a > b` in sequence space.
pub fn seq_gt(a: u32, b: u32) -> bool {
    seq_lt(b, a)
}

/// `a >= b` in sequence space.
pub fn seq_ge(a: u32, b: u32) -> bool {
    seq_le(b, a)
}

#[cfg(test)]
mod tests {
    use super::*;

    const CLIENT_IP: Ipv4Addr = Ipv4Addr::new(10, 0, 0, 1);
    const SERVER_IP: Ipv4Addr = Ipv4Addr::new(10, 0, 0, 2);

    #[test]
    fn rfc1071_example() {
        // RFC 1071 section 3: the words 0001 f203 f4f5 f6f7 add up to ddf2.
        let data = [0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7];
        assert_eq!(fold(sum_words(&data)), 0xddf2);
        assert_eq!(internet_checksum(&data), !0xddf2);
    }

    #[test]
    fn odd_length_is_padded_with_zero() {
        assert_eq!(sum_words(&[0x12, 0x34, 0x56]), 0x1234 + 0x5600);
    }

    #[test]
    fn header_layout_in_network_byte_order() {
        let header = TcpHeader::new(30000, 8080, 0x0102_0304, 0x0a0b_0c0d, SYN | ACK, 64240);
        let bytes = header.to_bytes();
        assert_eq!(&bytes[0..2], &[0x75, 0x30]); // 30000
        assert_eq!(&bytes[2..4], &[0x1f, 0x90]); // 8080
        assert_eq!(&bytes[4..8], &[0x01, 0x02, 0x03, 0x04]);
        assert_eq!(&bytes[8..12], &[0x0a, 0x0b, 0x0c, 0x0d]);
        assert_eq!(bytes[12], 0x50); // data offset 5, i.e. 20 bytes
        assert_eq!(bytes[13], 0x12); // SYN | ACK
        assert_eq!(&bytes[14..16], &[0xfa, 0xf0]); // 64240
        assert_eq!(&bytes[16..20], &[0, 0, 0, 0]);
    }

    #[test]
    fn header_roundtrip() {
        let header = TcpHeader::new(40000, 80, u32::MAX, 7, PSH | ACK, 512);
        assert_eq!(TcpHeader::parse(&header.to_bytes()), Some(header));
        assert_eq!(TcpHeader::parse(&[0u8; 19]), None);
    }

    #[test]
    fn syn_checksum_known_value() {
        // Computed by hand: pseudo-header 0x141d + header 0x1dfb3 = 0x1f3d0,
        // folded 0xf3d1, complemented 0x0c2e.
        let syn = TcpHeader::new(30000, 8080, 1, 0, SYN, 64240);
        let segment = build_segment(CLIENT_IP, SERVER_IP, &syn, &[]);
        assert_eq!(u16::from_be_bytes([segment[16], segment[17]]), 0x0c2e);
    }

    #[test]
    fn checksum_verifies_and_detects_corruption() {
        let payload: Vec<u8> = (0..1000u32).map(|i| (i * 7 % 251) as u8).collect();
        let header = TcpHeader::new(30000, 8080, 123_456, 654_321, PSH | ACK, 64240);
        let mut segment = build_segment(CLIENT_IP, SERVER_IP, &header, &payload);
        assert_eq!(segment.len(), TCP_HEADER_LEN + 1000);
        assert_eq!(tcp_checksum(CLIENT_IP, SERVER_IP, &segment), 0);
        // The pseudo-header protects the addresses too.
        assert_ne!(
            tcp_checksum(CLIENT_IP, Ipv4Addr::new(10, 0, 0, 3), &segment),
            0
        );
        segment[500] ^= 0x01;
        assert_ne!(tcp_checksum(CLIENT_IP, SERVER_IP, &segment), 0);
    }

    #[test]
    fn split_segment_with_options() {
        // SYN-ACK with MSS 1460, two NOPs and an end of list: data offset 7 (28 bytes).
        let mut header = TcpHeader::new(8080, 30000, 99, 2, SYN | ACK, 65535);
        header.data_offset = 7;
        let mut bytes = header.to_bytes().to_vec();
        bytes.extend_from_slice(&[2, 4, 0x05, 0xb4, 1, 1, 0, 0]);
        bytes.extend_from_slice(b"xy");
        let (parsed, options, payload) = split_segment(&bytes).unwrap();
        assert_eq!(parsed.header_len(), 28);
        assert_eq!(parse_mss(options), Some(1460));
        assert_eq!(payload, b"xy");
    }

    #[test]
    fn mss_option_parsing() {
        assert_eq!(parse_mss(&[]), None);
        assert_eq!(parse_mss(&[1, 1, 2, 4, 0xff, 0xd7]), Some(65495));
        assert_eq!(parse_mss(&[3, 3, 7, 2, 4, 0x02, 0x18]), Some(536)); // after window scale
        assert_eq!(parse_mss(&[2, 4, 0x05]), None); // truncated
        assert_eq!(parse_mss(&[8, 0]), None); // invalid length
    }

    #[test]
    fn flag_names() {
        assert_eq!(flags_to_string(SYN), "SYN");
        assert_eq!(flags_to_string(SYN | ACK), "SYN, ACK");
        assert_eq!(flags_to_string(PSH | ACK), "PSH, ACK");
        assert_eq!(flags_to_string(FIN | ACK), "FIN, ACK");
        assert_eq!(flags_to_string(0), "<ninguna>");
    }

    #[test]
    fn sequence_comparison_wraps_around() {
        assert!(seq_lt(1, 2));
        assert!(seq_lt(u32::MAX - 10, 5)); // 5 comes after the wrap
        assert!(seq_gt(5, u32::MAX - 10));
        assert!(seq_le(7, 7) && seq_ge(7, 7));
        assert!(!seq_lt(7, 7));
    }
}
