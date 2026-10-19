# Lab09 - SpeedTest con Sockets RAW

#### Por @GabGP

Este proyecto mide la **latencia** y la **velocidad de subida** hacia un servidor implementando TCP "a puro pulmón": el cliente abre un socket RAW (`AF_INET`, `SOCK_RAW`, `IPPROTO_TCP`) con `libc`, construye a mano cada encabezado TCP de 20 bytes (puertos, `SEQ`, `ACK`, *data offset*, banderas, ventana y *checksum* con el pseudo-encabezado IPv4) y recorre toda la sesión TCP sin ayuda del kernel: *3-way handshake*, 10 envíos de un archivo de 1 KB y cierre con `FIN`.

Funciones:

- Medir la latencia con Perl `Net::Ping`, invocado desde Rust con `std::process::Command`.
- Abrir la conexión con `SYN` → `SYN-ACK` → `ACK` y medir el RTT inicial.
- Enviar 10 veces `payload.bin` (1000 bytes) con `PSH | ACK`, esperando el `ACK` del servidor antes de cada envío siguiente.
- Cerrar la conexión con `FIN | ACK` → `ACK` + `FIN` del servidor → `ACK` final.
- Calcular la velocidad de subida con la fórmula del laboratorio e imprimirla en bps y Kbps.
- Registrar cada segmento con el formato de log CLF del curso.

## 0. ¿Cómo ejecutarlo?

```bash
make deps      # verifica que estén instaladas las herramientas necesarias
make           # genera payload.bin (1000 bytes) y compila en modo release
make demo      # servidor + cliente en 127.0.0.1 (pide la contraseña de sudo)
```

`make help` muestra todos los objetivos disponibles.

## 1. Requisitos previos

| Herramienta | Para qué se usa | Instalación (Ubuntu/Debian) |
|---|---|---|
| Linux | El cliente RAW depende del comportamiento de Linux (ver nota) | — |
| Rust ≥ 1.88 y `cargo` | Compilar el proyecto (edición 2024) | `curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \| sh` |
| Perl con `Net::Ping` | Etapa de latencia | Viene con Perl (`perl-modules`); se verifica con `make deps` |
| `iptables` | Regla que evita los `RST` automáticos del kernel | `sudo apt install iptables` |
| `sudo` | El socket RAW, el ping ICMP e iptables requieren superusuario | — |
| `make`, `zip` | Automatización y entrega | `sudo apt install make zip` |
| Wireshark o `tcpdump` (opcional) | Verificar el flujo de banderas | `sudo apt install wireshark tcpdump` |

> **Nota sobre macOS:** los kernels BSD (macOS incluido) nunca entregan segmentos TCP a un socket RAW, así que el cliente no podría leer el `SYN-ACK`. El proyecto compila en macOS, pero el cliente se niega a ejecutarse fuera de Linux: use una máquina virtual con Linux.

> **Importante:** no ejecute `cargo` con `sudo` (normalmente falla con `sudo: cargo: command not found`). El Makefile compila con su usuario y usa `sudo` solo para ejecutar los binarios ya compilados.

## 2. Estructura del proyecto

```
lab09/
├── Cargo.toml          # paquete "speedtest" (edición 2024), dependencias: libc y chrono
├── Makefile            # payload, compilación, iptables, ejecución con sudo, captura y zip
├── README.md
├── payload.bin         # archivo de prueba de 1000 bytes (lo genera `make payload`)
└── src/
    ├── lib.rs          # constantes (1000 bytes, 10 envíos), log CLF y argumentos KEY VALUE
    ├── tcp.rs          # encabezado TCP de 20 bytes, banderas y checksum con pseudo-encabezado
    ├── ipv4.rs         # lectura del encabezado IPv4 de los paquetes recibidos
    ├── raw.rs          # socket RAW con libc: socket, bind, sendto, poll y recv
    ├── session.rs      # handshake, envíos con SEQ/ACK, cierre con FIN y retransmisiones
    ├── ping.rs         # etapa de latencia: ejecuta Perl y extrae los milisegundos
    ├── ping.pl         # programa Perl con Net::Ping (queda incrustado en el binario)
    └── bin/
        ├── client.rs   # cliente: etapa 1 (latencia), etapa 2 (subida) y etapa 3 (resultados)
        └── server.rs   # servidor de apoyo con std::net::TcpListener
```

