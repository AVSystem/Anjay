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

#include <stdio.h>

#include "../objects.h"
#include "../utils.h"

#define APNCP_RES_PROFILE_NAME 0                            // string
#define APNCP_RES_APN 1                                     // string
#define APNCP_RES_AUTO_SELECT_APN_BY_DEVICE 2               // bool
#define APNCP_RES_ENABLE_STATUS 3                           // bool
#define APNCP_RES_AUTHENTICATION_TYPE 4                     // int
#define APNCP_RES_USER_NAME 5                               // string
#define APNCP_RES_SECRET 6                                  // string
#define APNCP_RES_RECONNECT_SCHEDULE 7                      // string
#define APNCP_RES_VALIDITY 8                                // string
#define APNCP_RES_CONNECTION_ESTABLISHMENT_TIME 9           // time
#define APNCP_RES_CONNECTION_ESTABLISHMENT_RESULT 10        // int
#define APNCP_RES_CONNECTION_ESTABLISHMENT_REJECT_CAUSE 11  // int[0:111]
#define APNCP_RES_CONNECTION_END_TIME 12                    // time
#define APNCP_RES_TOTAL_BYTES_SENT 13                       // int
#define APNCP_RES_TOTAL_BYTES_RECEIVED 14                   // int
#define APNCP_RES_IP_ADDRESS 15                             // string
#define APNCP_RES_PREFIX_LENGTH 16                          // string
#define APNCP_RES_SUBNET_MASK 17                            // string
#define APNCP_RES_GATEWAY 18                                // string
#define APNCP_RES_PRIMARY_DNS_ADDRESS 19                    // string
#define APNCP_RES_SECONDARY_DNS_ADDRESS 20                  // string
#define APNCP_RES_QCI 21                                    // int[1:9]
#define APNCP_RES_VENDOR_SPECIFIC_EXTENSIONS 22             // objlnk

#define APNCP_RID_BOUND_ 23

typedef enum {
    AUTH_PAP = 0,
    AUTH_CHAP,

    AUTH_END_
} apn_auth_type_t;

typedef struct {
    anjay_iid_t iid;

    bool has_profile_name;
    bool has_auth_type;
    char profile_name[256];
    apn_auth_type_t auth_type;
} apn_conn_profile_t;

typedef struct {
    const anjay_dm_object_def_t *def;
    AVS_LIST(apn_conn_profile_t) instances;
    AVS_LIST(apn_conn_profile_t) saved_instances;
} apn_conn_profile_repr_t;

static inline apn_conn_profile_repr_t *
get_apncp(const anjay_dm_object_def_t *const *obj_ptr) {
    if (!obj_ptr) {
        return NULL;
    }
    return container_of(obj_ptr, apn_conn_profile_repr_t, def);
}

static apn_conn_profile_t *find_instance(const apn_conn_profile_repr_t *repr,
                                         anjay_iid_t iid) {
    AVS_LIST(apn_conn_profile_t) it;
    AVS_LIST_FOREACH(it, repr->instances) {
        if (it->iid == iid) {
            return it;
        }
    }

    return NULL;
}

static int apncp_instance_it(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj_ptr,
                             anjay_iid_t *out,
                             void **cookie) {
    (void)anjay;

    AVS_LIST(apn_conn_profile_t) inst = (AVS_LIST(apn_conn_profile_t))*cookie;

    if (!inst) {
        inst = get_apncp(obj_ptr)->instances;
    } else {
        inst = AVS_LIST_NEXT(inst);
    }

    *out = inst ? inst->iid : ANJAY_IID_INVALID;
    *cookie = inst;
    return 0;
}

static int apncp_instance_present(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid) {
    (void)anjay;
    return find_instance(get_apncp(obj_ptr), iid) != NULL;
}

static anjay_iid_t get_new_iid(AVS_LIST(apn_conn_profile_t) instances) {
    anjay_iid_t iid = 1;
    AVS_LIST(apn_conn_profile_t) it;
    AVS_LIST_FOREACH(it, instances) {
        if (it->iid == iid) {
            ++iid;
        } else if (it->iid > iid) {
            break;
        }
    }
    return iid;
}

