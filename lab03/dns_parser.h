#ifndef DNS_PARSER_H
#define DNS_PARSER_H

#include <stdint.h>
#include <stddef.h>

// DNS Record Types
#define DNS_TYPE_A     1   // IPv4 address
#define DNS_TYPE_NS    2   // Authoritative name server
#define DNS_TYPE_CNAME 5   // Canonical name for an alias
#define DNS_TYPE_SOA   6   // Marks the start of a zone of authority
#define DNS_TYPE_PTR   12  // Domain name pointer
#define DNS_TYPE_MX    15  // Mail exchange
#define DNS_TYPE_TXT   16  // Text strings
#define DNS_TYPE_AAAA  28  // IPv6 address
#define DNS_TYPE_SRV   33  // Service locator
#define DNS_TYPE_OPT   41  // Option record
#define DNS_TYPE_ANY   255 // Request for all records

// DNS Classes
#define DNS_CLASS_IN   1   // Internet

// DNS Response Codes (RCODE)
#define DNS_RCODE_NOERROR  0 // No error
#define DNS_RCODE_FORMERR  1 // Format error
#define DNS_RCODE_SERVFAIL 2 // Server failure
#define DNS_RCODE_NXDOMAIN 3 // Non-Existent Domain
#define DNS_RCODE_NOTIMP   4 // Not Implemented
#define DNS_RCODE_REFUSED  5 // Query Refused

// DNS Header (12 bytes)
typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} dns_header_t;

// Flags accessors / helpers
#define DNS_FLAG_QR(flags)     (((flags) >> 15) & 0x01)
#define DNS_FLAG_OPCODE(flags) (((flags) >> 11) & 0x0F)
#define DNS_FLAG_AA(flags)     (((flags) >> 10) & 0x01)
#define DNS_FLAG_TC(flags)     (((flags) >> 9) & 0x01)
#define DNS_FLAG_RD(flags)     (((flags) >> 8) & 0x01)
#define DNS_FLAG_RA(flags)     (((flags) >> 7) & 0x01)
#define DNS_FLAG_Z(flags)      (((flags) >> 4) & 0x07)
#define DNS_FLAG_RCODE(flags)  ((flags) & 0x0F)

#define MAKE_DNS_FLAGS(qr, opcode, aa, tc, rd, ra, z, rcode) \
    ((((qr) & 0x01) << 15) | \
     (((opcode) & 0x0F) << 11) | \
     (((aa) & 0x01) << 10) | \
     (((tc) & 0x01) << 9) | \
     (((rd) & 0x01) << 8) | \
     (((ra) & 0x01) << 7) | \
     (((z) & 0x07) << 4) | \
     ((rcode) & 0x0F))

// DNS Question
typedef struct {
    char qname[256];
    uint16_t qtype;
    uint16_t qclass;
    size_t qname_raw_len; // raw byte length of qname in packet
} dns_question_t;

// DNS Resource Record
typedef struct {
    char name[256];
    uint16_t type;
    uint16_t class_;
    uint32_t ttl;
    uint16_t rdlength;
    uint8_t rdata[256];
} dns_rr_t;

// Function Prototypes
int parse_dns_header(const uint8_t *buf, size_t buf_len, dns_header_t *header);
int serialize_dns_header(const dns_header_t *header, uint8_t *buf, size_t buf_len);

int parse_dns_qname(const uint8_t *buf, size_t buf_len, size_t *offset, char *dest, size_t dest_size);
int parse_dns_question(const uint8_t *buf, size_t buf_len, size_t *offset, dns_question_t *question);

int encode_dns_qname(const char *name, uint8_t *buf, size_t buf_len, size_t *offset);
int encode_dns_qname_compressed(uint8_t *buf, size_t buf_len, size_t *offset, uint16_t pointer_offset);

const char *dns_type_to_string(uint16_t type);
const char *dns_rcode_to_string(uint8_t rcode);

#endif // DNS_PARSER_H
