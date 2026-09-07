/*
 * test_log.c
 *
 * Verificación del formato de salida pedido en el Requerimiento 2, sin
 * necesidad de root, de raw sockets ni del servidor levantado.
 *
 *   make test
 *
 * Se incluye tcp_session.c directamente porque las funciones de formato son
 * static (detalle interno del módulo, no parte de su API pública).
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tcp_session.c"

/* ISNs sintéticos: números "feos" a propósito, para comprobar que el log
   muestra SEQ/ACK relativos y no los valores absolutos del cable. */
#define CLIENT_ISN 1804289383u
#define SERVER_ISN 2887509571u

static void render(const tcp_session_t *s, int outgoing, uint8_t flags,
                   uint32_t seq, uint32_t ack, const char *data, int data_len,
                   char *out, size_t out_size)
{
    format_segment(s, outgoing, flags, seq, ack, data, data_len, out, out_size);
    printf("%s\n", out);
}

int main(void)
{
    tcp_session_t s = {0};
    char line[TCP_LOG_LINE_MAX];

    s.local_isn = CLIENT_ISN;
    s.remote_isn = SERVER_ISN;

    printf("── Formato del log (Requerimiento 2) ──\n");

    /* 1. SYN: sin bandera ACK, el campo ACK no debe aparecer. */
    render(&s, 1, TCP_SYN, CLIENT_ISN, 0, NULL, 0, line, sizeof(line));
    assert(strstr(line, "Client → Server"));
    assert(strstr(line, "[SYN]"));
    assert(strstr(line, "SEQ=0"));
    assert(strstr(line, "ACK=") == NULL);
    assert(strstr(line, "LEN=") == NULL);

    /* 2. SYN+ACK del servidor: SEQ relativo al ISN del servidor,
          ACK relativo al ISN del cliente. */
    render(&s, 0, TCP_SYN | TCP_ACK, SERVER_ISN, CLIENT_ISN + 1, NULL, 0, line, sizeof(line));
    assert(strstr(line, "Server → Client"));
    assert(strstr(line, "[SYN, ACK]"));
    assert(strstr(line, "SEQ=0"));
    assert(strstr(line, "ACK=1"));

    /* 3. ACK final del handshake. */
    render(&s, 1, TCP_ACK, CLIENT_ISN + 1, SERVER_ISN + 1, NULL, 0, line, sizeof(line));
    assert(strstr(line, "[ACK]"));
    assert(strstr(line, "SEQ=1"));
    assert(strstr(line, "ACK=1"));

    /* 4. Datos: LEN cuenta el '\n' que agrega tcp_session_send_message,
          y el salto de línea se muestra escapado como \n. */
    render(&s, 1, TCP_PSH | TCP_ACK, CLIENT_ISN + 1, SERVER_ISN + 1,
           "EXIT OFF\n", 9, line, sizeof(line));
    assert(strstr(line, "[PSH, ACK]"));
    assert(strstr(line, "LEN=9"));
    assert(strstr(line, "DATA=\"EXIT OFF\\n\""));

    /* 5. Eco del servidor: mismos 9 bytes en sentido contrario. */
    render(&s, 0, TCP_PSH | TCP_ACK, SERVER_ISN + 1, CLIENT_ISN + 10,
           "EXIT OFF\n", 9, line, sizeof(line));
    assert(strstr(line, "Server → Client"));
    assert(strstr(line, "SEQ=1"));
    assert(strstr(line, "ACK=10"));
    assert(strstr(line, "LEN=9"));

    /* 6. FIN+ACK del servidor: el cierre lo inicia el servidor (Opción 1). */
    render(&s, 0, TCP_FIN | TCP_ACK, SERVER_ISN + 10, CLIENT_ISN + 10, NULL, 0, line, sizeof(line));
    assert(strstr(line, "[FIN, ACK]"));
    assert(strstr(line, "SEQ=10"));
    assert(strstr(line, "ACK=10"));

    /* 7. Bytes no imprimibles y comillas dentro del payload. */
    render(&s, 1, TCP_PSH | TCP_ACK, CLIENT_ISN + 1, SERVER_ISN + 1,
           "a\"b\x01\n", 5, line, sizeof(line));
    assert(strstr(line, "DATA=\"a\\\"b\\x01\\n\""));
    assert(strstr(line, "LEN=5"));

    printf("\nOK: 7 verificaciones de formato pasaron.\n");
    return 0;
}
