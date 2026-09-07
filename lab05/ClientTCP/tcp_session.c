#include "tcp_session.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "ip_header.h"
#include "tcp_header.h"
#include "checksum.h"
#include "raw_socket.h"

#define MAX_PACKET_SIZE 4096
#define LOG_DATA_PREVIEW 64  /* bytes de payload que se muestran en el log */
#define TCP_LOG_LINE_MAX 512 /* largo máximo de una línea del log */

/* ---------------------------------------------------------------------- */
/* Registro de la sesión en consola (Requerimiento 2)                      */
/* ---------------------------------------------------------------------- */

/*
 * Traduce el byte de flags TCP al texto que muestra Wireshark en su columna
 * Info, p. ej. 0x12 -> "[SYN, ACK]". El orden FIN, SYN, RST, PSH, ACK, URG
 * es el mismo que usa Wireshark, para poder comparar ambas salidas línea a
 * línea sin traducir mentalmente nada.
 */
static const char *flags_to_string(uint8_t flags, char *buf, size_t buf_size)
{
    static const struct {
        uint8_t bit;
        const char *name;
    } table[] = {
        {TCP_FIN, "FIN"}, {TCP_SYN, "SYN"}, {TCP_RST, "RST"},
        {TCP_PSH, "PSH"}, {TCP_ACK, "ACK"}, {TCP_URG, "URG"},
    };

    size_t used = 1;
    buf[0] = '[';
    buf[1] = '\0';

    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (!(flags & table[i].bit)) continue;

        int n = snprintf(buf + used, buf_size - used, "%s%s",
                         used > 1 ? ", " : "", table[i].name);
        if (n < 0 || (size_t)n >= buf_size - used) break;
        used += (size_t)n;
    }

    snprintf(buf + used, buf_size - used, "]");
    return buf;
}

/*
 * Convierte el payload en algo imprimible en una sola línea: los saltos de
 * línea se muestran como \n (importante, porque ServerTCP.java usa readLine()
 * y ese \n es parte de los bytes que viajan y que cuentan en LEN) y los bytes
 * no imprimibles como \xNN. Se recorta a LOG_DATA_PREVIEW bytes.
 */
static void format_data(const char *data, int len, char *out, size_t out_size)
{
    size_t used = 0;
    int shown = len > LOG_DATA_PREVIEW ? LOG_DATA_PREVIEW : len;

    for (int i = 0; i < shown && used + 5 < out_size; i++) {
        unsigned char c = (unsigned char)data[i];

        switch (c) {
        case '\n': memcpy(out + used, "\\n", 2);  used += 2; break;
        case '\r': memcpy(out + used, "\\r", 2);  used += 2; break;
        case '\t': memcpy(out + used, "\\t", 2);  used += 2; break;
        case '"':  memcpy(out + used, "\\\"", 2); used += 2; break;
        case '\\': memcpy(out + used, "\\\\", 2); used += 2; break;
        default:
            if (isprint(c))
                out[used++] = (char)c;
            else
                used += (size_t)snprintf(out + used, out_size - used, "\\x%02X", c);
        }
    }

    if (shown < len && used + 3 < out_size) {
        memcpy(out + used, "...", 3);
        used += 3;
    }

    out[used] = '\0';
}

/*
 * Arma la línea de log de un segmento TCP: quién envía, quién recibe, flags,
 * SEQ, ACK, LEN y datos.
 *
 * SEQ y ACK se muestran RELATIVOS: a cada número se le resta el ISN (Initial
 * Sequence Number) del extremo correspondiente, que es exactamente lo que
 * hace Wireshark por defecto. Así la sesión empieza en SEQ=0 en ambos lados
 * en lugar de en números de 10 dígitos, y la salida del programa se puede
 * comparar directamente contra la captura.
 *
 * Se separa del printf para poder verificar el formato con test_log.c sin
 * necesidad de root ni de un servidor levantado.
 */
