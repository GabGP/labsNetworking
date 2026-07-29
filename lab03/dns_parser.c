#include "dns_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>

int parse_dns_header(const uint8_t *buf, size_t buf_len, dns_header_t *header) {
    if (buf == NULL || header == NULL || buf_len < 12) {
        return -1;
    }

    header->id = ntohs(*(uint16_t *)(buf + 0));
    header->flags = ntohs(*(uint16_t *)(buf + 2));
    header->qdcount = ntohs(*(uint16_t *)(buf + 4));
    header->ancount = ntohs(*(uint16_t *)(buf + 6));
    header->nscount = ntohs(*(uint16_t *)(buf + 8));
    header->arcount = ntohs(*(uint16_t *)(buf + 10));

    return 0;
}

int serialize_dns_header(const dns_header_t *header, uint8_t *buf, size_t buf_len) {
    if (header == NULL || buf == NULL || buf_len < 12) {
        return -1;
    }

    *(uint16_t *)(buf + 0) = htons(header->id);
    *(uint16_t *)(buf + 2) = htons(header->flags);
    *(uint16_t *)(buf + 4) = htons(header->qdcount);
    *(uint16_t *)(buf + 6) = htons(header->ancount);
    *(uint16_t *)(buf + 8) = htons(header->nscount);
    *(uint16_t *)(buf + 10) = htons(header->arcount);

    return 12;
}

int parse_dns_qname(const uint8_t *buf, size_t buf_len, size_t *offset, char *dest, size_t dest_size) {
    if (buf == NULL || offset == NULL || dest == NULL || dest_size == 0) {
        return -1;
    }

    size_t curr_off = *offset;
    size_t dest_idx = 0;
    int jumped = 0;
    size_t original_offset_increment = 0;
    int jumps_count = 0;
    const int max_jumps = 10;

    dest[0] = '\0';

    while (curr_off < buf_len) {
        uint8_t len_octet = buf[curr_off];

        if (len_octet == 0) {
            if (!jumped) {
                original_offset_increment = curr_off + 1 - *offset;
            }
            curr_off++;
            break;
        }

        // Check for DNS pointer compression (top 2 bits set: 0xC0)
        if ((len_octet & 0xC0) == 0xC0) {
            if (curr_off + 1 >= buf_len) {
                return -1; // Truncated pointer
            }
            uint16_t pointer = ((len_octet & 0x3F) << 8) | buf[curr_off + 1];
            if (pointer >= buf_len) {
                return -1; // Pointer out of bounds
            }

            if (!jumped) {
                original_offset_increment = curr_off + 2 - *offset;
                jumped = 1;
            }

            jumps_count++;
            if (jumps_count > max_jumps) {
                return -1; // Pointer loop detected
            }

            curr_off = pointer;
            continue;
        }

        // Standard label
        uint8_t label_len = len_octet;
        curr_off++;

        if (curr_off + label_len > buf_len) {
            return -1; // Label extends past buffer
        }

        if (dest_idx > 0 && dest_idx < dest_size - 1) {
            dest[dest_idx++] = '.';
        }

        for (uint8_t i = 0; i < label_len; i++) {
            if (dest_idx < dest_size - 1) {
                dest[dest_idx++] = (char)buf[curr_off + i];
            } else {
                return -1; // Destination buffer overflow
            }
        }

        curr_off += label_len;
    }

    dest[dest_idx] = '\0';

    if (jumped) {
        *offset += original_offset_increment;
    } else {
        *offset = curr_off;
    }

    return 0;
}

int parse_dns_question(const uint8_t *buf, size_t buf_len, size_t *offset, dns_question_t *question) {
    if (buf == NULL || offset == NULL || question == NULL) {
        return -1;
    }

    size_t start_offset = *offset;

    if (parse_dns_qname(buf, buf_len, offset, question->qname, sizeof(question->qname)) != 0) {
        return -1;
    }

    if (*offset + 4 > buf_len) {
        return -1;
    }

    question->qtype = ntohs(*(uint16_t *)(buf + *offset));
    question->qclass = ntohs(*(uint16_t *)(buf + *offset + 2));
    *offset += 4;

    question->qname_raw_len = *offset - start_offset;

    return 0;
}

int encode_dns_qname(const char *name, uint8_t *buf, size_t buf_len, size_t *offset) {
    if (name == NULL || buf == NULL || offset == NULL) {
        return -1;
    }

    size_t orig_offset = *offset;
    size_t name_len = strlen(name);

    if (name_len == 0 || strcmp(name, ".") == 0) {
        if (*offset + 1 > buf_len) return -1;
        buf[(*offset)++] = 0;
        return 0;
    }

    const char *start = name;
    const char *end;

    while (*start != '\0') {
        end = strchr(start, '.');
        size_t label_len;
        if (end == NULL) {
            label_len = strlen(start);
        } else {
            label_len = end - start;
        }

        if (label_len > 63 || label_len == 0) {
            *offset = orig_offset;
            return -1; // Invalid label length
        }

        if (*offset + 1 + label_len > buf_len) {
            *offset = orig_offset;
            return -1; // Buffer overflow
        }

        buf[(*offset)++] = (uint8_t)label_len;
        memcpy(buf + *offset, start, label_len);
        *offset += label_len;

        if (end == NULL) {
            break;
        }
        start = end + 1;
    }

    if (*offset + 1 > buf_len) {
        *offset = orig_offset;
        return -1;
    }
    buf[(*offset)++] = 0; // Terminating zero octet

    return 0;
}

int encode_dns_qname_compressed(uint8_t *buf, size_t buf_len, size_t *offset, uint16_t pointer_offset) {
    if (buf == NULL || offset == NULL || pointer_offset > 0x3FFF) {
        return -1;
    }

    if (*offset + 2 > buf_len) {
        return -1;
    }

    uint16_t comp_val = 0xC000 | pointer_offset;
    *(uint16_t *)(buf + *offset) = htons(comp_val);
    *offset += 2;

    return 0;
}

const char *dns_type_to_string(uint16_t type) {
    switch (type) {
        case DNS_TYPE_A:     return "A";
        case DNS_TYPE_NS:    return "NS";
        case DNS_TYPE_CNAME: return "CNAME";
        case DNS_TYPE_SOA:   return "SOA";
        case DNS_TYPE_PTR:   return "PTR";
        case DNS_TYPE_MX:    return "MX";
        case DNS_TYPE_TXT:   return "TXT";
        case DNS_TYPE_AAAA:  return "AAAA";
        case DNS_TYPE_SRV:   return "SRV";
        case DNS_TYPE_OPT:   return "OPT";
        case DNS_TYPE_ANY:   return "ANY";
        default:             return "UNKNOWN";
    }
}

const char *dns_rcode_to_string(uint8_t rcode) {
    switch (rcode) {
        case DNS_RCODE_NOERROR:  return "NOERROR";
        case DNS_RCODE_FORMERR:  return "FORMERR";
        case DNS_RCODE_SERVFAIL: return "SERVFAIL";
        case DNS_RCODE_NXDOMAIN: return "NXDOMAIN";
        case DNS_RCODE_NOTIMP:   return "NOTIMP";
        case DNS_RCODE_REFUSED:  return "REFUSED";
        default:                 return "UNKNOWN";
    }
}
