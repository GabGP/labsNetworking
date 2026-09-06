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
    uint32_t seq;         /* Próximo número de secuencia local */
    uint32_t ack;         /* Próximo número de secuencia esperado del servidor */
    uint32_t local_isn;   /* ISN del cliente: base para mostrar SEQ relativos */
    uint32_t remote_isn;  /* ISN del servidor: base para mostrar SEQ relativos */
    int close_logged;     /* Evita imprimir dos veces el separador CLOSE */
} tcp_session_t;

/* Resultado de tcp_session_recv() */
typedef enum {
    TCP_RX_TIMEOUT = 0, /* No llegó nada relevante dentro del tiempo indicado */
    TCP_RX_DATA    = 1, /* Llegó un segmento con datos; ya fue reconocido (ACK) */
    TCP_RX_FIN     = 2  /* Llegó el FIN del servidor; ya fue reconocido (ACK) */
} tcp_rx_result_t;

int tcp_session_open(tcp_session_t *s, const char *local_ip, const char *server_ip, uint16_t server_port, const char *capture_interface);

int tcp_session_handshake(tcp_session_t *s, int timeout_sec);
int tcp_session_send_message(tcp_session_t *s, const char *message);

/*
 * Espera el siguiente segmento del servidor, lo registra en consola y lo
 * reconoce con un ACK cuando corresponde (datos y FIN consumen secuencia).
 * Los ACK puros del servidor se registran pero NO terminan la espera, porque
 * no avanzan el número de secuencia: la función sigue esperando el eco.
 *
 * out_buf/out_buf_size son opcionales (pueden ser NULL/0) y reciben el
 * payload del segmento con datos.
 */
tcp_rx_result_t tcp_session_recv(tcp_session_t *s, char *out_buf, size_t out_buf_size, int timeout_sec);

void tcp_session_close(tcp_session_t *s, int wait_ack_timeout_sec);
void tcp_session_abort(tcp_session_t *s);

#endif
