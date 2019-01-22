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

#ifndef ANJAY_SCHED_INTERNAL_H
#define ANJAY_SCHED_INTERNAL_H

VISIBILITY_PRIVATE_HEADER_BEGIN

#if !(defined(ANJAY_SCHED_C) || defined(ANJAY_TEST))
#    error "sched_internal.h is not meant to be included from outside sched.c"
#endif

typedef struct {
    anjay_sched_handle_t *handle_ptr;
    avs_time_monotonic_t when;
    anjay_sched_clb_t clb;
    avs_max_align_t clb_data;
} anjay_sched_entry_t;

struct anjay_sched_struct {
    anjay_t *anjay;
    AVS_LIST(anjay_sched_entry_t) entries;
    bool shut_down;
};

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_SCHED_INTERNAL_H */
