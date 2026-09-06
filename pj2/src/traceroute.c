/*
 * traceroute - clon en C sobre sockets RAW.
 * Proyecto #02, Ciencias de la Computacion VIII.
 *
 * Todo el protocolo esta escrito a mano: el header IP y el header UDP se
 * arman byte por byte (ver packet.c), el TTL se incrementa salto por salto y
 * las respuestas ICMP se emparejan con su sondeo leyendo el header UDP que el
 * router cita dentro del mensaje ICMP. No se usa ninguna libreria de paquetes
 * ni se invoca el traceroute del sistema.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "packet.h"

/* Mismo puerto base que el traceroute de Linux */
#define BASE_PORT 33434

static void usage(void)
{
    fprintf(stderr,
            "usage: traceroute [-n] [-f first_ttl] [-m max_ttl] [-q nqueries]"
            " [-w waittime_s] [-z sendwait_ms] host\n");
}

/* Lee un entero de la linea de comandos y valida su rango */
static int parse_int_arg(const char *arg, int min, int max, const char *name,
                         int *out)
{
    char *end;
    long value;

    errno = 0;
    value = strtol(arg, &end, 10);
    if (*arg == '\0' || *end != '\0' || errno != 0 || value < min || value > max) {
        fprintf(stderr, "traceroute: invalid value for %s: \"%s\" (expected %d..%d)\n",
                name, arg, min, max);
        return -1;
    }
    *out = (int)value;
    return 0;
}

/* Milisegundos transcurridos entre dos marcas de CLOCK_MONOTONIC */
static double elapsed_ms(const struct timespec *from, const struct timespec *to)
{
    return (double)(to->tv_sec - from->tv_sec) * 1000.0
         + (double)(to->tv_nsec - from->tv_nsec) / 1000000.0;
}

/* Resuelve el host destino a una unica direccion IPv4 */
static int resolve_target(const char *host, struct in_addr *addr)
{
    struct addrinfo hints, *res;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;        /* solo IPv4 */
    hints.ai_socktype = SOCK_DGRAM;

    rc = getaddrinfo(host, NULL, &hints, &res);
    if (rc != 0) {
        fprintf(stderr, "traceroute: unknown host %s (%s)\n", host, gai_strerror(rc));
        return -1;
    }
    *addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);
    return 0;
}

/*
 * Averigua con que IP local saldrian los paquetes hacia el destino:
 * se hace connect() de un socket UDP desechable (no envia nada) y se lee
 * la direccion que el kernel eligio segun su tabla de rutas.
 */
static int local_source_addr(struct in_addr dst, struct in_addr *src)
{
    struct sockaddr_in sa;
    socklen_t len = sizeof(sa);
    int fd;

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr   = dst;
    sa.sin_port   = htons(BASE_PORT);

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0 ||
        getsockname(fd, (struct sockaddr *)&sa, &len) < 0) {
        close(fd);
        return -1;
    }
    close(fd);
    *src = sa.sin_addr;
    return 0;
}

/* Arma la etiqueta de un salto: "hostname (ip)", o solo "ip" con -n */
static void format_addr(struct in_addr addr, int numeric, char *out, size_t outlen)
{
    char ip[INET_ADDRSTRLEN];
    char host[NI_MAXHOST];
    struct sockaddr_in sa;

    inet_ntop(AF_INET, &addr, ip, sizeof(ip));
    if (numeric) {
        snprintf(out, outlen, "%s", ip);
        return;
    }

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr   = addr;
    if (getnameinfo((struct sockaddr *)&sa, sizeof(sa), host, sizeof(host),
                    NULL, 0, NI_NAMEREQD) == 0) {
        snprintf(out, outlen, "%s (%s)", host, ip);
    } else {
        snprintf(out, outlen, "%s (%s)", ip, ip);   /* sin PTR: se repite la IP */
    }
}

