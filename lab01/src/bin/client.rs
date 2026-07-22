use chrono::Local;
use std::env;
use std::io::{self, Read, Write};
use std::net::{TcpStream, UdpSocket};
use std::process;

fn get_datetime() -> String {
    Local::now().format("%Y/%m/%d %H:%M:%S").to_string()
}

fn log_message(direction: &str, host: &str, socket_type: &str, protocol: &str, msg: &str) {
    let datetime = get_datetime();
    println!(
        "{} {} {} [{}] {}: {}",
        direction, host, socket_type, datetime, protocol, msg
    );
}

// Function to handle TCP connections
fn handle_tcp(server_addr: &str, protocol: &str) {
    let mut stream = TcpStream::connect(&server_addr).expect("Could not connect to server");

    // Extract both the client (local) IP and the server (peer) IP
    let client_ip = stream.local_addr().unwrap().ip().to_string();
    let server_ip = stream.peer_addr().unwrap().ip().to_string();

    loop {
        // Request
        let mut request = String::new();
        print!("Ingrese la operación a realizar: ");
        io::stdout().flush().unwrap();
        io::stdin().read_line(&mut request).unwrap();
        let request = request.trim();

        if request.is_empty() {
            continue;
        }

        match stream.write_all(request.as_bytes()) {
            Ok(_) => log_message("<", &client_ip, "client", &protocol, request),
            Err(e) => eprintln!("Failed to send request: {}", e),
        }

        // Response
        let mut buffer = [0; 512];
        match stream.read(&mut buffer) {
            Ok(size) if size > 0 => {
                let response = String::from_utf8_lossy(&buffer[..size]).trim().to_string();
                log_message(">", &server_ip, "server", &protocol, &response);

                if request == "EXIT" {
                    process::exit(1);
                }
            }
            _ => {
                println!("Server disconnected or error.");
                process::exit(1);
            }
        }
    }
}

// Function to handle UDP connections
fn handle_udp(server_addr: &str, protocol: &str) {
    let socket = UdpSocket::bind("0.0.0.0:0").expect("Could not bind to address");

    // Connect the UDP socket to the server
    socket
        .connect(&server_addr)
        .expect("Could not connect UDP socket");

    // Extract both the client (local) IP and the server (peer) IP
    let client_ip = socket.local_addr().unwrap().ip().to_string();
    let server_ip = socket.peer_addr().unwrap().ip().to_string();

    loop {
        // Request
        let mut request = String::new();
        print!("Ingrese la operación a realizar: ");
        io::stdout().flush().unwrap();
        io::stdin().read_line(&mut request).unwrap();
        let request = request.trim();

        if request.is_empty() {
            continue;
        }

        match socket.send_to(request.as_bytes(), &server_addr) {
            Ok(_) => log_message("<", &client_ip, "client", &protocol, request),
            Err(e) => eprintln!("Failed to send request: {}", e),
        }

        // Response
        let mut buffer = [0; 512];
        match socket.recv_from(&mut buffer) {
            Ok((size, _)) => {
                let response = String::from_utf8_lossy(&buffer[..size]).trim().to_string();
                log_message(">", &server_ip, "server", &protocol, &response);

                if request == "EXIT" {
                    process::exit(1);
                }
            }
            Err(e) => eprintln!("Failed to receive response: {}", e),
        }
    }
}

fn main() {
    // Default arguments
    let mut protocol = "TCP".to_string();
    let mut server = "127.0.0.1".to_string();
    let mut port = "8080".to_string();

    // Parse KEY VALUE arguments regardless of order
    let args: Vec<String> = env::args().collect();
    let mut i = 1;
    while i < args.len() {
        match args[i].as_str() {
            "protocol" => {
                if i + 1 < args.len() {
                    protocol = args[i + 1].to_uppercase();
                }
            }
            "server" => {
                if i + 1 < args.len() {
                    server = args[i + 1].clone();
                }
            }
            "port" => {
                if i + 1 < args.len() {
                    port = args[i + 1].clone();
                }
            }
            _ => {}
        }
        i += 2;
    }

    let server_addr = format!("{}:{}", server, port);

    if protocol == "TCP" {
        handle_tcp(&server_addr, &protocol);
    } else if protocol == "UDP" {
        handle_udp(&server_addr, &protocol);
    } else {
        eprintln!("Invalid protocol. Use TCP or UDP.");
        process::exit(1);
    }
}
