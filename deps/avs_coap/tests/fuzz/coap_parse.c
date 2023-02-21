/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem CoAP library
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#define AVS_COAP_POISON_H // disable libc poisoning
#include <avs_coap_init.h>

#include <stdio.h>

#include "udp/avs_coap_udp_msg.h"

int main() {
    uint8_t buf[65536];
    size_t read = fread(buf, 1, sizeof(buf), stdin);

    avs_coap_udp_msg_t msg;
    return avs_is_err(_avs_coap_udp_msg_parse(&msg, buf, read));
}
