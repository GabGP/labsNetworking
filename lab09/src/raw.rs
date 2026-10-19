//! Raw socket `socket(AF_INET, SOCK_RAW, IPPROTO_TCP)` used by the client (Linux).
//!
//! * Sending: we write the TCP header + data. `IP_HDRINCL` is not enabled, so the
//!   kernel prepends the IPv4 header (addresses, TTL, IP checksum) for us.
//! * Receiving: the kernel gives this socket a copy of the incoming TCP packets,
//!   *including* their IPv4 header, so the caller keeps only its own connection.
//!
//! Opening it requires superuser privileges (or the CAP_NET_RAW capability).

use std::io;
use std::mem;
use std::net::{Ipv4Addr, SocketAddr, UdpSocket};
use std::os::fd::{AsRawFd, FromRawFd, OwnedFd};
use std::time::Duration;

const SOCKADDR_IN_LEN: libc::socklen_t = mem::size_of::<libc::sockaddr_in>() as libc::socklen_t;

pub struct RawSocket {
    fd: OwnedFd,
}

impl RawSocket {
    /// Opens the raw socket and binds it to `local_ip`, the source address of our
    /// segments (the same address must be used in the TCP pseudo-header).
    pub fn open(local_ip: Ipv4Addr) -> io::Result<RawSocket> {
        // SAFETY: plain system call with constant arguments.
        let fd = unsafe { libc::socket(libc::AF_INET, libc::SOCK_RAW, libc::IPPROTO_TCP) };
        if fd < 0 {
            return Err(io::Error::last_os_error());
        }
        // SAFETY: `fd` is a new descriptor that nobody else owns; OwnedFd closes it on drop.
        let socket = RawSocket {
            fd: unsafe { OwnedFd::from_raw_fd(fd) },
        };

        let addr = sockaddr_in(local_ip);
        // SAFETY: `addr` is a valid sockaddr_in and SOCKADDR_IN_LEN is its size.
        let rc = unsafe {
            libc::bind(
                socket.fd.as_raw_fd(),
                (&addr as *const libc::sockaddr_in).cast(),
                SOCKADDR_IN_LEN,
            )
        };
        if rc < 0 {
            return Err(io::Error::last_os_error());
        }
        Ok(socket)
    }

    /// Sends a TCP segment (header + data) to `dst`.
    pub fn send_to(&self, segment: &[u8], dst: Ipv4Addr) -> io::Result<()> {
        let addr = sockaddr_in(dst);
        // SAFETY: pointer and length come from a valid slice; `addr` is a valid sockaddr_in.
        let sent = unsafe {
            libc::sendto(
                self.fd.as_raw_fd(),
                segment.as_ptr().cast(),
                segment.len(),
                0,
                (&addr as *const libc::sockaddr_in).cast(),
                SOCKADDR_IN_LEN,
            )
        };
        if sent < 0 {
            return Err(io::Error::last_os_error());
        }
        if sent as usize != segment.len() {
            return Err(io::Error::new(
                io::ErrorKind::WriteZero,
                "el segmento se envió de forma incompleta",
            ));
        }
        Ok(())
    }

    /// Waits at most `timeout` for an incoming packet and copies it (IPv4 header
    /// included) into `buf`. Returns `Ok(None)` when the time runs out.
    pub fn recv_timeout(&self, buf: &mut [u8], timeout: Duration) -> io::Result<Option<usize>> {
        let mut pfd = libc::pollfd {
            fd: self.fd.as_raw_fd(),
            events: libc::POLLIN,
            revents: 0,
        };
        // poll() counts milliseconds: round up so a short wait does not become 0 (busy loop).
        let millis = timeout
            .as_micros()
            .div_ceil(1000)
            .min(libc::c_int::MAX as u128) as libc::c_int;
        // SAFETY: `pfd` is a valid array of exactly one pollfd.
        let ready = unsafe { libc::poll(&mut pfd, 1, millis) };
        if ready < 0 {
            let err = io::Error::last_os_error();
            return if err.kind() == io::ErrorKind::Interrupted {
                Ok(None)
            } else {
                Err(err)
            };
        }
        if ready == 0 {
            return Ok(None);
        }

        // SAFETY: pointer and length come from a valid mutable slice.
        let received = unsafe {
            libc::recv(
                self.fd.as_raw_fd(),
                buf.as_mut_ptr().cast(),
                buf.len(),
                libc::MSG_DONTWAIT,
            )
        };
        if received < 0 {
            let err = io::Error::last_os_error();
            return match err.kind() {
                io::ErrorKind::WouldBlock | io::ErrorKind::Interrupted => Ok(None),
                _ => Err(err),
            };
        }
        Ok(Some(received as usize))
    }
}

/// Builds a `sockaddr_in` for `ip`. The port stays 0: a raw socket has no ports,
/// the TCP ports travel inside the header we build ourselves.
fn sockaddr_in(ip: Ipv4Addr) -> libc::sockaddr_in {
    // SAFETY: sockaddr_in is plain old data and all zeros is a valid value.
    let mut addr: libc::sockaddr_in = unsafe { mem::zeroed() };
    addr.sin_family = libc::AF_INET as libc::sa_family_t;
    addr.sin_port = 0;
    addr.sin_addr = libc::in_addr {
        // The octets are copied as they are, which is already network byte order.
        s_addr: u32::from_ne_bytes(ip.octets()),
    };
    addr
}

/// Asks the kernel which local address it would use to reach `dst`. Connecting a
/// UDP socket only consults the routing table; it does not send any packet.
pub fn local_ip_for(dst: Ipv4Addr) -> io::Result<Ipv4Addr> {
    let socket = UdpSocket::bind((Ipv4Addr::UNSPECIFIED, 0))?;
    socket.connect((dst, 9))?;
    match socket.local_addr()? {
        SocketAddr::V4(addr) => Ok(*addr.ip()),
        SocketAddr::V6(_) => Err(io::Error::other("la dirección local no es IPv4")),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn loopback_route_uses_loopback_address() {
        assert_eq!(
            local_ip_for(Ipv4Addr::LOCALHOST).unwrap(),
            Ipv4Addr::LOCALHOST
        );
    }

    #[test]
    fn sockaddr_keeps_network_byte_order() {
        let addr = sockaddr_in(Ipv4Addr::new(192, 168, 1, 5));
        assert_eq!(addr.sin_addr.s_addr.to_ne_bytes(), [192, 168, 1, 5]);
        assert_eq!(addr.sin_family as i32, libc::AF_INET);
    }
}