static void format_segment(const tcp_session_t *s, int outgoing, uint8_t flags,
                           uint32_t seq, uint32_t ack, const char *data, int data_len,
                           char *out, size_t out_size)
{
    char flag_buf[32];
    char data_buf[LOG_DATA_PREVIEW * 4 + 8];
    size_t used = 0;
    int n;

    uint32_t rel_seq = seq - (outgoing ? s->local_isn : s->remote_isn);
    uint32_t rel_ack = ack - (outgoing ? s->remote_isn : s->local_isn);

    n = snprintf(out + used, out_size - used, "%s  %-12s SEQ=%-6u",
                 outgoing ? "Client → Server" : "Server → Client",
                 flags_to_string(flags, flag_buf, sizeof(flag_buf)),
                 rel_seq);
    if (n < 0 || (size_t)n >= out_size - used) return;
    used += (size_t)n;

    /* El campo ACK del header sólo tiene significado si la bandera ACK está
       encendida; en el SYN inicial se ignora, igual que en Wireshark. */
    if (flags & TCP_ACK) {
        n = snprintf(out + used, out_size - used, " ACK=%-6u", rel_ack);
        if (n < 0 || (size_t)n >= out_size - used) return;
        used += (size_t)n;
    }

    if (data_len > 0) {
        format_data(data, data_len, data_buf, sizeof(data_buf));
        n = snprintf(out + used, out_size - used, " LEN=%-4d DATA=\"%s\"", data_len, data_buf);
        if (n > 0 && (size_t)n < out_size - used) used += (size_t)n;
        else used = strlen(out);
    }

    /* Los campos se alinean con %-6u / %-4d; quitamos el relleno sobrante
       del final para no dejar espacios colgando en la línea. */
    while (used > 0 && out[used - 1] == ' ') out[--used] = '\0';
}

static void log_segment(const tcp_session_t *s, int outgoing, uint8_t flags,
                        uint32_t seq, uint32_t ack, const char *data, int data_len)
{
    char line[TCP_LOG_LINE_MAX];

    format_segment(s, outgoing, flags, seq, ack, data, data_len, line, sizeof(line));

    printf("%s\n", line);
    fflush(stdout);
}

static void print_banner(const char *title, const char *fill)
{
    printf("\n");
    for (int i = 0; i < 18; i++) printf("%s", fill);
    printf(" %s ", title);
    for (int i = 0; i < 18; i++) printf("%s", fill);
    printf("\n");
    fflush(stdout);
}

/* El cierre puede iniciarlo el servidor (Opción 1) o el cliente (Opción 2);
   este guard evita imprimir el separador dos veces si ocurren ambos. */
static void print_close_banner(tcp_session_t *s)
{
    if (s->close_logged) return;
    s->close_logged = 1;
    print_banner("CLOSE", "─");
}

/* ---------------------------------------------------------------------- */
/* Construcción y transporte de segmentos                                  */
/* ---------------------------------------------------------------------- */

static int build_packet(uint8_t *packet, const tcp_session_t *s, uint8_t flags,
                        const char *data, size_t data_len)
{
    ip_header_t *iph = (ip_header_t *)packet;
    tcp_header_t *tcph = (tcp_header_t *)(packet + sizeof(ip_header_t));
    uint8_t *payload = packet + sizeof(ip_header_t) + sizeof(tcp_header_t);

    if (data && data_len) memcpy(payload, data, data_len);

    uint16_t tcp_len = (uint16_t)(sizeof(tcp_header_t) + data_len);

    build_tcp_header(tcph, s->local_port, s->server_port, s->seq, s->ack, flags, 5840);
    build_ip_header(iph, s->local_ip, s->server_ip, tcp_len);

    iph->checksum = calculate_checksum((uint16_t *)iph, sizeof(ip_header_t));
    tcph->checksum = calculate_tcp_checksum(iph->src_addr, iph->dst_addr, (uint8_t *)tcph, tcp_len);

    return (int)(sizeof(ip_header_t) + tcp_len);
}

