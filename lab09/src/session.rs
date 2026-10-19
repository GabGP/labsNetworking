//! Client side of a TCP connection implemented by hand on top of a raw socket:
//! 3-way handshake, data transfer (stop-and-wait) and connection close.
//!
//! ```text
//!   client                                    server
//!     | --- SYN          seq=ISN_c ------------> |   connect()
//!     | <-- SYN, ACK     seq=ISN_s ack=ISN_c+1   |
//!     | --- ACK          ack=ISN_s+1 ----------> |
//!     | --- PSH, ACK     1000 bytes -----------> |   send_data() x 10
//!     | <-- ACK          ack=seq+1000            |
//!     | --- FIN, ACK --------------------------> |   close()
//!     | <-- ACK, then FIN, ACK (or one FIN, ACK) |
//!     | --- ACK -------------------------------> |
//! ```

use crate::ipv4;
use crate::log_message;
use crate::raw::RawSocket;
use crate::tcp::{self, ACK, FIN, PSH, RST, SYN, TcpHeader};
use std::fmt;
use std::fs::File;
use std::io::{self, Read};
use std::mem;
use std::net::{Ipv4Addr, SocketAddrV4};
use std::time::{Duration, Instant};

/// Receive window advertised by the client (it never receives data, any value works).
pub const WINDOW: u16 = 64240;

/// Initial retransmission timeout, as recommended by RFC 6298. It doubles after each retransmission.
pub const INITIAL_RTO: Duration = Duration::from_secs(1);

/// Transmissions of the same segment before giving up (1 original + 3 retransmissions).
pub const MAX_TRANSMISSIONS: u32 = 4;

/// How long to wait for the server's FIN once our FIN has been acknowledged.
pub const FIN_WAIT_TIMEOUT: Duration = Duration::from_secs(3);

/// Packet input/output used by the session. `RawSocket` implements it; the
/// tests implement it with a simulated server.
pub trait Link {
    /// Sends a TCP segment (header + data); the IPv4 header is added by the OS.
    fn send(&mut self, dst: Ipv4Addr, segment: &[u8]) -> io::Result<()>;
    /// Copies the next IPv4 packet into `buf`, waiting at most `timeout`.
    /// `Ok(None)` means that nothing arrived in time.
    fn recv(&mut self, buf: &mut [u8], timeout: Duration) -> io::Result<Option<usize>>;
}

impl Link for RawSocket {
    fn send(&mut self, dst: Ipv4Addr, segment: &[u8]) -> io::Result<()> {
        self.send_to(segment, dst)
    }

    fn recv(&mut self, buf: &mut [u8], timeout: Duration) -> io::Result<Option<usize>> {
        self.recv_timeout(buf, timeout)
    }
}

#[derive(Debug)]
pub enum SessionError {
    Io(io::Error),
    /// The connection was aborted with a valid RST segment.
    Reset,
    /// No valid answer after every retransmission. Says what was being waited for.
    Timeout(&'static str),
}

impl fmt::Display for SessionError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            SessionError::Io(e) => write!(f, "error de E/S en el socket RAW: {e}"),
            SessionError::Reset => write!(f, "la conexión fue reiniciada con RST"),
            SessionError::Timeout(what) => write!(f, "no llegó {what}"),
        }
    }
}

impl std::error::Error for SessionError {}

impl From<io::Error> for SessionError {
    fn from(e: io::Error) -> Self {
        SessionError::Io(e)
    }
}

/// Segment of our connection received from the server.
#[derive(Debug, Clone, Copy)]
struct Received {
    header: TcpHeader,
    payload_len: usize,
    mss: Option<u16>,
}

