#include "dns_server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static void log_request(const char *client_ip, uint16_t client_port, uint16_t id, 
                         const char *qname, uint16_t qtype, int rd) {
    time_t rawtime;
    struct tm *timeinfo;
    char time_str[32];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    printf("\031[36m[%s]\033[0m \033[1mREQ\033[0m  Client: \033[33m%s:%u\033[0m | ID: 0x%04X | QNAME: \033[32m%s\033[0m | QTYPE: \033[35m%s\033[0m (%u) | RD: %d\n",
           time_str, client_ip, client_port, id, qname, dns_type_to_string(qtype), qtype, rd);
}

static void log_response(const char *client_ip, uint16_t client_port, uint8_t rcode, 
                          int ancount, const char *source, double elapsed_ms) {
    time_t rawtime;
    struct tm *timeinfo;
    char time_str[32];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", timeinfo);

    const char *rcode_color = (rcode == DNS_RCODE_NOERROR) ? "\033[32m" : "\033[31m";

    printf("\033[36m[%s]\033[0m \033[1mRESP\033[0m Client: \033[33m%s:%u\033[0m | RCODE: %s%s\033[0m | ANS: %d | SRC: \033[34m%s\033[0m | Latency: \033[33m%.2f ms\033[0m\n",
           time_str, client_ip, client_port, rcode_color, dns_rcode_to_string(rcode), ancount, source, elapsed_ms);
}

