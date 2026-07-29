# Laboratorio 3 — Servidor DNS sobre UDP (implementación en C)

Servidor DNS completo escrito en **C puro (POSIX)**, que escucha en **UDP:53**,
interpreta consultas según el **RFC 1035 / RFC 5395**, construye respuestas
binarias válidas con **compresión de nombres**, mantiene **caché propia** y
resuelve de forma **recursiva** contra servidores públicos.

Está pensado para compilarse y ejecutarse en **Debian sobre BeagleBone Black**
conectada por Ethernet, que es la condición del bonus del **150 %**.

---

## 1. Estado de la implementación

| Requisito del enunciado | Estado |
|---|---|
| Escuchar en `UDP:53` | ✅ `0.0.0.0:53` (configurable) |
| Estructura de mensajes RFC 1035 / RFC 5395 | ✅ header, question, answer, authority, additional |
| Mínimo 3 tipos de registro completos | ✅ **9 tipos**: A, AAAA, PTR, CNAME, SOA, NS, MX, TXT, SRV |
| Tipos no implementados (SVCB, HTTPS, CAA…) | ✅ se reenvían intactos según RFC 3597 |
| Recursividad hacia DNS públicos | ✅ con reintento entre upstreams y respaldo TCP |
| Almacenar respuestas (caché) | ✅ caché en memoria con expiración por TTL |
| Multithreading con ThreadPool | ✅ pool `pthread` con cola acotada |
| Logs estructurados de request/response | ✅ consola + archivo, con tiempos y origen |
| Compresión de paquetes DNS (`0xC0`) | ✅ al escribir y al leer |
| Sin errores en consola | ✅ paquetes malformados se manejan sin caídas |
| Implementación en C sobre Debian/BBB | ✅ objetivo del bonus |

---

## 2. Compilación y ejecución

```bash
make                 # compila ./dns_server con -Wall -Wextra -O2 -pthread
sudo make run        # ejecuta en UDP:53 (requiere root)
make test            # ejecuta en UDP:5353 con log DEBUG, sin root
make clean           # borra binarios y objetos
make help            # lista todos los objetivos
```

La compilación es **limpia: cero advertencias** con `-Wall -Wextra`.

### Opciones de línea de comandos

```
-a, --address IP     Dirección de escucha        (por defecto 0.0.0.0)
-p, --port N         Puerto UDP                  (por defecto 53)
-t, --threads N      Hilos del pool              (por defecto 8)
-q, --queue N        Tamaño de la cola de tareas (por defecto 256)
-u, --upstream IP    Servidor DNS superior (repetible, hasta 8)
-w, --timeout MS     Timeout por upstream        (por defecto 3000)
-z, --zone ARCHIVO   Archivo de zona adicional
-l, --log ARCHIVO    Archivo de log              (por defecto dns_server.log)
-c, --cache N        Entradas máximas en caché   (por defecto 4096)
-n, --no-recursion   No reenviar (solo zona local y caché)
-v, --verbose        Log a nivel DEBUG (muestra cada RR)
-h, --help           Ayuda
```

Ejemplos:

```bash
sudo ./dns_server                      # listo para que lo use el sistema operativo
./dns_server -p 5353 -v                # pruebas sin privilegios de root
sudo ./dns_server -u 1.1.1.1 -t 16     # Cloudflare como upstream, 16 hilos
sudo ./dns_server -z zona.conf         # carga registros propios
```

---

## 3. Arquitectura

```
                    ┌──────────────────────────────┐
   consulta UDP ──► │  main.c / dns_server.c       │
                    │  socket UDP:53 + recvfrom    │
                    └──────────────┬───────────────┘
                                   │ encola la petición
                    ┌──────────────▼───────────────┐
                    │  threadpool.c                │
                    │  N workers, mutex + cond     │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────▼───────────────┐
                    │  dns_parser.c                │
                    │  decodifica el paquete        │
                    └──────────────┬───────────────┘
                                   │
             ┌─────────────────────┼─────────────────────┐
             ▼                     ▼                     ▼
    ┌────────────────┐   ┌─────────────────┐   ┌──────────────────┐
    │ 1. ZONA LOCAL  │   │ 2. CACHÉ        │   │ 3. UPSTREAM      │
    │ dns_records.c  │   │ dns_cache.c     │   │ dns_records.c    │
    │ AA=1           │   │ TTL vigente     │   │ 8.8.8.8 / 1.1.1.1│
    └────────┬───────┘   └────────┬────────┘   └────────┬─────────┘
             └─────────────────────┼─────────────────────┘
                                   │ se guarda en caché
                    ┌──────────────▼───────────────┐
                    │  dns_parser.c                │
                    │  serializa + comprime nombres│
                    └──────────────┬───────────────┘
                                   ▼
                             respuesta UDP
```

