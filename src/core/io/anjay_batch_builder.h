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

#ifndef ANJAY_BATCH_BUILDER_H
#define ANJAY_BATCH_BUILDER_H

#include <anjay/anjay.h>

#include "../anjay_dm_core.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

/**
 * From RFC8428 4.5.3. Time:
 * - Values less than 268,435,456 (2**28) represent time relative to the current
 *   time.
 * - Values greater than or equal to 2**28 represent an absolute time relative
 *   to the Unix epoch (1970-01-01T00:00Z in UTC time)
 */
#define SENML_TIME_SECONDS_THRESHOLD (1 << 28)

typedef struct anjay_batch_builder_struct anjay_batch_builder_t;

typedef struct anjay_batch_struct anjay_batch_t;

typedef struct anjay_batch_data_output_state_struct
        anjay_batch_data_output_state_t;

anjay_batch_builder_t *_anjay_batch_builder_new(void);

/**
 * Adds values of various types to the batch.
 *
 * @returns 0 on success, negative value otherwise.
 */
/**@{*/
int _anjay_batch_add_int(anjay_batch_builder_t *builder,
                         const anjay_uri_path_t *uri,
                         avs_time_real_t timestamp,
                         int64_t value);

int _anjay_batch_add_double(anjay_batch_builder_t *builder,
                            const anjay_uri_path_t *uri,
                            avs_time_real_t timestamp,
                            double value);

int _anjay_batch_add_bool(anjay_batch_builder_t *builder,
                          const anjay_uri_path_t *uri,
                          avs_time_real_t timestamp,
                          bool value);

int _anjay_batch_add_string(anjay_batch_builder_t *builder,
                            const anjay_uri_path_t *uri,
                            avs_time_real_t timestamp,
                            const char *str);

int _anjay_batch_add_objlnk(anjay_batch_builder_t *builder,
                            const anjay_uri_path_t *uri,
                            avs_time_real_t timestamp,
                            anjay_oid_t objlnk_oid,
                            anjay_iid_t objlnk_iid);
/**@}*/

/**
 * Releases batch builder and discards all data. It has no effect if builder was
 * previously compiled.
 *
 * @param builder Pointer to pointer to data builder. Set to NULL after cleanup.
 */
void _anjay_batch_builder_cleanup(anjay_batch_builder_t **builder);

/**
 * Compiles data from the batch builder into a reference-counted (with count
 * initialized to 1) immutable data batch.
 *
 * @param builder Pointer to pointer to batch builder. Set to NULL after
 *                successful return.
 *
 * @returns Pointer to compiled batch in case of success, NULL otherwise. If
 *          this function fails, batch builder is not modified and must be freed
 *          manually with @ref _anjay_batch_builder_cleanup() if it's not to be
 *          used anymore.
 */
anjay_batch_t *_anjay_batch_builder_compile(anjay_batch_builder_t **builder);

/**
 * Increments the refcount for a *batch.
 *
 * Note that conventionally, const anjay_batch_t * pointers are passed when
 * "borrowing" the batch, and non-const pointers when passing ownership of a
 * batch reference.
 *
 * @param batch Non-null batch which refcount will be incremented
 *
 * @returns @p batch
 */
anjay_batch_t *_anjay_batch_acquire(const anjay_batch_t *batch);

/**
 * Decreases the refcount for a *batch, sets it to NULL, and frees it if the
 * refcount has reached zero.
 *
 * @param *batch Pointer to compiled data batch.
 */
void _anjay_batch_release(anjay_batch_t **batch);

int _anjay_dm_read_into_batch(anjay_batch_builder_t *builder,
                              anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj,
                              const anjay_dm_path_info_t *path_info,
                              anjay_ssid_t requesting_ssid);

/**
 * Filters content of the batch for server with specified @p target_ssid
 * according to Access Control permissions of this server. Then outputs the data
 * into @p out_ctx . The output will contain only those entries of @p data which
 * paths were configured by @ref anjay_access_control_set_acl with enabled
 * @c ANJAY_ACCESS_MASK_READ .
 *
 * @param anjay       Anjay object to operate on
 * @param batch       Compiled batch
 * @param target_ssid SSID of the server for which this batch is being
 *                    serialized
 * @param out_ctx     Output context to serialize into
 *
 * @returns 0 for success, or a negative value in case of error.
 */