## 3. Compilar y ejecutar con el Makefile

### 3.1 Compilar

```bash
make
```

Genera `payload.bin` con 1000 bytes aleatorios de `/dev/urandom` (solo si no existe) y ejecuta `cargo build --release`. Los binarios quedan en `target/release/client` y `target/release/server`.

### 3.2 Prueba en una sola máquina

```bash
make demo
```

1. Pide la contraseña de `sudo` una sola vez.
2. Inicia el servidor con `sudo` en segundo plano (su salida va a `server.log`).
3. Agrega la regla de `iptables` para el puerto origen del cliente.
4. Ejecuta el cliente con `sudo` contra `127.0.0.1`.
5. Muestra el log del servidor, detiene el servidor y elimina la regla, aunque el cliente falle o se presione `Ctrl+C`.

### 3.3 Servidor y cliente en terminales separadas

```bash
# Terminal 1
make run-server

# Terminal 2
make run-client
```

`make run-client` agrega la regla de `iptables`, ejecuta el cliente con `sudo` y elimina la regla al terminar.

### 3.4 Servidor y cliente en máquinas distintas

```bash
# Máquina servidor (por ejemplo 192.168.1.5); si usa ufw, abra el puerto: sudo ufw allow 8080/tcp
make run-server PORT=8080

# Máquina cliente
make run-client SERVER=192.168.1.5 PORT=8080
```

### 3.5 Variables del Makefile

| Variable | Valor por defecto | Significado |
|---|---|---|
| `SERVER` | `127.0.0.1` | IP (o nombre) del servidor |
| `PORT` | `8080` | Puerto en el que escucha el servidor |
| `SPORT` | `30000` | Puerto origen del cliente; es el puerto de la regla de `iptables` |
| `PING` | `icmp` | Protocolo de `Net::Ping`: `icmp`, `tcp` o `udp` |
| `PINGS` | `4` | Cantidad de pings de la etapa de latencia |
| `PAYLOAD` | `payload.bin` | Archivo de 1000 bytes que se envía |
| `IFACE` | `lo` | Interfaz donde captura `make capture` |
| `PCAP` | `speedtest.pcap` | Archivo de captura de `make capture` |
| `ZIP` | `lab09_speedtest.zip` | Nombre del archivo de entrega |

### 3.6 Ejecutar los binarios sin el Makefile

Como en el laboratorio 1, los argumentos van en formato **KEY VALUE** sin importar el orden, y todos tienen un valor por defecto:

```bash
sudo iptables -A OUTPUT -p tcp --tcp-flags RST RST --sport 30000 -j DROP
sudo ./target/release/server port 8080
sudo ./target/release/client server 127.0.0.1 port 8080 sport 30000 file payload.bin ping icmp pings 4
sudo iptables -D OUTPUT -p tcp --tcp-flags RST RST --sport 30000 -j DROP
```

| Programa | Argumento | Por defecto | Descripción |
|---|---|---|---|
| `client` | `server` | `127.0.0.1` | IP o nombre del servidor |
| `client` | `port` | `8080` | Puerto del servidor |
| `client` | `sport` | `30000` | Puerto origen del cliente (debe coincidir con la regla de `iptables`) |
| `client` | `file` | `payload.bin` | Archivo de prueba; debe medir exactamente 1000 bytes |
| `client` | `ping` | `icmp` | Protocolo de `Net::Ping` (`icmp`, `tcp` o `udp`) |
| `client` | `pings` | `4` | Cantidad de pings (de 1 a 100) |
| `server` | `server` | `0.0.0.0` | IP local en la que escucha |
| `server` | `port` | `8080` | Puerto en el que escucha |

El servidor no necesita privilegios para escuchar en un puerto mayor a 1023, pero el Makefile lo ejecuta con `sudo` como pide el laboratorio.

## 4. ¿Por qué la regla de `iptables`?

El kernel de Linux no sabe que el cliente maneja el puerto `30000` por su cuenta. Cuando llega el `SYN-ACK` del servidor, el kernel busca un socket TCP dueño de ese puerto, no lo encuentra y responde automáticamente con un `RST`, que destruye la conexión en el servidor antes de que el cliente envíe su `ACK`. La regla descarta esos `RST` salientes:

