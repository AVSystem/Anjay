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

#ifndef ANJAY_INCLUDE_ANJAY_PERSISTENCE_H
#define ANJAY_INCLUDE_ANJAY_PERSISTENCE_H

#include <avsystem/commons/persistence.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef __GNUC__
#    warning "<anjay/persistence.h> is deprecated. Please use " \
             "<avsystem/commons/persistence.h> instead."
#endif

typedef avs_persistence_context_t anjay_persistence_context_t;

typedef avs_persistence_handler_collection_element_t
        anjay_persistence_handler_collection_element_t;

typedef avs_persistence_cleanup_collection_element_t
        anjay_persistence_cleanup_collection_element_t;

#define anjay_persistence_store_context_new avs_persistence_store_context_new
#define anjay_persistence_restore_context_new \
    avs_persistence_restore_context_new
#define anjay_persistence_ignore_context_new avs_persistence_ignore_context_new
#define anjay_persistence_context_delete avs_persistence_context_delete

#define anjay_persistence_bool avs_persistence_bool
#define anjay_persistence_bytes avs_persistence_bytes
#define anjay_persistence_u16 avs_persistence_u16
#define anjay_persistence_u32 avs_persistence_u32
#define anjay_persistence_double avs_persistence_double
#define anjay_persistence_sized_buffer avs_persistence_sized_buffer
#define anjay_persistence_string avs_persistence_string
#define anjay_persistence_list(...) avs_persistence_list(__VA_ARGS__, NULL)
#define anjay_persistence_tree avs_persistence_tree

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_PERSISTENCE_H */
