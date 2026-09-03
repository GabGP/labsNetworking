# Cliente TCP en C con Raw Sockets

Cliente TCP que construye manualmente los headers IP y TCP y los envía por un **raw socket**, en lugar de utilizar la interfaz estándar `connect()`/`send()` del sistema operativo. Su propósito es didáctico: mostrar cómo se arma un segmento TCP/IP byte a byte, incluyendo el three-way handshake, el envío de datos y el cierre de la conexión.

Incluye `ServerTCP.java` como servidor de prueba para validar el cliente.

## Estructura del laboratorio

| Archivo           | Responsabilidad                                                                                     |
| ----------------- | --------------------------------------------------------------------------------------------------- |
| `ip_header.h/c`   | Estructura del header IPv4 y función para completarlo                                               |
| `tcp_header.h/c`  | Estructura del header TCP y función para completarlo                                                |
| `checksum.h/c`    | Checksum de Internet (IP) y checksum TCP con pseudo-header                                          |
| `raw_socket.h/c`  | Socket de envío (`IP_HDRINCL`) y captura de recepción (pcap en macOS, el mismo raw socket en Linux) |
| `tcp_session.h/c` | Protocolo de la conexión: `tcp_session_open/handshake/send_message/drain_until_close/close`         |
| `tcp_client.h/c`  | Interfaz de línea de comandos y orquestación (`main()`)                                             |
| `ServerTCP.java`  | Servidor de prueba (socket estándar de Java), para validar el cliente                               |

## Compilar

```bash
make
```

Genera el binario `tcp_client`.

Para el servidor de prueba:

```bash
javac ServerTCP.java
```

## Requisito previo: bloquear el RST automático del kernel

Este cliente arma la conexión TCP fuera del stack del sistema operativo: nunca llama a `connect()`, por lo que el kernel no tiene registro de esa conexión. Cuando llega el `SYN+ACK` del servidor, el propio kernel —al no reconocerlo— responde automáticamente con un `RST`, abortando el handshake antes de que el programa pueda completarlo.

Es necesario indicarle al firewall del sistema que descarte esos `RST` salientes para el puerto que va a utilizar el cliente.

El programa imprime el puerto local elegido al arrancar; utilice ese puerto para crear la regla correspondiente.

### Linux (`iptables`) 

> Validar la versión de Linux y si es compatible el comando antes de ejecutarlo.

```bash
sudo iptables -A OUTPUT -p tcp --sport <PUERTO_LOCAL> --tcp-flags RST RST -j DROP
```

Para eliminar la regla al finalizar:

```bash
sudo iptables -D OUTPUT -p tcp --sport <PUERTO_LOCAL> --tcp-flags RST RST -j DROP
```

### macOS (`pf`)

> Validar la versión de MacOS y si es compatible el comando antes de ejecutarlo.

macOS no utiliza `iptables`; el firewall a nivel de paquete es `pf`. Para cargar una regla temporal:

```bash
echo 'block drop out proto tcp from any port <PUERTO_LOCAL> flags R/R' | sudo pfctl -f -

sudo pfctl -e   # activa pf (puede indicar "already enabled", es normal)
```

Para restaurar la configuración por defecto al finalizar:

```bash
sudo pfctl -f /etc/pf.conf

sudo pfctl -d   # sólo si pf no estaba activo antes de esta prueba
```

> `pfctl` es una herramienta estándar de administración de red; no requiere desactivar System Integrity Protection (SIP).

## Ejecutar

1. Levantar el servidor de prueba (puerto fijo 5001):

   ```bash
   java ServerTCP
   ```