```bash
sudo iptables -A OUTPUT -p tcp --tcp-flags RST RST --sport 30000 -j DROP   # make rst-block
sudo iptables -D OUTPUT -p tcp --tcp-flags RST RST --sport 30000 -j DROP   # make rst-unblock
```

- `-A OUTPUT`: se agrega a la cadena de paquetes que salen de esta máquina.
- `--tcp-flags RST RST`: solo afecta a los segmentos con la bandera `RST` activa.
- `--sport 30000`: solo los que salen del puerto origen del cliente; el resto del tráfico no se toca.

`make run-client`, `make demo` y `make capture` agregan la regla antes del cliente y la eliminan al terminar. Si cambia `SPORT`, la regla usa el nuevo puerto automáticamente.

## 5. ¿Cómo funciona?

### 5.1 Etapa 1: latencia con Perl `Net::Ping`

El cliente ejecuta, mediante `std::process::Command`:

```
perl -e <contenido de src/ping.pl> <ip del servidor> <protocolo> <cantidad> <puerto>
```

`src/ping.pl` usa `Net::Ping` con `hires()` (resolución de microsegundos) y escribe una línea por ping en la salida estándar:

```
ping 1 127.0.0.1 0.071 ms
ping 2 127.0.0.1 timeout
```

Rust captura esa salida, extrae el número que está antes de `ms` en cada línea y calcula el promedio; ese promedio es la **latencia inicial** que aparece en los resultados.

- `icmp` (por defecto) es el ping clásico; requiere superusuario, que ya se tiene porque el cliente corre con `sudo`.
- `tcp` mide cuánto tarda un `connect()` al puerto del servidor. El servidor registra esas conexiones como "conexión cerrada sin datos".
- Si Perl o `Net::Ping` fallan, el cliente muestra una advertencia y continúa con la etapa 2.

### 5.2 Etapa 2: TCP a mano sobre un socket RAW

#### El socket RAW

`src/raw.rs` llama directamente a `libc`: `socket(AF_INET, SOCK_RAW, IPPROTO_TCP)`, `bind`, `sendto`, `poll` y `recv`.

- **Envío:** el cliente escribe el encabezado TCP + los datos. Como no se activa `IP_HDRINCL`, el kernel agrega el encabezado IPv4.
- **Recepción:** el kernel entrega al socket una copia de los paquetes TCP que llegan a la máquina, **con** su encabezado IPv4. `src/ipv4.rs` salta ese encabezado (IHL × 4 bytes) y `src/session.rs` se queda solo con los segmentos que vienen de la IP y el puerto del servidor hacia la IP y el puerto del cliente.
- **IP origen:** se obtiene preguntándole al kernel qué dirección usaría para llegar al servidor (un `connect()` UDP, que no envía nada). El socket RAW se enlaza a esa IP, que también se usa en el pseudo-encabezado del checksum.

#### El encabezado TCP de 20 bytes (`src/tcp.rs`)

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Source Port          |       Destination Port        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Sequence Number                        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                    Acknowledgment Number                      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Data |       |C|E|U|A|P|R|S|F|                               |
| Offset| Rsrvd |W|C|R|C|S|S|Y|I|            Window             |
|       |       |R|E|G|K|H|T|N|N|                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Checksum            |         Urgent Pointer        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Campo | Bytes | Valor que pone el cliente |
|---|---|---|
| Puerto origen | 0–1 | `SPORT` (30000) |
| Puerto destino | 2–3 | `PORT` (8080) |
| Número de secuencia (`SEQ`) | 4–7 | ISN aleatorio; avanza 1 con el `SYN`, 1000 por envío y 1 con el `FIN` |
| Número de confirmación (`ACK`) | 8–11 | ISN del servidor + 1 (y + 1 más tras su `FIN`) |
| *Data offset* | 12 (4 bits altos) | `5` → 5 × 4 = 20 bytes, sin opciones |
| Banderas | 13 | `SYN`, `ACK`, `PSH \| ACK` o `FIN \| ACK` |
| Ventana | 14–15 | 64240 |
| Checksum | 16–17 | Calculado como se explica abajo |
| Puntero urgente | 18–19 | 0 |

Todos los campos se escriben en orden de red (*big endian*) con `to_be_bytes()`.

