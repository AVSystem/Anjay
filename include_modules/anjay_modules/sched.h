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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_SCHED_H
#define ANJAY_INCLUDE_ANJAY_MODULES_SCHED_H

#include <time.h>

#include <anjay/core.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef void *anjay_sched_handle_t;

typedef void (*anjay_sched_clb_t)(anjay_t *anjay, const void *data);

typedef struct anjay_sched_struct anjay_sched_t;

anjay_sched_t *_anjay_sched_get(anjay_t *anjay);

ssize_t _anjay_sched_run(anjay_sched_t *sched);
void _anjay_sched_delete(anjay_sched_t **sched_ptr);

/**
 * Schedules oneshot job, that will be removed from the scheduler after
 * it is executed.
 *
 * Note: @p out_handle is optional pointer parameter where job handle will be
 * stored during time it is being scheduled in the scheduler. Moreover, when
 * the job is fetched from the scheduler queue to be executed, scheduler sets
 * (*out_handle) to NULL (indicating to the user that it is no longer a valid
 * job handle). Therefore, one have to carefully manage @p out_handle lifetime
 * or otherwise the behavior will be undefined.
 *
 * @param sched         Scheduler object to add the job into.
 * @param out_handle    Pointer to the storage of scheduler handle, that might
 *                      be used to cancel the job or NULL if no handle
 *                      information is required.
 * @param delay         Delay until the first job execution.
 * @param clb           Scheduled task.
 * @param clb_data      Pointer to data that will be passed to @p clb.
 * @param clb_data_size Number of bytes at @p clb_data that will be stored in
 *                      the job structure.
 *
 * @return 0 on success, negative value in case of error.
 */
int _anjay_sched(anjay_sched_t *sched,
                 anjay_sched_handle_t *out_handle,
                 avs_time_duration_t delay,
                 anjay_sched_clb_t clb,
                 const void *clb_data,
                 size_t clb_data_size);
/**
 * Removes job handle (pointed by @p handle) from the scheduler, and therefore
 * invalidates it by setting it to NULL.
 *
 * @param sched     Scheduler object to remove job from.
 * @param handle    Pointer to the job handle to remove.
 *
 * @return 0 on success, negative value in case of an error.
 */
int _anjay_sched_del(anjay_sched_t *sched, anjay_sched_handle_t *handle);

int _anjay_sched_time_to_next(anjay_sched_t *sched, avs_time_duration_t *delay);

/**
 * See @ref _anjay_sched for details.
 */
static inline int _anjay_sched_now(anjay_sched_t *sched,
                                   anjay_sched_handle_t *out_handle,
                                   anjay_sched_clb_t clb,
                                   const void *clb_data,
                                   size_t clb_data_size) {
    return _anjay_sched(sched, out_handle, AVS_TIME_DURATION_ZERO, clb,
                        clb_data, clb_data_size);
}

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_SCHED_H */