### Archivos fuente

| Archivo | Responsabilidad |
|---|---|
| `src/main.c` | Argumentos, señales, arranque |
| `src/dns_server.c` | Socket UDP, despacho al pool, armado de la respuesta |
| `src/dns_parser.c` | Codificación/decodificación binaria y compresión de nombres |
| `src/dns_records.c` | Zona local autoritativa + reenviador recursivo |
| `src/dns_cache.c` | Caché con expiración por TTL |
| `src/threadpool.c` | Pool de hilos POSIX |
| `src/log.c` | Logger estructurado (consola + archivo) |
| `src/dns_proto.h` | Constantes y estructuras del protocolo |
| `zona.conf` | Archivo de zona de ejemplo (opcional) |

### Orden de resolución

Cada consulta recorre tres etapas, y **solo la tercera sale a Internet**:

1. **Zona local** — el servidor es autoritativo (`AA=1`) para `lab.local.` y su
   zona inversa. Responde sin tocar la red.
2. **Caché** — si el nombre ya se resolvió antes y su TTL sigue vigente, la
   respuesta sale de nuestra propia memoria.
3. **Reenvío recursivo** — solo si las dos anteriores fallan. La respuesta se
   **decodifica, se guarda en caché y se vuelve a serializar** con nuestro
   código; nunca se reenvía el paquete del upstream tal cual.

Esto cumple la condición del enunciado: *«no debe hacer bypass con el servidor
elegido, solo la inicial porque no existe; todas las respuestas deben salir de
su laboratorio si existen»*.

---

## 4. Tipos de registro

### Implementados por completo (decodificados y re-serializados)

| Tipo | Valor | Hex | Uso |
|---|---|---|---|
| `A` | 1 | `0x0001` | Dirección IPv4 |
| `NS` | 2 | `0x0002` | Servidor de nombres |
| `CNAME` | 5 | `0x0005` | Alias (se siguen cadenas dentro de la zona) |
| `SOA` | 6 | `0x0006` | Autoridad de zona (respuestas negativas) |
| `PTR` | 12 | `0x000C` | DNS inverso |
| `MX` | 15 | `0x000F` | Correo, con glue en Additional |
| `TXT` | 16 | `0x0010` | Texto (SPF, verificaciones) |
| `AAAA` | 28 | `0x001C` | Dirección IPv6 |
| `SRV` | 33 | `0x0021` | Localizador de servicios |
| `OPT` | 41 | `0x0029` | Pseudo-RR EDNS0 |

### Tipos opacos

`SVCB`, `HTTPS`, `CAA`, `DNSKEY`, `RRSIG` y cualquier otro se transportan
íntegros. El **RFC 3597** prohíbe usar compresión dentro del RDATA de tipos
desconocidos, así que copiarlo byte a byte es exacto y seguro.

---

## 5. Compresión de nombres (RFC 1035 §4.1.4)

Al escribir, el serializador recuerda cada sufijo ya emitido y, si vuelve a
aparecer, escribe un puntero de 2 bytes `11xxxxxx xxxxxxxx` en vez de repetir
el dominio.

Ejemplo real capturado del servidor (consulta `A www.lab.local`):

```
offset 12 ─── QNAME "www.lab.local." en la sección Question
offset 31 ─── puntero 0xC00C ─────► apunta de vuelta al offset 12
```

Es exactamente el caso del tab **MENSAJE COMPRIMIDO** del enunciado. En una
respuesta `MX gmail.com` con 5 registros se emiten **10 punteros**, que bajan
la respuesta de **302 a 150 bytes** (la mitad exacta).

Al leer, el parser sigue los punteros con dos protecciones: solo se permiten
saltos **hacia atrás** y hay un límite de 32 saltos, de modo que un paquete
malicioso no puede provocar un bucle infinito.

---

## 6. Formato de logs

