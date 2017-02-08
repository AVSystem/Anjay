/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_OBSERVE_H
#define ANJAY_OBSERVE_H

#include <avsystem/commons/rbtree.h>
#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_outbuf.h>

#include <anjay_modules/observe.h>

#include "coap/stream.h"
#include "servers.h"
#include "sched.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

#ifdef WITH_OBSERVE

typedef struct {
    avs_stream_outbuf_t outbuf;
    anjay_msg_details_t *details;
} anjay_observe_stream_t;

const anjay_observe_stream_t *_anjay_observe_stream_initializer__(void);

static inline anjay_observe_stream_t
_anjay_new_observe_stream(anjay_msg_details_t *details) {
    anjay_observe_stream_t retval = *_anjay_observe_stream_initializer__();
    retval.details = details;
    return retval;
}

typedef struct anjay_observe_entry_struct anjay_observe_entry_t;
typedef struct anjay_observe_connection_entry_struct
        anjay_observe_connection_entry_t;

typedef struct {
    AVS_RBTREE(anjay_observe_connection_entry_t) connection_entries;
} anjay_observe_state_t;

typedef struct {
    anjay_observe_entry_t *ref;
    anjay_msg_details_t details;
    anjay_coap_msg_identity_t identity;
    struct timespec timestamp;
    double numeric;
    const size_t value_length;
    char value[1]; // actually a FAM
} anjay_observe_resource_value_t;

typedef struct {
    anjay_ssid_t ssid;
    anjay_connection_type_t type;
} anjay_observe_connection_key_t;

typedef struct {
    anjay_observe_connection_key_t connection;
    anjay_oid_t oid;
    anjay_iid_t iid;
    int32_t rid;
    uint16_t format;
} anjay_observe_key_t;

int _anjay_observe_init(anjay_t *anjay);

void _anjay_observe_cleanup(anjay_t *anjay);

int _anjay_observe_put_entry(anjay_t *anjay,
                             const anjay_observe_key_t *key,
                             const anjay_msg_details_t *details,
                             const anjay_coap_msg_identity_t *identity,
                             double numeric,
                             const void *data,
                             size_t size);

void _anjay_observe_remove_entry(anjay_t *anjay,
                                 const anjay_observe_key_t *key);

void _anjay_observe_remove_by_msg_id(anjay_t *anjay,
                                     uint16_t notify_id);

int _anjay_observe_sched_flush(anjay_t *anjay,
                               anjay_ssid_t ssid,
                               anjay_connection_type_t conn_type);

int _anjay_observe_notify(anjay_t *anjay,
                          const anjay_observe_key_t *origin_key,
                          bool invert_ssid_match);

anjay_output_ctx_t *_anjay_observe_decorate_ctx(anjay_output_ctx_t *backend,
                                                double *out_numeric);

#else // WITH_OBSERVE

#define _anjay_observe_sched_flush(...) ((void) 0)

#endif // WITH_OBSERVE

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_OBSERVE_H */

