/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_EXECUTE_CORE_H
#define ANJAY_EXECUTE_CORE_H

#include <anjay_modules/dm/anjay_execute.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    STATE_READ_ARGUMENT = 0,
    STATE_READ_VALUE,
    STATE_FINISHED_READING_ARGUMENT,
    STATE_EOF,
    STATE_ERROR
} anjay_execute_state_t;

struct anjay_unlocked_execute_ctx_struct {
    avs_stream_t *payload_stream;
    anjay_execute_state_t state;
    int arg;
    bool arg_has_value;
};

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_EXECUTE_CORE_H
