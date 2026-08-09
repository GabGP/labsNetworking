# prompt.md — Prompts por partes para construir el Lab 4 (Cliente NTP en C)

Usa estas partes en orden, una por sesión/mensaje. Cada parte asume que las
anteriores ya están hechas y compilando. El contexto completo del laboratorio
está en `CLAUDE.md` de esta carpeta.

---

## Parte 1 — Estructura del proyecto y paquete NTP

> Crea la base del cliente NTP en C (POSIX, para Ubuntu/Solaris/BeagleBone):
>
> - Estructura de archivos: `ntp_packet.c/h`, `ntp_time.c/h`, `ntp_net.c/h`,
>   `sync.c/h`, `log.c/h`, `main.c`, y un `Makefile` con targets `all` y
>   `clean` usando `-Wall -Wextra -O2 -std=gnu99`.
> - En `ntp_packet`: define la estructura del paquete NTP de 48 bytes según
>   RFC 5905 (LI 2 bits, VN 3 bits, Mode 3 bits, Stratum, Poll, Precision,
>   Root Delay, Root Dispersion, Reference ID, y los 4 timestamps de 64 bits:
>   Reference, Originate, Receive, Transmit). Usa `uint8_t`/`uint32_t` y
>   serialización explícita con `htonl`/`ntohl` (no dependas del padding del
>   struct).
> - Función para construir una solicitud de cliente v3: primer byte
>   `LI=0, VN=3, Mode=3` (0x1B), y función para parsear la respuesta
>   extrayendo T2 (Receive) y T3 (Transmit).
> - Todo debe compilar limpio con `make`.

## Parte 2 — Conversión de tiempo (NTP epoch, UTC, Guatemala)

> Implementa en `ntp_time.c` las conversiones de tiempo:
>
> - Tipo para timestamp NTP de 64 bits: 32 bits de segundos desde
>   1900-01-01 00:00:00 UTC y 32 bits de fracción (`frac/2^32` segundos).
> - Conversión Unix→NTP: sumar 2,208,988,800 segundos. Conversión
>   local Guatemala→UTC: sumar 6 horas (21600 s), sin DST.
> - Fracción: `frac32 = round(frac_seg * 4294967296.0)` con manejo del carry
>   si el redondeo llega a 2^32.
> - Parseo de una fecha configurable formato Guatemala
>   `"YYYY-MM-DD HH:MM:SS.mmm"` a timestamp NTP, y el camino inverso para
>   imprimir.
> - Resta segura de timestamps NTP usando `int64_t` sobre el fixed-point
>   32.32, y conversión del resultado a milisegundos para mostrar.
> - Agrega una prueba (target `make test` o `#ifdef TEST`) con el vector del
>   PDF: local `2020-09-01 21:00:17.123` Guatemala → NTP segundos
>   `3808004417`, fracción `528280977`; y con
>   T1=0xE33A16911F9843A4, T2=0xE33A18CBBE25E755, T3=0xE33A18CC05A4D480,
>   T4=0xE33A169200C3F6D6 → delay ≈ 601 ms, offset ≈ 586 s + 319.5 ms.

## Parte 3 — Red: consulta UDP a 7+ servidores en paralelo

> Implementa en `ntp_net.c` la consulta a servidores NTP:
>
> - Lista de servidores (mínimo 7 simultáneos): time.google.com,
>   time.apple.com, time.windows.com, time.cloudflare.com, ntp.cern.ch,
>   time.nist.gov, time-a.nist.gov, time-b.nist.gov, pool.ntp.org,
>   north-america.pool.ntp.org. Puerto UDP 123.
> - Resolución con `getaddrinfo` (IPv4); si un servidor no resuelve, loguear
>   y continuar con los demás.
> - Enviar la solicitud a todos y esperar respuestas con `select`/`poll` o un
>   socket por servidor con `SO_RCVTIMEO` (timeout ~2–3 s); un servidor caído
>   no debe bloquear a los demás.
> - Por cada servidor: registrar T1 (reloj interno simulado al enviar),
>   T2 y T3 (de la respuesta), T4 (reloj interno simulado al recibir).
> - Devolver un arreglo de resultados por servidor: nombre, IP, T1–T4, y si
>   respondió o no.

## Parte 4 — Algoritmo de sincronización y reloj simulado

> Implementa en `sync.c` el algoritmo de sincronización:
>
> - Reloj interno simulado: parte de la fecha configurable (Parte 2) y avanza
>   con el tiempo real transcurrido (`clock_gettime(CLOCK_MONOTONIC)`) más el
>   ajuste acumulado.
> - Para cada respuesta: `offset = ((T2-T1)+(T3-T4))/2` y
>   `delay = (T4-T1)-(T3-T2)`, en fixed-point 64 bits con salida en ms.
> - Elegir el mejor resultado: el offset del servidor con **menor delay**.
> - Aplicar el offset al reloj simulado y repetir el ciclo completo
>   (consultar de nuevo los 7+ servidores) hasta que `|offset|` sea menor o
>   igual al umbral configurable en milisegundos.
> - Al quedar sincronizado, seguir verificando cada intervalo configurable
>   (default 60 s) en un loop infinito.

## Parte 5 — Log claro en consola

> Implementa en `log.c` el log del proceso (puede ser en español; es
> requisito de calificación):
>
> - Por iteración: número de iteración, hora del reloj interno simulado.
> - Por servidor: nombre e IP, T1–T4 en hex de 64 bits y en
>   `segundos - fracción` y fecha UTC legible, delay y offset en ms.
> - Servidor elegido (menor delay) y el ajuste aplicado al reloj.
> - Estado final: reloj antes/después, offset restante, y mensaje claro de
>   "SINCRONIZADO" cuando `|offset| <= umbral`, más el log de cada
>   verificación periódica posterior.

## Parte 6 — Configuración por CLI y main

> Implementa `main.c` con configuración **antes de ejecutar** vía argumentos:
>
> - `--fecha "YYYY-MM-DD HH:MM:SS.mmm"` fecha/hora local inicial (Guatemala)
>   del reloj simulado (default: hora del sistema).
> - `--umbral-ms N` umbral de offset en ms para considerarse sincronizado.
> - `--intervalo N` segundos entre verificaciones tras sincronizar
>   (default 60).
> - `--help` con uso claro. Validar argumentos y mostrar la configuración al
>   iniciar.
> - Conectar todo: configurar reloj → loop de sincronización → verificación
>   periódica, con manejo limpio de Ctrl+C (SIGINT) para terminar.

## Parte 7 — Verificación final de entrega

> Revisa contra el PDF que todo esté cumplido y prepara la entrega:
>
> - `make` compila limpio (`-Wall -Wextra`) en Linux; `make clean` funciona.
> - Corre `make test` y verifica los vectores del PDF.
> - Ejecuta el programa con una fecha atrasada varios minutos y confirma en
>   el log: 7+ servidores consultados, offset/delay por servidor, elección
>   por menor delay, iteraciones hasta offset ~0, verificación periódica.
> - Checklist de la entrega ZIP al GES: archivos `.c`/`.h`, `Makefile`
>   (el video de 5–10 min lo grabo yo). Genera un README breve con
>   instrucciones de compilación y ejecución para acompañar el video.