2. Ejecutar el cliente (requiere privilegios de root, ya que los raw sockets necesitan `CAP_NET_RAW`):

   ```bash
   sudo ./tcp_client [opciones]
   ```

   Opciones disponibles (`./tcp_client --help`):

   | Opción                 | Descripción                      | Valor por defecto |
   | ---------------------- | -------------------------------- | ----------------- |
   | `-l`, `--local IP`     | IP local de la máquina           | `127.0.0.1`       |
   | `-s`, `--server IP`    | IP del servidor                  | `127.0.0.1`       |
   | `-p`, `--port PUERTO`  | Puerto del servidor              | `5001`            |
   | `-m`, `--message MSG`  | Mensaje a enviar                 | `"EXIT OFF"`      |
   | `-i`, `--interface IF` | Interfaz a capturar (sólo macOS) | `lo0`             |

   La IP local debe ser una dirección real de la máquina; el programa no realiza *spoofing*, sino que arma el paquete utilizando la IP indicada.

   Ejemplo, con cliente y servidor en la misma máquina:

   ```bash
   sudo ./tcp_client -l 127.0.0.1 -s 127.0.0.1 -p 5001 -m "Hola servidor" -i lo0
   ```

### Sobre el mensaje y `ServerTCP.java`

`ServerTCP.java` utiliza `BufferedReader.readLine()`, que espera un salto de línea (`\n`) para considerar completa una línea de entrada; `tcp_session.c` agrega ese `\n` automáticamente si el mensaje no lo incluye.

El servidor recibe cada línea, la reenvía (eco) y evalúa su contenido:

* Si la línea contiene `"EXIT"`, cierra la conexión con ese cliente.
* Si además contiene `"OFF"`, detiene el servidor por completo.

Por eso el mensaje por defecto del cliente es `"EXIT OFF"`: cierra la conexión de manera adecuada y también termina el servidor de prueba.

## Notas y limitaciones

* Funciona en Linux y macOS (utiliza `netinet/in.h`; en macOS además utiliza libpcap para la recepción, ya que los raw sockets de BSD no reciben paquetes TCP entrantes). En Windows requeriría Winsock con `SIO_RCVALL`.
* No hay retransmisión propia ni control de congestión: el protocolo se implementa con fines didácticos, no para reemplazar los sockets estándar en una aplicación de producción.
* No hay soporte para opciones TCP (MSS, window scaling, SACK), por lo que en redes con MTU reducida o servidores estrictos podría requerir ajustes.

## Para profundizar

Este README es intencionalmente breve. Para comprender en detalle el funcionamiento del three-way handshake, el cálculo de checksums, el formato de los headers IP/TCP y el comportamiento de los raw sockets en Linux frente a macOS/BSD, se recomienda estudiar cada componente del laboratorio y utilizar herramientas de apoyo, incluyendo asistentes de inteligencia artificial como ChatGPT, Claude u otros.

La inteligencia artificial debe utilizarse como **una herramienta de apoyo para el aprendizaje y la comprensión del código**, no como un sustituto del análisis del estudiante. Antes de pedir una explicación o una solución a la IA, el estudiante debe intentar comprender qué está haciendo el programa y plantear preguntas concretas sobre aquello que no entienda.

Al utilizar un asistente de inteligencia artificial, es recomendable proporcionar el archivo, la función o el fragmento de código específico sobre el que existen dudas y solicitar una explicación de:

* Qué hace el código.
* Por qué se implementó de esa manera.
* Qué datos recibe y qué datos produce.
* Qué ocurre internamente cuando se ejecuta.
* Cómo se relaciona con el funcionamiento de TCP/IP.
* Qué consecuencias tendría modificar una parte determinada.
* Cómo comprobar experimentalmente que el comportamiento explicado es correcto.

El objetivo no es simplemente obtener código que funcione, sino **comprender qué está sucediendo en el código y poder explicarlo sin depender de la inteligencia artificial**. Si un estudiante utiliza la IA para resolver una parte del laboratorio sin comprenderla, puede obtener un resultado funcional, pero estará perdiendo precisamente el conocimiento técnico que este laboratorio busca desarrollar.

Como parte del aprendizaje, se recomienda que después de recibir una explicación de la IA el estudiante vuelva al código, lo ejecute, observe su comportamiento y sea capaz de explicar con sus propias palabras el proceso completo: desde la construcción de los headers IP y TCP, pasando por el three-way handshake, hasta el envío de datos y el cierre de la conexión.
