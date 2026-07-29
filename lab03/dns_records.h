#ifndef DNS_RECORDS_H
#define DNS_RECORDS_H

#include <stdint.h>
#include <stddef.h>
#include "dns_parser.h"

#define MAX_RECORDS 256

typedef struct {
    char name[256];
    uint16_t type;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t rdata[256];
} record_entry_t;

typedef struct {
    record_entry_t records[MAX_RECORDS];
    int count;
} dns_db_t;

void dns_db_init(dns_db_t *db);
int dns_db_add_a(dns_db_t *db, const char *name, const char *ip_str, uint32_t ttl);
int dns_db_add_aaaa(dns_db_t *db, const char *name, const char *ip6_str, uint32_t ttl);
int dns_db_add_ptr(dns_db_t *db, const char *name, const char *target_domain, uint32_t ttl);
int dns_db_add_cname(dns_db_t *db, const char *name, const char *target_domain, uint32_t ttl);
int dns_db_add_txt(dns_db_t *db, const char *name, const char *txt_data, uint32_t ttl);
int dns_db_add_soa(dns_db_t *db, const char *name, const char *mname, const char *rname, 
                   uint32_t serial, uint32_t refresh, uint32_t retry, uint32_t expire, uint32_t minimum, uint32_t ttl);

int dns_db_load_file(dns_db_t *db, const char *filename);
void dns_db_add_defaults(dns_db_t *db);

int dns_db_lookup(const dns_db_t *db, const char *qname, uint16_t qtype, record_entry_t *results, int max_results);

int dns_forward_upstream(const char *upstream_ip, uint16_t upstream_port,
                         const uint8_t *query_buf, size_t query_len,
                         uint8_t *resp_buf, size_t max_resp_len,
                         size_t *resp_len);

#endif // DNS_RECORDS_H
