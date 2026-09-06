# README_Lab — Cliente TCP con Raw Sockets

Laboratorio: **Cliente TCP con Raw Sockets — Modificación, análisis y visualización de una sesión TCP**
Archivos modificados: `ClientTCP/tcp_client.c`, `ClientTCP/tcp_session.c`, `ClientTCP/tcp_session.h`, `ClientTCP/Makefile`
Archivo agregado: `ClientTCP/test_log.c`
Servidor de prueba: `ServerTCP.java` — **sin modificar**, tal como indica el enunciado.

> Documento complementario: [`CONCEPTOS_RED.md`](CONCEPTOS_RED.md) explica los conceptos de red que este
> laboratorio ejercita, con los cálculos hechos a mano (checksums, SEQ/ACK, byte order, encapsulamiento).

---

## 1. Qué pedía el laboratorio y qué se entrega

| # | Requerimiento del enunciado | Estado | Dónde se implementó |
|---|-----------------------------|--------|---------------------|
| 1 | Permitir mensajes personalizados del usuario, N mensajes, conservando que `EXIT OFF` termina la ejecución | Implementado (versión completa: N mensajes) | `tcp_client.c` → `run_session()` |
| 2 | Mejorar el output de consola: emisor, receptor, flags, SEQ, ACK, LEN y datos de **cada** segmento | Implementado | `tcp_session.c` → `format_segment()` / `log_segment()` |
| — | Validar qué opción de cierre realiza el código proporcionado | Analizado y documentado | Sección 5 de este documento |
| — | No alterar el funcionamiento fundamental de TCP ni el orden del cierre | Respetado | Sección 6 |

---

## 2. Qué modificaciones se realizaron

### 2.1 `tcp_client.c` — interacción con el usuario (Requerimiento 1)

Antes, `run_session()` hacía una secuencia fija y lineal: handshake → enviar **un** mensaje
(el que viniera en `-m`, por defecto `"EXIT OFF"`) → esperar el cierre → terminar. Para enviar
otro mensaje había que volver a ejecutar el programa, y para cambiar el mensaje por defecto había
que recompilar.

Ahora `run_session()` mantiene **una sola conexión** y dentro de ella un ciclo de mensajes:

```c
if (tcp_session_handshake(&session, 10) != 0) { ... }   /* handshake UNA sola vez */

for (;;) {
    /* pide el mensaje al usuario */
    printf("\nIngrese un mensaje: ");
    if (read_line(line, sizeof(line)) != 0) break;      /* Ctrl-D: cierra el cliente */
    if (line[0] == '\0') continue;                      /* enter vacío: no se envía nada */

    tcp_session_send_message(&session, line);           /* PSH+ACK con los datos    */
    rc = tcp_session_recv(&session, response, ..., 10); /* ACK del servidor + eco   */

    if (is_exit_message(line)) {                        /* el servidor va a cerrar  */
        tcp_session_recv(&session, NULL, 0, 10);        /* espera y reconoce su FIN */
        break;
    }
}

tcp_session_close(&session, 5);                          /* nuestro FIN+ACK          */
```

Funciones nuevas en este archivo:

* `read_line()` — lee una línea de `stdin` con `fgets()` y le quita el `\n`/`\r` final con `strcspn()`.
  Devuelve `-1` en fin de entrada (Ctrl-D), que es la señal para cerrar desde el cliente.
* `is_exit_message()` — `strstr(msg, "EXIT") != NULL`. **Replica exactamente el criterio del
  servidor**, que en `ServerTCP.java` hace `if (msg.contains("EXIT")) { break; }`: mismo texto,
  misma sensibilidad a mayúsculas, misma coincidencia como subcadena en cualquier posición.
  Gracias a esto el cliente sabe *de antemano* que el servidor va a cerrar y se pone a esperar
  el FIN en lugar de volver a pedir un mensaje.