static void handle_dns_task(dns_task_t *task, void *user_arg) {
    dns_server_t *server = (dns_server_t *)user_arg;

    struct timeval start_tv, end_tv;
    gettimeofday(&start_tv, NULL);

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(task->client_addr.sin_addr), client_ip, sizeof(client_ip));
    uint16_t client_port = ntohs(task->client_addr.sin_port);

    dns_header_t query_header;
    if (parse_dns_header(task->buffer, task->length, &query_header) != 0 || query_header.qdcount == 0) {
        // Formerr
        uint8_t resp[12];
        dns_header_t resp_hdr;
        memset(&resp_hdr, 0, sizeof(resp_hdr));
        resp_hdr.id = (task->length >= 2) ? ntohs(*(uint16_t *)task->buffer) : 0;
        resp_hdr.flags = MAKE_DNS_FLAGS(1, 0, 0, 0, 0, 1, 0, DNS_RCODE_FORMERR);
        serialize_dns_header(&resp_hdr, resp, sizeof(resp));
        sendto(server->sockfd, resp, sizeof(resp), 0, (struct sockaddr *)&task->client_addr, task->addr_len);
        return;
    }

    size_t offset = 12;
    dns_question_t question;
    if (parse_dns_question(task->buffer, task->length, &offset, &question) != 0) {
        // Formerr
        uint8_t resp[12];
        dns_header_t resp_hdr;
        memset(&resp_hdr, 0, sizeof(resp_hdr));
        resp_hdr.id = query_header.id;
        resp_hdr.flags = MAKE_DNS_FLAGS(1, DNS_FLAG_OPCODE(query_header.flags), 0, 0, DNS_FLAG_RD(query_header.flags), 1, 0, DNS_RCODE_FORMERR);
        serialize_dns_header(&resp_hdr, resp, sizeof(resp));
        sendto(server->sockfd, resp, sizeof(resp), 0, (struct sockaddr *)&task->client_addr, task->addr_len);
        return;
    }

    int rd = DNS_FLAG_RD(query_header.flags);
    log_request(client_ip, client_port, query_header.id, question.qname, question.qtype, rd);

    // Look up in local records database
    record_entry_t records[16];
    int record_count = dns_db_lookup(&server->db, question.qname, question.qtype, records, 16);

    uint8_t response_buf[MAX_DNS_PACKET_SIZE];
    size_t resp_len = 0;

    if (record_count > 0) {
        // Local Match Found
        dns_header_t resp_hdr;
        resp_hdr.id = query_header.id;
        resp_hdr.flags = MAKE_DNS_FLAGS(1, 0, 1, 0, rd, 1, 0, DNS_RCODE_NOERROR);
        resp_hdr.qdcount = 1;
        resp_hdr.ancount = (uint16_t)record_count;
        resp_hdr.nscount = 0;
        resp_hdr.arcount = 0;

        resp_len = serialize_dns_header(&resp_hdr, response_buf, sizeof(response_buf));

        // Copy Question section directly from query
        size_t question_section_len = offset - 12;
        if (resp_len + question_section_len <= sizeof(response_buf)) {
            memcpy(response_buf + resp_len, task->buffer + 12, question_section_len);
            resp_len += question_section_len;
        }

        // Encode Answer section with domain compression pointers (0xC00C pointing to QNAME at byte offset 12)
        for (int i = 0; i < record_count; i++) {
            if (resp_len + 2 + 10 + records[i].rdlength > sizeof(response_buf)) {
                break; // Buffer safety check
            }

            // Compressed domain name pointer (0xC00C -> offset 12)
            encode_dns_qname_compressed(response_buf, sizeof(response_buf), &resp_len, 0x000C);

            // Type (2 bytes), Class (2 bytes), TTL (4 bytes), RDLength (2 bytes)
            *(uint16_t *)(response_buf + resp_len) = htons(records[i].type); resp_len += 2;
            *(uint16_t *)(response_buf + resp_len) = htons(DNS_CLASS_IN);    resp_len += 2;
            *(uint32_t *)(response_buf + resp_len) = htonl(records[i].ttl);  resp_len += 4;
            *(uint16_t *)(response_buf + resp_len) = htons(records[i].rdlength); resp_len += 2;

            // RDATA (rdlength bytes)
            memcpy(response_buf + resp_len, records[i].rdata, records[i].rdlength);
            resp_len += records[i].rdlength;
        }

        sendto(server->sockfd, response_buf, resp_len, 0, (struct sockaddr *)&task->client_addr, task->addr_len);

        gettimeofday(&end_tv, NULL);
        double elapsed = (end_tv.tv_sec - start_tv.tv_sec) * 1000.0 + (end_tv.tv_usec - start_tv.tv_usec) / 1000.0;
        log_response(client_ip, client_port, DNS_RCODE_NOERROR, record_count, "LOCAL_DB", elapsed);

    } else if (rd && strlen(server->config.upstream_ip) > 0) {
        // Forward recursively to Upstream DNS server
        size_t up_resp_len = 0;
        int ret = dns_forward_upstream(server->config.upstream_ip, server->config.upstream_port,
                                       task->buffer, task->length,
                                       response_buf, sizeof(response_buf),
                                       &up_resp_len);

        gettimeofday(&end_tv, NULL);
        double elapsed = (end_tv.tv_sec - start_tv.tv_sec) * 1000.0 + (end_tv.tv_usec - start_tv.tv_usec) / 1000.0;

        if (ret == 0 && up_resp_len >= 12) {
            // Forward upstream response directly to client
            sendto(server->sockfd, response_buf, up_resp_len, 0, (struct sockaddr *)&task->client_addr, task->addr_len);
            
            dns_header_t up_hdr;
            parse_dns_header(response_buf, up_resp_len, &up_hdr);
            uint8_t rcode = DNS_FLAG_RCODE(up_hdr.flags);

            char src_desc[128];
            snprintf(src_desc, sizeof(src_desc), "UPSTREAM (%s)", server->config.upstream_ip);
            log_response(client_ip, client_port, rcode, up_hdr.ancount, src_desc, elapsed);
        } else {
            // Upstream query failed / timed out -> return SERVFAIL
            dns_header_t resp_hdr;
            resp_hdr.id = query_header.id;
            resp_hdr.flags = MAKE_DNS_FLAGS(1, 0, 0, 0, rd, 1, 0, DNS_RCODE_SERVFAIL);
            resp_hdr.qdcount = 1;
            resp_hdr.ancount = 0;
            resp_hdr.nscount = 0;
            resp_hdr.arcount = 0;

            resp_len = serialize_dns_header(&resp_hdr, response_buf, sizeof(response_buf));
            size_t question_section_len = offset - 12;
            if (resp_len + question_section_len <= sizeof(response_buf)) {
                memcpy(response_buf + resp_len, task->buffer + 12, question_section_len);
                resp_len += question_section_len;
            }

            sendto(server->sockfd, response_buf, resp_len, 0, (struct sockaddr *)&task->client_addr, task->addr_len);
            log_response(client_ip, client_port, DNS_RCODE_SERVFAIL, 0, "UPSTREAM_TIMEOUT", elapsed);
        }
    } else {
        // Not found locally and no recursion -> NXDOMAIN
        dns_header_t resp_hdr;
        resp_hdr.id = query_header.id;
        resp_hdr.flags = MAKE_DNS_FLAGS(1, 0, 1, 0, rd, 1, 0, DNS_RCODE_NXDOMAIN);
        resp_hdr.qdcount = 1;
        resp_hdr.ancount = 0;
        resp_hdr.nscount = 0;
        resp_hdr.arcount = 0;

        resp_len = serialize_dns_header(&resp_hdr, response_buf, sizeof(response_buf));
        size_t question_section_len = offset - 12;
        if (resp_len + question_section_len <= sizeof(response_buf)) {
            memcpy(response_buf + resp_len, task->buffer + 12, question_section_len);
            resp_len += question_section_len;
        }

        sendto(server->sockfd, response_buf, resp_len, 0, (struct sockaddr *)&task->client_addr, task->addr_len);

        gettimeofday(&end_tv, NULL);
        double elapsed = (end_tv.tv_sec - start_tv.tv_sec) * 1000.0 + (end_tv.tv_usec - start_tv.tv_usec) / 1000.0;
        log_response(client_ip, client_port, DNS_RCODE_NXDOMAIN, 0, "LOCAL_DB", elapsed);
    }
}