static int apncp_instance_create(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t *inout_iid,
                                 anjay_ssid_t ssid) {
    (void) anjay;
    (void) ssid;
    apn_conn_profile_repr_t* repr = get_apncp(obj_ptr);

    AVS_LIST(apn_conn_profile_t) created =
            AVS_LIST_NEW_ELEMENT(apn_conn_profile_t);
    if (!created) {
        return ANJAY_ERR_INTERNAL;
    }

    if (*inout_iid == ANJAY_IID_INVALID) {
        *inout_iid = get_new_iid(repr->instances);
        if (*inout_iid == ANJAY_IID_INVALID) {
            AVS_LIST_CLEAR(&created);
            return ANJAY_ERR_INTERNAL;
        }
    }

    created->iid = *inout_iid;

    AVS_LIST(apn_conn_profile_t) *ptr;
    AVS_LIST_FOREACH_PTR(ptr, &repr->instances) {
        if ((*ptr)->iid > created->iid) {
            break;
        }
    }

    AVS_LIST_INSERT(ptr, created);
    return 0;
}

static int apncp_instance_remove(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj_ptr,
                                 anjay_iid_t iid) {
    (void)anjay;
    apn_conn_profile_repr_t *repr  = get_apncp(obj_ptr);

    AVS_LIST(apn_conn_profile_t) *it;
    AVS_LIST_FOREACH_PTR(it, &repr->instances) {
        if ((*it)->iid == iid) {
            AVS_LIST_DELETE(it);
            return 0;
        }
    }

    return ANJAY_ERR_NOT_FOUND;
}

static int apncp_resource_supported(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_rid_t rid) {
    (void) anjay;
    (void) obj_ptr;

    switch (rid) {
    case APNCP_RES_PROFILE_NAME:
    case APNCP_RES_AUTHENTICATION_TYPE:
        return 1;
    default:
        return 0;
    }
}

static int apncp_resource_present(anjay_t *anjay,
                                  const anjay_dm_object_def_t *const *obj_ptr,
                                  anjay_iid_t iid,
                                  anjay_rid_t rid) {
    (void) iid;
    return apncp_resource_supported(anjay, obj_ptr, rid);
}