Cambio en las opciones de línea de comandos: `-m` ya **no** es "el único mensaje" sino
"el primer mensaje, que se envía sin preguntar"; después el programa sigue pidiendo mensajes
de forma interactiva. Su valor por defecto pasó de `"EXIT OFF"` a `NULL`, de modo que si no se
pasa `-m` el cliente siempre pregunta. Esto conserva la ejecución clásica del laboratorio base:

```bash
sudo ./tcp_client -m "EXIT OFF"    # se comporta como antes: un mensaje y cierra
```

### 2.2 `tcp_session.c` / `tcp_session.h` — registro de la sesión (Requerimiento 2)

**Estado nuevo en `tcp_session_t`:**

```c
uint32_t local_isn;   /* ISN del cliente:  base para mostrar SEQ/ACK relativos */
uint32_t remote_isn;  /* ISN del servidor: base para mostrar SEQ/ACK relativos */
int      close_logged;/* evita imprimir dos veces el separador CLOSE           */
```

**`tcp_session_drain_until_close()` se reemplazó por `tcp_session_recv()`**, que devuelve un
resultado en lugar de intentar hacer todo el cierre de una vez:

```c
typedef enum {
    TCP_RX_TIMEOUT = 0, /* no llegó nada relevante a tiempo             */
    TCP_RX_DATA    = 1, /* llegó un segmento con datos, ya reconocido   */
    TCP_RX_FIN     = 2  /* llegó el FIN del servidor, ya reconocido     */
} tcp_rx_result_t;
```

Este cambio es lo que hace posible el Requerimiento 1: la función original mezclaba "esperar la
respuesta" con "esperar el cierre", de modo que sólo servía para un intercambio y terminaba la
sesión. Ahora el cliente la llama una vez por mensaje y decide qué hacer según el resultado.

**Funciones de formato nuevas:**

| Función | Qué hace |
|---------|----------|
| `flags_to_string()` | `0x12` → `"[SYN, ACK]"`, en el mismo orden que Wireshark (FIN, SYN, RST, PSH, ACK, URG) |
| `format_data()` | Escapa el payload a una línea: `\n`, `\r`, `\t`, `\"`, `\\` y `\xNN` para bytes no imprimibles; recorta a 64 bytes |
| `format_segment()` | Arma la línea completa del segmento en un buffer |
| `log_segment()` | Imprime la línea que armó `format_segment()` |
| `print_banner()` / `print_close_banner()` | Separadores `TCP SESSION`, `DATA`, `CLOSE`, `CONNECTION CLOSED` |

**Puntos donde ahora se registra cada segmento** (antes sólo había mensajes sueltos tipo
`[*] Sending SYN...`):

* `tcp_session_handshake()` → el SYN que enviamos y el SYN+ACK que recibimos.
* `send_control()` → **todos** los segmentos sin datos que enviamos (el ACK final del handshake,
  los ACK de los datos, el FIN+ACK del cierre). Como todos pasan por aquí, basta un solo
  `log_segment()` dentro de esta función para cubrirlos.
* `tcp_session_send_message()` → el segmento PSH+ACK con los datos.
* `tcp_session_recv()` → **todo** segmento del servidor que pasa el filtro de la conexión,
  incluidos los ACK puros.
* `tcp_session_close()` → el ACK final del servidor.

**El detalle que más cambió respecto al código original:** antes, el ACK puro del servidor
(paso 5 de la secuencia del enunciado) se descartaba en silencio con

```c
if (payload_len == 0 && !has_fin) continue;   /* nunca se veía en pantalla */
```

Ahora se registra **antes** de esa decisión, y sólo después se descarta para efectos de la
máquina de estados (un ACK puro no avanza secuencia y no se responde). Por eso la secuencia
completa del enunciado, los 11 pasos, es visible en el log.

### 2.3 `Makefile` + `test_log.c` — verificación del formato

