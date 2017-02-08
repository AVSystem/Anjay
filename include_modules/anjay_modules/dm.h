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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_DM_H
#define ANJAY_INCLUDE_ANJAY_MODULES_DM_H

#include <anjay/anjay.h>

#include <assert.h>
#include <limits.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef struct anjay_resource_path {
    anjay_oid_t oid;
    anjay_iid_t iid;
    anjay_rid_t rid;
} anjay_resource_path_t;

typedef enum anjay_request_action {
    ANJAY_ACTION_READ,
    ANJAY_ACTION_DISCOVER,
    ANJAY_ACTION_WRITE,
    ANJAY_ACTION_WRITE_UPDATE,
    ANJAY_ACTION_WRITE_ATTRIBUTES,
    ANJAY_ACTION_EXECUTE,
    ANJAY_ACTION_CREATE,
    ANJAY_ACTION_DELETE,
    ANJAY_ACTION_CANCEL_OBSERVE,
    ANJAY_ACTION_BOOTSTRAP_FINISH
} anjay_request_action_t;

int _anjay_dm_res_read(anjay_t *anjay,
                       const anjay_resource_path_t *path,
                       char *buffer,
                       size_t buffer_size,
                       size_t *out_bytes_read);

static inline int _anjay_dm_res_read_string(anjay_t *anjay,
                                            const anjay_resource_path_t *path,
                                            char *buffer,
                                            size_t buffer_size) {
    assert(buffer && buffer_size > 0);
    size_t bytes_read;
    int result = _anjay_dm_res_read(anjay, path, buffer, buffer_size - 1,
                                    &bytes_read);
    if (!result) {
        buffer[bytes_read] = '\0';
    }
    return result;
}

#define DEFINE_RES_READ_FUNC(Suffix, Type) \
    static inline int _anjay_dm_res_read_##Suffix( \
            anjay_t *anjay, \
            const anjay_resource_path_t *path, \
            Type *out_value) { \
        size_t bytes_read; \
        int result = _anjay_dm_res_read(anjay, path, (char*) out_value, \
                                        sizeof(*out_value), &bytes_read); \
        if (result) { \
            return result; \
        } \
        return bytes_read != sizeof(*out_value); \
    }

DEFINE_RES_READ_FUNC(i64, int64_t)   // _anjay_dm_res_read_i64
DEFINE_RES_READ_FUNC(double, double) // _anjay_dm_res_read_double
DEFINE_RES_READ_FUNC(bool, bool)     // _anjay_dm_res_read_bool

typedef struct anjay_dm anjay_dm_t;

#define ANJAY_DM_FOREACH_BREAK INT_MIN
#define ANJAY_DM_FOREACH_CONTINUE 0

typedef int
anjay_dm_foreach_object_handler_t(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj,
                                  void *data);

int _anjay_dm_foreach_object(anjay_t *anjay,
                             anjay_dm_foreach_object_handler_t *handler,
                             void *data);

typedef int
anjay_dm_foreach_instance_handler_t(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj,
                                    anjay_iid_t iid,
                                    void *data);

int _anjay_dm_foreach_instance(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_dm_foreach_instance_handler_t *handler,
                               void *data);

int _anjay_dm_object_read_default_attrs(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_ssid_t ssid,
                                        anjay_dm_attributes_t *out);
int _anjay_dm_object_write_default_attrs(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_ssid_t ssid,
                                         const anjay_dm_attributes_t *attrs);
int _anjay_dm_instance_it(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *obj_ptr,
                          anjay_iid_t *out,
                          void **cookie);
int _anjay_dm_instance_reset(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid);
int _anjay_dm_instance_present(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid);
int _anjay_dm_instance_create(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t *inout_iid,
                              anjay_ssid_t ssid);
int _anjay_dm_instance_remove(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr,
                              anjay_iid_t iid);
int _anjay_dm_instance_read_default_attrs(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr,
                                          anjay_iid_t iid,
                                          anjay_ssid_t ssid,
                                          anjay_dm_attributes_t *out);
int _anjay_dm_instance_write_default_attrs(anjay_t *anjay,
                                           const anjay_dm_object_def_t *const *obj_ptr,
                                           anjay_iid_t iid,
                                           anjay_ssid_t ssid,
                                           const anjay_dm_attributes_t *attrs);
int _anjay_dm_resource_present(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid);

int _anjay_dm_resource_supported(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_rid_t rid);
int
_anjay_dm_resource_supported_and_present(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr,
                                         anjay_iid_t iid,
                                         anjay_rid_t rid);

int _anjay_dm_resource_operations(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_rid_t rid,
                                  anjay_dm_resource_op_mask_t *out);

int _anjay_dm_resource_read(anjay_t *anjay,
                            const anjay_dm_object_def_t *const *obj_ptr,
                            anjay_iid_t iid,
                            anjay_rid_t rid,
                            anjay_output_ctx_t *ctx);