static int apncp_resource_read(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_output_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;

    apn_conn_profile_t *inst = find_instance(get_apncp(obj_ptr), iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case APNCP_RES_PROFILE_NAME:
        return anjay_ret_string(ctx, inst->profile_name);
    case APNCP_RES_AUTHENTICATION_TYPE:
        return anjay_ret_i32(ctx, inst->auth_type);
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int apncp_resource_write(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t iid,
                                anjay_rid_t rid,
                                anjay_input_ctx_t *ctx) {
    (void) anjay; (void) obj_ptr; (void) iid;

    apn_conn_profile_t *inst = find_instance(get_apncp(obj_ptr), iid);
    if (!inst) {
        return ANJAY_ERR_NOT_FOUND;
    }

    switch (rid) {
    case APNCP_RES_PROFILE_NAME:
        {
            char buf[sizeof(inst->profile_name)];
            if (anjay_get_string(ctx, buf, sizeof(buf)) < 0) {
                return ANJAY_ERR_INTERNAL;
            }

            ssize_t result = snprintf(inst->profile_name,
                                      sizeof(inst->profile_name), "%s", buf);
            if (result < 0 || result >= (ssize_t)sizeof(inst->profile_name)) {
                return ANJAY_ERR_INTERNAL;
            }
            inst->has_profile_name = true;
            return 0;
        }
    case APNCP_RES_AUTHENTICATION_TYPE:
        {
            int new_val = 0;
            if (anjay_get_i32(ctx, &new_val)) {
                return ANJAY_ERR_INTERNAL;
            }

            if (new_val < 0 || new_val >= AUTH_END_) {
                return ANJAY_ERR_BAD_REQUEST;
            }

            inst->auth_type = (apn_auth_type_t)new_val;
            inst->has_auth_type = true;
            return 0;
        }
    default:
        return ANJAY_ERR_NOT_FOUND;
    }
}

static int
apncp_transaction_begin(anjay_t *anjay,
                        const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    apn_conn_profile_repr_t *repr = get_apncp(obj_ptr);
    if (!repr->instances) {
        return 0;
    }
    repr->saved_instances = AVS_LIST_SIMPLE_CLONE(repr->instances);
    if (!repr->saved_instances) {
        demo_log(ERROR, "cannot clone APN Connection Profile repr");
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

static int
apncp_transaction_validate(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    AVS_LIST(apn_conn_profile_t) it;
    AVS_LIST_FOREACH(it, get_apncp(obj_ptr)->instances) {
        if (!it->has_profile_name || !it->has_auth_type) {
            return ANJAY_ERR_BAD_REQUEST;
        }
    }
    return 0;
}

static int
apncp_transaction_commit(anjay_t *anjay,
                         const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    AVS_LIST_CLEAR(&get_apncp(obj_ptr)->saved_instances);
    return 0;
}

static int
apncp_transaction_rollback(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj_ptr) {
    (void) anjay;
    apn_conn_profile_repr_t *repr = get_apncp(obj_ptr);
    AVS_LIST_CLEAR(&repr->instances);
    repr->instances = repr->saved_instances;
    repr->saved_instances = NULL;
    return 0;
}

static int
apncp_instance_reset(anjay_t *anjay,
                     const anjay_dm_object_def_t *const *obj_ptr,
                     anjay_iid_t iid) {
    (void) anjay;
    apn_conn_profile_t *inst = find_instance(get_apncp(obj_ptr), iid);
    inst->has_auth_type = false;
    inst->has_profile_name = false;
    return 0;
}

static const anjay_dm_object_def_t apn_conn_profile = {
    .oid = 11,
    .rid_bound = APNCP_RID_BOUND_,
    .instance_it = apncp_instance_it,
    .instance_present = apncp_instance_present,
    .instance_create = apncp_instance_create,
    .instance_remove = apncp_instance_remove,
    .instance_reset = apncp_instance_reset,
    .resource_supported = apncp_resource_supported,
    .resource_present = apncp_resource_present,
    .resource_read = apncp_resource_read,
    .resource_write = apncp_resource_write,
    .transaction_begin = apncp_transaction_begin,
    .transaction_validate = apncp_transaction_validate,
    .transaction_commit = apncp_transaction_commit,
    .transaction_rollback = apncp_transaction_rollback
};

const anjay_dm_object_def_t **apn_conn_profile_object_create(void) {
    apn_conn_profile_repr_t *repr = (apn_conn_profile_repr_t *)
            calloc(1, sizeof(apn_conn_profile_repr_t));
    if (!repr) {
        return NULL;
    }

    repr->def = &apn_conn_profile;

    return &repr->def;
}

void apn_conn_profile_object_release(const anjay_dm_object_def_t **def) {
    apn_conn_profile_repr_t *apncp = get_apncp(def);
    AVS_LIST_CLEAR(&apncp->instances);
    AVS_LIST_CLEAR(&apncp->saved_instances);
    free(apncp);
}

AVS_LIST(anjay_iid_t)
apn_conn_profile_list_activated(const anjay_dm_object_def_t **def) {
    AVS_LIST(anjay_iid_t) iids = NULL;
    AVS_LIST(anjay_iid_t) *tail = &iids;

    AVS_LIST(apn_conn_profile_t) it;
    AVS_LIST_FOREACH(it, get_apncp(def)->instances) {
        AVS_LIST(anjay_iid_t) elem = AVS_LIST_NEW_ELEMENT(anjay_iid_t);
        if (!elem) {
            AVS_LIST_CLEAR(&iids);
            return NULL;
        }

        *elem = it->iid;
        AVS_LIST_INSERT(tail, elem);
        tail = AVS_LIST_NEXT_PTR(tail);
    }

    return iids;
}
