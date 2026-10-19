//! SpeedTest: a TCP client built by hand over a raw socket (`client`) and a
//! standard TCP server (`server`) that acts as its counterpart.
//!
//! The code shared by both binaries lives in this library crate.

pub mod ipv4;
pub mod ping;
pub mod raw;
pub mod session;
pub mod tcp;

use chrono::Local;
use std::collections::HashMap;

/// Size of the test file: 1 KB as defined by the lab (1000 bytes).
pub const PAYLOAD_SIZE: usize = 1000;

/// Number of times the file is sent over the same TCP connection.
pub const SEND_COUNT: usize = 10;

pub fn get_datetime() -> String {
    Local::now().format("%Y/%m/%d %H:%M:%S").to_string()
}

/// Prints a log line with the CLF based format of the course:
/// `[s] host socketType [dateTime] protocol: description`
pub fn log_message(direction: &str, host: &str, socket_type: &str, protocol: &str, msg: &str) {
    let datetime = get_datetime();
    println!(
        "{} {} {} [{}] {}: {}",
        direction, host, socket_type, datetime, protocol, msg
    );
}

/// Parses `KEY VALUE` arguments regardless of their order of appearance,
/// e.g. `port 8080 server 192.168.1.5`.
pub fn parse_key_values(
    args: &[String],
    known: &[&str],
) -> Result<HashMap<String, String>, String> {
    let mut values = HashMap::new();
    let mut i = 0;
    while i < args.len() {
        let key = args[i].to_lowercase();
        if !known.contains(&key.as_str()) {
            return Err(format!(
                "argumento desconocido '{}' (válidos: {})",
                args[i],
                known.join(", ")
            ));
        }
        let value = args
            .get(i + 1)
            .ok_or_else(|| format!("falta el valor del argumento '{}'", key))?;
        values.insert(key, value.clone());
        i += 2;
    }
    Ok(values)
}

/// 32-bit FNV-1a hash. Client and server print it so it is easy to check that
/// the bytes received are exactly the bytes sent.
pub fn fnv1a(data: &[u8]) -> u32 {
    data.iter().fold(0x811c_9dc5, |hash, &byte| {
        (hash ^ byte as u32).wrapping_mul(0x0100_0193)
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn strings(args: &[&str]) -> Vec<String> {
        args.iter().map(|s| s.to_string()).collect()
    }

    #[test]
    fn key_values_in_any_order() {
        let known = ["server", "port"];
        let a =
            parse_key_values(&strings(&["server", "10.0.0.5", "port", "9000"]), &known).unwrap();
        let b =
            parse_key_values(&strings(&["PORT", "9000", "server", "10.0.0.5"]), &known).unwrap();
        assert_eq!(a, b);
        assert_eq!(a["port"], "9000");
        assert_eq!(a["server"], "10.0.0.5");
    }

    #[test]
    fn key_values_errors() {
        let known = ["port"];
        assert!(parse_key_values(&strings(&["port"]), &known).is_err());
        assert!(parse_key_values(&strings(&["color", "red"]), &known).is_err());
        assert!(parse_key_values(&[], &known).unwrap().is_empty());
    }

    #[test]
    fn fnv1a_reference_values() {
        assert_eq!(fnv1a(b""), 0x811c_9dc5);
        assert_eq!(fnv1a(b"a"), 0xe40c_292c);
        assert_eq!(fnv1a(b"foobar"), 0xbf9c_f968);
    }
}
