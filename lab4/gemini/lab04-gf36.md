# Laboratorio 4: Cliente NTP en C (RFC 5905) — Informe de Implementación y Protocolo

---

## PARTE 1: ¿Qué Hace el Laboratorio? (Visión General)

Este laboratorio consiste en el diseño e implementación de un **cliente manual del protocolo de tiempo de red NTP (Network Time Protocol, RFC 5905)** escrito íntegramente en lenguaje **C** para sistemas POSIX/Linux, sin depender de bibliotecas externas de NTP.

El objetivo principal es sincronizar un **reloj interno simulado** (cuya hora inicial puede ser configurada manualmente en la zona horaria de Guatemala, UTC-6) con una red de servidores de tiempo reales en Internet.

### Funciones Principales del Cliente NTP:
1. **Construcción Manual de Paquetes UDP:** Genera solicitudes NTP versión 3 de 48 bytes estructuradas a nivel de bits.
2. **Sondeo Simultáneo a Múltiples Servidores:** Realiza consultas concurrentes en paralelo sobre el puerto UDP 123 a 10 servidores NTP reconocidos a nivel mundial (`time.google.com`, `time.apple.com`, `time.windows.com`, `time.cloudflare.com`, `time.nist.gov`, `pool.ntp.org`, entre otros).
3. **Captura de Marcas de Tiempo ($T_1$ a $T_4$):**
   - $T_1$: Momento en que el cliente envía la solicitud.
   - $T_2$: Momento en que el servidor recibe la solicitud.
   - $T_3$: Momento en que el servidor envía la respuesta.
   - $T_4$: Momento en que el cliente recibe la respuesta.
4. **Cálculo de Parámetros de Sincronización:**
   - **Retardo de Ida y Vuelta / Delay ($\delta$):** Mide el tiempo total en tránsito por la red.
   - **Desfase del Reloj / Offset ($\theta$):** Mide la diferencia exacta entre la hora del cliente y la hora del servidor.
5. **Selección del Mejor Servidor:** Filtra las respuestas exitosas y selecciona el offset perteneciente al servidor con el **menor Delay ($\delta$)**, ya que los caminos de red con menor retardo presentan menor fluctuación (jitter) y asimetría.
6. **Sincronización Iterativa:** Aplica la corrección al reloj simulado y repite la iteración hasta que el offset sea menor o igual al umbral especificado (por ejemplo, 50 milisegundos).
7. **Verificación Periódica:** Al alcanzar el estado **SINCRONIZADO**, entra en un bucle continuo de control que re-verifica la sincronización a intervalos configurables (por defecto cada 60 segundos).
8. **Logging Detallado en Consola:** Muestra en cada ciclo un desglose completo de los timestamps en formato hexadecimal de 64 bits, enteros de segundos y fracción, hora legible en UTC y Guatemala, así como las métricas calculadas.

---

## PARTE 2: ¿Cómo fue Implementado? (Arquitectura y Código)

La implementación se diseñó bajo una estructura **modular y desacoplada** en lenguaje C99/POSIX (`-Wall -Wextra -O2 -std=gnu99`), dividida en los siguientes componentes principales:

```
                  +-----------------------------------+
                  |              main.c               |
                  | CLI Parsing / Sincronización Loop |
                  +-----------------+-----------------+
                                    |
         +--------------------------+--------------------------+
         |                          |                          |
+--------v--------+        +--------v--------+        +--------v--------+
|    ntp_net.c    |        |    sync.c       |        |     log.c       |
| Consultas UDP   |        | Reloj Simulado  |        | Registro y      |
|  con poll()     |        | Selección Best  |        | Formato Consola |
+--------+--------+        +--------+--------+        +-----------------+
         |                          |
         +--------------------------+
                                    |
         +--------------------------+--------------------------+
         |                                                     |
+--------v--------+                                   +--------v--------+
|   ntp_packet.c  |                                   |    ntp_time.c   |
| Paquete 48 B    |                                   | Formato 32.32   |
| Byte Order Net  |                                   | Conversión Epoch|
+-----------------+                                   +-----------------+
```

### Detalle de los Módulos:

1. **Estructura y Serialización del Paquete (`ntp_packet.h / ntp_packet.c`):**
   - Define la estructura de 48 bytes especificada por el RFC 5905.
   - Modifica el primer byte colocando los bits `LI = 0` (0b00), `VN = 3` (0b011), `Mode = 3` (0b011, Cliente NTP), resultando en el valor hexadecimal `0x1B`.
   - Implementa funciones para empaquetar y desempaquetar campos usando orden de bytes de red (`htonl` / `ntohl`) para evitar problemas de endianness.

