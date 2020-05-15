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

#ifndef ANJAY_WRITE_CORE_H
#define ANJAY_WRITE_CORE_H

#include <anjay/core.h>

#include "../anjay_dm_core.h"
#include "../anjay_io_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

int _anjay_dm_write(anjay_t *anjay,
                    const anjay_dm_object_def_t *const *obj,
                    const anjay_request_t *request,
                    anjay_input_ctx_t *in_ctx);

/**
 * NOTE: This function is used in one situation, that is: after LwM2M Create to
 * initialize newly created Instance with data contained within the request
 * payload (handled by input context @p in_ctx).
 *
 * Apart from that, the function has no more applications and @ref
 * _anjay_dm_write() shall be used instead.
 */
int _anjay_dm_write_created_instance(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     anjay_iid_t iid,
                                     anjay_input_ctx_t *in_ctx);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_WRITE_CORE_H
