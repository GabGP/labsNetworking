# Lab01 - Sockets TCP Y UDP

#### By @GabGP and @guillermo-martinez42

This project aims to demonstrate the usage of network sockets and multi-protocol networking by simulating a client/server mathematical calculator.

Functions:

- Simulate a client requesting mathematical operations and a server processing them.
- Implement communication using both TCP and UDP protocols.
- Evaluate basic string-based mathematical expressions and handle errors like division by zero.
- Print formatted logs of the system's requests and responses.

## 0. How to Run?

**Running the Server:**
```
cargo run --bin server
```

**Running the Client:**
```
cargo run --bin client
```

Additional arguments can be added like this:

```
cargo run --bin server -- server 172.0.0.1 protocol UDP port 3333
```

## 1. Client/Server Calculator System

### Client Execution (client.rs)

The client application is responsible for establishing a connection to the server and gathering user input.

The behavior of the client includes:

- **Prompting:** The client repeatedly asks the user to input a mathematical operation using the prompt "Ingrese la operación a realizar: ".
- **Sending:** Depending on the active protocol, the client uses `stream.write_all()` for TCP or `socket.send_to()` for UDP to transmit the user's string.
- **Receiving:** The client waits for a response buffer of up to 512 bytes, parses it from UTF-8, and displays the server's answer before looping.

### Server Execution (server.rs)

The server application binds to the specified port and listens for incoming data.

The behavior of the server includes:

- **Listening:** The server utilizes `TcpListener::bind` or `UdpSocket::bind` to wait for incoming client traffic.
- **Calculating:** The calculate function parses the incoming string, finds the mathematical operator (+, -, *, /, %), splits the left and right operands, and returns the computed result.It includes error handling for invalid numbers, unknown operators, and division by zero.
- **Logging:** A `log_message` function is utilized across both programs to print actions using a standard Common Log Format (CLF). This includes the direction of traffic, client/server IPs, the protocol in use, and the exact timestamp.

## 2. Main Program

The main function in both the client and the server acts as the configuration entry point.

It parses command-line arguments in a KEY VALUE format, allowing the user to pass protocol, server, and port arguments regardless of their appearance order. 

Once the parameters are extracted, the main function routes execution to either handle_tcp or handle_udp based on the specified protocol.

Both programs will continue executing their respective loops until they receive the "EXIT" command, at which point they utilize `process::exit(1)` or `process::exit(0)` to terminate.