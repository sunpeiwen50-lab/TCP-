#include "protocol.h"

int proto_pack(char *out, msg_type_t type,
               const char *from, const char *to,
               const void *body, uint32_t body_len)
{
    if (!out || body_len > MAX_BODY_LEN) return -1;

    proto_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic  = PROTO_MAGIC;
    hdr.type   = (uint16_t)type;
    hdr.length = body_len;
    if (from) strncpy(hdr.from, from, NAME_LEN - 1);
    if (to)   strncpy(hdr.to,   to,   NAME_LEN - 1);

    memcpy(out, &hdr, PROTO_HEADER_LEN);
    if (body && body_len > 0)
        memcpy(out + PROTO_HEADER_LEN, body, body_len);

    return (int)(PROTO_HEADER_LEN + body_len);
}
