use speedtest::ping;
use speedtest::raw::{self, RawSocket};
use speedtest::session::{self, MAX_TRANSMISSIONS, SessionError, TcpSession};
use speedtest::{PAYLOAD_SIZE, SEND_COUNT, fnv1a, parse_key_values};
use std::env;
use std::fs;
use std::io;
use std::net::{IpAddr, Ipv4Addr, SocketAddrV4, ToSocketAddrs};
use std::process;
use std::time::Duration;

const USAGE: &str = "Uso: sudo ./client server <ip> port <puerto> sport <puerto_origen> \
                     file <archivo> ping <icmp|tcp|udp> pings <cantidad>";

struct Config {
    server: Ipv4Addr,
    port: u16,
    sport: u16,
    file: String,
    ping_protocol: String,
    ping_count: u32,
}

fn parse_port(value: &str, name: &str) -> Result<u16, String> {
    value
        .parse::<u16>()
        .ok()
        .filter(|port| *port != 0)
        .ok_or_else(|| format!("{name} inválido: '{value}'"))
}

/// Accepts an IPv4 address or a host name (the raw socket needs IPv4).
fn resolve_ipv4(host: &str) -> Result<Ipv4Addr, String> {
    if let Ok(ip) = host.parse::<Ipv4Addr>() {
        return Ok(ip);
    }
    (host, 0)
        .to_socket_addrs()
        .map_err(|e| format!("no se pudo resolver '{host}': {e}"))?
        .find_map(|addr| match addr.ip() {
            IpAddr::V4(ip) => Some(ip),
            IpAddr::V6(_) => None,
        })
        .ok_or_else(|| format!("'{host}' no tiene una dirección IPv4"))
}

fn parse_config(args: &[String]) -> Result<Config, String> {
    let values = parse_key_values(args, &["server", "port", "sport", "file", "ping", "pings"])?;
    let get = |key: &str, default: &str| {
        values
            .get(key)
            .cloned()
            .unwrap_or_else(|| default.to_string())
    };

    let ping_protocol = get("ping", "icmp").to_lowercase();
    if !ping::PROTOCOLS.contains(&ping_protocol.as_str()) {
        return Err(format!(
            "protocolo de ping inválido '{ping_protocol}' (use icmp, tcp o udp)"
        ));
    }
    let ping_count = get("pings", "4")
        .parse::<u32>()
        .ok()
        .filter(|n| (1..=100).contains(n))
        .ok_or("pings debe ser un número entre 1 y 100")?;

    Ok(Config {
        server: resolve_ipv4(&get("server", "127.0.0.1"))?,
        port: parse_port(&get("port", "8080"), "port")?,
        sport: parse_port(&get("sport", "30000"), "sport")?,
        file: get("file", "payload.bin"),
        ping_protocol,
        ping_count,
    })
}

fn ms(duration: Duration) -> f64 {
    duration.as_secs_f64() * 1000.0
}

/// Stage 1: latency measured by Perl's Net::Ping.
fn measure_latency(config: &Config) -> Option<f64> {
    println!(
        "[Etapa 1] Latencia con Perl Net::Ping (protocolo {}, {} pings)",
        config.ping_protocol, config.ping_count
    );
    let report = match ping::run(
        config.server,
        &config.ping_protocol,
        config.ping_count,
        config.port,
    ) {
        Ok(report) => report,
        Err(e) => {
            println!("   Advertencia: {e}");
            return None;
        }
    };
    for line in report.output.lines() {
        println!("   {line}");
    }
    let average = report.average_ms();
    match average {
        Some(avg) => println!(
            "   Latencia promedio: {avg:.3} ms ({}/{} respuestas)",
            report.received(),
            report.samples.len()
        ),
        None => println!("   Advertencia: ningún ping obtuvo respuesta"),
    }
    average
}

/// Turns a session error into a message with a hint about its usual cause.
fn explain(error: SessionError, during_handshake: bool) -> String {
    match error {
        SessionError::Reset if during_handshake => {
            "el servidor respondió RST: ¿está corriendo el servidor en ese puerto?".to_string()
        }
        SessionError::Reset => {
            "la conexión fue reiniciada (RST). Si el RST lo generó el kernel local, \
                                agregue la regla de iptables (make rst-block)"
                .to_string()
        }
        SessionError::Timeout(what) => format!(
            "no llegó {what} después de {MAX_TRANSMISSIONS} intentos: revise la IP, el puerto y el firewall"
        ),
        SessionError::Io(e) => format!("error de E/S en el socket RAW: {e}"),
    }
}

