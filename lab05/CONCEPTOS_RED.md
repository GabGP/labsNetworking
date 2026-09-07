# Conceptos de red del Laboratorio — Cliente TCP con Raw Sockets

Documento de estudio que acompaña a [`README_Lab.md`](README_Lab.md). Aquí no se explica *qué se
modificó*, sino **qué conceptos de redes ejercita este laboratorio**, con los números y los bytes
reales calculados a mano.

---

## Parte I — Panorama: qué hace el laboratorio

### 1. La idea en una frase

Un programa en C que **habla TCP sin usar TCP del sistema operativo**: arma los headers IP y TCP
byte por byte, los mete en un socket crudo, y conversa con un servidor Java normal que cree estar
hablando con un cliente TCP común y corriente.

### 2. Por qué eso es interesante

Cuando un programa normal hace esto:

```c
connect(fd, ...);          // el kernel hace el handshake
send(fd, "Hola", 4);       // el kernel arma el segmento, calcula checksums, numera bytes
```

el sistema operativo hace todo el trabajo: elige el ISN, envía el SYN, espera el SYN+ACK, lleva
la cuenta de los bytes, retransmite lo perdido, controla la ventana. El programador nunca ve un
número de secuencia.

En este laboratorio **nosotros somos el stack TCP**. Cada uno de esos pasos aparece explícitamente
en el código, y por eso se pueden observar conceptos que normalmente están escondidos.

### 3. Las dos mitades del laboratorio

| Lado | Tecnología | Qué representa |
|------|-----------|----------------|
| `ClientTCP/` (C) | Raw socket, headers armados a mano | El "modo difícil": ver el protocolo por dentro |
| `ServerTCP.java` | `ServerSocket` / `Socket` de Java | El "modo normal": un servidor cualquiera de la vida real |

El servidor es intencionalmente aburrido: acepta una conexión, lee líneas, hace eco de cada una,
y cierra cuando la línea contiene `EXIT`. Su gracia está en que **no sabe** que del otro lado hay
un cliente artesanal. Si nuestro cliente se equivoca en un checksum, en un número de secuencia o
en un flag, el servidor simplemente no responde: es un juez imparcial de si implementamos TCP bien.

### 4. El recorrido completo de un mensaje

```
Usuario escribe "Hello World"
        │
        ▼
tcp_client.c        run_session()          lee la línea de stdin
        │
        ▼
tcp_session.c       tcp_session_send_message()   agrega '\n', decide flags PSH+ACK
        │
        ▼
tcp_session.c       build_packet()          reserva 4096 bytes y arma el paquete
        │           ├── build_tcp_header()   puertos, SEQ, ACK, flags, ventana
        │           ├── build_ip_header()    versión, TTL, protocolo, IPs
        │           ├── calculate_checksum()          → checksum IP
        │           └── calculate_tcp_checksum()      → checksum TCP + pseudo-header
        ▼
raw_socket.c        send_packet()           sendto() sobre un socket con IP_HDRINCL
        │
        ▼
   kernel → interfaz de red → kernel del servidor → JVM → readLine()
```

---

## Parte II — Conceptos, con ejemplos digitales

### 5. Modelo de capas: dónde está parado este código

| Capa | Quién la hace en este laboratorio |
|------|-----------------------------------|
| 7 Aplicación | El texto que escribe el usuario y el eco del servidor |
| 4 Transporte (TCP) | **Nosotros**, en `tcp_header.c` + `tcp_session.c` |
| 3 Red (IPv4) | **Nosotros**, en `ip_header.c` |
| 2 Enlace (Ethernet / loopback) | El kernel |
| 1 Física | El hardware (o nada, si es loopback) |

El raw socket con `IP_HDRINCL` es exactamente el punto de corte: le decimos al kernel
*"yo escribo desde la capa 3 hacia arriba, tú encárgate de la 2 hacia abajo"*.