#### El checksum con pseudo-encabezado IPv4

El checksum cubre un pseudo-encabezado de 12 bytes que nunca se transmite, seguido del segmento TCP completo (encabezado con el checksum en 0 + datos):

```
+--------+--------+--------+--------+
|         IP origen (4 bytes)       |
+--------+--------+--------+--------+
|         IP destino (4 bytes)      |
+--------+--------+--------+--------+
|  cero  | proto=6|  longitud TCP   |
+--------+--------+--------+--------+
```

Se suman todas las palabras de 16 bits, se "doblan" los acarreos sobre los 16 bits bajos y se complementa el resultado (RFC 1071):

```rust
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
```

El receptor repite la misma cuenta sobre el segmento con el checksum incluido y debe obtener 0. Las pruebas verifican un valor calculado a mano (`0x0c2e` para un `SYN` de ejemplo) y que cualquier byte alterado se detecta.

#### La sesión completa

Con *x* = ISN del cliente e *y* = ISN del servidor:

```
Cliente (30000)                                          Servidor (8080)
 SYN      Seq=x                         ------------->
                                        <-------------   SYN, ACK  Seq=y Ack=x+1
 ACK      Seq=x+1     Ack=y+1           ------------->
 PSH,ACK  Seq=x+1     Ack=y+1  Len=1000 ------------->   envío 1
                                        <-------------   ACK  Ack=x+1001
   ...                                                   envíos 2 a 9
 PSH,ACK  Seq=x+9001  Ack=y+1  Len=1000 ------------->   envío 10
                                        <-------------   ACK  Ack=x+10001
 FIN,ACK  Seq=x+10001 Ack=y+1           ------------->
                                        <-------------   ACK  Ack=x+10002
                                        <-------------   FIN, ACK  Seq=y+1 Ack=x+10002
 ACK      Seq=x+10002 Ack=y+2           ------------->
```

1. **Conexión (*3-way handshake*):** se envía `SYN` con un ISN aleatorio leído de `/dev/urandom`. El cliente filtra el `SYN-ACK` cuyo `ACK` sea ISN + 1, extrae el `SEQ` del servidor (su ISN), responde con `ACK` y reporta como **RTT inicial** el tiempo entre el envío del `SYN` y la llegada del `SYN-ACK`.
2. **Transmisión:** lee `payload.bin` (exactamente 1000 bytes) y lo envía 10 veces con `PSH | ACK` (*stop-and-wait*): antes de cada envío siguiente espera el `ACK` cuyo número sea `SEQ + 1000`. Después actualiza `SEQ` (+1000) y el `ACK` (siguiente byte esperado del servidor). El tiempo de cada envío (RTT + tiempo de transmisión) se guarda en el arreglo `[f64; 10]`.
3. **Cierre:** envía `FIN | ACK`, que consume un número de secuencia. El servidor responde con `ACK` y luego `FIN | ACK`, o con un único `FIN | ACK`; el cliente acepta ambas formas y envía el `ACK` final (`SEQ` del servidor + 1).

Detalles de robustez:

- Si no llega respuesta, el segmento se retransmite con un RTO inicial de 1 s que se duplica en cada intento (RFC 6298), hasta 4 transmisiones.
- Un `RST` solo se acepta si es válido según RFC 9293: antes del handshake debe confirmar nuestro `SYN`; después, su `SEQ` debe ser exactamente el esperado.
- Los números de secuencia se comparan con aritmética módulo 2³², así que el ISN puede estar cerca de 4294967295 sin problemas.
- Los logs de los segmentos recibidos se imprimen **después** de detener el cronómetro, para que escribir en la consola no infle las mediciones.

### 5.3 Etapa 3: cálculo de la velocidad

Se aplica la fórmula del laboratorio con el archivo de 1 KB (1000 × 8 bits) y el promedio de los 10 tiempos en segundos:

$$\text{Velocidad} = \frac{1000 \times 8}{2 \times RTT_{\text{promedio}}}$$

Ejemplo: si el RTT promedio es 0.083 ms = 0.000083 s,

```
Velocidad = 8000 bits / (2 × 0.000083 s) = 48 192 771.08 bps = 48 192.77 Kbps
```

(1 Kbps = 1000 bps).

### 5.4 Servidor de apoyo (`src/bin/server.rs`)