Se agregó el objetivo `make test`, que compila y ejecuta `test_log.c`. Este programa arma
segmentos sintéticos (SYN, SYN+ACK, ACK, PSH+ACK con datos, FIN+ACK, y un payload con bytes no
imprimibles) y comprueba con `assert` que la línea generada contenga el emisor, los flags, el
SEQ y ACK relativos correctos, el LEN y los datos escapados.

Sirve para revisar el formato de salida **sin necesidad de root, de raw sockets ni del servidor
levantado**, que es justamente lo incómodo de probar a mano.

---

## 3. Cómo funciona la interacción con el usuario

```
sudo ./tcp_client -l 127.0.0.1 -s 127.0.0.1 -p 5001 -i lo
```

1. El programa abre el raw socket, elige un puerto local efímero y lo imprime.
2. Hace el three-way handshake **una sola vez**.
3. Entra en el ciclo y muestra `Ingrese un mensaje: `.
4. Lo que escriba el usuario viaja como datos de un segmento `PSH+ACK`, sobre la misma conexión.
5. El servidor responde con su ACK y con el eco del mensaje; el cliente lo reconoce con un ACK.
6. Vuelve al paso 3, indefinidamente.

Formas de terminar:

| Entrada del usuario | Qué pasa | Quién inicia el cierre |
|---------------------|----------|------------------------|
| Cualquier texto con `EXIT` (p. ej. `EXIT`, `EXIT OFF`, `salir EXIT ya`) | El servidor cierra la conexión al recibir esa línea. El cliente lo sabe y espera su FIN. | **Servidor (Opción 1)** |
| `EXIT OFF` | Igual que arriba y **además** el servidor se apaga por completo (`msg.contains("OFF")`) | Servidor (Opción 1) |
| Ctrl-D (fin de entrada) | El cliente deja de pedir mensajes y manda su FIN+ACK | **Cliente (Opción 2)** |
| Enter vacío | No se envía ningún segmento; se vuelve a preguntar | — |

Ejemplo de sesión con dos mensajes:

```
Ingrese un mensaje: Hello World
Ingrese un mensaje: Hola desde Guatemala
Ingrese un mensaje: EXIT OFF
```

Las tres líneas viajan por la **misma** conexión TCP: un solo handshake al inicio y un solo
cierre al final, con tres intercambios de datos en medio.

---

## 4. Cómo se genera el output y qué información TCP muestra

### 4.1 Ejemplo real de salida

```
[*] Conexión: 127.0.0.1:43127 → 127.0.0.1:5001
[*] Puerto local: 43127  (úselo en la regla de firewall que descarta el RST del kernel)

══════════════════ TCP SESSION ══════════════════
Client → Server  [SYN]        SEQ=0
Server → Client  [SYN, ACK]   SEQ=0      ACK=1
Client → Server  [ACK]        SEQ=1      ACK=1

────────────────── DATA ──────────────────

Ingrese un mensaje: Hello World
Client → Server  [PSH, ACK]   SEQ=1      ACK=1      LEN=12   DATA="Hello World\n"
Server → Client  [ACK]        SEQ=1      ACK=13
Server → Client  [PSH, ACK]   SEQ=1      ACK=13     LEN=12   DATA="Hello World\n"
Client → Server  [ACK]        SEQ=13     ACK=13

Ingrese un mensaje: EXIT OFF
Client → Server  [PSH, ACK]   SEQ=13     ACK=13     LEN=9    DATA="EXIT OFF\n"
Server → Client  [ACK]        SEQ=13     ACK=22
Server → Client  [PSH, ACK]   SEQ=13     ACK=22     LEN=9    DATA="EXIT OFF\n"
Client → Server  [ACK]        SEQ=22     ACK=22

────────────────── CLOSE ──────────────────
Server → Client  [FIN, ACK]   SEQ=22     ACK=22
Client → Server  [ACK]        SEQ=22     ACK=23
Client → Server  [FIN, ACK]   SEQ=22     ACK=23
Server → Client  [ACK]        SEQ=23     ACK=23

══════════════════ CONNECTION CLOSED ══════════════════
```

### 4.2 Qué muestra cada campo