### 6. Encapsulamiento: los bytes reales de un paquete

Para el mensaje `"EXIT OFF\n"` (9 bytes) el paquete completo mide **49 bytes**:

```
┌──────────────────────┬──────────────────────┬─────────────────┐
│  Header IPv4         │  Header TCP          │  Datos          │
│  20 bytes            │  20 bytes            │  9 bytes        │
└──────────────────────┴──────────────────────┴─────────────────┘
 0                    19 20                  39 40             48
```

Volcado hexadecimal de un paquete así (cliente `127.0.0.1:43127` → servidor `127.0.0.1:5001`):

```
        ┌ versión 4, IHL 5 (5×4 = 20 bytes)
        │  ┌ TOS
        │  │  ┌─── total length = 0x0031 = 49
        │  │  │     ┌─── id = 0xD431 = 54321
        │  │  │     │     ┌─── flags/frag = 0x4000 = Don't Fragment
        │  │  │     │     │     ┌ TTL 0x40 = 64
        │  │  │     │     │     │  ┌ protocolo 0x06 = TCP
        │  │  │     │     │     │  │  ┌─── checksum IP
0000   45 00 00 31 D4 31 40 00 40 06 68 93 7F 00 00 01
                                            └──────────┘ src 127.0.0.1
0010   7F 00 00 01 A8 77 13 89 6B 8B 45 A8 AC 19 92 45
       └──────────┘ dst 127.0.0.1
                    └────┘ src port 0xA877 = 43127
                          └────┘ dst port 0x1389 = 5001
                                └───────────┘ SEQ = 0x6B8B45A8
                                            └───────────┘ ACK = 0xAC199245
        ┌ data offset 5 (<<4 = 0x50), reservado 0
        │  ┌ flags 0x18 = PSH|ACK
        │  │  ┌─── ventana = 0x16D0 = 5840
        │  │  │     ┌─── checksum TCP
        │  │  │     │     ┌─── urgent pointer
0020   50 18 16 D0 XX XX 00 00 45 58 49 54 20 4F 46 46
                                └──────────────────────┘ "EXIT OFF"
0030   0A
       └┘ '\n'
```

Cada capa **antepone** su header a lo que le entrega la capa de arriba. Eso es encapsulamiento,
y aquí se ve literalmente como aritmética de punteros en `build_packet()`:

```c
ip_header_t  *iph     = (ip_header_t *)packet;                                   /* byte 0  */
tcp_header_t *tcph    = (tcp_header_t *)(packet + sizeof(ip_header_t));          /* byte 20 */
uint8_t      *payload = packet + sizeof(ip_header_t) + sizeof(tcp_header_t);     /* byte 40 */
```

### 7. Byte order (endianness): `htons` y `htonl`

Las redes usan **big-endian** ("network byte order"): el byte más significativo primero. Los
procesadores x86 y ARM usan **little-endian**. Sin conversión, los números salen al revés.

Ejemplo concreto con el puerto 5001:

| | Valor | Bytes en memoria (x86) |
|---|---|---|
| Sin `htons()` | `5001 = 0x1389` | `89 13` ← el servidor lee **0x8913 = 35091** |
| Con `htons()` | `0x8913` | `13 89` ← el servidor lee **0x1389 = 5001** ✓ |

De ahí que `tcp_header.c` haga:

```c
tcph->src_port = htons(src_port);   /* h→n, short: 16 bits */
tcph->seq_num  = htonl(seq_num);    /* h→n, long:  32 bits */
```

y que al **leer** un segmento que llegó haya que hacer lo inverso:

```c
uint32_t server_seq = ntohl(recv_tcph.seq_num);   /* n→h */
```

