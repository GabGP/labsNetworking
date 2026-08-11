#define _POSIX_C_SOURCE 200809L
#include "ntp_net.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <poll.h>
#include <sys/time.h>

const char *DEFAULT_NTP_SERVERS[] = {
    "time.google.com",
    "time.apple.com",
    "time.windows.com",
    "time.cloudflare.com",
    "ntp.cern.ch",
    "time.nist.gov",
    "time-a.nist.gov",
    "time-b.nist.gov",
    "pool.ntp.org",
    "north-america.pool.ntp.org"
};

const size_t DEFAULT_NTP_SERVER_COUNT = sizeof(DEFAULT_NTP_SERVERS) / sizeof(DEFAULT_NTP_SERVERS[0]);

size_t ntp_query_all_servers(const char **hostnames,
                            size_t count,
                            int timeout_sec,
                            get_sim_time_fn sim_time_cb,
                            ntp_server_result_t *results) {
    if (!hostnames || count == 0 || !results || !sim_time_cb) return 0;
    if (count > MAX_SERVERS) count = MAX_SERVERS;

    memset(results, 0, sizeof(ntp_server_result_t) * count);

    int sockets[MAX_SERVERS];
    struct pollfd fds[MAX_SERVERS];
    size_t active_count = 0;

    for (size_t i = 0; i < count; i++) {
        sockets[i] = -1;
        fds[i].fd = -1;
        fds[i].events = POLLIN;
        fds[i].revents = 0;

        snprintf(results[i].hostname, sizeof(results[i].hostname), "%s", hostnames[i]);
        results[i].responded = false;

        struct addrinfo hints, *res = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family   = AF_INET; /* IPv4 */
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        int rc = getaddrinfo(hostnames[i], DEFAULT_NTP_PORT, &hints, &res);
        if (rc != 0 || !res) {
            snprintf(results[i].error_msg, sizeof(results[i].error_msg),
                     "DNS Resolution Failed: %s", gai_strerror(rc));
            continue;
        }

        struct sockaddr_in *ipv4 = (struct sockaddr_in *)res->ai_addr;
        inet_ntop(AF_INET, &(ipv4->sin_addr), results[i].ip_str, sizeof(results[i].ip_str));

        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (fd < 0) {
            snprintf(results[i].error_msg, sizeof(results[i].error_msg),
                     "Socket Creation Failed: %s", strerror(errno));
            freeaddrinfo(res);
            continue;
        }

        /* Make socket non-blocking */
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) {
            fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        }

        /* Capture T1 immediately before sending packet */
        results[i].t1 = sim_time_cb();

        uint8_t tx_buf[NTP_PACKET_SIZE];
        ntp_packet_init_request(tx_buf, sizeof(tx_buf), results[i].t1);

        ssize_t sent = sendto(fd, tx_buf, NTP_PACKET_SIZE, 0, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);

        if (sent < (ssize_t)NTP_PACKET_SIZE) {
            snprintf(results[i].error_msg, sizeof(results[i].error_msg),
                     "Send Failed: %s", strerror(errno));
            close(fd);
            continue;
        }

        sockets[i] = fd;
        fds[i].fd = fd;
        active_count++;
    }

    if (active_count == 0) {
        return 0;
    }

    int timeout_ms = timeout_sec * 1000;
    struct timeval start_tv, now_tv;
    gettimeofday(&start_tv, NULL);

    size_t responses_received = 0;

    while (responses_received < active_count) {
        gettimeofday(&now_tv, NULL);
        long elapsed_ms = (now_tv.tv_sec - start_tv.tv_sec) * 1000 +
                          (now_tv.tv_usec - start_tv.tv_usec) / 1000;
        int remaining_ms = timeout_ms - (int)elapsed_ms;
        if (remaining_ms <= 0) {
            break; /* Timeout expired */
        }

        int ret = poll(fds, (nfds_t)count, remaining_ms);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) {
            break; /* Timeout */
        }

        for (size_t i = 0; i < count; i++) {
            if (fds[i].fd >= 0 && (fds[i].revents & POLLIN)) {
                /* Capture T4 immediately upon socket readability */
                ntp_time_t t4 = sim_time_cb();

                uint8_t rx_buf[NTP_PACKET_SIZE * 2];
                struct sockaddr_in src_addr;
                socklen_t addr_len = sizeof(src_addr);

                ssize_t recvd = recvfrom(fds[i].fd, rx_buf, sizeof(rx_buf), 0,
                                         (struct sockaddr *)&src_addr, &addr_len);
                
                /* Close socket so we don't poll it again */
                close(fds[i].fd);
                sockets[i] = -1;
                fds[i].fd = -1;
                responses_received++;

                if (recvd >= NTP_PACKET_SIZE) {
                    ntp_packet_t pkt;
                    if (ntp_packet_parse_response(rx_buf, (size_t)recvd, &pkt)) {
                        results[i].t4 = t4;
                        results[i].t2 = pkt.recv_ts;
                        results[i].t3 = pkt.tx_ts;
                        results[i].stratum = pkt.stratum;

                        /* If server returned valid orig_ts, verify or use it */
                        if (pkt.orig_ts.seconds != 0) {
                            results[i].t1 = pkt.orig_ts;
                        }

                        results[i].offset_ms = ntp_calc_offset_ms(results[i].t1, results[i].t2,
                                                                  results[i].t3, results[i].t4);
                        results[i].delay_ms  = ntp_calc_delay_ms(results[i].t1, results[i].t2,
                                                                 results[i].t3, results[i].t4);
                        results[i].responded = true;
                        results[i].error_msg[0] = '\0';
                    } else {
                        snprintf(results[i].error_msg, sizeof(results[i].error_msg),
                                 "Malformed NTP Packet Response");
                    }
                } else {
                    snprintf(results[i].error_msg, sizeof(results[i].error_msg),
                             "Truncated Packet Response (%zd bytes)", recvd);
                }
            } else if (fds[i].fd >= 0 && (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL))) {
                close(fds[i].fd);
                sockets[i] = -1;
                fds[i].fd = -1;
                responses_received++;
                snprintf(results[i].error_msg, sizeof(results[i].error_msg),
                         "Socket Error / Hangup");
            }
        }
    }

    /* Cleanup remaining open sockets */
    for (size_t i = 0; i < count; i++) {
        if (sockets[i] >= 0) {
            close(sockets[i]);
            sockets[i] = -1;
            fds[i].fd = -1;
            if (!results[i].responded && results[i].error_msg[0] == '\0') {
                snprintf(results[i].error_msg, sizeof(results[i].error_msg),
                         "Request Timed Out (%d s)", timeout_sec);
            }
        }
    }

    size_t valid_responses = 0;
    for (size_t i = 0; i < count; i++) {
        if (results[i].responded) {
            valid_responses++;
        }
    }

    return valid_responses;
}