| Campo | Significado | De dónde sale |
|-------|-------------|---------------|
| `Client → Server` / `Server → Client` | **quién envía y quién recibe** | parámetro `outgoing` de `format_segment()` |
| `[SYN, ACK]` | **flags TCP** activos, en orden de Wireshark | `flags_to_string()` sobre `tcph->flags` |
| `SEQ=` | **número de secuencia**, relativo al ISN del emisor | `ntohl(tcph->seq_num) - isn` |
| `ACK=` | **número de reconocimiento**, relativo al ISN del otro extremo. Sólo se imprime si la bandera ACK está encendida, porque si no el campo no tiene significado (igual que Wireshark) | `ntohl(tcph->ack_num) - isn` |
| `LEN=` | **longitud de los datos** del segmento, sin contar headers. Sólo aparece si hay datos | payload total − offset del header TCP |
| `DATA=` | **datos** enviados o recibidos, escapados a una línea | `format_data()` |

Además el log incluye:

* Los separadores `TCP SESSION` / `DATA` / `CLOSE` / `CONNECTION CLOSED`, que delimitan las
  tres fases y hacen evidente **quién inició el cierre**: si la línea que aparece justo debajo
  de `CLOSE` es `Server → Client [FIN, ACK]`, cerró el servidor.
* El puerto local efímero, necesario para la regla de firewall.
* Una advertencia `(fuera de orden o retransmisión: se esperaba SEQ=N)` cuando llega un
  segmento cuyo SEQ no coincide con el esperado.

### 4.3 Por qué SEQ y ACK son relativos

Los números reales en el cable son de 10 dígitos: el ISN del cliente es `rand()` y el del
servidor lo elige el kernel. Mostrarlos crudos hace la sesión ilegible.

`format_segment()` le resta a cada número el ISN del extremo que lo generó:

```c
uint32_t rel_seq = seq - (outgoing ? s->local_isn : s->remote_isn);
uint32_t rel_ack = ack - (outgoing ? s->remote_isn : s->local_isn);
```

Nótese el cruce: en un segmento **saliente**, el SEQ es nuestro (se resta `local_isn`) pero el
ACK se refiere a los bytes del servidor (se resta `remote_isn`). En uno **entrante** es al revés.

Esto es exactamente lo que hace Wireshark con *Relative sequence numbers* activado (que es su
valor por defecto), y es la razón por la que ambas salidas se pueden comparar renglón por
renglón sin traducir nada.

### 4.4 Por qué `LEN=12` para `"Hello World"`

`"Hello World"` tiene 11 caracteres. `tcp_session_send_message()` agrega un `\n` si el mensaje
no lo trae, porque `ServerTCP.java` lee con `BufferedReader.readLine()`, que bloquea hasta
encontrar un salto de línea. Ese `\n` es un byte real que viaja, cuenta en `LEN` y cuenta en la
aritmética de números de secuencia: el SEQ del cliente pasa de 1 a **13**, no a 12.

---

## 5. Validación de la opción de cierre (lo que pedía el enunciado)

**El código proporcionado realiza la Opción 1: el cierre lo inicia el servidor.**

Evidencia en el código original:

1. `run_session()` llamaba a `tcp_session_drain_until_close()` **antes** de `tcp_session_close()`.
2. `tcp_session_drain_until_close()` se quedaba esperando un segmento del servidor con la
   bandera FIN, lo reconocía (`s->ack++; send_control(s, TCP_ACK);`) y sólo entonces retornaba.
3. Recién después `tcp_session_close()` enviaba el `FIN+ACK` del cliente.

Es decir: FIN del servidor → ACK del cliente → FIN del cliente → ACK del servidor, que es la
Opción 1 del enunciado.

**La causa está en el servidor**, y por eso no hace falta modificarlo: el mensaje por defecto
era `"EXIT OFF"`, y `ServerTCP.java` hace

