/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <avs_coap_init.h>

#ifdef WITH_AVS_COAP_TCP

#    include <ctype.h>

#    include <avsystem/commons/avs_utils.h>

#    include "avs_coap_tcp_utils.h"

VISIBILITY_SOURCE_BEGIN

static int
add_escaped_char(char *escaped_buf, size_t remaining_size, char to_escape) {
    char *format = NULL;
    if (isprint((unsigned char) to_escape)) {
        switch (to_escape) {
        case '\"':
        case '\'':
        case '\\':
            format = "\\%c";
            break;

        default:
            format = "%c";
            break;
        }
    } else {
        format = "\\x%02X";
    }

    return avs_simple_snprintf(
            escaped_buf, remaining_size, format, (unsigned char) to_escape);
}

size_t _avs_coap_tcp_escape_payload(const char *payload,
                                    size_t payload_size,
                                    char *escaped_buf,
                                    size_t escaped_buf_size) {
    assert(escaped_buf);
    assert(escaped_buf_size);

    size_t offset = 0;
    size_t i = 0;
    for (; i < payload_size; i++) {
        int bytes_written = add_escaped_char(
                escaped_buf + offset, escaped_buf_size - offset, payload[i]);
        if (bytes_written <= 0) {
            break;
        }
        offset += (size_t) bytes_written;
    }
    escaped_buf[offset] = '\0';
    return i;
}

#endif // WITH_AVS_COAP_TCP
