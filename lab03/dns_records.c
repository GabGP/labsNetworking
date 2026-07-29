#include "dns_records.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

static void str_tolower(char *str) {
    for (; *str; ++str) *str = (char)tolower((unsigned char)*str);
}

void dns_db_init(dns_db_t *db) {
    if (db == NULL) return;
    db->count = 0;
    memset(db->records, 0, sizeof(db->records));
}

int dns_db_add_a(dns_db_t *db, const char *name, const char *ip_str, uint32_t ttl) {
    if (db == NULL || name == NULL || ip_str == NULL || db->count >= MAX_RECORDS) {
        return -1;
    }

    record_entry_t *rec = &db->records[db->count];
    strncpy(rec->name, name, sizeof(rec->name) - 1);
    str_tolower(rec->name);
    rec->type = DNS_TYPE_A;
    rec->ttl = ttl;
    rec->rdlength = 4;

    if (inet_pton(AF_INET, ip_str, rec->rdata) != 1) {
        return -1; // Invalid IPv4 address
    }

    db->count++;
    return 0;
}

int dns_db_add_aaaa(dns_db_t *db, const char *name, const char *ip6_str, uint32_t ttl) {
    if (db == NULL || name == NULL || ip6_str == NULL || db->count >= MAX_RECORDS) {
        return -1;
    }

    record_entry_t *rec = &db->records[db->count];
    strncpy(rec->name, name, sizeof(rec->name) - 1);
    str_tolower(rec->name);
    rec->type = DNS_TYPE_AAAA;
    rec->ttl = ttl;
    rec->rdlength = 16;

    if (inet_pton(AF_INET6, ip6_str, rec->rdata) != 1) {
        return -1; // Invalid IPv6 address
    }

    db->count++;
    return 0;
}

int dns_db_add_ptr(dns_db_t *db, const char *name, const char *target_domain, uint32_t ttl) {
    if (db == NULL || name == NULL || target_domain == NULL || db->count >= MAX_RECORDS) {
        return -1;
    }

    record_entry_t *rec = &db->records[db->count];
    strncpy(rec->name, name, sizeof(rec->name) - 1);
    str_tolower(rec->name);
    rec->type = DNS_TYPE_PTR;
    rec->ttl = ttl;

    size_t off = 0;
    if (encode_dns_qname(target_domain, rec->rdata, sizeof(rec->rdata), &off) != 0) {
        return -1;
    }
    rec->rdlength = (uint16_t)off;

    db->count++;
    return 0;
}

int dns_db_add_cname(dns_db_t *db, const char *name, const char *target_domain, uint32_t ttl) {
    if (db == NULL || name == NULL || target_domain == NULL || db->count >= MAX_RECORDS) {
        return -1;
    }

    record_entry_t *rec = &db->records[db->count];
    strncpy(rec->name, name, sizeof(rec->name) - 1);
    str_tolower(rec->name);
    rec->type = DNS_TYPE_CNAME;
    rec->ttl = ttl;

    size_t off = 0;
    if (encode_dns_qname(target_domain, rec->rdata, sizeof(rec->rdata), &off) != 0) {
        return -1;
    }
    rec->rdlength = (uint16_t)off;

    db->count++;
    return 0;
}

int dns_db_add_txt(dns_db_t *db, const char *name, const char *txt_data, uint32_t ttl) {
    if (db == NULL || name == NULL || txt_data == NULL || db->count >= MAX_RECORDS) {
        return -1;
    }

    size_t txt_len = strlen(txt_data);
    if (txt_len > 254) return -1;

    record_entry_t *rec = &db->records[db->count];
    strncpy(rec->name, name, sizeof(rec->name) - 1);
    str_tolower(rec->name);
    rec->type = DNS_TYPE_TXT;
    rec->ttl = ttl;

    rec->rdata[0] = (uint8_t)txt_len;
    memcpy(&rec->rdata[1], txt_data, txt_len);
    rec->rdlength = (uint16_t)(txt_len + 1);

    db->count++;
    return 0;
}

int dns_db_add_soa(dns_db_t *db, const char *name, const char *mname, const char *rname, 
                   uint32_t serial, uint32_t refresh, uint32_t retry, uint32_t expire, uint32_t minimum, uint32_t ttl) {
    if (db == NULL || name == NULL || mname == NULL || rname == NULL || db->count >= MAX_RECORDS) {
        return -1;
    }

    record_entry_t *rec = &db->records[db->count];
    strncpy(rec->name, name, sizeof(rec->name) - 1);
    str_tolower(rec->name);
    rec->type = DNS_TYPE_SOA;
    rec->ttl = ttl;

    size_t off = 0;
    if (encode_dns_qname(mname, rec->rdata, sizeof(rec->rdata), &off) != 0) return -1;
    if (encode_dns_qname(rname, rec->rdata, sizeof(rec->rdata), &off) != 0) return -1;

    if (off + 20 > sizeof(rec->rdata)) return -1;

    *(uint32_t *)(rec->rdata + off) = htonl(serial); off += 4;
    *(uint32_t *)(rec->rdata + off) = htonl(refresh); off += 4;
    *(uint32_t *)(rec->rdata + off) = htonl(retry); off += 4;
    *(uint32_t *)(rec->rdata + off) = htonl(expire); off += 4;
    *(uint32_t *)(rec->rdata + off) = htonl(minimum); off += 4;

    rec->rdlength = (uint16_t)off;

    db->count++;
    return 0;
}