pub struct TcpSession<L: Link> {
    link: L,
    local: SocketAddrV4,
    remote: SocketAddrV4,
    /// Initial send sequence number (our ISN).
    iss: u32,
    /// Initial receive sequence number (the server's ISN).
    irs: u32,
    /// Oldest sequence number sent and not yet acknowledged.
    snd_una: u32,
    /// Next sequence number to send.
    snd_nxt: u32,
    /// Next sequence number expected from the server: the value of our ACK field.
    rcv_nxt: u32,
    /// True once the SYN-ACK arrived (the sequence numbers of both sides are known).
    synchronized: bool,
    peer_window: u16,
    peer_mss: Option<u16>,
    initial_rto: Duration,
    buf: Vec<u8>,
    /// Received segments waiting to be logged. They are printed after the time
    /// measurement so that writing to the console does not distort it.
    pending: Vec<Received>,
}

impl<L: Link> TcpSession<L> {
    pub fn new(link: L, local: SocketAddrV4, remote: SocketAddrV4, iss: u32) -> Self {
        TcpSession {
            link,
            local,
            remote,
            iss,
            irs: 0,
            snd_una: iss,
            snd_nxt: iss,
            rcv_nxt: 0,
            synchronized: false,
            peer_window: 0,
            peer_mss: None,
            initial_rto: INITIAL_RTO,
            buf: vec![0; 65536],
            pending: Vec::new(),
        }
    }

    pub fn set_initial_rto(&mut self, rto: Duration) {
        self.initial_rto = rto;
    }

    pub fn iss(&self) -> u32 {
        self.iss
    }

    pub fn irs(&self) -> u32 {
        self.irs
    }

    pub fn snd_nxt(&self) -> u32 {
        self.snd_nxt
    }

    pub fn rcv_nxt(&self) -> u32 {
        self.rcv_nxt
    }

    pub fn peer_mss(&self) -> Option<u16> {
        self.peer_mss
    }

    pub fn peer_window(&self) -> u16 {
        self.peer_window
    }

    /// 3-way handshake. Returns the initial RTT of the connection (SYN -> SYN-ACK).
    pub fn connect(&mut self) -> Result<Duration, SessionError> {
        let iss = self.iss;
        let syn = self.header(iss, 0, SYN);
        let (syn_ack, rtt) = self.exchange(syn, &[], "el SYN-ACK", |seg| {
            seg.header.flags & (SYN | ACK | RST) == (SYN | ACK)
                && seg.header.ack == iss.wrapping_add(1)
        })?;

        // The SYN-ACK carries the server's ISN. A SYN consumes one sequence number
        // on each side, hence the + 1.
        self.irs = syn_ack.header.seq;
        self.rcv_nxt = self.irs.wrapping_add(1);
        self.snd_una = iss.wrapping_add(1);
        self.snd_nxt = self.snd_una;
        self.peer_window = syn_ack.header.window;
        self.peer_mss = syn_ack.mss;
        self.synchronized = true;
        self.flush_received();

        // Third step of the handshake: acknowledge the server's SYN.
        let ack = self.header(self.snd_nxt, self.rcv_nxt, ACK);
        self.transmit(ack, &[])?;
        Ok(rtt)
    }

    /// Sends `data` in one PSH|ACK segment and waits until the server acknowledges
    /// all of it before returning (stop-and-wait). Returns the time between the
    /// transmission and the arrival of the ACK: RTT + transmission time.
    pub fn send_data(&mut self, data: &[u8]) -> Result<Duration, SessionError> {
        let seq = self.snd_nxt;
        let end = seq.wrapping_add(data.len() as u32);
        let segment = self.header(seq, self.rcv_nxt, PSH | ACK);
        self.snd_nxt = end;
        let (ack, elapsed) = self.exchange(segment, data, "el ACK de los datos", |seg| {
            seg.header.flags & ACK != 0 && seg.header.ack == end
        })?;
        self.snd_una = end;
        self.peer_window = ack.header.window;
        self.flush_received();
        Ok(elapsed)
    }

