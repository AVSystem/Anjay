/*
 * Copyright 2017-2021 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_DM_EXECUTE_H
#define ANJAY_INCLUDE_ANJAY_MODULES_DM_EXECUTE_H

#include <anjay/io.h>

#include <anjay_modules/dm/anjay_modules.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

anjay_unlocked_execute_ctx_t *
_anjay_execute_ctx_create(avs_stream_t *payload_stream);
void _anjay_execute_ctx_destroy(anjay_unlocked_execute_ctx_t **ctx);

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_DM_EXECUTE_H */
