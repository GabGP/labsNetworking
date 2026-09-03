#include "tcp_session.h"

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

static int send_control(tcp_session_t *s, uint8_t flags)
{
    uint8_t packet[MAX_PACKET_SIZE];
    int len = build_packet(packet, s, flags, NULL, 0);

    return send_packet(s->sockfd, packet, len, s->server_ip);
}

int tcp_session_open(tcp_session_t *s, const char *local_ip, const char *server_ip,
                     uint16_t server_port, const char *capture_interface)
{
    memset(s, 0, sizeof(*s));

    strncpy(s->local_ip, local_ip, sizeof(s->local_ip) - 1);
    strncpy(s->server_ip, server_ip, sizeof(s->server_ip) - 1);

    s->server_port = server_port;
    s->local_port = (uint16_t)(40000 + rand() % 10000);
    s->seq = (uint32_t)rand();

    printf("[*] Local port: %u\n", s->local_port);

    s->sockfd = create_raw_socket(capture_interface);
    return s->sockfd < 0 ? -1 : 0;
}

int tcp_session_handshake(tcp_session_t *s, int timeout_sec)
{
    uint8_t packet[MAX_PACKET_SIZE];
    tcp_header_t recv_tcph;
    int recv_payload_len;

    printf("[*] Sending SYN...\n");

    int len = build_packet(packet, s, TCP_SYN, NULL, 0);
    if (send_packet(s->sockfd, packet, len, s->server_ip) < 0) return -1;

    printf("[*] Waiting for SYN+ACK...\n");

    if (wait_for_packet(s, TCP_SYN | TCP_ACK, TCP_SYN | TCP_ACK,
                        &recv_tcph, NULL, 0, &recv_payload_len, timeout_sec) != 0) {
        fprintf(stderr, "[!] SYN+ACK timeout\n");
        return -1;
    }

    uint32_t server_seq = ntohl(recv_tcph.seq_num);

    s->seq++;
    s->ack = server_seq + 1;

    printf("[*] SYN+ACK received (server seq=%u)\n", server_seq);
    printf("[*] Sending final ACK...\n");

    return send_control(s, TCP_ACK) < 0 ? -1 : 0;
}

int tcp_session_send_message(tcp_session_t *s, const char *message)
{
    char buffer[TCP_SESSION_MAX_MSG + 2];
    size_t len = strlen(message);

    if (len > TCP_SESSION_MAX_MSG - 1) len = TCP_SESSION_MAX_MSG - 1;

    memcpy(buffer, message, len);

    if (len == 0 || buffer[len - 1] != '\n') buffer[len++] = '\n';

    printf("[*] Sending message: %s\n", message);

    uint8_t packet[MAX_PACKET_SIZE];
    int packet_len = build_packet(packet, s, TCP_PSH | TCP_ACK, buffer, len);

    if (send_packet(s->sockfd, packet, packet_len, s->server_ip) < 0) return -1;

    s->seq += (uint32_t)len;
    return 0;
}

int tcp_session_drain_until_close(tcp_session_t *s, char *out_buf,
                                  size_t out_buf_size, int timeout_sec)
{
    uint8_t payload[TCP_SESSION_MAX_MSG + 1];
    tcp_header_t recv_tcph;
    int payload_len;
    time_t start = time(NULL);

    printf("[*] Waiting for server response and close...\n");

    while (difftime(time(NULL), start) < timeout_sec) {
        if (wait_for_packet(s, TCP_ACK, TCP_ACK, &recv_tcph, payload,
                            TCP_SESSION_MAX_MSG, &payload_len, 2) != 0)
            continue;

        uint32_t seq = ntohl(recv_tcph.seq_num);
        int has_fin = recv_tcph.flags & TCP_FIN;

        if (seq != s->ack) {
            if (payload_len > 0 || has_fin) send_control(s, TCP_ACK);
            continue;
        }

        if (payload_len == 0 && !has_fin) continue;

        if (payload_len > 0) {
            payload[payload_len] = '\0';
            printf("[*] Server response: %s", payload);

            if (out_buf && out_buf_size > 0) {
                strncpy(out_buf, (char *)payload, out_buf_size - 1);
                out_buf[out_buf_size - 1] = '\0';
            }
        }

        s->ack += (uint32_t)payload_len;
        if (has_fin) s->ack++;

        send_control(s, TCP_ACK);

        if (has_fin) {
            printf("[*] Server FIN received and acknowledged\n");
            return 1;
        }
    }

    printf("[!] Server close timeout\n");
    return 0;
}

void tcp_session_close(tcp_session_t *s, int timeout_sec)
{
    tcp_header_t recv_tcph;
    int payload_len;

    printf("[*] Closing connection (FIN+ACK)...\n");

    send_control(s, TCP_FIN | TCP_ACK);
    s->seq++;

    wait_for_packet(s, TCP_ACK, TCP_ACK, &recv_tcph, NULL, 0, &payload_len, timeout_sec);

    close_raw_socket(s->sockfd);
    printf("[*] Done.\n");
}

void tcp_session_abort(tcp_session_t *s)
{
    close_raw_socket(s->sockfd);
}