- Escucha con `std::net::TcpListener`. Aquí el kernel hace todo el trabajo TCP; ese es el contraste con el cliente.
- Lee bloques de 1000 bytes. TCP es un flujo de bytes, así que un bloque puede llegar en varias lecturas y el servidor las junta. Registra cada envío con su hash FNV-1a, que debe coincidir con el `fnv1a` que imprime el cliente para `payload.bin`.
- En Linux activa `TCP_QUICKACK` antes de cada lectura para que el kernel confirme cada segmento de inmediato. Sin eso, el *delayed ACK* podría sumar hasta ~40 ms a la medición.
- Cuando recibe el `FIN` (lectura de 0 bytes), cierra el socket y el kernel envía su propio `FIN`. Atiende clientes uno tras otro hasta que se presione `Ctrl+C`; una conexión sin datos durante 30 s se cierra.

### 5.5 Formato de los logs

Se mantiene el formato CLF del laboratorio 1:

```
[s] host socketType [dateTime] protocol: description
```

- `[s]`: `<` sale y `>` entra, desde el punto de vista del programa que escribe el log.
- `host`: IP del cliente o del servidor; `socketType`: `client` o `server`; `dateTime`: `yyyy/mm/dd hh:mm:ss`.
- En el cliente, la descripción sigue el estilo de Wireshark (`puerto → puerto [banderas] Seq Ack Win Len`). Usa **números de secuencia relativos** (el ISN cuenta como 0), y las líneas `SYN` muestran además el ISN real.

## 6. Ejemplo de ejecución

Salida **ilustrativa** de `make demo` (los ISN, tiempos y hashes cambian en cada ejecución):

```
==================================================================
 SpeedTest: TCP sobre sockets RAW
 Cliente : 127.0.0.1:30000    Servidor: 127.0.0.1:8080
 Archivo : payload.bin (1000 bytes, fnv1a=0x27814480)
==================================================================

[Etapa 1] Latencia con Perl Net::Ping (protocolo icmp, 4 pings)
   ping 1 127.0.0.1 0.071 ms
   ping 2 127.0.0.1 0.039 ms
   ping 3 127.0.0.1 0.036 ms
   ping 4 127.0.0.1 0.038 ms
   Latencia promedio: 0.046 ms (4/4 respuestas)

[Etapa 2] Velocidad de subida con socket RAW (AF_INET, SOCK_RAW, IPPROTO_TCP)
-- Conexión (3-way handshake) --
< 127.0.0.1 client [2026/10/18 21:30:12] TCP: 30000 → 8080 [SYN] Seq=0 Win=64240 Len=0 (ISN=2183942570)
> 127.0.0.1 server [2026/10/18 21:30:12] TCP: 8080 → 30000 [SYN, ACK] Seq=0 Ack=1 Win=65495 Len=0 MSS=65495 (ISN=3051278310)
< 127.0.0.1 client [2026/10/18 21:30:12] TCP: 30000 → 8080 [ACK] Seq=1 Ack=1 Win=64240 Len=0
   RTT inicial de la conexión (SYN -> SYN-ACK): 0.094 ms
-- Transmisión: 10 envíos de 1000 bytes (PSH, ACK) --
< 127.0.0.1 client [2026/10/18 21:30:12] TCP: 30000 → 8080 [PSH, ACK] Seq=1 Ack=1 Win=64240 Len=1000
> 127.0.0.1 server [2026/10/18 21:30:12] TCP: 8080 → 30000 [ACK] Seq=1 Ack=1001 Win=65495 Len=0
   envío  1/10: 0.142 ms
   ...
< 127.0.0.1 client [2026/10/18 21:30:12] TCP: 30000 → 8080 [PSH, ACK] Seq=9001 Ack=1 Win=64240 Len=1000
> 127.0.0.1 server [2026/10/18 21:30:12] TCP: 8080 → 30000 [ACK] Seq=1 Ack=10001 Win=65495 Len=0
   envío 10/10: 0.075 ms
-- Cierre de la conexión --
< 127.0.0.1 client [2026/10/18 21:30:12] TCP: 30000 → 8080 [FIN, ACK] Seq=10001 Ack=1 Win=64240 Len=0
> 127.0.0.1 server [2026/10/18 21:30:12] TCP: 8080 → 30000 [ACK] Seq=1 Ack=10002 Win=65495 Len=0
> 127.0.0.1 server [2026/10/18 21:30:12] TCP: 8080 → 30000 [FIN, ACK] Seq=1 Ack=10002 Win=65495 Len=0
< 127.0.0.1 client [2026/10/18 21:30:12] TCP: 30000 → 8080 [ACK] Seq=10002 Ack=2 Win=64240 Len=0

[Etapa 3] Resultados
==================================================================
 Latencia inicial (Net::Ping)   : 0.046 ms
 RTT inicial (SYN -> SYN-ACK)   : 0.094 ms
 Tiempo por envío (RTT + tiempo de transmisión):
   envío  1:      0.142 ms
   envío  2:      0.081 ms
   ...
   envío 10:      0.075 ms
 RTT promedio                   : 0.083 ms = 0.000083 s
 Velocidad = (1000 × 8) / (2 × RTT promedio) = 8000 bits / (2 × 0.000083 s)
 Velocidad de subida            : 48192771.08 bps = 48192.77 Kbps
==================================================================
```