    /// Active close: FIN|ACK, the server answers with ACK + FIN (or a single
    /// FIN|ACK) and the client sends the final ACK.
    pub fn close(&mut self) -> Result<(), SessionError> {
        let fin_seq = self.snd_nxt;
        let fin_end = fin_seq.wrapping_add(1); // the FIN consumes one sequence number
        let fin = self.header(fin_seq, self.rcv_nxt, FIN | ACK);
        self.snd_nxt = fin_end;
        let (answer, _) = self.exchange(fin, &[], "el ACK del FIN", |seg| {
            seg.header.flags & ACK != 0 && seg.header.ack == fin_end
        })?;
        self.snd_una = fin_end;

        // The server's FIN may come in the same segment or in a later one.
        let server_fin = if answer.header.flags & FIN != 0 {
            answer
        } else {
            self.wait_for_fin()?
        };
        self.rcv_nxt = server_fin
            .header
            .seq
            .wrapping_add(server_fin.payload_len as u32)
            .wrapping_add(1);
        self.flush_received();

        let last_ack = self.header(self.snd_nxt, self.rcv_nxt, ACK);
        self.transmit(last_ack, &[])
    }

    fn header(&self, seq: u32, ack: u32, flags: u8) -> TcpHeader {
        TcpHeader::new(
            self.local.port(),
            self.remote.port(),
            seq,
            ack,
            flags,
            WINDOW,
        )
    }

    fn build(&self, header: &TcpHeader, payload: &[u8]) -> Vec<u8> {
        tcp::build_segment(*self.local.ip(), *self.remote.ip(), header, payload)
    }

    /// Sends a segment that does not expect an answer (the ACKs).
    fn transmit(&mut self, header: TcpHeader, payload: &[u8]) -> Result<(), SessionError> {
        let segment = self.build(&header, payload);
        self.log_sent(&header, payload.len(), "");
        self.link.send(*self.remote.ip(), &segment)?;
        Ok(())
    }

    /// Sends a segment and waits for an answer accepted by `accept`. If the
    /// retransmission timer expires the segment is sent again, doubling the
    /// timeout each time (RFC 6298). Returns the accepted segment and the time
    /// elapsed since the first transmission.
    fn exchange(
        &mut self,
        header: TcpHeader,
        payload: &[u8],
        waiting_for: &'static str,
        accept: impl Fn(&Received) -> bool,
    ) -> Result<(Received, Duration), SessionError> {
        // The segment is built and logged before the clock starts.
        let segment = self.build(&header, payload);
        self.log_sent(&header, payload.len(), "");

        let mut rto = self.initial_rto;
        let mut transmissions = 1;
        let start = Instant::now();
        self.link.send(*self.remote.ip(), &segment)?;
        let mut deadline = start + rto;

        loop {
            match self.recv_until(deadline)? {
                Some((seg, arrival)) => {
                    if seg.header.flags & RST != 0 && self.is_acceptable_rst(&seg) {
                        self.flush_received();
                        return Err(SessionError::Reset);
                    }
                    if accept(&seg) {
                        return Ok((seg, arrival - start));
                    }
                }
                None if transmissions < MAX_TRANSMISSIONS => {
                    transmissions += 1;
                    rto *= 2;
                    self.flush_received();
                    self.log_sent(&header, payload.len(), " (retransmisión)");
                    self.link.send(*self.remote.ip(), &segment)?;
                    deadline = Instant::now() + rto;
                }
                None => {
                    self.flush_received();
                    return Err(SessionError::Timeout(waiting_for));
                }
            }
        }
    }

    /// Waits for the server's FIN after our FIN has been acknowledged (FIN-WAIT-2).
    fn wait_for_fin(&mut self) -> Result<Received, SessionError> {
        let deadline = Instant::now() + FIN_WAIT_TIMEOUT;
        while let Some((seg, _)) = self.recv_until(deadline)? {
            if seg.header.flags & RST != 0 && self.is_acceptable_rst(&seg) {
                self.flush_received();
                return Err(SessionError::Reset);
            }
            if seg.header.flags & FIN != 0 {
                return Ok(seg);
            }
        }
        self.flush_received();
        Err(SessionError::Timeout("el FIN del servidor"))
    }

