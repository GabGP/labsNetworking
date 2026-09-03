#ifndef TCP_SESSION_H
#define TCP_SESSION_H

#include <stdint.h>
#include <stddef.h>

#define TCP_SESSION_MAX_MSG 1024

typedef struct {
    int sockfd;
    char local_ip[46];
    char server_ip[46];
    uint16_t local_port;
    uint16_t server_port;
    uint32_t seq;  /* Next local sequence number */
    uint32_t ack;  /* Next expected server sequence number */
} tcp_session_t;

int tcp_session_open(tcp_session_t *s, const char *local_ip, const char *server_ip, uint16_t server_port, const char *capture_interface);

int tcp_session_handshake(tcp_session_t *s, int timeout_sec);
int tcp_session_send_message(tcp_session_t *s, const char *message);
int tcp_session_drain_until_close(tcp_session_t *s, char *out_buf, size_t out_buf_size, int timeout_sec);
void tcp_session_close(tcp_session_t *s, int wait_ack_timeout_sec);
void tcp_session_abort(tcp_session_t *s);

#endif