**La excepción de macOS.** `ip_header.c` tiene un `#if defined(__APPLE__)` que deja
`total_length` y `flags_fo` **sin** `htons()`. No es un error: los BSD, con `IP_HDRINCL`,
esperan esos dos campos en orden del host y el kernel los convierte. Si se les aplica `htons()`
en macOS, `sendto()` falla con `EINVAL`. Es un buen ejemplo de que "network byte order siempre"
tiene excepciones dependientes del sistema operativo.

### 8. Checksum de Internet (RFC 1071), calculado a mano

El algoritmo: sumar el buffer en palabras de 16 bits, doblar los acarreos sobre los 16 bits
bajos, y complementar a uno.

Tomemos el header IP del ejemplo, con el campo checksum en cero:

```
45 00   00 31   D4 31   40 00   40 06   00 00   7F 00   00 01   7F 00   00 01
```

Suma de las diez palabras de 16 bits:

```
  0x4500
+ 0x0031  = 0x04531
+ 0xD431  = 0x11962
+ 0x4000  = 0x15962
+ 0x4006  = 0x19968
+ 0x0000  = 0x19968
+ 0x7F00  = 0x21868
+ 0x0001  = 0x21869
+ 0x7F00  = 0x29769
+ 0x0001  = 0x2976A
```

Doblar el acarreo (los bits por encima de 16):

```
0x2976A → 0x976A + 0x2 = 0x976C
```

Complemento a uno:

```
~0x976C = 0x6893      ← este es el checksum que va en el header
```

Comprobación: `0x976C + 0x6893 = 0xFFFF`. El receptor suma **todo** el header incluyendo el
checksum; si el resultado es `0xFFFF`, el header llegó intacto.

**Detalle sutil de la implementación.** `calculate_checksum()` suma las palabras tal como están
en memoria, es decir ya invertidas en x86, y guarda el resultado sin `htons()`. Funciona igual,
porque el checksum de Internet tiene la propiedad de que sumar las palabras intercambiadas da el
intercambio de la suma. Los bytes que terminan en el cable son los correctos.

### 9. Pseudo-header TCP: por qué el checksum TCP mira direcciones IP

El checksum TCP no cubre sólo el segmento TCP. Antes de calcularlo se le antepone un
**pseudo-header** de 12 bytes que **no se transmite**:

```
┌─────────────────────────────────┐
│ IP origen               (4 B)   │
│ IP destino              (4 B)   │  ← pseudo-header: sólo para el cálculo
│ cero (1 B) │ proto=6 (1 B)      │
│ longitud TCP            (2 B)   │
├─────────────────────────────────┤
│ Header TCP + datos              │  ← esto sí viaja
└─────────────────────────────────┘
```

¿Para qué? Para que TCP detecte un paquete **bien formado pero entregado a la máquina
equivocada**. IP podría corromper una dirección de destino de forma que el checksum IP siga
cuadrando; sin el pseudo-header, TCP aceptaría datos que no eran para él. Es una violación
deliberada del aislamiento entre capas, justificada por robustez.

En `checksum.c` esto es literal: se hace `malloc()` de `12 + tcp_len`, se llena el pseudo-header,
se copia el segmento detrás, se calcula, y se libera. El buffer temporal nunca se envía.

### 10. Three-way handshake y el ISN

```
Cliente                                  Servidor
   │                                        │
   │──────── SYN, SEQ=x ───────────────────►│   x = ISN del cliente
   │                                        │
   │◄─────── SYN+ACK, SEQ=y, ACK=x+1 ───────│   y = ISN del servidor
   │                                        │
   │──────── ACK, SEQ=x+1, ACK=y+1 ────────►│
   │                                        │
   │             ESTABLISHED                │
```

**¿Por qué el ACK es x+1 y no x?** Porque el ACK significa *"el próximo byte que espero"*, no
*"el último que recibí"*. Y **el SYN consume un número de secuencia** aunque no lleve datos:
ocupa el número `x`, así que el siguiente byte disponible es `x+1`.

En el código eso son exactamente estas dos líneas de `tcp_session_handshake()`:

