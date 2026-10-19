//! Latency stage: runs Perl with the `Net::Ping` module (through
//! `std::process::Command`), captures its standard output and extracts the
//! latency of each ping in milliseconds.

use std::net::Ipv4Addr;
use std::process::Command;

/// Perl program executed with `perl -e`, embedded in the binary at compile time.
pub const PING_SCRIPT: &str = include_str!("ping.pl");

/// Protocols supported by the latency stage. ICMP needs root, like the raw socket.
pub const PROTOCOLS: [&str; 3] = ["icmp", "tcp", "udp"];

#[derive(Debug)]
pub struct PingReport {
    /// Standard output of the Perl program, as it was printed.
    pub output: String,
    /// Latency of each ping in milliseconds (`None` = no answer).
    pub samples: Vec<Option<f64>>,
}

impl PingReport {
    /// Number of pings that got an answer.
    pub fn received(&self) -> usize {
        self.samples.iter().flatten().count()
    }

    /// Average latency of the pings that got an answer.
    pub fn average_ms(&self) -> Option<f64> {
        let answered: Vec<f64> = self.samples.iter().flatten().copied().collect();
        if answered.is_empty() {
            None
        } else {
            Some(answered.iter().sum::<f64>() / answered.len() as f64)
        }
    }
}

/// Runs `perl -e <ping.pl> <host> <protocol> <count> <port>` and parses its output.
pub fn run(host: Ipv4Addr, protocol: &str, count: u32, port: u16) -> Result<PingReport, String> {
    let output = Command::new("perl")
        .arg("-e")
        .arg(PING_SCRIPT)
        .arg(host.to_string())
        .arg(protocol)
        .arg(count.to_string())
        .arg(port.to_string())
        .output()
        .map_err(|e| format!("no se pudo ejecutar perl: {e}"))?;

    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        return Err(format!(
            "perl/Net::Ping terminó con error: {}",
            stderr.trim()
        ));
    }
    let stdout = String::from_utf8_lossy(&output.stdout).into_owned();
    let samples = parse_output(&stdout);
    if samples.is_empty() {
        return Err(format!(
            "salida inesperada de Net::Ping: {:?}",
            stdout.trim()
        ));
    }
    Ok(PingReport {
        output: stdout,
        samples,
    })
}

/// Extracts the latency of every `ping ...` line of the output.
pub fn parse_output(stdout: &str) -> Vec<Option<f64>> {
    stdout
        .lines()
        .filter(|line| line.starts_with("ping "))
        .map(parse_latency_ms)
        .collect()
}

/// `ping 1 127.0.0.1 0.052 ms` gives `Some(0.052)`; `ping 2 10.0.0.9 timeout` gives `None`.
pub fn parse_latency_ms(line: &str) -> Option<f64> {
    let mut words = line.split_whitespace().rev();
    match (words.next(), words.next()) {
        (Some("ms"), Some(value)) => value
            .parse()
            .ok()
            .filter(|ms: &f64| ms.is_finite() && *ms >= 0.0),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::net::TcpListener;

    #[test]
    fn parses_latency_lines() {
        assert_eq!(parse_latency_ms("ping 1 127.0.0.1 0.052 ms"), Some(0.052));
        assert_eq!(parse_latency_ms("ping 3 192.168.1.5 12.500 ms"), Some(12.5));
        assert_eq!(parse_latency_ms("ping 2 10.0.0.9 timeout"), None);
        assert_eq!(parse_latency_ms("ping 2 10.0.0.9 abc ms"), None);
    }

    #[test]
    fn report_statistics() {
        let report = PingReport {
            output: String::new(),
            samples: parse_output(
                "ping 1 h 1.000 ms\nping 2 h timeout\nping 3 h 3.000 ms\nnoise\n",
            ),
        };
        assert_eq!(report.samples.len(), 3);
        assert_eq!(report.received(), 2);
        assert_eq!(report.average_ms(), Some(2.0));
        let empty = PingReport {
            output: String::new(),
            samples: vec![None],
        };
        assert_eq!(empty.average_ms(), None);
    }

    /// Runs the real Perl program with the TCP protocol (it does not need root)
    /// against a local listener. Skipped if perl is not installed.
    #[test]
    fn perl_net_ping_over_tcp() {
        let listener = TcpListener::bind((Ipv4Addr::LOCALHOST, 0)).unwrap();
        let port = listener.local_addr().unwrap().port();
        match run(Ipv4Addr::LOCALHOST, "tcp", 2, port) {
            Ok(report) => {
                assert_eq!(report.samples.len(), 2, "salida: {}", report.output);
                assert_eq!(report.received(), 2, "salida: {}", report.output);
            }
            Err(e) if e.starts_with("no se pudo ejecutar perl") => eprintln!("omitido: {e}"),
            Err(e) => panic!("{e}"),
        }
    }
}