/// Stage 3: the lab formula and the summary.
fn print_results(latency_ms: Option<f64>, handshake: Duration, times: &[f64; SEND_COUNT]) {
    let average = times.iter().sum::<f64>() / SEND_COUNT as f64;
    let bits = (PAYLOAD_SIZE * 8) as f64;
    let speed_bps = bits / (2.0 * average);
    let speed_kbps = speed_bps / 1000.0;

    println!();
    println!("[Etapa 3] Resultados");
    println!("==================================================================");
    match latency_ms {
        Some(latency) => println!(" Latencia inicial (Net::Ping)   : {latency:.3} ms"),
        None => println!(" Latencia inicial (Net::Ping)   : no disponible"),
    }
    println!(" RTT inicial (SYN -> SYN-ACK)   : {:.3} ms", ms(handshake));
    println!(" Tiempo por envío (RTT + tiempo de transmisión):");
    for (i, time) in times.iter().enumerate() {
        println!("   envío {:>2}: {:>10.3} ms", i + 1, time * 1000.0);
    }
    println!(
        " RTT promedio                   : {:.3} ms = {average:.6} s",
        average * 1000.0
    );
    println!(
        " Velocidad = ({PAYLOAD_SIZE} × 8) / (2 × RTT promedio) = {bits} bits / (2 × {average:.6} s)"
    );
    println!(" Velocidad de subida            : {speed_bps:.2} bps = {speed_kbps:.2} Kbps");
    println!("==================================================================");
}

fn run(config: &Config) -> Result<(), String> {
    if !cfg!(target_os = "linux") {
        return Err(
            "el cliente RAW requiere Linux: en macOS/BSD el kernel no entrega los segmentos \
                    TCP a los sockets RAW (use una máquina virtual con Linux)"
                .to_string(),
        );
    }

    let payload = fs::read(&config.file).map_err(|e| {
        format!(
            "no se pudo leer '{}': {e} (genérelo con `make payload`)",
            config.file
        )
    })?;
    if payload.len() != PAYLOAD_SIZE {
        return Err(format!(
            "'{}' mide {} bytes y la prueba requiere exactamente {PAYLOAD_SIZE} bytes (1 KB)",
            config.file,
            payload.len()
        ));
    }

    let local_ip = raw::local_ip_for(config.server)
        .map_err(|e| format!("no hay ruta hacia {}: {e}", config.server))?;
    let local = SocketAddrV4::new(local_ip, config.sport);
    let remote = SocketAddrV4::new(config.server, config.port);

    println!("==================================================================");
    println!(" SpeedTest: TCP sobre sockets RAW");
    println!(" Cliente : {local}    Servidor: {remote}");
    println!(
        " Archivo : {} ({} bytes, fnv1a=0x{:08x})",
        config.file,
        payload.len(),
        fnv1a(&payload)
    );
    println!("==================================================================");
    println!();

    let latency_ms = measure_latency(config);
    println!();

    println!("[Etapa 2] Velocidad de subida con socket RAW (AF_INET, SOCK_RAW, IPPROTO_TCP)");
    let socket = RawSocket::open(local_ip).map_err(|e| match e.kind() {
        io::ErrorKind::PermissionDenied => {
            "no se pudo abrir el socket RAW: se requieren permisos de superusuario (ejecute con sudo)"
                .to_string()
        }
        _ => format!("no se pudo abrir el socket RAW: {e}"),
    })?;
    let iss = session::random_isn().map_err(|e| format!("no se pudo generar el ISN: {e}"))?;
    let mut tcp = TcpSession::new(socket, local, remote, iss);

    println!("-- Conexión (3-way handshake) --");
    let handshake = tcp.connect().map_err(|e| explain(e, true))?;
    println!(
        "   RTT inicial de la conexión (SYN -> SYN-ACK): {:.3} ms",
        ms(handshake)
    );
    if let Some(mss) = tcp.peer_mss().filter(|mss| (*mss as usize) < PAYLOAD_SIZE) {
        println!("   Advertencia: el MSS del servidor ({mss}) es menor que {PAYLOAD_SIZE} bytes");
    }

    println!("-- Transmisión: {SEND_COUNT} envíos de {PAYLOAD_SIZE} bytes (PSH, ACK) --");
    let mut times = [0.0f64; SEND_COUNT];
    for (i, time) in times.iter_mut().enumerate() {
        let elapsed = tcp.send_data(&payload).map_err(|e| explain(e, false))?;
        *time = elapsed.as_secs_f64();
        println!("   envío {:>2}/{SEND_COUNT}: {:.3} ms", i + 1, ms(elapsed));
    }

    println!("-- Cierre de la conexión --");
    if let Err(e) = tcp.close() {
        println!("   Advertencia: el cierre no se completó ({e})");
    }

    print_results(latency_ms, handshake, &times);
    Ok(())
}

fn main() {
    let args: Vec<String> = env::args().skip(1).collect();
    let config = match parse_config(&args) {
        Ok(config) => config,
        Err(e) => {
            eprintln!("Error: {e}");
            eprintln!("{USAGE}");
            process::exit(1);
        }
    };
    if let Err(e) = run(&config) {
        eprintln!("Error: {e}");
        process::exit(1);
    }
}
