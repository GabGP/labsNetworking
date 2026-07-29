# Laboratorio 3 - Servidor DNS sobre UDP en C (BeagleBone Black Target - 150% Extra Credit)

Este laboratorio contiene la implementacin completa de un **Servidor DNS de Alto Rendimiento sobre UDP** desarrollado en **C (POSIX)** para ejecutarse en Debian Linux sobre la plataforma de hardware **BeagleBone Black (BBB)** via Ethernet.

Al estar implementado 100% en C nativo con un modelo de concurrencia basado en **ThreadPool** (`pthread`) inspirado en la arquitectura del Lab #1, este proyecto aplica para el **150% de la nota (+50% extra credit)**.

---

## 🚀 Características Principales

- **Lenguaje Nativo C (POSIX):** Desarrollado utilizando `gcc`, `pthreads` y sockets datagrama POSIX (`SOCK_DGRAM`).
- **Arquitectura Multihilo (ThreadPool):** Inspirado en el Lab #1, utiliza un pool de hilos (`pthread_mutex_t`, `pthread_cond_t`) con cola circular para procesar múltiples peticiones UDP concurrentes sin bloqueo.
- **Protocolo DNS Estándar (RFC 1035 & RFC 5395):**
  - Decodificación y construcción del **Header DNS de 12 bytes** (`ID`, `Flags: QR, Opcode, AA, TC, RD, RA, Z, RCODE`, `QDCOUNT`, `ANCOUNT`, `NSCOUNT`, `ARCOUNT`).
  - Parsing dinámico del **Question Section** (`QNAME` codificado en octetos de longitud, `QTYPE`, `QCLASS`).
  - Soporte completo de **Compresión de Nombres de Dominio** mediante punteros de compensación (`0xC0` con offset a `QNAME` en `0x000C`), tal como se detalla en `Laboratorio DNS.xlsx`.
- **Soporte Completo de Tipos de Registros DNS (6 Tipos):**
  - `Type A` (0x0001): Resolución IPv4.
  - `Type AAAA` (0x001C): Resolución IPv6.
  - `Type PTR` (0x000C): Búsqueda DNS inversa (`in-addr.arpa`).
  - `Type CNAME` (0x0005): Alias de nombre canónico.
  - `Type TXT` (0x0010): Registros de texto (SPF, verificación).
  - `Type SOA` (0x0006): Inicio de autoridad de zona.
- **Recursividad y Reenvío (Upstream Forwarding):** Reenvío automático de consultas no encontradas en la base local hacia servidores DNS públicos (`8.8.8.8` / `1.1.1.1`) sobre sockets UDP temporales con timeout.
- **Base de Datos de Registros Locales Configurable:** Soporte para carga de zonas personalizadas desde archivo plano `records.conf`.
- **Logging Estructurado en Tiempo Real:** Formato limpio indicando timestamp, IP:Puerto del cliente, ID de transacción, QNAME, QTYPE, RCODE de respuesta, fuente de resolución (`LOCAL_DB` vs `UPSTREAM`) y tiempo de latencia en milisegundos.
- **Apagado Limpio y Resiliencia:** Manejo de señales (`SIGINT`, `SIGTERM`) para liberar hilos y sockets sin fugas de memoria ni crasheos por paquetes malformados.

---

## 📁 Estructura del Código

- `main.c`: Punto de entrada principal, procesamiento de argumentos CLI (`-p`, `-u`, `-t`, `-z`) y manejo de señales.
- `dns_server.h` / `dns_server.c`: Bucle principal del servidor UDP, cola de tareas y despachador de respuestas.
- `dns_parser.h` / `dns_parser.c`: Decodificación binaria de paquetes DNS, parsing de cabeceras/preguntas, codificación de compresión de dominios.
- `dns_records.h` / `dns_records.c`: Almacenamiento en memoria de registros DNS locales, lector de `records.conf` y reenvío recursivo upstream.
- `threadpool.h` / `threadpool.c`: Pool de hilos POSIX con variables de condición y exclusión mutua.
- `records.conf`: Archivo de configuración de registros de zona local.
- `test_dns.py`: Script de prueba y verificación de integración de la suite DNS.
- `Makefile`: Script de compilación y ejecución simplificado.