int _anjay_dm_resource_write(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_input_ctx_t *ctx);
int _anjay_dm_resource_execute(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_execute_ctx_t *execute_ctx);
int _anjay_dm_resource_dim(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr,
                           anjay_iid_t iid,
                           anjay_rid_t rid);
int _anjay_dm_resource_read_attrs(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid,
                                  anjay_ssid_t ssid,
                                  anjay_dm_attributes_t *out);
int _anjay_dm_resource_write_attrs(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_ssid_t ssid,
                                   const anjay_dm_attributes_t *attrs);

/**
 * Starts a transaction on the data model. If a transaction is already in
 * progress, it has nesting semantics.
 *
 * @param anjay ANJAY object to operate on.
 */
void _anjay_dm_transaction_begin(anjay_t *anjay);

/**
 * Includes a given object in transaction, calling its <c>transaction_begin</c>
 * handler if not already called during the current global transaction.
 *
 * During the outermost call to @ref _anjay_dm_transaction_finish, the
 * <c>transaction_commit</c> (preceded by <c>transaction_validate</c>) or
 * <c>transaction_rollback</c> handler will be called on all objects included
 * in this way.
 *
 * This function is automatically called by @ref _anjay_dm_instance_reset,
 * @ref _anjay_dm_instance_create, @ref _anjay_dm_instance_remove and
 * @ref _anjay_dm_resource_write .
 *
 * NOTE: Attempting to call this function without a global transaction in place
 * will cause an assertion fail.
 *
 * @param anjay   ANJAY object to operate on.
 * @param obj_ptr Handle to an object to include in transaction.
 *
 * @return 0 for success, or an error code.
 */
int _anjay_dm_transaction_include_object(
        anjay_t *anjay, const anjay_dm_object_def_t *const *obj_ptr);

/**
 * After having been called a number of times corresponding to number of
 * preceding calls to @ref _anjay_dm_transaction_begin, finishes the transaction
 * by performing either a commit or a rollback, depending on the value of the
 * <c>result</c> parameter.
 *
 * @param anjay  ANJAY object to operate on.
 * @param result Result code from the operations performed during the
 *               transaction. 0 denotes a success and causes the transaction to
 *               be committed. Non-zero value is treated as an error code and
 *               causes the transaction to be rolled back.
 *
 * @return Final result code of the transaction. If an error occured during the
 *         transaction handling routines (e.g. the transaction did not
 *         validate), a nonzero error code from those routines is returned.
 *         Otherwise, <c>result</c> is propagated. Note that it means that
 *         <c>0</c> is returned only after a successful commit following a
 *         successful transaction (denoted by <c>result == 0</c>).
 */
int _anjay_dm_transaction_finish(anjay_t *anjay, int result);

const anjay_dm_object_def_t *const *
_anjay_dm_find_object_by_oid(anjay_t *anjay, anjay_oid_t oid);

bool _anjay_dm_ssid_exists(anjay_t *anjay, anjay_ssid_t ssid);

bool _anjay_dm_attributes_empty(const anjay_dm_attributes_t *attrs);
bool _anjay_dm_attributes_full(const anjay_dm_attributes_t *attrs);

#define ANJAY_DM_OID_SECURITY 0
#define ANJAY_DM_OID_SERVER 1
#define ANJAY_DM_OID_ACCESS_CONTROL 2

#define ANJAY_DM_RID_SECURITY_SERVER_URI 0
#define ANJAY_DM_RID_SECURITY_BOOTSTRAP 1
#define ANJAY_DM_RID_SECURITY_MODE 2
#define ANJAY_DM_RID_SECURITY_PK_OR_IDENTITY 3
#define ANJAY_DM_RID_SECURITY_SERVER_PK_OR_IDENTITY 4
#define ANJAY_DM_RID_SECURITY_SECRET_KEY 5
#define ANJAY_DM_RID_SECURITY_SSID 10
#define ANJAY_DM_RID_SECURITY_CLIENT_HOLD_OFF_TIME 11
#define ANJAY_DM_RID_SECURITY_BOOTSTRAP_TIMEOUT 12

#define ANJAY_DM_RID_SERVER_SSID 0
#define ANJAY_DM_RID_SERVER_LIFETIME 1
#define ANJAY_DM_RID_SERVER_DEFAULT_PMIN 2
#define ANJAY_DM_RID_SERVER_DEFAULT_PMAX 3
#define ANJAY_DM_RID_SERVER_DISABLE_TIMEOUT 5
#define ANJAY_DM_RID_SERVER_NOTIFICATION_STORING 6
#define ANJAY_DM_RID_SERVER_BINDING 7

#define ANJAY_DM_RID_ACCESS_CONTROL_OID 0
#define ANJAY_DM_RID_ACCESS_CONTROL_OIID 1
#define ANJAY_DM_RID_ACCESS_CONTROL_ACL 2
#define ANJAY_DM_RID_ACCESS_CONTROL_OWNER 3

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_DM_H */