Log del servidor (`server.log`):

```
Servidor SpeedTest escuchando en 0.0.0.0:8080 (Ctrl+C para terminar)
> 127.0.0.1 client [2026/10/18 21:30:12] TCP: conexión establecida desde el puerto 30000 (SYN, SYN-ACK, ACK)
> 127.0.0.1 client [2026/10/18 21:30:12] TCP: envío #1: 1000 bytes recibidos (fnv1a=0x27814480)
...
> 127.0.0.1 client [2026/10/18 21:30:12] TCP: envío #10: 1000 bytes recibidos (fnv1a=0x27814480)
> 127.0.0.1 client [2026/10/18 21:30:12] TCP: FIN recibido: el cliente terminó de enviar
< 127.0.0.1 server [2026/10/18 21:30:12] TCP: FIN enviado: conexión cerrada (10 envíos de 1000 bytes, 10000 bytes en total)
```

## 7. Verificar el flujo de banderas en Wireshark

1. Abra Wireshark con permisos de captura (`sudo wireshark`, o agregue su usuario al grupo `wireshark`).
2. Elija la interfaz: **Loopback: lo** si cliente y servidor están en la misma máquina, o la interfaz de red (por ejemplo `eth0`/`wlan0`) si están en máquinas distintas.
3. Escriba el filtro de visualización `tcp.port == 8080` y empiece a capturar.
4. En una terminal ejecute `make demo` (o `make run-client`).
5. Detenga la captura. Deben aparecer, en orden:

| # | Origen → destino | Banderas en Wireshark | Qué muestra |
|---|---|---|---|
| 1 | 30000 → 8080 | `[SYN]` `Seq=0` | Inicio del handshake con el ISN del cliente |
| 2 | 8080 → 30000 | `[SYN, ACK]` `Seq=0 Ack=1` | El servidor confirma y envía su ISN |
| 3 | 30000 → 8080 | `[ACK]` `Seq=1 Ack=1` | Fin del handshake |
| 4–23 | alternados | 10 × (`[PSH, ACK]` `Len=1000` → `[ACK]`) | Cada `Ack` avanza 1000: 1001, 2001, …, 10001 |
| 24 | 30000 → 8080 | `[FIN, ACK]` `Seq=10001` | El cliente cierra |
| 25–26 | 8080 → 30000 | `[ACK]` `Ack=10002` y `[FIN, ACK]` (o un solo `[FIN, ACK]`) | El servidor confirma y cierra |
| final | 30000 → 8080 | `[ACK]` `Seq=10002 Ack=2` | `ACK` final del cliente |

Filtros útiles:

| Filtro | Muestra |
|---|---|
| `tcp.flags.syn == 1` | `SYN` y `SYN-ACK` |
| `tcp.flags.push == 1 && tcp.len == 1000` | Los 10 envíos de datos |
| `tcp.flags.fin == 1` | El cierre |
| `tcp.flags.reset == 1` | Debe estar **vacío**: la regla de `iptables` funciona |