static int wait_for_packet(tcp_session_t *s, uint8_t flags_mask, uint8_t flags_value,
                           tcp_header_t *out_tcph, uint8_t *out_payload,
                           int max_payload, int *out_payload_len, int timeout_sec)
{
    static uint8_t buffer[MAX_PACKET_SIZE];
    time_t start = time(NULL);

    while (difftime(time(NULL), start) < timeout_sec) {
        int len = receive_packet(s->sockfd, buffer, sizeof(buffer), 200);
        if (len < (int)(sizeof(ip_header_t) + sizeof(tcp_header_t))) continue;

        ip_header_t *iph = (ip_header_t *)buffer;
        if (iph->protocol != IPPROTO_TCP) continue;

        int ip_len = (iph->ihl_version & 0x0F) * 4;
        if (len < ip_len + (int)sizeof(tcp_header_t)) continue;

        tcp_header_t *tcph = (tcp_header_t *)(buffer + ip_len);
        struct in_addr addr = {.s_addr = iph->src_addr};

        /* Este filtro hace las veces de "demultiplexado": nos quedamos sólo
           con los segmentos de la 4-tupla de nuestra conexión (IP origen,
           puerto origen, IP destino, puerto destino). */
        if (strcmp(inet_ntoa(addr), s->server_ip) != 0) continue;
        if (ntohs(tcph->src_port) != s->server_port) continue;
        if (ntohs(tcph->dst_port) != s->local_port) continue;
        if ((tcph->flags & flags_mask) != flags_value) continue;

        memcpy(out_tcph, tcph, sizeof(*out_tcph));

        int tcp_header_len = ((tcph->data_offset_reserved >> 4) & 0x0F) * 4;
        int payload_offset = ip_len + tcp_header_len;
        int payload_len = len - payload_offset;

        if (payload_len < 0) payload_len = 0;

        if (out_payload && max_payload > 0) {
            int copy_len = payload_len < max_payload ? payload_len : max_payload;
            memcpy(out_payload, buffer + payload_offset, copy_len);
            *out_payload_len = copy_len;
        } else {
            *out_payload_len = payload_len;
        }

        return 0;
    }

    return -1;
}

/* Envía un segmento sin datos (ACK, FIN+ACK...) y lo deja registrado. */
static int send_control(tcp_session_t *s, uint8_t flags)
{
    uint8_t packet[MAX_PACKET_SIZE];
    int len = build_packet(packet, s, flags, NULL, 0);

    int rc = send_packet(s->sockfd, packet, len, s->server_ip);
    if (rc >= 0) log_segment(s, 1, flags, s->seq, s->ack, NULL, 0);

    return rc;
}

/* ---------------------------------------------------------------------- */
/* API pública de la sesión                                                */
/* ---------------------------------------------------------------------- */

int tcp_session_open(tcp_session_t *s, const char *local_ip, const char *server_ip,
                     uint16_t server_port, const char *capture_interface)
{
    memset(s, 0, sizeof(*s));

    strncpy(s->local_ip, local_ip, sizeof(s->local_ip) - 1);
    strncpy(s->server_ip, server_ip, sizeof(s->server_ip) - 1);

    s->server_port = server_port;
    s->local_port = (uint16_t)(40000 + rand() % 10000); /* puerto efímero */
    s->seq = (uint32_t)rand();                          /* ISN del cliente */
    s->local_isn = s->seq;

    printf("[*] Conexión: %s:%u → %s:%u\n",
           s->local_ip, s->local_port, s->server_ip, s->server_port);
    printf("[*] Puerto local: %u  (úselo en la regla de firewall que descarta el RST del kernel)\n",
           s->local_port);

    s->sockfd = create_raw_socket(capture_interface);
    return s->sockfd < 0 ? -1 : 0;
}

int tcp_session_handshake(tcp_session_t *s, int timeout_sec)
{
    uint8_t packet[MAX_PACKET_SIZE];
    tcp_header_t recv_tcph;
    int recv_payload_len;

    print_banner("TCP SESSION", "═");

    /* Paso 1: SYN. No lleva datos, pero consume un número de secuencia
       (por eso más abajo s->seq++). */
    int len = build_packet(packet, s, TCP_SYN, NULL, 0);
    if (send_packet(s->sockfd, packet, len, s->server_ip) < 0) return -1;
    log_segment(s, 1, TCP_SYN, s->seq, 0, NULL, 0);

    /* Paso 2: SYN+ACK del servidor, que trae el ISN del otro extremo. */
    if (wait_for_packet(s, TCP_SYN | TCP_ACK, TCP_SYN | TCP_ACK,
                        &recv_tcph, NULL, 0, &recv_payload_len, timeout_sec) != 0) {
        fprintf(stderr, "[!] Timeout esperando SYN+ACK\n");
        return -1;
    }

    uint32_t server_seq = ntohl(recv_tcph.seq_num);
    s->remote_isn = server_seq;

    log_segment(s, 0, recv_tcph.flags, server_seq, ntohl(recv_tcph.ack_num), NULL, 0);

    /* El SYN de cada lado cuenta como un byte de secuencia: por eso nuestro
       seq avanza en 1 y el ACK que enviamos es el ISN del servidor + 1. */
    s->seq++;
    s->ack = server_seq + 1;

    /* Paso 3: ACK final. A partir de aquí la conexión está establecida. */
    if (send_control(s, TCP_ACK) < 0) return -1;

    print_banner("DATA", "─");
    return 0;
}