2. **Conversión y Matemática de Tiempo (`ntp_time.h / ntp_time.c`):**
   - Define el tipo `ntp_time_t` compuesto por 32 bits de segundos y 32 bits de fracción de segundo.
   - Implementa la conversión entre la **Época NTP** (1 de enero de 1900) y la **Época Unix** (1 de enero de 1970), aplicando el desfase exacto de **2,208,988,800 segundos**.
   - Convierte fechas en formato local de Guatemala (`YYYY-MM-DD HH:MM:SS.mmm`) a timestamps UTC-6.
   - Realiza restas y sumas seguras de timestamps usando enteros con signo de 64 bits (`int64_t`) sobre formato punto fijo 32.32:
     $$\text{val64} = (\text{sec} \ll 32) \mid \text{fraction}$$
     $$\text{diferencia\_ms} = \frac{(\text{val64}_2 - \text{val64}_1) \times 1000.0}{2^{32}}$$

3. **Conexión de Red Concurrente Multiseridor (`ntp_net.h / ntp_net.c`):**
   - Resuelve direcciones de 10 servidores NTP con `getaddrinfo` (IPv4, `SOCK_DGRAM`).
   - Crea sockets UDP no bloqueantes (`fcntl(O_NONBLOCK)`).
   - Envía las solicitudes en paralelo registrando $T_1$ para cada servidor.
   - Utiliza la llamada de sistema `poll()` para esperar respuestas concurrentes con un timeout acotado (3 segundos), evitando que un servidor caído o bloqueado detenga al cliente.

4. **Reloj Simulado y Algoritmo de Selección (`sync.h / sync.c`):**
   - El reloj simulado avanza en tiempo real utilizando `clock_gettime(CLOCK_MONOTONIC)` sumado a la base inicial y las correcciones acumuladas.
   - Filtra los servidores que respondieron correctamente y selecciona el servidor con el **menor Delay positivo**.
   - Aplica el ajuste al reloj simulado mediante `sim_clock_adjust_ms()`.

5. **Consola y Registro Detallado (`log.h / log.c`):**
   - Imprime los banners, la configuración inicial y el resumen por iteración.
   - Muestra el valor de $T_1, T_2, T_3, T_4$ en hexadecimal de 64 bits, enteros de segundos/fracción y fecha UTC legible.

6. **Verificación y Pruebas Unitarias (`test_ntp.c` & `Makefile`):**
   - Incluye un runner de pruebas unitarias (`make test`) que valida las conversiones de fecha y los vectores de prueba de cálculo del PDF de la práctica.

---

## PARTE 3: El Protocolo NTP, Prerrequisitos y Aprendizajes (Análisis Profundo)

Esta sección aborda los aspectos teóricos y prácticos indispensables para comprender la naturaleza de NTP, la ingeniería requerida para su desarrollo y las lecciones aprendidas durante la programación.

---

### 1. El Protocolo NTP (RFC 5905) en Profundidad

#### ¿Por qué es vital la Sincronización de Tiempo?
En sistemas distribuidos modernos, la noción de un "reloj global único" no existe físicamente. Sin una sincronización precisa:
- Los registros de eventos (**logs**) en servidores distribuidos no pueden ordenarse cronológicamente.
- Las transacciones financieras y bases de datos relacionales distribuidas sufren violaciones de consistencia (ej. ordenamiento causal).
- Los protocolos de autenticación como **Kerberos** o los certificados **TLS/SSL** fallan por expiración o discrepancia temporal.

#### La Jerarquía de Stratum (Estratos NTP)
NTP organiza los servidores de tiempo en una estructura jerárquica de capas llamadas **Stratum**:
- **Stratum 0:** Dispositivos de precisión física absoluta (relojes atómicos de cesio/rubidio, receptores GPS). No se conectan directamente a la red IP.
- **Stratum 1:** Servidores de tiempo conectados directamente a dispositivos Stratum 0. Ofrecen la máxima precisión en red.
- **Stratum 2:** Servidores que sincronizan su hora a través de la red consultando servidores Stratum 1.
- **Stratum 3 a 15:** Servidores que se sincronizan con la capa inmediatamente superior. El estrato 16 indica un reloj no sincronizado.

```
 [Reloj Atómico / GPS] (Stratum 0)
          |
   [Servidor NTP]      (Stratum 1) (ej. time.nist.gov, time.google.com)
    /           \
[Servidor NTP]  [Servidor NTP] (Stratum 2) (ej. pool.ntp.org)
      |
[Cliente NTP]                  (Nuestro Programa en C)
```

---

#### Estructura del Paquete NTP de 48 Bytes

Un paquete NTP binario consta de un encabezado fijo de 48 bytes ordenado en modo **Big-Endian** (Network Byte Order):

