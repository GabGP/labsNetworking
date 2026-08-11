# Laboratorio 4: Cliente NTP en C (RFC 5905)
**Curso:** Ciencias de la Computación VIII  
**Plataforma:** Linux / POSIX (Ubuntu, Solaris, BeagleBone)

---

## 📌 Descripción del Proyecto

Este laboratorio implementa un **Cliente NTP manual en C** conforme al protocolo **RFC 5905**, desarrollado sin bibliotecas NTP externas. El programa:

1. **Construye manualmente solicitudes UDP de 48 bytes** (NTP v3, LI=0, VN=3, Modo=3).
2. **Sondea simultáneamente a 10 servidores NTP públicos** (`time.google.com`, `time.apple.com`, `time.windows.com`, `time.cloudflare.com`, `time.nist.gov`, `pool.ntp.org`, etc.).
3. **Captura los 4 timestamps (T1–T4)** y realiza el algoritmo de sincronización:
   $$\text{Offset } (\theta) = \frac{(T_2 - T_1) + (T_3 - T_4)}{2}$$
   $$\text{Delay } (\delta) = (T_4 - T_1) - (T_3 - T_2)$$
4. **Filtra las respuestas y selecciona el servidor con el menor Delay ($\delta$)**, aplicando su Offset ($\theta$) a un **reloj interno simulado** (configurado en hora local de Guatemala, UTC-6).
5. **Itera la sincronización** hasta que el offset sea inferior al umbral configurable (por defecto $50\text{ ms}$), y luego mantiene una **verificación periódica continua** (por defecto cada $60\text{ segundos}$).
6. Muestra un **log detallado en consola** con desglose hexadecimal, segundos, fracción, fechas UTC/Guatemala, delay y offset de cada servidor en cada iteración.

---

## 🛠️ Estructura del Código Source

| Archivo | Descripción |
|---|---|
| `ntp_packet.h / .c` | Estructura del paquete NTP de 48 bytes, inicialización y parseo de respuestas. |
| `ntp_time.h / .c` | Conversión de épocas (NTP vs Unix), hora local de Guatemala (UTC-6), fixed-point 32.32, resta de timestamps en `int64_t`. |
| `ntp_net.h / .c` | Conexión UDP multiseridor en paralelo con `poll()` no bloqueante y manejo de timeouts/DNS. |
| `sync.h / .c` | Estado del reloj simulado interno y selección del mejor servidor (menor delay). |
| `log.h / .c` | Impresión formateada del proceso, desgloses T1–T4, cálculos y resúmenes. |
| `main.c` | Punto de entrada, parseo de argumentos CLI (`--fecha`, `--umbral-ms`, `--intervalo`), bucles de sincronización y verificación. |
| `test_ntp.c` | Runner de pruebas unitarias para validar las conversiones y los vectores de prueba del PDF. |
| `Makefile` | Compilación con `-Wall -Wextra -O2 -std=gnu99`. Targets `all`, `test`, `clean`. |

---

## ⚙️ Instrucciones de Compilación y Ejecución

### 1. Compilación
Para compilar el cliente NTP ejecute:
```bash
make
```
Esto generará el ejecutable `ntp_client`.

### 2. Ejecutar Pruebas Unitarias (Vectores del PDF)
Para verificar las conversiones de fecha y la matemática con el vector exacto del PDF (página 4):
```bash
make test
```

### 3. Ejecución del Cliente NTP

#### Ejemplo con fecha inicial simulada (atrasada):
```bash
./ntp_client --fecha "2020-09-01 21:00:17.123" --umbral-ms 50 --intervalo 60
```

#### Ejemplo con hora actual del sistema:
```bash
./ntp_client --umbral-ms 30 --intervalo 30
```

#### Ver ayuda de argumentos:
```bash
./ntp_client --help
```

---

## 📊 Parámetros Configurables (`main.c`)

- `--fecha "YYYY-MM-DD HH:MM:SS.mmm"`: Fecha y hora inicial local de Guatemala (UTC-6) para el reloj simulado.
- `--umbral-ms NUM`: Umbral en milisegundos para considerar el reloj sincronizado (Ej: `50`).
- `--intervalo NUM`: Intervalo en segundos entre verificaciones periódicas tras lograr la sincronización (Ej: `60`).
- `--help`: Muestra la guía de uso.

---

## 📦 Limpieza
Para borrar los objetos compilados y ejecutables:
```bash
make clean
```