Dos líneas por transacción, una de entrada y una de salida:

```
2026-07-29 03:02:51.084 [INFO ] [worker-1] > QUERY  id=0x1234 cliente=127.0.0.1:50170 nombre=www.lab.local. tipo=A clase=IN rd=1 edns=si
2026-07-29 03:02:51.084 [INFO ] [worker-1] < REPLY  id=0x1234 cliente=127.0.0.1:50170 rcode=NOERROR origen=LOCAL aa=1 an=1 ns=0 ar=0 bytes=47 tc=0 tiempo=0.487ms
```

Campos: marca de tiempo con milisegundos, nivel, hilo que atendió, Transaction
ID, IP:puerto del cliente, nombre consultado, QTYPE, QCLASS, RCODE, **origen de
la respuesta** (`LOCAL`, `CACHE`, `UPSTREAM@8.8.8.8`), conteo por sección,
tamaño y tiempo de ejecución.

Con `-v` se añade una línea por cada RR devuelto. Cada 100 consultas se emite un
resumen de caché y del pool. Todo se escribe simultáneamente en consola y en
`dns_server.log`.

---

## 7. Despliegue en BeagleBone Black

### 7.1 Conectar y entrar

```bash
ssh debian@beaglebone.local          # o la IP asignada por el router
ip addr show eth0                    # anotar la IP; ej. 192.168.1.50
```

### 7.2 Transferir y compilar

```bash
# Desde la PC:
scp -r Lab03 debian@192.168.1.50:~/

# En la BeagleBone:
cd ~/Lab03
sudo apt-get install -y build-essential   # si hiciera falta
make
```

### 7.3 Liberar el puerto 53

Debian trae `systemd-resolved` escuchando en el 53. Hay que apagarlo:

```bash
sudo systemctl stop systemd-resolved
sudo systemctl disable systemd-resolved
sudo lsof -i UDP:53          # debe salir vacío
```

`make free53` hace los dos primeros pasos.

### 7.4 Ejecutar

```bash
sudo ./dns_server -v
# o, cargando registros propios:
sudo ./dns_server -z zona.conf -v
```

### 7.5 Apuntar la PC a la BeagleBone

- **Linux/macOS**: poner la IP de la BBB como primer DNS de la interfaz.
- **Windows**: Configuración → Red → Adaptador → IPv4 → DNS preferido = IP de la BBB.
- Como respaldo, un DNS del tab PROVEEDORES (`8.8.8.8` o `1.1.1.1`).

Limpiar la caché del sistema antes de probar:

```bash
sudo resolvectl flush-caches                                    # Ubuntu 22.04+
sudo systemd-resolve --flush-caches                             # Ubuntu 18.04
sudo dscacheutil -flushcache; sudo killall -HUP mDNSResponder   # macOS
ipconfig /flushdns                                              # Windows (Admin)
```

---

## 8. Verificación

### Con `dig` (apuntando a la BeagleBone)

```bash
BBB=192.168.1.50

dig @$BBB www.lab.local A          # zona local, debe responder AA=1
dig @$BBB www.lab.local AAAA
dig @$BBB 100.1.168.192.in-addr.arpa PTR
dig @$BBB alias.lab.local A        # cadena CNAME
dig @$BBB lab.local MX
dig @$BBB lab.local TXT
dig @$BBB lab.local SOA
dig @$BBB google.com A             # recursividad -> upstream
dig @$BBB google.com A             # otra vez -> ahora sale de la caché
dig @$BBB noexiste.lab.local A     # NXDOMAIN con SOA en Authority
```

En el log se ve el cambio de `origen=UPSTREAM@8.8.8.8` a `origen=CACHE` entre
la primera y la segunda consulta de `google.com`, y el tiempo baja de decenas
de milisegundos a fracciones.

### Con `nslookup`

```bash
nslookup www.lab.local 192.168.1.50
nslookup -type=MX lab.local 192.168.1.50
```

### Con Wireshark

Filtrar por `udp.port == 53` para inspeccionar los paquetes y comprobar los
punteros de compresión (`c0 0c`) dentro de las respuestas.

### Pruebas ya ejecutadas

La implementación se validó con un cliente DNS crudo que arma y desarma los
paquetes byte a byte:

- **17/17** pruebas de zona local (los 9 tipos, cadena CNAME, glue en
  Additional, NXDOMAIN, NODATA, ANY, verificación de punteros de compresión).