int dns_server_init(dns_server_t *server, const server_config_t *config) {
    if (server == NULL || config == NULL) return -1;

    memset(server, 0, sizeof(dns_server_t));
    server->config = *config;

    // Create UDP Socket
    server->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server->sockfd < 0) {
        perror("[-] Failed to create UDP socket");
        return -1;
    }

    int opt = 1;
    if (setsockopt(server->sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[-] Failed to set SO_REUSEADDR");
        close(server->sockfd);
        return -1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(server->config.port);

    if (bind(server->sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("[-] Failed to bind UDP socket");
        close(server->sockfd);
        return -1;
    }

    // Initialize Records DB
    dns_db_init(&server->db);
    dns_db_add_defaults(&server->db);

    if (strlen(server->config.records_file) > 0) {
        if (dns_db_load_file(&server->db, server->config.records_file) == 0) {
            printf("[+] Loaded custom zone records file: %s\n", server->config.records_file);
        }
    }

    // Create Thread Pool
    server->pool = threadpool_create(server->config.num_threads, server->config.queue_size, handle_dns_task, server);
    if (server->pool == NULL) {
        fprintf(stderr, "[-] Failed to initialize thread pool\n");
        close(server->sockfd);
        return -1;
    }

    server->running = 1;
    return 0;
}

int dns_server_run(dns_server_t *server) {
    if (server == NULL || !server->running) return -1;

    printf("\n========================================================\033[0m\n");
    printf("\033[1;32m  DNS Server over UDP (C / BeagleBone Black Target)\033[0m\n");
    printf("========================================================\033[0m\n");
    printf(" Listening Port:     \033[33m%u\033[0m\n", server->config.port);
    printf(" Upstream Forwarder: \033[33m%s:%u\033[0m\n", server->config.upstream_ip, server->config.upstream_port);
    printf(" Worker Threads:     \033[33m%d\033[0m\n", server->config.num_threads);
    printf(" Local Records DB:   \033[33m%d records loaded\033[0m\n", server->db.count);
    printf(" Architecture Model: \033[36mPOSIX C Pthread Threadpool (Lab #1 inspired)\033[0m\n");
    printf("========================================================\n\n");

    dns_task_t task;
    task.addr_len = sizeof(task.client_addr);

    while (server->running) {
        ssize_t n = recvfrom(server->sockfd, task.buffer, sizeof(task.buffer), 0,
                             (struct sockaddr *)&task.client_addr, &task.addr_len);
        if (n < 0) {
            if (!server->running) break;
            continue;
        }

        task.length = (size_t)n;
        gettimeofday(&task.receive_time, NULL);

        if (threadpool_add(server->pool, &task) != 0) {
            fprintf(stderr, "\033[31m[!] Task queue full! Dropping incoming query.\033[0m\n");
        }
    }

    return 0;
}

void dns_server_stop(dns_server_t *server) {
    if (server == NULL || !server->running) return;

    printf("\n[+] Shutting down DNS server...\n");
    server->running = 0;

    if (server->sockfd >= 0) {
        close(server->sockfd);
        server->sockfd = -1;
    }

    if (server->pool != NULL) {
        threadpool_destroy(server->pool);
        server->pool = NULL;
    }

    printf("[+] DNS server shut down cleanly.\n");
}