---

## 🛠 Compilador y Opciones Makefile

El `Makefile` incluido facilita la compilación, ejecución y limpieza en entornos Linux / Debian (BeagleBone Black).

```bash
# Compilar el binario ejecutable (dns_server)
make

# Ejecutar el servidor en puerto 53 con privilegios elevados (requerido para puerto 53)
make run

# Ejecutar el servidor en puerto de prueba de usuario (puerto 9999)
make run-user

# Limpiar objetos y binarios
make clean
```

---

## 📋 Guía de Despliegue en BeagleBone Black (Debian Linux)

### 1. Conexión e Instalación
1. Conecta la BeagleBone Black a tu red local mediante el puerto Ethernet RJ45.
2. Inicia sesión vía SSH en la BeagleBone Black desde tu computadora host:
   ```bash
   ssh debian@beaglebone.local
   # O mediante la dirección IP asignada por el router:
   ssh debian@192.168.1.X
   ```

### 2. Transferencia y Compilación
3. Transfiere el proyecto a la BeagleBone Black (`scp` o `git clone`):
   ```bash
   scp -r . debian@beaglebone.local:~/lab3-dns/
   ```
4. En la consola de la BeagleBone Black, navega al directorio y compila nativamente:
   ```bash
   cd ~/lab3-dns
   make
   ```

### 3. Ejecución del Servidor DNS
5. Ejecuta el servidor DNS en la BeagleBone Black escuchando en el puerto 53:
   ```bash
   sudo ./dns_server -p 53 -u 8.8.8.8 -t 4 -z records.conf
   ```

---

## 🧪 Pruebas y Verificación del Servidor

Desde cualquier equipo de la red (host PC o laptop), puedes verificar el servidor ejecutando utilidades estándar como `dig` o `nslookup`:

### 1. Consulta Registro IPv4 (Type A)
```bash
dig @<BEAGLEBONE_IP> beaglebone.local A
# O usando nslookup:
nslookup beaglebone.local <BEAGLEBONE_IP>
```

### 2. Consulta Registro IPv6 (Type AAAA)
```bash
dig @<BEAGLEBONE_IP> beaglebone.local AAAA
```

### 3. Consulta DNS Inversa (Type PTR)
```bash
dig @<BEAGLEBONE_IP> -x 192.168.1.100
```

### 4. Consulta Alias CNAME (Type CNAME)
```bash
dig @<BEAGLEBONE_IP> www.lab3.test CNAME
```

### 5. Consulta Registro de Texto (Type TXT)
```bash
dig @<BEAGLEBONE_IP> lab3.test TXT
```

### 6. Consulta Recursiva Externa (Upstream Forwarding a Google 8.8.8.8)
```bash
dig @<BEAGLEBONE_IP> google.com A
```

---

## 📊 Formato de Registros y Configuración (`records.conf`)

Puedes definir dominios locales editando el archivo `records.conf`:

```conf
# Registros IPv4 (A)
beaglebone.local    A       192.168.1.100       300
lab3.test           A       127.0.0.1           300

# Registros IPv6 (AAAA)
beaglebone.local    AAAA    fe80::100           300

# Registros DNS Inversos (PTR)
100.1.168.192.in-addr.arpa  PTR beaglebone.local  300

# Alias CNAME
www.lab3.test       CNAME   lab3.test           300

# Registros TXT
lab3.test           TXT     "v=spf1 redirect=_spf.google.com" 300
```

---

## 💯 Demostración de Calificación (150% Target)

1. **Implementación 100% en C:** Cumple con la exigencia de desarrollo en lenguaje C nativo (`gcc`, `pthreads`).
2. **Modelo Concurrente de Lab #1:** Implementación de ThreadPool dinámico (`threadpool.c`) evitando cuellos de botella por socket síncrono.
3. **Plataforma BeagleBone Black:** Diseñado y optimizado para ejecutarse en Debian sobre BeagleBone Black con conexión Ethernet.
4. **Respuesta DNS Binaria y Compresión de Dominios:** Codificación de punteros `0xC00C` respetando la especificación del RFC 1035 y los diagramas de `Laboratorio DNS.xlsx`.