```c
s->seq++;                  /* nuestro SYN ocupó un número */
s->ack = server_seq + 1;   /* el SYN del servidor también */
```

**¿Por qué el ISN es aleatorio y no cero?** Dos razones:

1. **Seguridad.** Si fuera predecible, un atacante podría inyectar segmentos falsos en una
   conexión ajena adivinando el número de secuencia (*TCP sequence prediction attack*).
2. **Conexiones viejas.** Si se reusa el mismo par de puertos, un segmento retrasado de una
   conexión anterior podría colarse en la nueva. Un ISN distinto lo hace caer fuera de ventana.

Aquí el ISN sale de `rand()` (`s->seq = (uint32_t)rand();`), que es suficiente para un
laboratorio pero **no** criptográficamente seguro. Un stack real usa un generador con un hash
del 4-tupla y una semilla secreta (RFC 6528).

### 11. Aritmética de SEQ y ACK: la sesión completa, número por número

Regla única, de la que se deriva todo:

> **SYN consume 1. FIN consume 1. Los datos consumen su longitud. Un ACK puro no consume nada.**

Sesión con dos mensajes: `"Hello World"` (12 bytes con el `\n`) y `"EXIT OFF"` (9 bytes con el `\n`).
Números relativos, como los muestra el programa y Wireshark:

| # | Dirección | Flags | SEQ | ACK | LEN | Qué cambió |
|---|-----------|-------|-----|-----|-----|------------|
| 1 | C → S | SYN | 0 | — | 0 | el SYN ocupa el número 0 |
| 2 | S → C | SYN, ACK | 0 | 1 | 0 | ACK=1: "recibí tu SYN, espero tu byte 1" |
| 3 | C → S | ACK | 1 | 1 | 0 | conexión establecida |
| 4 | C → S | PSH, ACK | 1 | 1 | 12 | `"Hello World\n"` ocupa los bytes 1..12 |
| 5 | S → C | ACK | 1 | **13** | 0 | 1 + 12 = 13 |
| 6 | S → C | PSH, ACK | 1 | 13 | 12 | el eco ocupa los bytes 1..12 del servidor |
| 7 | C → S | ACK | **13** | **13** | 0 | ambos lados van 13 |
| 8 | C → S | PSH, ACK | 13 | 13 | 9 | `"EXIT OFF\n"` ocupa 13..21 |
| 9 | S → C | ACK | 13 | **22** | 0 | 13 + 9 = 22 |
| 10 | S → C | PSH, ACK | 13 | 22 | 9 | el eco |
| 11 | C → S | ACK | **22** | **22** | 0 | |
| 12 | S → C | FIN, ACK | 22 | 22 | 0 | el FIN ocupa el número 22 del servidor |
| 13 | C → S | ACK | 22 | **23** | 0 | 22 + 1 (el FIN cuenta) |
| 14 | C → S | FIN, ACK | 22 | 23 | 0 | nuestro FIN ocupa el número 22 |
| 15 | S → C | ACK | 23 | **23** | 0 | 22 + 1 |

Nótese la simetría: cada lado lleva **su propio** contador. El `SEQ` del cliente y el `SEQ` del
servidor son numeraciones independientes; el `ACK` de uno es una afirmación sobre el contador
del otro. Por eso el log resta ISNs cruzados (sección 4.3 del README).

En el código, los lugares donde se avanza el contador:

```c
s->seq++;                         /* handshake: nuestro SYN         */
s->seq += (uint32_t)len;          /* send_message: los datos        */
s->ack += (uint32_t)payload_len;  /* recv: los datos que recibimos  */
if (has_fin) s->ack++;            /* recv: el FIN del servidor      */
s->seq++;                         /* close: nuestro FIN             */
```

### 12. Los flags TCP

El byte de flags de `tcp_header.h`:

| Bit | Valor | Flag | Para qué se usa aquí |
|-----|-------|------|----------------------|
| 0 | `0x01` | FIN | "terminé de enviar"; inicia el cierre |
| 1 | `0x02` | SYN | sincronizar números de secuencia; abre la conexión |
| 2 | `0x04` | RST | abortar; el kernel lo manda y por eso hay que bloquearlo |
| 3 | `0x08` | PSH | "entrega esto a la aplicación ya, no lo acumules" |
| 4 | `0x10` | ACK | el campo *acknowledgment number* es válido |
| 5 | `0x20` | URG | datos urgentes; no se usa en este laboratorio |

Combinaciones que se ven en la sesión, como número y como texto:

```
0x02 = SYN            0x12 = SYN|ACK        0x10 = ACK
0x18 = PSH|ACK        0x11 = FIN|ACK
```

De ahí que `flags_to_string(0x12, ...)` devuelva `"[SYN, ACK]"`: es literalmente
`0x02 | 0x10`.

**¿Por qué casi todo lleva ACK?** Porque después del handshake, TCP es *full duplex* y cada
segmento aprovecha para informar hasta dónde recibió. Sólo el primer SYN va sin ACK, porque
todavía no ha recibido nada que reconocer.

**¿Y PSH?** Sin PSH, la pila receptora podría acumular los datos en su buffer esperando más.
Con PSH le decimos "entrégalo ahora". En la práctica los stacks modernos entregan igual, pero
es lo correcto para un mensaje interactivo, y es lo que hace que `readLine()` del servidor
despierte de inmediato.

### 13. Puertos, la 4-tupla y demultiplexado

Una conexión TCP se identifica de forma única por **cuatro** valores:

```
(IP origen, puerto origen, IP destino, puerto destino)
= (127.0.0.1, 43127, 127.0.0.1, 5001)
```

Por eso mil navegadores pueden hablar con el puerto 443 del mismo servidor sin confundirse: lo
que los distingue es su puerto de origen.

El cliente elige un **puerto efímero** al azar:

```c
s->local_port = (uint16_t)(40000 + rand() % 10000);   /* 40000–49999 */
```

En un programa normal esto lo hace el kernel al llamar a `bind()` o `connect()`. Aquí lo elegimos
nosotros, y por eso corremos el riesgo de chocar con un puerto en uso.

El demultiplexado, que normalmente hace el kernel, aquí es este bloque explícito de
`wait_for_packet()`:

```c
if (strcmp(inet_ntoa(addr), s->server_ip) != 0) continue;   /* ¿IP origen correcta?     */
if (ntohs(tcph->src_port) != s->server_port)   continue;    /* ¿puerto origen correcto? */
if (ntohs(tcph->dst_port) != s->local_port)    continue;    /* ¿es para nosotros?       */
```

Un raw socket recibe **todo** el tráfico TCP de la máquina; sin este filtro, el programa
procesaría paquetes de cualquier otra conexión.

### 14. El RST del kernel: el concepto más instructivo del laboratorio

Este es el problema que hace falta resolver para que el laboratorio corra, y explica mejor que
nada por qué existen los sockets.

```
Nuestro programa                    Kernel de la misma máquina
      │                                        │
      │─ SYN ─────────────────────────────────►│ (sale por el raw socket)
      │                                        │
      │                     ◄── SYN+ACK ───────│ llega del servidor
      │                                        │
      │                                        │ "¿puerto 43127? Yo no tengo
      │                                        │  ninguna conexión abierta ahí"
      │                     ─── RST ──────────►│ ✗ conexión abortada
```

El kernel mantiene una tabla de conexiones activas, indexada por la 4-tupla. Como nunca llamamos
a `connect()`, **nuestra conexión no está en esa tabla**. Cuando llega el SYN+ACK, el kernel
aplica la regla del RFC 793: segmento para un puerto que no está escuchando → responder `RST`.

La solución no es de programación sino de firewall: decirle al sistema que descarte esos RST
salientes.