```java
if (msg.contains("EXIT")) { break; }   // sale del ciclo de lectura
...
client.close();                        // ← esto genera el FIN del servidor
```

El servidor cierra el socket en cuanto recibe una línea que contiene `EXIT`, así que siempre es
él quien manda el primer FIN.

**En la versión modificada se conserva ese orden.** Cuando el usuario escribe algo con `EXIT`,
`is_exit_message()` lo detecta, el cliente espera el FIN del servidor y sólo después envía el
suyo: Opción 1, exactamente igual que antes.

La Opción 2 (cliente primero) aparece únicamente en el caso que el código original no
contemplaba: si el usuario termina con Ctrl-D sin haber mandado nunca `EXIT`, el servidor sigue
esperando líneas y no va a cerrar nunca, así que el cliente inicia el cierre. Sin este camino el
programa se quedaría colgado hasta el timeout. **El orden de la Opción 1 no se modificó**; sólo
se agregó una salida limpia para un escenario nuevo.

---

## 6. Qué NO se modificó (el protocolo sigue intacto)

* La construcción de los headers IPv4 y TCP (`ip_header.c`, `tcp_header.c`) — sin cambios.
* El cálculo de checksums (`checksum.c`) — sin cambios.
* La creación, envío y recepción por raw socket (`raw_socket.c`) — sin cambios.
* `ServerTCP.java` — sin cambios.
* La aritmética de números de secuencia: el SYN consume 1, los datos consumen `LEN`, el FIN
  consume 1. Las mismas líneas que en el original, sólo que ahora comentadas.
* El orden del cierre en la Opción 1.

Los cambios son de **interacción** (ciclo de mensajes) y de **observabilidad** (log). Ningún
segmento nuevo se inventa y ninguno de los que existían dejó de enviarse.

---

## 7. Cómo compilar y ejecutar

### 7.1 Compilar

```bash
cd ClientTCP
make clean && make          # genera ./tcp_client
javac ../ServerTCP.java     # o compile el servidor donde lo tenga
```

> `make clean` importa: el ZIP original traía archivos `.o` precompilados que ya no
> corresponden al código fuente modificado.

Verificación rápida del formato de salida, sin root ni servidor:

```bash
make test
```

### 7.2 Regla de firewall (obligatoria)

El cliente nunca llama a `connect()`, así que el kernel no tiene registro de esta conexión.
Cuando llega el `SYN+ACK` del servidor, el kernel no lo reconoce como perteneciente a ninguna
conexión suya y responde con un `RST` que aborta el handshake antes de que el programa pueda
completarlo.

El cliente elige un puerto efímero **aleatorio entre 40000 y 49999** en cada ejecución
(`40000 + rand() % 10000` en `tcp_session_open()`). Por eso conviene crear la regla para todo el
rango una sola vez, en lugar de leer el puerto y crearla en cada corrida:

**Linux:**

```bash
sudo iptables -A OUTPUT -p tcp --sport 40000:49999 --tcp-flags RST RST -j DROP

# al terminar:
sudo iptables -D OUTPUT -p tcp --sport 40000:49999 --tcp-flags RST RST -j DROP
```

**macOS:**

```bash
echo 'block drop out proto tcp from any port 40000:49999 flags R/R' | sudo pfctl -f -
sudo pfctl -e

# al terminar:
sudo pfctl -f /etc/pf.conf
```

### 7.3 Ejecutar

```bash
# terminal 1
java ServerTCP

# terminal 2
sudo ./tcp_client -l 127.0.0.1 -s 127.0.0.1 -p 5001 -i lo   # macOS: -i lo0
```

---

## 8. Cómo se verificó con Wireshark

### 8.1 Captura

1. Abrir Wireshark **antes** de correr el cliente y capturar en la interfaz de loopback
   (`lo` en Linux, `lo0` en macOS). Wireshark necesita permisos: en Linux, `sudo wireshark` o
   tener el usuario en el grupo `wireshark`.