- **26/26** pruebas de red (recursividad, caché, EDNS0, truncamiento,
  10 casos de paquetes malformados y 200 consultas concurrentes).
- **200 consultas concurrentes** atendidas a ~4 000 consultas/s con 0,38 ms de
  latencia media, todas con su Transaction ID correcto.
- **Sin fugas de memoria ni comportamiento indefinido**: 234 consultas bajo
  `-fsanitize=address,undefined` sin un solo hallazgo.

---

## 9. Manejo de errores

El servidor nunca aborta por un paquete de entrada. Casos cubiertos y probados:

| Entrada | Respuesta |
|---|---|
| Paquete menor a 12 bytes | Se descarta en silencio (no amplificar tráfico) |
| Cabecera sin sección Question | `FORMERR` |
| Longitud de etiqueta inválida (`0xFF`) | `FORMERR` |
| Puntero de compresión en bucle | `FORMERR` (límite de saltos + solo hacia atrás) |
| Puntero fuera del mensaje | `FORMERR` |
| Question truncada | `FORMERR` |
| `RDLENGTH` mayor que el paquete | `FORMERR` |
| `QR=1` (es una respuesta, no consulta) | Se descarta |
| Opcode distinto de QUERY | `NOTIMP` |
| Clase distinta de IN | `REFUSED` |
| Upstream sin responder | `SERVFAIL` |
| Cola del pool llena | Se descarta con aviso, el servidor sigue atendiendo |

Sobre el bit **TC**: si la respuesta no cabe en el límite del cliente se emiten
los RR que quepan y se marca `TC=1`, como manda el RFC. Los clientes modernos
(`dig`, `systemd-resolved`, Windows) anuncian EDNS0 con 4096 bytes, así que en
la práctica reciben la respuesta completa sin truncar.

---

## 10. Caché

- **Volátil**: arranca vacía en cada ejecución, como pide el enunciado.
- **TTL real**: la vigencia es el menor TTL de los RR de la respuesta, acotado
  entre 5 s y 24 h; al servir desde caché los TTL se descuentan por el tiempo
  transcurrido.
- **Caché negativa**: `NXDOMAIN` y `NODATA` se guardan 60 s (RFC 2308).
- **Límite y desalojo**: 4096 entradas por defecto; al llenarse primero se
  barren las vencidas y luego se descarta la más antigua.
- **Concurrencia**: tabla hash de 1024 cubetas protegida con `pthread_rwlock`.

---

## 11. Archivo de zona

Se incluye `zona.conf` como ejemplo. Formato:

```
ZONE  <origen>                     # declara un origen autoritativo
<nombre>  <ttl>  <TIPO>  <valor>   # agrega un registro
```

```bash
sudo ./dns_server -z zona.conf
```

Los registros del archivo se **suman** a la zona de demostración compilada
(`lab.local.`), que ya trae ejemplos de los 9 tipos soportados.

---

## 12. Material base del laboratorio

Los archivos originales entregados con el enunciado siguen disponibles:

```bash
make runudp     # compila y ejecuta lab_3.c (ejemplo UDP en el puerto 9999)
make runjava    # ejecuta UdpBroadcastSender.java
make killudp    # libera el puerto UDP 9999
```

---

## 13. Referencias

1. [RFC 1035 — Domain Names: Implementation and Specification](https://datatracker.ietf.org/doc/html/rfc1035) (§4.1.4 Message compression)
2. [RFC 5395 — IANA Considerations for DNS](https://datatracker.ietf.org/doc/html/rfc5395) (§2.2 OpCode, §2.3 RCODE)
3. [RFC 2308 — Negative Caching of DNS Queries](https://datatracker.ietf.org/doc/html/rfc2308)
4. [RFC 3597 — Handling of Unknown DNS Resource Record Types](https://datatracker.ietf.org/doc/html/rfc3597)
5. [RFC 6891 — Extension Mechanisms for DNS (EDNS0)](https://datatracker.ietf.org/doc/html/rfc6891)
6. [zytrax.com — DNS QTYPE reference](https://www.zytrax.com/books/dns/ch15/#qtype)
7. `Laboratorio DNS.xlsx` — tabs HEADER, CONSULTA BINARIA, MENSAJE COMPRIMIDO, PROVEEDORES
