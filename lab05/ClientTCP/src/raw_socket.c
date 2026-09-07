#include "raw_socket.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __APPLE__
/* ---------------------------------------------------------------------- */
/* Implementación para macOS/BSD: enviar por raw socket, recibir por pcap */
/* ---------------------------------------------------------------------- */

#include <pcap/pcap.h>
/* No incluir <net/bpf.h>: pcap/pcap.h ya trae pcap/bpf.h, que define las
   mismas estructuras (struct bpf_program, struct bpf_insn, etc.) y
   provoca errores de "redefinition" si se incluyen ambos. */

static pcap_t *g_pcap_handle = NULL;
static int g_pcap_link_offset = 0; /* bytes a saltar antes de llegar al header IP */

int create_raw_socket(const char *capture_interface) {
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sockfd < 0) {
        perror("[raw_socket] Error creando el socket de envio (¿estas corriendo con sudo?)");
        return -1;
    }

    int one = 1;
    if (setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("[raw_socket] Error en setsockopt(IP_HDRINCL)");
        close(sockfd);
        return -1;
    }

    char errbuf[PCAP_ERRBUF_SIZE];
    const char *iface = (capture_interface != NULL) ? capture_interface : "lo0";

    /* to_ms=1: el timeout real se maneja "a mano" en receive_packet() */
    g_pcap_handle = pcap_open_live(iface, 65535, 0, 1, errbuf);
    if (g_pcap_handle == NULL) {
        fprintf(stderr, "[raw_socket] Error abriendo pcap en '%s': %s\n", iface, errbuf);
        fprintf(stderr, "[raw_socket] Verifica que corres con sudo y que la interfaz existe (ifconfig -l)\n");
        close(sockfd);
        return -1;
    }

    if (pcap_setnonblock(g_pcap_handle, 1, errbuf) < 0) {
        fprintf(stderr, "[raw_socket] Aviso: pcap_setnonblock falló: %s\n", errbuf);
    }

    /* Filtramos sólo TCP para no procesar tráfico irrelevante */
    struct bpf_program filter;
    if (pcap_compile(g_pcap_handle, &filter, "tcp", 1, PCAP_NETMASK_UNKNOWN) == 0) {
        pcap_setfilter(g_pcap_handle, &filter);
        pcap_freecode(&filter);
    }

    switch (pcap_datalink(g_pcap_handle)) {
        case DLT_NULL:   g_pcap_link_offset = 4;  break; /* loopback: 4 bytes de "familia" */
        case DLT_EN10MB: g_pcap_link_offset = 14; break; /* Ethernet normal */
        default:         g_pcap_link_offset = 0;  break;
    }

    return sockfd;
}

int receive_packet(int sockfd, uint8_t *buffer, size_t buffer_len, int timeout_ms) {
    (void)sockfd; /* en macOS se lee de g_pcap_handle, no del socket */

    if (g_pcap_handle == NULL) {
        return -1;
    }

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        struct pcap_pkthdr *header;
        const u_char *data;
        int rc = pcap_next_ex(g_pcap_handle, &header, &data);

        if (rc == 1) {
            if ((int)header->caplen > g_pcap_link_offset) {
                int ip_len = (int)header->caplen - g_pcap_link_offset;
                if (ip_len > (int)buffer_len) ip_len = (int)buffer_len;
                memcpy(buffer, data + g_pcap_link_offset, ip_len);
                return ip_len;
            }
            /* frame demasiado chico como para tener header IP: seguir esperando */
        } else if (rc < 0) {
            return -1; /* error de pcap */
        }
        /* rc == 0: todavía no hay paquete (no bloqueante) */

        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000 +
                           (now.tv_nsec - start.tv_nsec) / 1000000;
        if (elapsed_ms >= timeout_ms) {
            return -1;
        }
        usleep(1000); /* 1ms, para no consumir 100% de CPU en el sondeo */
    }
}

void close_raw_socket(int sockfd) {
    if (g_pcap_handle != NULL) {
        pcap_close(g_pcap_handle);
        g_pcap_handle = NULL;
    }
    close(sockfd);
}

#else
/* ---------------------------------------------------------------------- */
/* Implementación para Linux: enviar y recibir por el mismo raw socket    */
/* ---------------------------------------------------------------------- */

#include <sys/time.h>

int create_raw_socket(const char *capture_interface) {
    (void)capture_interface; /* no se usa en Linux */

    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_TCP);
    if (sockfd < 0) {
        perror("[raw_socket] Error creando el socket (¿estas corriendo como root?)");
        return -1;
    }

    int one = 1;
    if (setsockopt(sockfd, IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
        perror("[raw_socket] Error en setsockopt(IP_HDRINCL)");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

int receive_packet(int sockfd, uint8_t *buffer, size_t buffer_len, int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in src;
    socklen_t src_len = sizeof(src);

    ssize_t received = recvfrom(sockfd, buffer, buffer_len, 0,
                                 (struct sockaddr *)&src, &src_len);
    if (received < 0) {
        return -1; /* timeout (EAGAIN/EWOULDBLOCK) u otro error */
    }
    return (int)received;
}

void close_raw_socket(int sockfd) {
    close(sockfd);
}

#endif /* __APPLE__ */

/* send_packet es igual en ambas plataformas: siempre se manda por el raw socket */
int send_packet(int sockfd, uint8_t *packet, size_t packet_len, const char *dst_ip) {
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_addr.s_addr = inet_addr(dst_ip);

    ssize_t sent = sendto(sockfd, packet, packet_len, 0,
                           (struct sockaddr *)&dest, sizeof(dest));
    if (sent < 0) {
        perror("[raw_socket] Error en sendto");
        return -1;
    }
    return (int)sent;
}