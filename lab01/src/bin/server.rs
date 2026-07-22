use chrono::Local;
use std::env;
use std::io::{Read, Write};
use std::net::{TcpListener, UdpSocket};
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

fn calculate(expr: &str) -> Result<String, &str> {
    let expr = expr.trim();
    if expr == "EXIT" {
        return Ok("EXIT".to_string());
    }

    if let Some((pos, _)) = expr
        .char_indices()
        .skip(1)
        .find(|&(_, c)| ['+', '-', '*', '/', '%'].contains(&c))
    {
        let op = &expr[pos..pos + 1];
        let left: f64 = expr[..pos].trim().parse().map_err(|_| "Invalid number")?;
        let right: f64 = expr[pos + 1..]
            .trim()
            .parse()
            .map_err(|_| "Invalid number")?;

        let res = match op {
            "+" => left + right,
            "-" => left - right,
            "*" => left * right,
            "/" => {
                if right == 0.0 {
                    return Err("Division by zero");
                } else {
                    left / right
                }
            }
            "%" => left % right,
            _ => return Err("Unknown operator"),
        };
        Ok(res.to_string())
    } else {
        Err("No operator found or invalid format")
    }
}

// Function to handle TCP connections
fn handle_tcp(bind_addr: &str, protocol: &str) {
    let listener = TcpListener::bind(&bind_addr).expect("Could not bind to address");

    for stream in listener.incoming() {
        let mut stream = stream.expect("Failed to accept connection");

        // Extract both the client (peer) IP and the server (local) IP
        let client_ip = stream.peer_addr().unwrap().ip().to_string();
        let server_ip = stream.local_addr().unwrap().ip().to_string();

        loop {
            // Request
            let mut buffer = [0; 512];
            match stream.read(&mut buffer) {
                Ok(size) if size > 0 => {
                    let request = String::from_utf8_lossy(&buffer[..size]).trim().to_string();
                    log_message(">", &client_ip, "client", &protocol, &request);

                    // Response
                    let response = match calculate(&request) {
                        Ok(res) => res,
                        Err(e) => e.to_string(),
                    };

                    match stream.write_all(response.as_bytes()) {
                        Ok(_) => log_message("<", &server_ip, "server", &protocol, &response),
                        Err(e) => eprintln!("Failed to send response: {}", e),
                    }

                    if request == "EXIT" {
                        process::exit(0);
                    }
                }
                _ => {
                    println!("Client disconnected or error.");
                    process::exit(0);
                }
            }
        }
    }
}

// Function to handle UDP connections
fn handle_udp(bind_addr: &str, protocol: &str) {
    let socket = UdpSocket::bind(&bind_addr).expect("Could not bind to address");

    // Extract the server (local) IP
    let server_ip = socket.local_addr().unwrap().ip().to_string();

    loop {
        // Request
        let mut buffer = [0; 512];
        match socket.recv_from(&mut buffer) {
            Ok((size, src)) => {
                let request = String::from_utf8_lossy(&buffer[..size]).trim().to_string();
                let client_ip = src.ip().to_string();

                log_message(">", &client_ip, "client", &protocol, &request);

                // Response
                let response = match calculate(&request) {
                    Ok(res) => res,
                    Err(e) => e.to_string(),
                };

                match socket.send_to(response.as_bytes(), &src) {
                    Ok(_) => log_message("<", &server_ip, "server", &protocol, &response),
                    Err(e) => eprintln!("Failed to send response: {}", e),
                }

                if request == "EXIT" {
                    process::exit(0);
                }
            }
            Err(e) => eprintln!("Failed to receive request: {}", e),
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

    let bind_addr = format!("{}:{}", server, port);

    if protocol == "TCP" {
        handle_tcp(&bind_addr, &protocol);
    } else if protocol == "UDP" {
        handle_udp(&bind_addr, &protocol);
    } else {
        eprintln!("Invalid protocol. Use TCP or UDP.");
        process::exit(0);
    }
}