| Bytes | Campo | Tamaño | Descripción |
|---|---|---|---|
| 0 | `LI` (2 bits) / `VN` (3 bits) / `Mode` (3 bits) | 1 byte | Leap Indicator, Versión NTP (3 o 4), Modo (3 = Cliente, 4 = Servidor) |
| 1 | `Stratum` | 1 byte | Nivel en la jerarquía (0 en solicitud de cliente) |
| 2 | `Poll` | 1 byte | Intervalo máximo entre mensajes (potencia de 2 en segundos) |
| 3 | `Precision` | 1 byte | Precisión del reloj del sistema (entero de 8 bits con signo, potencia de 2) |
| 4 – 7 | `Root Delay` | 4 bytes | Retardo total de ida y vuelta al reloj de referencia principal (fijo 16.16) |
| 8 – 11 | `Root Dispersion` | 4 bytes | Máxima desviación acumulada respecto a la fuente primaria |
| 12 – 15 | `Reference ID` | 4 bytes | Identificador del reloj de referencia (código ASCII o dirección IP) |
| 16 – 23 | `Reference Timestamp` | 8 bytes | Hora en que el reloj del servidor fue ajustado por última vez |
| 24 – 31 | `Originate Timestamp` ($T_1$) | 8 bytes | Hora en que la solicitud salió del cliente |
| 32 – 39 | `Receive Timestamp` ($T_2$) | 8 bytes | Hora en que la solicitud llegó al servidor |
| 40 – 47 | `Transmit Timestamp` ($T_3$) | 8 bytes | Hora en que la respuesta salió del servidor |

---

#### El Formato de Timestamp NTP de 64 bits (32.32 Fixed-Point)

A diferencia de los timestamps UNIX estándar (que cuentan segundos desde 1970), NTP utiliza un formato de punto fijo de 64 bits:
- **32 bits superiores:** Segundos transcurridos desde la **Época NTP: 1 de enero de 1900 a las 00:00:00 UTC**.
- **32 bits inferiores:** Fracción de segundo en unidades de $\frac{1}{2^{32}}$ partes de segundo.

$$\text{Resolución teórica} = \frac{1}{2^{32}} = \frac{1}{4,294,967,296} \approx 232.8 \text{ picosegundos}$$

##### Conversión de Fracción:
- **Segundos a Fracción:** $\text{frac32} = \text{round}(\text{fracción\_decimal} \times 4,294,967,296.0)$
- **Fracción a Segundos:** $\text{fracción\_decimal} = \frac{\text{frac32}}{4,294,967,296.0}$

##### Conversión entre Época Unix (1970) y NTP (1900):
Entre el 1 de enero de 1900 y el 1 de enero de 1970 hay exactamente 70 años (incluyendo 17 años bisiestos):
$$\text{Diferencia de Épocas} = (70 \times 365 + 17) \times 86,400 = 2,208,988,800 \text{ segundos}$$
$$\text{Segundos NTP} = \text{Segundos UNIX} + 2,208,988,800$$

---

#### La Matemática de la Sincronización

NTP asume que el retardo del medio de red entre el cliente y el servidor es aproximadamente simétrico (el tiempo de ida es igual al tiempo de retorno).

```
CLIENTE                                SERVIDOR
   |                                      |
T1 +------------------------------------->| T2  (Solicitud en tránsito)
   |                                      |
   |                                      | T3  (Servidor procesa y responde)
T4 |<-------------------------------------+
   v                                      v
```

Con las 4 marcas de tiempo:
1. **Retardo total de ida y vuelta ($\delta$):**
   $$\delta = (T_4 - T_1) - (T_3 - T_2)$$
   - $(T_4 - T_1)$ es el tiempo total transcurrido medido por el cliente.
   - $(T_3 - T_2)$ es el tiempo que el servidor tardó en procesar el paquete.
   - La resta nos da el tiempo que el paquete estuvo viajando por el cable/red.

2. **Desfase o Corrección de Reloj ($\theta$):**
   $$\theta = \frac{(T_2 - T_1) + (T_3 - T_4)}{2}$$
   - Si $\theta > 0$, el reloj del cliente está **atrasado** con respecto al servidor (debe adelantarse).
   - Si $\theta < 0$, el reloj del cliente está **adelantado** con respecto al servidor (debe atrasarse).

##### ¿Por qué se Elige el Servidor con Menor Delay?
En redes conmutadas e IP (Internet), los paquetes sufren de jitter y colas de espera en los enrutadores. Cuando el **Delay ($\delta$)** es mínimo, la probabilidad de que haya ocurrido una congestión asimétrica en el camino es significativamente menor, lo que garantiza que la estimación del **Offset ($\theta$)** sea lo más certera posible.

---

### 2. ¿Qué se Necesita Saber Previamente para Desarrollarlo? (Prerrequisitos)

Para implementar este proyecto exitosamente desde cero, un desarrollador requiere dominar los siguientes conocimientos teóricos y técnicos:

1. **Programación en C de Bajo Nivel y Manipulación Binaria:**
   - Dominio de operadores a nivel de bit (shifts `<<`, `>>`, máscaras AND `&`, OR `|`).
   - Uso de tipos de ancho fijo (`uint8_t`, `uint32_t`, `int64_t`) de `<stdint.h>`.
   - Empaquetado estricto de estructuras binarias para evitar desalineación por padding del compilador.

2. **Representación de Datos y Orden de Bytes (Endianness):**
   - Entender la diferencia entre **Big-Endian** (Network Byte Order) y **Little-Endian** (Host Byte Order en arquitecturas x86_64).
   - Uso correcto de `htonl()`, `ntohl()`, `htons()`, `ntohs()` al leer y escribir enteros multibyte en el buffer UDP.

3. **Programación de Sockets POSIX (Networking UDP):**
   - Conocimiento del protocolo no orientado a conexión **UDP (User Datagram Protocol)**.
   - Funciones del API de Sockets POSIX: `socket()`, `getaddrinfo()`, `sendto()`, `recvfrom()`, `close()`.
   - Configuración de sockets no bloqueantes mediante `fcntl()` y la bandera `O_NONBLOCK`.
   - Multiplexación de I/O mediante `poll()` o `select()` para monitorear múltiples sockets en paralelo con tiempos de espera acotados (*timeouts*).

4. **Aritmética de Punto Fijo (Fixed-Point Arithmetic):**
   - Manejo de enteros de 64 bits para representar cantidades con parte entera y parte fraccionaria (formato 32.32).
   - Capacidad de transformar fracciones decimales de punto flotante a representación escalar en potencia de 2 ($2^{32}$) y viceversa.
   - Prevención de desbordamientos de entero (*overflow/underflow*) durante restas de timestamps.

5. **Manejo del Tiempo en Sistemas Operativos Unix/Linux:**
   - Diferencia entre tiempo de pared (Wall-Clock, `CLOCK_REALTIME`) y tiempo monótono continuo (`CLOCK_MONOTONIC`).
   - Estructuras estándar de tiempo de POSIX: `struct tm`, `time_t`, `struct timeval`, `struct timespec`.
   - Funcionamiento de zonas horarias (UTC vs hora local de Guatemala UTC-6).

---

### 3. Aprendizajes Clave Obtenidos al Programarlo

Construir manualmente este cliente NTP proporciona aprendizajes prácticos de alto valor en ciencias de la computación e ingeniería de software:

1. **Desmitificación de los Protocolos de Red:**
   - Se comprende que un protocolo de red no es una entidad abstracta ni "mágica", sino una convención estricta de bytes transmitidos sobre un socket UDP.
   - Se aprende a construir solicitudes binarias campo por campo consultando la RFC oficial del protocolo.

2. **Manejo Riguroso del Tiempo Real y Relojes Monótonos:**
   - Se experimenta de primera mano la fragilidad del tiempo de sistema (`CLOCK_REALTIME`), el cual puede cambiar si el usuario ajusta el reloj o si ocurre un cambio de zona horaria.
   - Se aprende la importancia crítica de usar `CLOCK_MONOTONIC` para medir intervalos de tiempo transcurridos reales durante la ejecución del programa.

3. **Dominio de la Redundancia y Tolerancia a Fallos (Multiseridor):**
   - Se aprende a diseñar software preparado para el fallo de infraestructura externa: servidores que no responden, fallos de DNS o paquetes perdidos en la red.
   - El uso de consultas concurrentes con `poll()` enseña cómo evitar que el bloqueo de un recurso de red ralentice toda la aplicación.

4. **Dominio de la Aritmética de Alta Precisión Sin Flotantes Enmascarados:**
   - Al trabajar con timestamps de 64 bits en formato 32.32, se comprende el impacto de la pérdida de precisión en números de punto flotante (`float`/`double`) al hacer restas de números muy grandes (milisegundos vs segundos desde 1900).
   - Se aprende a realizar operaciones matemáticas exactas mediante escalamiento de enteros de 64 bits.

5. **Desarrollo de Software Modular, Probable y Robusto en C:**
   - Organización de proyectos en C utilizando separación clara de responsabilidades en archivos `.h` y `.c`.
   - Creación de suites de pruebas unitarias (`test_ntp.c`) basadas en vectores matemáticos de referencia para validar el código antes de desplegarlo en producción.
   - Implementación de logging transparente que permite auditar y depurar cada etapa de la ejecución del algoritmo.

---

## Conclusión

El desarrollo de este cliente NTP en C permite pasar de la teoría de redes y sistemas operativos a la implementación práctica de bajo nivel. El proyecto demuestra cómo, mediante matemáticas sencillas de retardo y desfase combinadas con una arquitectura de red tolerante a fallos, un sistema puede mantener la sincronía temporal perfecta con fuentes atómicas de tiempo distribuidas en todo el mundo.
