/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_COAP_STREAM_STREAM_H
#define ANJAY_COAP_STREAM_STREAM_H

#include <avsystem/commons/stream_v_table.h>

#include "client_internal.h"
#include "common.h"
#include "server_internal.h"

#include "../id_source/id_source.h"

#ifndef ANJAY_COAP_STREAM_INTERNALS
#    error "Headers from coap/stream are not meant to be included from outside"
#endif

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum coap_stream_state {
    STREAM_STATE_IDLE,
    STREAM_STATE_CLIENT,
    STREAM_STATE_SERVER
} coap_stream_state_t;

typedef union {
    coap_stream_common_t common;
    coap_client_t client;
    coap_server_t server;
} coap_stream_data_t;

typedef struct coap_stream {
    const avs_stream_v_table_t *vtable;

    coap_id_source_t *id_source;

    coap_stream_state_t state;

    coap_stream_data_t data;
} coap_stream_t;

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_COAP_STREAM_STREAM_H