- **Diagrama de la sesión:** *Statistics → Flow Graph*, marque *Limit to display filter* y elija *Flow type: TCP Flows*.
- **`SEQ` y `ACK` reales:** Wireshark muestra números relativos, igual que el log del cliente. El ISN real aparece en el detalle del paquete como *Sequence Number (raw)*, y coincide con el `(ISN=...)` del log. También puede desactivar *Edit → Preferences → Protocols → TCP → Relative sequence numbers*.
- **Checksum:** active *Edit → Preferences → Protocols → TCP → Validate the TCP checksum if possible*. Los segmentos del cliente deben verse como `[correct]`. En `lo`, los segmentos que genera el kernel del servidor pueden verse como "incorrect" por el *checksum offload*: el kernel deja ese cálculo a la tarjeta de red, y eso es normal.
- **Sin interfaz gráfica:** `make capture` hace lo mismo que `make demo` mientras graba los segmentos con `tcpdump` en `speedtest.pcap`, y al final los lista con `tcpdump -nn -r speedtest.pcap`. Luego puede abrir el archivo con `wireshark speedtest.pcap`. En `tcpdump`, `[S]` es `SYN`, `[S.]` `SYN-ACK`, `[.]` `ACK`, `[P.]` `PSH-ACK`, `[F.]` `FIN-ACK` y `[R]` `RST`.

## 8. Pruebas automáticas

```bash
make test      # o: cargo test
```

No requieren `sudo`. Cubren:

- El checksum (ejemplo del RFC 1071, un valor calculado a mano, detección de corrupción y del cambio de IP).
- La disposición de cada campo del encabezado en orden de red y la lectura de opciones (MSS).
- La lectura de paquetes IPv4 y la comparación de números de secuencia con desborde.
- La extracción de la latencia de la salida de Perl, incluida una ejecución real de `Net::Ping` por TCP.
- La sesión completa contra un servidor simulado: handshake, 10 envíos, los dos tipos de cierre, retransmisión de un `SYN` perdido, conexión rechazada con `RST`, abandono tras 4 intentos y paquetes ajenos que se ignoran.

## 9. Empaquetar la entrega (.zip)

```bash
make zip
```

Crea `lab09_speedtest.zip` con la carpeta `lab09/` completa (código, `Cargo.toml`, `Cargo.lock`, `Makefile`, `README.md` y `payload.bin`). Excluye `target/` y los archivos generados al probar (`*.pcap`, `*.log`). Para revisar el contenido:

```bash
unzip -l lab09_speedtest.zip
```

Sin el Makefile, desde la carpeta que contiene `lab09/`:

```bash
zip -r lab09_speedtest.zip lab09 -x "lab09/target/*" "*.pcap" "*.log"
```

## 10. Solución de problemas

| Síntoma | Causa probable | Solución |
|---|---|---|
| `se requieren permisos de superusuario` | El socket RAW necesita root | Use `make run-client` / `make demo`, o `sudo ./target/release/client ...` |
| `el servidor respondió RST` durante el handshake | No hay servidor escuchando en ese puerto | Inicie el servidor (`make run-server`) y revise `PORT` |
| `la conexión fue reiniciada (RST)` después del handshake | El kernel local envió un `RST`: falta la regla o `SPORT` no coincide | `make rst-block SPORT=...` con el mismo puerto que usa el cliente |
| `no llegó el SYN-ACK después de 4 intentos` | IP incorrecta o un firewall descarta los paquetes | Revise `SERVER`; en el servidor, `sudo ufw allow 8080/tcp` |
| Falla con un servidor remoto y `ufw` o `firewalld` activos en el cliente | El seguimiento de conexiones (*conntrack*) puede marcar como inválidos los segmentos del servidor | Desactive temporalmente el firewall del cliente durante la prueba |
| `icmp ping requires root privilege` | `Net::Ping` con ICMP sin `sudo` | Ejecute el cliente con `sudo` o use `PING=tcp` |
| `sudo: cargo: command not found` | `cargo` se ejecutó con `sudo` | Compile sin `sudo` (`make build`); el Makefile solo usa `sudo` para los binarios |
| `payload.bin mide N bytes` | El archivo no mide exactamente 1000 bytes | `rm payload.bin && make payload` |
| `iptables: command not found` | No está instalado | `sudo apt install iptables` |
| El cliente se niega a correr en macOS | macOS no entrega segmentos TCP a sockets RAW | Use Linux (por ejemplo, una máquina virtual) |