    /// Returns the next segment of this connection that arrives before `deadline`
    /// together with its arrival time. The raw socket also sees the TCP packets
    /// of every other connection of the machine; those are discarded here.
    fn recv_until(&mut self, deadline: Instant) -> io::Result<Option<(Received, Instant)>> {
        loop {
            let now = Instant::now();
            if now >= deadline {
                return Ok(None);
            }
            let Some(len) = self.link.recv(&mut self.buf, deadline - now)? else {
                continue;
            };
            let arrival = Instant::now();
            if let Some(seg) = self.filter(len) {
                self.pending.push(seg);
                return Ok(Some((seg, arrival)));
            }
        }
    }

    /// Keeps only TCP packets from the server's address and port to ours.
    fn filter(&self, len: usize) -> Option<Received> {
        let packet = ipv4::parse(&self.buf[..len])?;
        if packet.protocol != tcp::IPPROTO_TCP
            || packet.src != *self.remote.ip()
            || packet.dst != *self.local.ip()
        {
            return None;
        }
        let (header, options, payload) = tcp::split_segment(packet.payload)?;
        if header.src_port != self.remote.port() || header.dst_port != self.local.port() {
            return None;
        }
        Some(Received {
            header,
            payload_len: payload.len(),
            mss: tcp::parse_mss(options),
        })
    }

    /// RFC 9293 section 3.10.7: before the handshake a RST is valid only if it
    /// acknowledges our SYN; afterwards its sequence number must be the expected one.
    fn is_acceptable_rst(&self, seg: &Received) -> bool {
        if self.synchronized {
            seg.header.seq == self.rcv_nxt
        } else {
            seg.header.flags & ACK != 0 && seg.header.ack == self.iss.wrapping_add(1)
        }
    }

    fn log_sent(&self, header: &TcpHeader, payload_len: usize, note: &str) {
        let description = self.describe(header, payload_len, None, true) + note;
        log_message(
            "<",
            &self.local.ip().to_string(),
            "client",
            "TCP",
            &description,
        );
    }

    fn flush_received(&mut self) {
        for seg in mem::take(&mut self.pending) {
            let description = self.describe(&seg.header, seg.payload_len, seg.mss, false);
            log_message(
                ">",
                &self.remote.ip().to_string(),
                "server",
                "TCP",
                &description,
            );
        }
    }

    /// Describes a segment the way Wireshark does, with relative sequence numbers
    /// (Seq=0 is the ISN), e.g. `30000 → 8080 [PSH, ACK] Seq=1 Ack=1 Win=64240 Len=1000`.
    fn describe(
        &self,
        header: &TcpHeader,
        payload_len: usize,
        mss: Option<u16>,
        sent: bool,
    ) -> String {
        let (seq_base, ack_base) = if sent {
            (self.iss, self.irs)
        } else {
            (self.irs, self.iss)
        };
        let mut text = format!(
            "{} → {} [{}] Seq={}",
            header.src_port,
            header.dst_port,
            tcp::flags_to_string(header.flags),
            header.seq.wrapping_sub(seq_base)
        );
        if header.flags & ACK != 0 {
            text += &format!(" Ack={}", header.ack.wrapping_sub(ack_base));
        }
        text += &format!(" Win={} Len={}", header.window, payload_len);
        if let Some(mss) = mss {
            text += &format!(" MSS={mss}");
        }
        if header.flags & SYN != 0 {
            text += &format!(" (ISN={})", header.seq);
        }
        text
    }
}