/* Arma y envia un sondeo con el TTL y el puerto destino indicados */
static int send_probe(int fd, struct in_addr src, struct in_addr dst, int ttl,
                      uint16_t src_port, uint16_t dst_port, uint16_t ip_id)
{
    static uint8_t buf[PROBE_LEN] __attribute__((aligned(4)));
    struct sockaddr_in to;

    build_probe_packet(buf, src.s_addr, dst.s_addr, (uint8_t)ttl,
                       src_port, dst_port, ip_id);

    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr   = dst;
    to.sin_port   = htons(dst_port);

    if (sendto(fd, buf, PROBE_LEN, 0,
               (struct sockaddr *)&to, sizeof(to)) != PROBE_LEN) {
        return -1;
    }
    return 0;
}

/*
 * Espera hasta waittime segundos una respuesta ICMP que corresponda al sondeo
 * con puerto destino dst_port. Cualquier otro paquete ICMP (un ping en
 * paralelo, la respuesta tardia de otro sondeo) se descarta y se sigue
 * esperando hasta que venza el plazo. Devuelve 1 si llego, 0 si expiro.
 */
static int wait_reply(int fd, const struct timespec *sent, int waittime,
                      uint16_t src_port, uint16_t dst_port,
                      struct in_addr *from, icmp_reply_t *reply, double *rtt_ms)
{
    static uint8_t buf[2048] __attribute__((aligned(4)));
    struct timespec now, deadline;

    deadline = *sent;
    deadline.tv_sec += waittime;

    for (;;) {
        struct sockaddr_in sa;
        socklen_t slen = sizeof(sa);
        struct timeval tv;
        fd_set rfds;
        double remaining;
        ssize_t n;
        int rc;

        clock_gettime(CLOCK_MONOTONIC, &now);
        remaining = elapsed_ms(&now, &deadline);
        if (remaining <= 0.0) {
            return 0;
        }
        tv.tv_sec  = (time_t)(remaining / 1000.0);
        tv.tv_usec = (suseconds_t)((remaining - (double)tv.tv_sec * 1000.0) * 1000.0);

        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        rc = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        if (rc == 0) {
            return 0;                    /* vencio el plazo: timeout */
        }

        n = recvfrom(fd, buf, sizeof(buf), 0, (struct sockaddr *)&sa, &slen);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return 0;
        }
        clock_gettime(CLOCK_MONOTONIC, &now);

        if (!parse_icmp_reply(buf, (int)n, src_port, reply)) {
            continue;                    /* no es respuesta a un sondeo nuestro */
        }
        if (reply->probe_port != dst_port) {
            continue;                    /* es de otro sondeo: se ignora */
        }

        *from   = sa.sin_addr;
        *rtt_ms = elapsed_ms(sent, &now);
        return 1;
    }
}