int _anjay_batch_data_output(anjay_t *anjay,
                             const anjay_batch_t *batch,
                             anjay_ssid_t target_ssid,
                             anjay_output_ctx_t *out_ctx);

/**
 * Serializes part of the batch associated with a single Resource or Resource
 * Instance.
 *
 * <example>
 * The following code is equivalent to @ref _anjay_batch_data_output:
 *
 * @code
 * const avs_time_real_t serialization_time = avs_time_real_now();
 * const anjay_batch_data_output_state_t *state = NULL;
 * int result = 0;
 * do {
 *     result = _anjay_batch_data_output_entry(
 *             anjay, batch, target_ssid, serialization_time, &state, out_ctx);
 * } while (!result && state);
 * @endcode
 * </example>
 *
 * @param [in]    anjay              Anjay object to operate on.
 *
 * @param [in]    batch              Compiled batch.
 *
 * @param [in]    target_ssid        SSID of the server for which this batch is
 *                                   being serialized.
 *
 * @param [in]    serialization_time Time point that shall be treated as the
 *                                   current time during serialization.
 *                                   Serialized values of timestamps may depend
 *                                   on it, so the same time SHOULD be passed to
 *                                   all calls in a given iteration.
 *
 * @param [inout] state              Pointer to a state variable. Before the
 *                                   initial call, <c>*state</c> shall be
 *                                   <c>NULL</c> - this function will then
 *                                   serialize the first batch element, and set
 *                                   <c>*state</c> so that the next call will
 *                                   serialize the subsequent entry. If
 *                                   <c>state</c> is <c>NULL</c> on return, it
 *                                   means that there are no more entries to
 *                                   serialize.
 *
 *                                   The value of <c>*state</c> shall be treated
 *                                   as opaque - the caller MUST NOT ever
 *                                   attempt to dereference, deallocate or
 *                                   otherwise access it in any way other than
 *                                   passing it to
 *                                   @ref _anjay_batch_data_output_entry again.
 *                                   Iteration does not allocate any additional
 *                                   resources, so it is memory-safe, and does
 *                                   not require any additional deallocation, to
 *                                   stop serializing without reaching the end.
 *
 * @param [in]    out_ctx            Output context to serialize into.
 *
 * @returns 0 for success, or a negative value in case of error. On error, the
 *          value of <c>*state</c> shall be treated as invalid.
 *
 * If <c>*state</c> is neither <c>NULL</c> nor a value set by a previous call to
 * this function with otherwise the same set of arguments, the behaviour is
 * undefined.
 */
int _anjay_batch_data_output_entry(
        anjay_t *anjay,
        const anjay_batch_t *batch,
        anjay_ssid_t target_ssid,
        avs_time_real_t serialization_time,
        const anjay_batch_data_output_state_t **state,
        anjay_output_ctx_t *out_ctx);

/**
 * Returns whether two batches have exactly the same data.
 *
 * NOTE: Timestamps are NOT taken into account in comparisons.
 *
 * NOTE: Batches that contain the same data set, but in different order, are
 * treated as different.
 *
 * NOTE: Data that has equivalent value, but different data type (e.g. int(42)
 * and double(42.0)) is treated as different.
 */
bool _anjay_batch_values_equal(const anjay_batch_t *a, const anjay_batch_t *b);

bool _anjay_batch_data_requires_hierarchical_format(const anjay_batch_t *batch);

/**
 * If batch consists of a single entry pertaining to a Single Resource or
 * Resource Instance, with a value of numeric type (int, uint or double), then
 * returns its numerical value. Otherwise returns NAN.
 */
double _anjay_batch_data_numeric_value(const anjay_batch_t *batch);

/**
 * Returns the time when the batch was returned from @ref
 * _anjay_batch_builder_compile
 */
avs_time_real_t _anjay_batch_get_compilation_time(const anjay_batch_t *batch);

VISIBILITY_PRIVATE_HEADER_END

#endif // ANJAY_BATCH_BUILDER_H