void dns_db_add_defaults(dns_db_t *db) {
    // Default local records for BBB / test environment
    dns_db_add_a(db, "beaglebone.local", "192.168.1.100", 300);
    dns_db_add_aaaa(db, "beaglebone.local", "fe80::100", 300);
    
    dns_db_add_a(db, "lab3.test", "127.0.0.1", 300);
    dns_db_add_aaaa(db, "lab3.test", "::1", 300);
    dns_db_add_cname(db, "www.lab3.test", "lab3.test", 300);
    dns_db_add_txt(db, "lab3.test", "v=spf1 redirect=_spf.google.com", 300);

    dns_db_add_a(db, "galileo.edu", "10.0.0.1", 600);
    dns_db_add_aaaa(db, "galileo.edu", "2001:db8::1", 600);

    // Reverse DNS (PTR)
    dns_db_add_ptr(db, "100.1.168.192.in-addr.arpa", "beaglebone.local", 300);
    dns_db_add_ptr(db, "1.0.0.127.in-addr.arpa", "localhost", 300);

    // SOA record
    dns_db_add_soa(db, "lab3.test", "ns1.lab3.test", "admin.lab3.test", 2026072901, 7200, 3600, 1209600, 3600, 3600);
}

int dns_db_load_file(dns_db_t *db, const char *filename) {
    if (db == NULL || filename == NULL) return -1;

    FILE *f = fopen(filename, "r");
    if (!f) return -1;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        // Strip comment or newline
        char *p = strchr(line, '#');
        if (p) *p = '\0';
        p = strchr(line, ';');
        if (p) *p = '\0';

        // Trim leading whitespace
        char *ptr = line;
        while (*ptr && isspace((unsigned char)*ptr)) ptr++;
        if (*ptr == '\0') continue;

        char type_str[16] = {0};
        char name[256] = {0};
        char val1[256] = {0};
        char val2[256] = {0};
        uint32_t ttl = 300;

        int fields = sscanf(ptr, "%255s %15s %255s %255s %u", name, type_str, val1, val2, &ttl);
        if (fields < 3) continue;

        for (char *c = type_str; *c; ++c) *c = (char)toupper((unsigned char)*c);

        if (strcmp(type_str, "A") == 0) {
            dns_db_add_a(db, name, val1, ttl);
        } else if (strcmp(type_str, "AAAA") == 0) {
            dns_db_add_aaaa(db, name, val1, ttl);
        } else if (strcmp(type_str, "PTR") == 0) {
            dns_db_add_ptr(db, name, val1, ttl);
        } else if (strcmp(type_str, "CNAME") == 0) {
            dns_db_add_cname(db, name, val1, ttl);
        } else if (strcmp(type_str, "TXT") == 0) {
            dns_db_add_txt(db, name, val1, ttl);
        }
    }

    fclose(f);
    return 0;
}

int dns_db_lookup(const dns_db_t *db, const char *qname, uint16_t qtype, record_entry_t *results, int max_results) {
    if (db == NULL || qname == NULL || results == NULL || max_results <= 0) {
        return 0;
    }

    char clean_qname[256];
    strncpy(clean_qname, qname, sizeof(clean_qname) - 1);
    clean_qname[sizeof(clean_qname) - 1] = '\0';
    str_tolower(clean_qname);

    int found = 0;
    for (int i = 0; i < db->count && found < max_results; i++) {
        const record_entry_t *rec = &db->records[i];
        if (strcasecmp(rec->name, clean_qname) == 0) {
            if (rec->type == qtype || qtype == DNS_TYPE_ANY) {
                results[found++] = *rec;
            } else if (qtype == DNS_TYPE_A && rec->type == DNS_TYPE_CNAME) {
                // If asking for A but record is CNAME, return CNAME
                results[found++] = *rec;
            }
        }
    }

    return found;
}

int dns_forward_upstream(const char *upstream_ip, uint16_t upstream_port,
                         const uint8_t *query_buf, size_t query_len,
                         uint8_t *resp_buf, size_t max_resp_len,
                         size_t *resp_len) {
    if (upstream_ip == NULL || query_buf == NULL || resp_buf == NULL || resp_len == NULL) {
        return -1;
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        return -1;
    }

    // Set 2.5 second timeout on receive
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 500000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_in up_addr;
    memset(&up_addr, 0, sizeof(up_addr));
    up_addr.sin_family = AF_INET;
    up_addr.sin_port = htons(upstream_port);
    if (inet_pton(AF_INET, upstream_ip, &up_addr.sin_addr) != 1) {
        close(sockfd);
        return -1;
    }

    ssize_t sent = sendto(sockfd, query_buf, query_len, 0, (struct sockaddr *)&up_addr, sizeof(up_addr));
    if (sent < 0) {
        close(sockfd);
        return -1;
    }

    socklen_t addr_len = sizeof(up_addr);
    ssize_t rcvd = recvfrom(sockfd, resp_buf, max_resp_len, 0, (struct sockaddr *)&up_addr, &addr_len);
    close(sockfd);

    if (rcvd < 0) {
        return -1; // Timeout or error
    }

    *resp_len = (size_t)rcvd;
    return 0;
}