/// Random initial sequence number (RFC 9293 recommends that it be unpredictable).
pub fn random_isn() -> io::Result<u32> {
    let mut bytes = [0u8; 4];
    File::open("/dev/urandom")?.read_exact(&mut bytes)?;
    Ok(u32::from_ne_bytes(bytes))
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::VecDeque;

    const CLIENT: SocketAddrV4 = SocketAddrV4::new(Ipv4Addr::new(10, 0, 0, 1), 30000);
    const SERVER: SocketAddrV4 = SocketAddrV4::new(Ipv4Addr::new(10, 0, 0, 2), 8080);

    /// Simulated server that answers like the TCP stack of a kernel would.
    #[derive(Default)]
    struct FakeServer {
        isn: u32,
        rcv_nxt: u32,
        received: Vec<u8>,
        /// Packets waiting to be read by the client.
        inbox: VecDeque<Vec<u8>>,
        /// Flags of every segment written by the client, in order.
        client_flags: Vec<u8>,
        /// Ignore the first SYN (to exercise the retransmission).
        drop_first_syn: bool,
        /// Answer the FIN with a pure ACK followed by FIN|ACK instead of a single FIN|ACK.
        separate_fin: bool,
        /// Refuse the connection with RST|ACK (closed port).
        refuse: bool,
        /// Never answer anything.
        silent: bool,
        /// Packets of other connections that the raw socket also sees.
        noise: bool,
        final_ack: bool,
    }

    impl FakeServer {
        fn reply(&mut self, seq: u32, ack: u32, flags: u8) {
            let header = TcpHeader::new(SERVER.port(), CLIENT.port(), seq, ack, flags, 65535);
            let segment = tcp::build_segment(*SERVER.ip(), *CLIENT.ip(), &header, &[]);
            if self.noise {
                // Same addresses but another port, then another host: both must be ignored.
                let other = TcpHeader::new(SERVER.port(), 40000, 1, 1, ACK, 1);
                let other = tcp::build_segment(*SERVER.ip(), *CLIENT.ip(), &other, &[]);
                self.inbox
                    .push_back(ipv4::build_packet(*SERVER.ip(), *CLIENT.ip(), &other));
                let stranger = Ipv4Addr::new(8, 8, 8, 8);
                self.inbox
                    .push_back(ipv4::build_packet(stranger, *CLIENT.ip(), &segment));
            }
            self.inbox
                .push_back(ipv4::build_packet(*SERVER.ip(), *CLIENT.ip(), &segment));
        }
    }

    impl Link for FakeServer {
        fn send(&mut self, dst: Ipv4Addr, segment: &[u8]) -> io::Result<()> {
            assert_eq!(dst, *SERVER.ip());
            assert_eq!(
                tcp::tcp_checksum(*CLIENT.ip(), dst, segment),
                0,
                "checksum incorrecto"
            );
            let (header, options, payload) = tcp::split_segment(segment).unwrap();
            assert_eq!(header.header_len(), tcp::TCP_HEADER_LEN);
            assert!(options.is_empty());
            assert_eq!(
                (header.src_port, header.dst_port),
                (CLIENT.port(), SERVER.port())
            );
            self.client_flags.push(header.flags);
            if self.silent {
                return Ok(());
            }

            let my_seq = self.isn.wrapping_add(1);
            if header.flags == SYN {
                if self.refuse {
                    self.reply(0, header.seq.wrapping_add(1), RST | ACK);
                } else if self.drop_first_syn {
                    self.drop_first_syn = false;
                } else {
                    self.rcv_nxt = header.seq.wrapping_add(1);
                    self.reply(self.isn, self.rcv_nxt, SYN | ACK);
                }
            } else if !payload.is_empty() {
                assert_eq!(header.flags, PSH | ACK);
                assert_eq!(header.seq, self.rcv_nxt, "número de secuencia inesperado");
                assert_eq!(header.ack, my_seq);
                self.received.extend_from_slice(payload);
                self.rcv_nxt = self.rcv_nxt.wrapping_add(payload.len() as u32);
                self.reply(my_seq, self.rcv_nxt, ACK);
            } else if header.flags & FIN != 0 {
                assert_eq!(header.seq, self.rcv_nxt);
                self.rcv_nxt = self.rcv_nxt.wrapping_add(1);
                if self.separate_fin {
                    self.reply(my_seq, self.rcv_nxt, ACK);
                }
                self.reply(my_seq, self.rcv_nxt, FIN | ACK);
            } else if header.flags == ACK && header.ack == my_seq.wrapping_add(1) {
                assert_eq!(header.seq, self.rcv_nxt);
                self.final_ack = true;
            }
            Ok(())
        }

        fn recv(&mut self, buf: &mut [u8], timeout: Duration) -> io::Result<Option<usize>> {
            match self.inbox.pop_front() {
                Some(packet) => {
                    buf[..packet.len()].copy_from_slice(&packet);
                    Ok(Some(packet.len()))
                }
                None => {
                    std::thread::sleep(timeout);
                    Ok(None)
                }
            }
        }
    }

    fn session(server: FakeServer, iss: u32) -> TcpSession<FakeServer> {
        let mut session = TcpSession::new(server, CLIENT, SERVER, iss);
        session.set_initial_rto(Duration::from_millis(20));
        session
    }

    fn full_transfer(server: FakeServer, iss: u32) -> TcpSession<FakeServer> {
        let payload: Vec<u8> = (0..1000).map(|i| (i % 256) as u8).collect();
        let mut s = session(server, iss);
        s.connect().unwrap();
        for _ in 0..10 {
            s.send_data(&payload).unwrap();
        }
        s.close().unwrap();
        assert_eq!(s.link.received.len(), 10_000);
        assert_eq!(&s.link.received[..1000], &payload[..]);
        assert!(s.link.final_ack);
        s
    }

    #[test]
    fn handshake_transfer_and_close() {
        let s = full_transfer(
            FakeServer {
                isn: 5000,
                ..Default::default()
            },
            1000,
        );
        let mut expected = vec![SYN, ACK];
        expected.extend([PSH | ACK; 10]);
        expected.extend([FIN | ACK, ACK]);
        assert_eq!(s.link.client_flags, expected);
        assert_eq!(s.snd_nxt(), 1000 + 1 + 10_000 + 1);
        assert_eq!(s.rcv_nxt(), 5000 + 2);
        assert_eq!(s.irs(), 5000);
    }

    #[test]
    fn sequence_numbers_wrap_around() {
        full_transfer(
            FakeServer {
                isn: u32::MAX,
                ..Default::default()
            },
            u32::MAX - 4000,
        );
    }

    #[test]
    fn server_ack_and_fin_in_separate_segments() {
        full_transfer(
            FakeServer {
                separate_fin: true,
                ..Default::default()
            },
            7,
        );
    }

    #[test]
    fn foreign_packets_are_ignored() {
        full_transfer(
            FakeServer {
                noise: true,
                ..Default::default()
            },
            42,
        );
    }

    #[test]
    fn lost_syn_is_retransmitted() {
        let mut s = session(
            FakeServer {
                drop_first_syn: true,
                ..Default::default()
            },
            1,
        );
        s.connect().unwrap();
        assert_eq!(s.link.client_flags, vec![SYN, SYN, ACK]);
    }

    #[test]
    fn refused_connection_is_reported() {
        let mut s = session(
            FakeServer {
                refuse: true,
                ..Default::default()
            },
            1,
        );
        assert!(matches!(s.connect(), Err(SessionError::Reset)));
    }

    #[test]
    fn gives_up_after_max_transmissions() {
        let mut s = session(
            FakeServer {
                silent: true,
                ..Default::default()
            },
            1,
        );
        s.set_initial_rto(Duration::from_millis(5));
        assert!(matches!(s.connect(), Err(SessionError::Timeout(_))));
        assert_eq!(s.link.client_flags.len(), MAX_TRANSMISSIONS as usize);
    }
}
