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

struct anjay_execute_ctx_struct {
    avs_stream_t *payload_stream;
    anjay_execute_state_t state;
    int arg;
    bool arg_has_value;
};

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_EXECUTE_CORE_H