```bash
sudo iptables -A OUTPUT -p tcp --sport 40000:49999 --tcp-flags RST RST -j DROP
```

Lo que esto enseña: **un socket no es sólo una API, es una entrada en una tabla de estado del
kernel.** Cuando se salta la API, se pierde también el estado, y el sistema operativo trata
nuestros propios paquetes como tráfico ajeno.

### 15. Raw sockets: Linux vs. BSD/macOS

```c
int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one));
```

`IP_HDRINCL` significa "el header IP lo incluyo yo"; sin él, el kernel construiría el suyo.
Requiere privilegios (`root`, o la capability `CAP_NET_RAW` en Linux), porque permite falsificar
direcciones de origen.

La diferencia entre plataformas, que explica por qué `raw_socket.c` tiene dos implementaciones:

| | Linux | macOS / BSD |
|---|-------|-------------|
| Enviar | raw socket | raw socket |
| **Recibir** | el mismo raw socket recibe copia del TCP entrante | **el raw socket no recibe TCP**: el kernel consume esos paquetes con su propio stack y sólo entrega a raw sockets los protocolos que él no maneja (como ICMP) |
| Solución | `recvfrom()` | abrir **libpcap** sobre la interfaz y "espiarla", igual que hace `tcpdump` |

De ahí la opción `-i` del cliente: en macOS hay que decirle qué interfaz capturar (`lo0`, `en0`),
mientras que en Linux se ignora.

Y de ahí también el `g_pcap_link_offset`: pcap entrega la trama **con** su header de enlace, así
que hay que saltarlo para llegar al header IP — 14 bytes en Ethernet, 4 en loopback BSD.

### 16. Cierre de conexión: cuatro segmentos y estados

TCP se cierra en dos mitades independientes, porque es full duplex: cada lado anuncia por
separado que terminó de enviar.

**Opción 1 — cierra el servidor (lo que hace este laboratorio):**

```
Cliente                                  Servidor
   │                                        │
   │  ESTABLISHED                           │  ESTABLISHED
   │◄──────── FIN, ACK ─────────────────────│  el servidor hizo client.close()
   │  CLOSE_WAIT                            │  FIN_WAIT_1
   │───────── ACK ─────────────────────────►│
   │                                        │  FIN_WAIT_2
   │───────── FIN, ACK ────────────────────►│  ya no tenemos nada que enviar
   │  LAST_ACK                              │
   │◄──────── ACK ──────────────────────────│
   │  CLOSED                                │  TIME_WAIT → CLOSED
```

**Opción 2 — cierra el cliente:** el mismo diagrama con los papeles invertidos.

**¿Por qué cuatro segmentos y no tres como al abrir?** Porque el ACK del FIN y el propio FIN del
otro lado no siempre se pueden juntar: entre uno y otro, el lado que recibió el FIN todavía puede
seguir enviando datos (estado `CLOSE_WAIT`). En este laboratorio no hay nada más que enviar, así
que los segmentos 2 y 3 podrían haberse combinado — pero se mandan separados, que es lo que hace
un stack conservador y lo que pide el enunciado.

**`TIME_WAIT`**, que aquí no implementamos: el lado que manda el último ACK debe esperar
2×MSL (típicamente 60 s) antes de liberar el puerto, por si ese ACK se pierde y hay que
retransmitirlo. Nuestro programa termina de inmediato. Es la razón por la que en un servidor real
se ven miles de conexiones en `TIME_WAIT` con `netstat`.

### 17. Ventana y control de flujo

```c
build_tcp_header(tcph, ..., /* window */ 5840);
```

El campo *window* anuncia **cuántos bytes más puede recibir el emisor** sin que se los reconozcan.
Es el mecanismo de control de flujo: evita que un emisor rápido ahogue a un receptor lento.

5840 = 4 × 1460, cuatro segmentos de MSS típico en Ethernet. Es un valor histórico razonable.

