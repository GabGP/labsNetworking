use speedtest::{PAYLOAD_SIZE, fnv1a, log_message, parse_key_values};
use std::env;
use std::io::{self, Read};
use std::net::{TcpListener, TcpStream};
use std::process;
use std::time::Duration;

/// An idle connection is closed after this time without data (e.g. if the client dies).
const READ_TIMEOUT: Duration = Duration::from_secs(30);

/// Asks the kernel to acknowledge the next segment immediately instead of
/// delaying the ACK (Linux), so the client measures the real round trip.
/// Linux may leave quick ACK mode on its own, so it is set before every read.
#[cfg(target_os = "linux")]
fn enable_quickack(stream: &TcpStream) {
    use std::os::fd::AsRawFd;
    let one: libc::c_int = 1;
    // SAFETY: valid socket descriptor and a pointer to a c_int of the declared size.
    unsafe {
        libc::setsockopt(
            stream.as_raw_fd(),
            libc::IPPROTO_TCP,
            libc::TCP_QUICKACK,
            (&one as *const libc::c_int).cast(),
            std::mem::size_of::<libc::c_int>() as libc::socklen_t,
        );
    }
}

#[cfg(not(target_os = "linux"))]
fn enable_quickack(_stream: &TcpStream) {}

/// Reads one block of the size of the test file. TCP is a byte stream, so a
/// block may arrive in several reads. Returns fewer bytes (0 = none) only if
/// the client closed the connection (FIN) first.
fn read_block(stream: &mut TcpStream, block: &mut [u8]) -> io::Result<usize> {
    let mut filled = 0;
    while filled < block.len() {
        match stream.read(&mut block[filled..]) {
            Ok(0) => break,
            Ok(n) => filled += n,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(e),
        }
    }
    Ok(filled)
}

fn handle_client(mut stream: TcpStream, protocol: &str) {
    let (client_ip, client_port) = match stream.peer_addr() {
        Ok(addr) => (addr.ip().to_string(), addr.port()),
        Err(_) => ("?".to_string(), 0),
    };
    let server_ip = stream
        .local_addr()
        .map(|addr| addr.ip().to_string())
        .unwrap_or_else(|_| "?".to_string());

    log_message(
        ">",
        &client_ip,
        "client",
        protocol,
        &format!("conexión establecida desde el puerto {client_port} (SYN, SYN-ACK, ACK)"),
    );
    if let Err(e) = stream.set_read_timeout(Some(READ_TIMEOUT)) {
        eprintln!("No se pudo configurar el timeout de lectura: {e}");
    }

    let mut block = [0u8; PAYLOAD_SIZE];
    let mut sends = 0;
    let mut total = 0;
    loop {
        enable_quickack(&stream);
        match read_block(&mut stream, &mut block) {
            Ok(0) => {
                log_message(
                    ">",
                    &client_ip,
                    "client",
                    protocol,
                    "FIN recibido: el cliente terminó de enviar",
                );
                break;
            }
            Ok(n) if n == PAYLOAD_SIZE => {
                sends += 1;
                total += n;
                let msg = format!(
                    "envío #{sends}: {n} bytes recibidos (fnv1a=0x{:08x})",
                    fnv1a(&block)
                );
                log_message(">", &client_ip, "client", protocol, &msg);
            }
            Ok(n) => {
                total += n;
                let msg = format!("bloque incompleto: {n} bytes antes del FIN");
                log_message(">", &client_ip, "client", protocol, &msg);
            }
            Err(e) => {
                log_message(
                    ">",
                    &client_ip,
                    "client",
                    protocol,
                    &format!("error de lectura: {e}"),
                );
                break;
            }
        }
    }

    // Closing the socket makes the kernel send our FIN (passive close).
    drop(stream);
    let msg = if total == 0 {
        "FIN enviado: conexión cerrada sin datos (por ejemplo, el ping TCP de Net::Ping)"
            .to_string()
    } else {
        format!(
            "FIN enviado: conexión cerrada ({sends} envíos de {PAYLOAD_SIZE} bytes, {total} bytes en total)"
        )
    };
    log_message("<", &server_ip, "server", protocol, &msg);
}

fn main() {
    // KEY VALUE arguments in any order; by default it listens on 0.0.0.0:8080
    let args: Vec<String> = env::args().skip(1).collect();
    let values = match parse_key_values(&args, &["server", "port"]) {
        Ok(values) => values,
        Err(e) => {
            eprintln!("Error: {e}");
            eprintln!("Uso: ./server server <ip_a_escuchar> port <puerto>");
            process::exit(1);
        }
    };
    let server = values.get("server").map_or("0.0.0.0", String::as_str);
    let port = values.get("port").map_or("8080", String::as_str);
    let bind_addr = format!("{server}:{port}");

    let listener = TcpListener::bind(&bind_addr).unwrap_or_else(|e| {
        eprintln!("Error: no se pudo escuchar en {bind_addr}: {e}");
        process::exit(1);
    });
    println!("Servidor SpeedTest escuchando en {bind_addr} (Ctrl+C para terminar)");

    for stream in listener.incoming() {
        match stream {
            Ok(stream) => handle_client(stream, "TCP"),
            Err(e) => eprintln!("Error al aceptar la conexión: {e}"),
        }
    }
}