2. Filtro de visualización: `tcp.port == 5001`.
3. Confirmar que *Relative sequence numbers* esté activo, que es el valor por defecto:
   **Edit → Preferences → Protocols → TCP → Relative sequence numbers**. Si está desactivado,
   Wireshark mostrará los números absolutos y no coincidirán con el log del programa.
4. Correr el cliente, escribir dos o tres mensajes y terminar con `EXIT OFF`.
5. Detener la captura y guardarla.

Alternativa por línea de comandos, útil si Wireshark no está disponible en la VM:

```bash
sudo tcpdump -i lo -w lab05.pcap 'tcp port 5001'
```

y después abrir `lab05.pcap` en Wireshark.

### 8.2 Qué se comparó

La comprobación consiste en poner el log del programa al lado de la lista de paquetes de
Wireshark y verificar, paquete por paquete, que coincidan las cuatro cosas:

| Del log del programa | De Wireshark |
|----------------------|--------------|
| `Client → Server` / `Server → Client` | columnas *Source* y *Destination* + los puertos |
| `[PSH, ACK]` | columna *Info*, que usa exactamente los mismos nombres y el mismo orden |
| `SEQ=` / `ACK=` | campos `Seq=` / `Ack=` de la columna *Info* |
| `LEN=` | campo `Len=` de la columna *Info* |
| `DATA="Hello World\n"` | clic derecho en el paquete → **Follow → TCP Stream**, o el panel de bytes |

La sesión completa debe verse como 15 paquetes para dos mensajes: 3 del handshake, 4 por cada
mensaje, y 4 del cierre.

### 8.3 Diferencias esperadas entre el log y Wireshark

Tres cosas que aparecen en la captura y conviene saber interpretar, porque no son errores:

1. **El ACK puro del servidor puede no aparecer como paquete separado.** El kernel del servidor
   puede juntar el ACK de nuestros datos con el eco en un solo segmento `PSH, ACK`
   (*piggybacking*), sobre todo en loopback, donde Java responde de inmediato. En ese caso los
   pasos 5 y 6 de la secuencia del enunciado viajan en un único paquete. El log refleja lo que
   realmente ocurrió: si el ACK puro no llegó, no se imprime. Ambos comportamientos son
   correctos según TCP.
2. **`RST` salientes del cliente**, si la regla de firewall no está puesta o el puerto quedó
   fuera del rango. Si aparecen, el handshake se corta y el programa reporta timeout.
3. **`checksum incorrect` en los paquetes que genera el kernel del servidor.** Es el efecto del
   *checksum offloading*: el kernel deja el campo sin calcular porque normalmente lo completa la
   tarjeta de red. Los paquetes **del cliente** sí deben tener checksum válido, porque los
   calculamos nosotros en `checksum.c`; ese es justamente un buen control de que el cálculo del
   pseudo-header está bien hecho.

### 8.4 Entregable de la captura

Incluir en el ZIP una captura de pantalla (o el `.pcap`) donde se vea la sesión completa: los
3 paquetes del handshake, los intercambios de datos de al menos dos mensajes distintos, y los
4 paquetes del cierre en el orden de la Opción 1.

---

## 9. Limitaciones conocidas

Se mantienen las del laboratorio base, y son deliberadas: el objetivo es mostrar el formato de
los segmentos, no reemplazar el stack del sistema operativo.

* No hay retransmisión propia ni control de congestión. Si un segmento se pierde, la sesión se
  cae por timeout en lugar de reintentar.
* No se respeta la ventana anunciada del servidor: se envía el mensaje completo sin verificar
  cuánto espacio declaró. En loopback con mensajes cortos nunca se llega al límite.
* No hay opciones TCP (MSS, window scaling, SACK): el header siempre mide 20 bytes.
* No se implementa `TIME_WAIT`; el proceso termina apenas recibe el ACK final.
* Un mensaje se limita a 1023 bytes y viaja en un solo segmento; no hay segmentación.
* La IP local debe ser una dirección real de la máquina. El programa no hace *spoofing*.