int tcp_session_send_message(tcp_session_t *s, const char *message)
{
    char buffer[TCP_SESSION_MAX_MSG + 2];
    size_t len = strlen(message);

    if (len > TCP_SESSION_MAX_MSG - 1) len = TCP_SESSION_MAX_MSG - 1;

    memcpy(buffer, message, len);

    /* ServerTCP.java lee con readLine(): sin '\n' se quedaría bloqueado
       esperando el resto de la línea y nunca respondería. */
    if (len == 0 || buffer[len - 1] != '\n') buffer[len++] = '\n';

    uint8_t packet[MAX_PACKET_SIZE];
    int packet_len = build_packet(packet, s, TCP_PSH | TCP_ACK, buffer, len);

    if (send_packet(s->sockfd, packet, packet_len, s->server_ip) < 0) return -1;

    log_segment(s, 1, TCP_PSH | TCP_ACK, s->seq, s->ack, buffer, (int)len);

    /* Los datos sí consumen secuencia: nuestro próximo segmento empieza
       len bytes más adelante. */
    s->seq += (uint32_t)len;
    return 0;
}

tcp_rx_result_t tcp_session_recv(tcp_session_t *s, char *out_buf,
                                 size_t out_buf_size, int timeout_sec)
{
    uint8_t payload[TCP_SESSION_MAX_MSG + 1];
    tcp_header_t recv_tcph;
    int payload_len;
    time_t start = time(NULL);

    while (difftime(time(NULL), start) < timeout_sec) {
        if (wait_for_packet(s, TCP_ACK, TCP_ACK, &recv_tcph, payload,
                            TCP_SESSION_MAX_MSG, &payload_len, 2) != 0)
            continue;

        uint32_t seq = ntohl(recv_tcph.seq_num);
        uint32_t ack = ntohl(recv_tcph.ack_num);
        int has_fin = recv_tcph.flags & TCP_FIN;

        payload[payload_len] = '\0';

        if (has_fin) print_close_banner(s);
        log_segment(s, 0, recv_tcph.flags, seq, ack, (const char *)payload, payload_len);

        if (seq != s->ack) {
            /* Fuera de orden o retransmisión: no avanzamos s->ack, pero
               reconocemos de nuevo lo último que sí procesamos para que el
               servidor deje de retransmitir. */
            printf("    (fuera de orden o retransmisión: se esperaba SEQ=%u)\n",
                   s->ack - s->remote_isn);
            if (payload_len > 0 || has_fin) send_control(s, TCP_ACK);
            continue;
        }

        /* ACK puro del servidor: ya quedó registrado en el log, pero no
           avanza secuencia ni se responde. Seguimos esperando el eco. */
        if (payload_len == 0 && !has_fin) continue;

        if (payload_len > 0 && out_buf && out_buf_size > 0) {
            strncpy(out_buf, (char *)payload, out_buf_size - 1);
            out_buf[out_buf_size - 1] = '\0';
        }

        /* Datos y FIN consumen secuencia; el ACK que enviamos lo refleja. */
        s->ack += (uint32_t)payload_len;
        if (has_fin) s->ack++;

        send_control(s, TCP_ACK);

        return has_fin ? TCP_RX_FIN : TCP_RX_DATA;
    }

    return TCP_RX_TIMEOUT;
}

void tcp_session_close(tcp_session_t *s, int timeout_sec)
{
    tcp_header_t recv_tcph;
    int payload_len;

    print_close_banner(s);

    send_control(s, TCP_FIN | TCP_ACK);
    s->seq++; /* el FIN también consume un número de secuencia */

    if (wait_for_packet(s, TCP_ACK, TCP_ACK, &recv_tcph, NULL, 0,
                        &payload_len, timeout_sec) == 0)
        log_segment(s, 0, recv_tcph.flags, ntohl(recv_tcph.seq_num),
                    ntohl(recv_tcph.ack_num), NULL, 0);
    else
        fprintf(stderr, "[!] Timeout esperando el ACK final del servidor\n");

    close_raw_socket(s->sockfd);
    print_banner("CONNECTION CLOSED", "═");
}

void tcp_session_abort(tcp_session_t *s)
{
    close_raw_socket(s->sockfd);
}
