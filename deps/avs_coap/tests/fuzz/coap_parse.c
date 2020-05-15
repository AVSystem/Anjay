/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