int main(int argc, char **argv)
{
    int first_ttl = 1, max_ttl = 64, nqueries = 3, waittime = 3, sendwait = 100;
    int numeric = 0;
    int send_fd, recv_fd, on = 1, opt, ttl, done = 0;
    uint16_t src_port, dst_port, ip_id = 1;
    struct in_addr dst_addr, src_addr;
    char dst_ip[INET_ADDRSTRLEN];
    const char *host;

    while ((opt = getopt(argc, argv, "nf:m:q:w:z:")) != -1) {
        switch (opt) {
        case 'n':
            numeric = 1;
            break;
        case 'f':
            if (parse_int_arg(optarg, 1, 255, "-f", &first_ttl) < 0) return 1;
            break;
        case 'm':
            if (parse_int_arg(optarg, 1, 255, "-m", &max_ttl) < 0) return 1;
            break;
        case 'q':
            if (parse_int_arg(optarg, 1, 10, "-q", &nqueries) < 0) return 1;
            break;
        case 'w':
            if (parse_int_arg(optarg, 1, 60, "-w", &waittime) < 0) return 1;
            break;
        case 'z':
            if (parse_int_arg(optarg, 0, 10000, "-z", &sendwait) < 0) return 1;
            break;
        default:
            usage();
            return 1;
        }
    }

    if (optind != argc - 1) {
        usage();
        return 1;
    }
    host = argv[optind];

    if (first_ttl > max_ttl) {
        fprintf(stderr,
                "traceroute: first ttl (%d) may not be greater than max ttl (%d)\n",
                first_ttl, max_ttl);
        usage();
        return 1;
    }

    /* Los sockets RAW son privilegiados: sin root no hay nada que hacer */
    if (geteuid() != 0) {
        fprintf(stderr, "traceroute: raw sockets require root privileges; "
                        "run it with sudo\n");
        return 1;
    }

    if (resolve_target(host, &dst_addr) < 0) {
        return 1;
    }
    if (local_source_addr(dst_addr, &src_addr) < 0) {
        perror("traceroute: cannot determine local source address");
        return 1;
    }
    inet_ntop(AF_INET, &dst_addr, dst_ip, sizeof(dst_ip));

    /* Socket de envio: nosotros escribimos el header IP completo */
    send_fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
    if (send_fd < 0) {
        perror("traceroute: socket(SOCK_RAW, IPPROTO_RAW)");
        return 1;
    }
    if (setsockopt(send_fd, IPPROTO_IP, IP_HDRINCL, &on, sizeof(on)) < 0) {
        perror("traceroute: setsockopt(IP_HDRINCL)");
        return 1;
    }

    /* Socket de recepcion: aqui llegan los ICMP de los routers */
    recv_fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (recv_fd < 0) {
        perror("traceroute: socket(SOCK_RAW, IPPROTO_ICMP)");
        return 1;
    }

    /* Puerto origen fijo de la corrida: marca todos nuestros sondeos */
    src_port = (uint16_t)((getpid() & 0x7FFF) | 0x8000);
    dst_port = BASE_PORT;

    printf("traceroute to %s (%s), %d hops max, %d byte packets\n",
           host, dst_ip, max_ttl, PROBE_LEN);

    for (ttl = first_ttl; ttl <= max_ttl && !done; ttl++) {
        struct in_addr last_addr;
        int have_last = 0, q;

        last_addr.s_addr = 0;
        printf("%2d ", ttl);
        fflush(stdout);

        for (q = 0; q < nqueries; q++) {
            struct timespec sent;
            struct in_addr from;
            icmp_reply_t reply;
            double rtt = 0.0;

            clock_gettime(CLOCK_MONOTONIC, &sent);
            if (send_probe(send_fd, src_addr, dst_addr, ttl,
                           src_port, dst_port, ip_id++) < 0) {
                printf("\n");
                perror("traceroute: sendto");
                return 1;
            }

            if (!wait_reply(recv_fd, &sent, waittime, src_port, dst_port,
                            &from, &reply, &rtt)) {
                printf(" *");                       /* nadie respondio a tiempo */
            } else {
                if (!have_last || from.s_addr != last_addr.s_addr) {
                    char label[NI_MAXHOST + INET_ADDRSTRLEN + 4];
                    format_addr(from, numeric, label, sizeof(label));
                    printf(" %s", label);
                    last_addr = from;
                    have_last = 1;
                }
                printf("  %.3f ms", rtt);

                if (reply.type == ICMP_DEST_UNREACH) {
                    /* Llegamos al destino: puerto cerrado (code 3) o un error
                       de entrega, que se anota con " !<code>" */
                    if (reply.code != ICMP_PORT_UNREACH_CODE) {
                        printf(" !%d", reply.code);
                    }
                    done = 1;
                }
            }
            fflush(stdout);

            dst_port++;
            if (sendwait > 0) {
                struct timespec pause;
                pause.tv_sec  = sendwait / 1000;
                pause.tv_nsec = (long)(sendwait % 1000) * 1000000L;
                nanosleep(&pause, NULL);
            }
        }
        printf("\n");
        fflush(stdout);
    }

    close(send_fd);
    close(recv_fd);
    return 0;
}