**Lo que este laboratorio no hace:** nunca lee la ventana que anuncia el servidor ni la respeta.
Envía el mensaje completo sin preguntar. Con mensajes de decenas de bytes en loopback nunca se
nota, pero es una de las diferencias importantes con un stack real.

Tampoco hay **control de congestión** (slow start, congestion avoidance), que es el mecanismo
distinto y complementario: la ventana protege al *receptor*, la congestión protege a la *red*.

### 18. Lo que un TCP real hace y este no

Vale la pena tener presente la lista, porque define el límite del laboratorio:

| Mecanismo | Qué hace un stack real | Qué hace este cliente |
|-----------|------------------------|-----------------------|
| Retransmisión | temporizador por segmento; reenvía lo no reconocido | nada: expira el timeout y falla |
| RTO adaptativo | estima el RTT (Karn, Jacobson) | timeouts fijos de 2/5/10 s |
| Control de congestión | slow start, AIMD, fast retransmit | nada |
| Control de flujo | respeta la ventana anunciada | anuncia 5840 y no lee la del otro |
| Reordenamiento | buffer de reensamblado | descarta y avisa "fuera de orden" |
| Segmentación | parte según MSS/MTU | un mensaje = un segmento (máx. 1023 B) |
| Opciones TCP | MSS, SACK, window scaling, timestamps | ninguna; header fijo de 20 bytes |
| `TIME_WAIT` | espera 2×MSL | termina de inmediato |
| ISN | generador seguro (RFC 6528) | `rand()` |
| Nagle / delayed ACK | agrupa segmentos pequeños | envía de inmediato |

Ninguna de estas ausencias es un descuido: cada una es un tema completo, y verlas listadas es
entender por qué el stack TCP del kernel tiene decenas de miles de líneas.

---

## Parte III — Cómo comprobar cada concepto experimentalmente

Los conceptos anteriores se pueden **ver**, no sólo leer:

| Concepto | Cómo observarlo |
|----------|-----------------|
| Encapsulamiento | En Wireshark, expandir el árbol del paquete: Frame → Ethernet/Loopback → IPv4 → TCP → Data |
| Byte order | En el panel de bytes de Wireshark, buscar `13 89` (puerto 5001 en big-endian) |
| Checksum | Wireshark valida el checksum del cliente; si se comenta la línea `iph->checksum = ...` en `build_packet()`, aparece marcado en rojo |
| Pseudo-header | Cambiar la IP de destino sin recalcular el checksum TCP: el servidor deja de responder |
| ISN aleatorio | Correr el cliente dos veces con *Relative sequence numbers* **desactivado**: los números iniciales son distintos |
| SYN consume 1 | Comparar el SEQ del paquete 1 (SYN) con el del paquete 3 (ACK): difieren en 1 sin haber enviado datos |
| Datos consumen LEN | Enviar un mensaje de largo distinto y verificar que el ACK del servidor avance exactamente esa cantidad |
| Demultiplexado | Abrir una segunda conexión cualquiera al puerto 5001; el filtro por puerto local hace que el cliente la ignore |
| RST del kernel | Correr **sin** la regla de `iptables` y capturar: se ve el RST saliente justo después del SYN+ACK |
| Opción de cierre | Mirar quién manda el primer FIN: con `EXIT` es el servidor; con Ctrl-D es el cliente |
| Full duplex del cierre | Contar los cuatro segmentos del cierre y los estados por los que pasa cada lado |

---

## Referencias

* **RFC 791** — Internet Protocol (header IPv4)
* **RFC 793** — Transmission Control Protocol (handshake, estados, pseudo-header) — actualizado por **RFC 9293**
* **RFC 1071** — Computing the Internet Checksum
* **RFC 6528** — Defending against Sequence Number Attacks (generación del ISN)
* `man 7 raw`, `man 7 ip`, `man 7 tcp` en Linux
