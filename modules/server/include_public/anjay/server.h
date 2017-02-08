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

#ifndef ANJAY_INCLUDE_ANJAY_SERVER_H
#define ANJAY_INCLUDE_ANJAY_SERVER_H

#include <anjay/anjay.h>

#include <avsystem/commons/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /** Resource: Short Server ID */
    anjay_ssid_t ssid;
    /** Resource: Lifetime */
    int32_t lifetime;
    /** Resource: Default Minimum Period - or a negative value to disable presence */
    int32_t default_min_period;
    /** Resource: Default Maximum Period - or a negative value to disable presence */
    int32_t default_max_period;
    /** Resource: Disable Timeout - or a negative value to disable presence */
    int32_t disable_timeout;
    /** Resource: Binding */
    anjay_binding_mode_t binding;
    /** Resource: Notification Storing When Disabled or Offline */
    bool notification_storing;
} anjay_server_instance_t;

/**
 * Creates Server Object ready to get registered in Anjay.
 *
 * @returns pointer to Server Object or NULL in case of error.
 */
const anjay_dm_object_def_t **anjay_server_object_create(void);

/**
 * Adds new Instance of Server Object and returns newly created Instance id
 * via @p inout_iid .
 *
 * Note: if @p *inout_iid is set to @ref ANJAY_IID_INVALID then the Instance id
 * is generated automatically, otherwise value of @p *inout_iid is used as a
 * new Server Instance Id.
 *
 * Note: @p instance may be safely freed by the user code after this function
 * finishes (internally a deep copy of @ref anjay_server_instance_t is
 * performed).
 *
 * @param obj       Server Object to operate on.
 * @param instance  Server Instance to insert.
 * @param inout_iid Server Instance id to use or @ref ANJAY_IID_INVALID .
 *
 * @return 0 on success, negative value in case of an error or if the instance
 * of specified id already exists.
 */
int anjay_server_object_add_instance(const anjay_dm_object_def_t *const *obj,
                                     const anjay_server_instance_t *instance,
                                     anjay_iid_t *inout_iid);


/**
 * Removes all instances of Server Object leaving it in an empty state.
 *
 * @param obj   Server Object to purge.
 */
void anjay_server_object_purge(const anjay_dm_object_def_t *const *obj);

/**
 * Deletes Server Object
 *
 * @param server    Server Object to remove.
 */
void anjay_server_object_delete(const anjay_dm_object_def_t **server);

/**
 * Dumps Server Object Instance into the @p out_stream .
 * Warning: @p obj MUST NOT be wrapped.
 *
 * @param obj           Server Object.
 * @param out_stream    Stream to write to.
 * @return 0 in case of success, negative value in case of an error.
 */
int anjay_server_object_persist(const anjay_dm_object_def_t *const *obj,
                                avs_stream_abstract_t *out_stream);

/**
 * Attempts to restore Server Object Instances from specified @p in_stream .
 * Warning: @p obj MUST NOT be wrapped.
 *
 * Note: if restore fails, then Server Object will be left untouched, on
 * success though all Instances stored within the Object will be purged.
 *
 * @param obj       Server Object.
 * @param in_stream Stream to read from.
 * @return 0 in case of success, negative value in case of an error.
 */
int anjay_server_object_restore(const anjay_dm_object_def_t *const *obj,
                                avs_stream_abstract_t *in_stream);
#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_SERVER_H */
