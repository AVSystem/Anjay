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

#include <anjay_init.h>

#ifdef ANJAY_WITH_DISCOVER

#    include <inttypes.h>

#    include <anjay_modules/anjay_time_defs.h>

#    include "anjay_discover.h"
#    include "anjay_query.h"

#    include "../anjay_access_utils_private.h"
#    include "../anjay_core.h"
#    include "../anjay_dm_core.h"

VISIBILITY_SOURCE_BEGIN

static int
print_period_attr(avs_stream_t *stream, const char *name, int32_t t) {
    if (t < 0) {
        return 0;
    }
    return avs_is_ok(avs_stream_write_f(stream, ";%s=%lu", name,
                                        (unsigned long) t))
                   ? 0
                   : -1;
}

#    ifdef ANJAY_WITH_CON_ATTR
static int print_con_attr(avs_stream_t *stream, anjay_dm_con_attr_t value) {
    if (value < 0) {
        return 0;
    }
    return avs_is_ok(avs_stream_write_f(stream, ";" ANJAY_CUSTOM_ATTR_CON "=%d",
                                        (int) value))
                   ? 0
                   : -1;
}
#    else // ANJAY_WITH_CON_ATTR
#        define print_con_attr(...) 0
#    endif // ANJAY_WITH_CON_ATTR

static int
print_double_attr(avs_stream_t *stream, const char *name, double value) {
    if (isnan(value)) {
        return 0;
    }
    return avs_is_ok(avs_stream_write_f(stream, ";%s=%s", name,
                                        AVS_DOUBLE_AS_STRING(value, 17)))
                   ? 0
                   : -1;
}

static int print_oi_attrs(avs_stream_t *stream,
                          const anjay_dm_internal_oi_attrs_t *attrs) {
    int result = 0;
    (void) ((result = print_period_attr(stream, ANJAY_ATTR_PMIN,
                                        attrs->standard.min_period))
            || (result = print_period_attr(stream, ANJAY_ATTR_PMAX,
                                           attrs->standard.max_period))
            || (result = print_period_attr(stream, ANJAY_ATTR_EPMIN,
                                           attrs->standard.min_eval_period))
            || (result = print_period_attr(stream, ANJAY_ATTR_EPMAX,
                                           attrs->standard.max_eval_period))
            || (result = print_con_attr(stream, attrs->custom.data.con)));
    return result;
}

static int print_resource_dim(avs_stream_t *stream, int32_t dim) {
    if (dim >= 0) {
        return avs_is_ok(avs_stream_write_f(stream, ";dim=%" PRIu32,
                                            (uint32_t) dim))
                       ? 0
                       : -1;
    }
    return 0;
}

static int print_r_attrs(avs_stream_t *stream,
                         const anjay_dm_internal_r_attrs_t *attrs) {
    int result;
    (void) ((result = print_oi_attrs(stream,
                                     _anjay_dm_get_internal_oi_attrs_const(
                                             &attrs->standard.common)))
            || (result = print_double_attr(stream, ANJAY_ATTR_GT,
                                           attrs->standard.greater_than))
            || (result = print_double_attr(stream, ANJAY_ATTR_LT,
                                           attrs->standard.less_than))
            || (result = print_double_attr(stream, ANJAY_ATTR_ST,
                                           attrs->standard.step)));
    return result;
}

static int print_discovered_object(avs_stream_t *stream,
                                   const anjay_dm_object_def_t *const *obj,
                                   const anjay_dm_internal_oi_attrs_t *attrs) {
    if (avs_is_err(avs_stream_write_f(stream, "</%" PRIu16 ">", (*obj)->oid))) {
        return -1;
    }
    if ((*obj)->version
            && avs_is_err(avs_stream_write_f(stream, ";ver=\"%s\"",
                                             (*obj)->version))) {
        return -1;
    }
    return print_oi_attrs(stream, attrs);
}

static int
print_discovered_instance(avs_stream_t *stream,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          const anjay_dm_internal_oi_attrs_t *attrs) {
    if (avs_is_err(avs_stream_write_f(stream, "</%" PRIu16 "/%" PRIu16 ">",
                                      (*obj)->oid, iid))) {
        return -1;
    }
    return print_oi_attrs(stream, attrs);
}

static int print_discovered_resource(avs_stream_t *stream,
                                     const anjay_dm_object_def_t *const *obj,
                                     anjay_iid_t iid,
                                     anjay_rid_t rid,
                                     int32_t resource_dim,
                                     const anjay_dm_internal_r_attrs_t *attrs) {
    if (avs_is_err(avs_stream_write_f(stream,
                                      "</%" PRIu16 "/%" PRIu16 "/%" PRIu16 ">",
                                      (*obj)->oid, iid, rid))
            || print_resource_dim(stream, resource_dim)
            || print_r_attrs(stream, attrs)) {
        return -1;
    }
    return 0;
}

static int print_separator(avs_stream_t *stream) {
    return avs_is_ok(avs_stream_write(stream, ",", 1)) ? 0 : -1;
}

static int read_resource_dim_clb(anjay_t *anjay,
                                 const anjay_dm_object_def_t *const *obj,
                                 anjay_iid_t iid,
                                 anjay_rid_t rid,
                                 anjay_riid_t riid,
                                 void *out_dim_) {
    (void) anjay;
    (void) obj;
    (void) iid;
    (void) rid;
    (void) riid;
    ++*(int32_t *) out_dim_;
    return 0;
}

static int read_resource_dim(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             int32_t *out_dim) {
    *out_dim = 0;
    int result =
            _anjay_dm_foreach_resource_instance(anjay, obj, iid, rid,
                                                read_resource_dim_clb, out_dim);
    if (result == ANJAY_ERR_METHOD_NOT_ALLOWED
            || result == ANJAY_ERR_NOT_IMPLEMENTED) {
        *out_dim = -1;
        return 0;
    }
    return result;
}

#    if defined(ANJAY_WITH_LWM2M11) || defined(ANJAY_WITH_BOOTSTRAP)
static anjay_lwm2m_version_t current_lwm2m_version(anjay_t *anjay) {
    assert(anjay->current_connection.server);
    return _anjay_server_registration_info(anjay->current_connection.server)
            ->lwm2m_version;
}
#    endif // defined(ANJAY_WITH_LWM2M11) || defined(ANJAY_WITH_BOOTSTRAP)

static int discover_resource(anjay_t *anjay,
                             avs_stream_t *stream,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_dm_resource_kind_t kind,
                             anjay_id_type_t requested_path_type) {
    int32_t resource_dim = -1;
    int result = 0;

    if (requested_path_type != ANJAY_ID_OID && _anjay_dm_res_kind_multiple(kind)
            && (result = read_resource_dim(anjay, obj, iid, rid,
                                           &resource_dim))) {
        return result;
    }

    anjay_dm_internal_r_attrs_t resource_attributes =
            ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;
    switch (requested_path_type) {
    case ANJAY_ID_OID:
        result = print_discovered_resource(stream, obj, iid, rid, resource_dim,
                                           &resource_attributes);
        break;
    case ANJAY_ID_IID:
        (void) ((result = _anjay_dm_call_resource_read_attrs(
                         anjay, obj, iid, rid, _anjay_dm_current_ssid(anjay),
                         &resource_attributes, NULL))
                || (result = print_discovered_resource(stream, obj, iid, rid,
                                                       resource_dim,
                                                       &resource_attributes)));
        break;
    case ANJAY_ID_RID:
        (void) ((result = _anjay_dm_effective_attrs(
                         anjay,
                         &(const anjay_dm_attrs_query_details_t) {
                             .obj = obj,
                             .iid = iid,
                             .rid = rid,
                             .riid = ANJAY_ID_INVALID,
                             .ssid = _anjay_dm_current_ssid(anjay),
                             /**
                              * Spec says we care about inherited attributes
                              * from Object and Instance levels only.
                              */
                             .with_server_level_attrs = false
                         },
                         &resource_attributes))
                || (result = print_discovered_resource(stream, obj, iid, rid,
                                                       resource_dim,
                                                       &resource_attributes)));
        break;
    default:
        AVS_UNREACHABLE("LwM2M Discover can be performed only on Object, "
                        "Object Instance or Resource path");
    }
    return result;
}

typedef struct {
    anjay_id_type_t requested_path_type;
    avs_stream_t *stream;
} discover_instance_resource_args_t;

static int
discover_instance_resource_clb(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_dm_resource_kind_t kind,
                               anjay_dm_resource_presence_t presence,
                               void *args_) {
    discover_instance_resource_args_t *args =
            (discover_instance_resource_args_t *) args_;
    int result = 0;
    if (presence != ANJAY_DM_RES_ABSENT
            && !(result = print_separator(args->stream))) {
        result = discover_resource(anjay, args->stream, obj, iid, rid, kind,
                                   args->requested_path_type);
    }
    return result;
}

static int discover_instance_resources(anjay_t *anjay,
                                       avs_stream_t *stream,
                                       const anjay_dm_object_def_t *const *obj,
                                       anjay_iid_t iid,
                                       anjay_id_type_t requested_path_type) {
    return _anjay_dm_foreach_resource(
            anjay, obj, iid, discover_instance_resource_clb,
            &(discover_instance_resource_args_t) {
                .requested_path_type = requested_path_type,
                .stream = stream
            });
}

static int discover_object_instance(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj,
                                    anjay_iid_t iid,
                                    void *stream_) {
    avs_stream_t *stream = (avs_stream_t *) stream_;
    int result = 0;
    (void) ((result = print_separator(stream))
            || (result = print_discovered_instance(
                        stream, obj, iid, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY))
            || (result = discover_instance_resources(anjay, stream, obj, iid,
                                                     ANJAY_ID_OID)));
    return result;
}

static int discover_object(anjay_t *anjay,
                           avs_stream_t *stream,
                           const anjay_dm_object_def_t *const *obj) {
    anjay_dm_internal_oi_attrs_t object_attributes =
            ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
    int result = 0;
    (void) ((result = _anjay_dm_call_object_read_default_attrs(
                     anjay, obj, _anjay_dm_current_ssid(anjay),
                     &object_attributes, NULL))
            || (result = print_discovered_object(stream, obj,
                                                 &object_attributes)));
    if (result) {
        return result;
    }
    return _anjay_dm_foreach_instance(anjay, obj, discover_object_instance,
                                      stream);
}

static int discover_instance(anjay_t *anjay,
                             avs_stream_t *stream,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid) {
    anjay_dm_internal_oi_attrs_t instance_attributes =
            ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
    int result = 0;
    (void) ((result = _anjay_dm_call_instance_read_default_attrs(
                     anjay, obj, iid, _anjay_dm_current_ssid(anjay),
                     &instance_attributes, NULL))
            || (result = print_discovered_instance(stream, obj, iid,
                                                   &instance_attributes)));
    if (result) {
        return result;
    }
    return discover_instance_resources(anjay, stream, obj, iid, ANJAY_ID_IID);
}

int _anjay_discover(anjay_t *anjay,
                    avs_stream_t *stream,
                    const anjay_dm_object_def_t *const *obj,
                    anjay_iid_t iid,
                    anjay_rid_t rid) {
    assert(obj && *obj);

    if (iid == ANJAY_ID_INVALID) {
        return discover_object(anjay, stream, obj);
    }

    int result = _anjay_dm_verify_instance_present(anjay, obj, iid);
    if (result) {
        return result;
    }

    const anjay_action_info_t info = {
        .oid = (*obj)->oid,
        .iid = iid,
        .ssid = _anjay_dm_current_ssid(anjay),
        .action = ANJAY_ACTION_DISCOVER
    };
    if (!_anjay_instance_action_allowed(anjay, &info)) {
        return ANJAY_ERR_UNAUTHORIZED;
    }

    if (rid == ANJAY_ID_INVALID) {
        return discover_instance(anjay, stream, obj, iid);
    }

    anjay_dm_resource_kind_t kind;
    if ((result = _anjay_dm_verify_resource_present(anjay, obj, iid, rid,
                                                    &kind))) {
        return result;
    }

    return discover_resource(anjay, stream, obj, iid, rid, kind, ANJAY_ID_RID);
}

#    ifdef ANJAY_WITH_BOOTSTRAP
static int print_ssid_attr(avs_stream_t *stream, uint16_t ssid) {
    return avs_is_ok(avs_stream_write_f(stream, ";" ANJAY_ATTR_SSID "=%" PRIu16,
                                        ssid))
                   ? 0
                   : -1;
}

static int print_enabler_version(avs_stream_t *stream,
                                 anjay_lwm2m_version_t version) {
    // Bug in specification.
    // Technically it should be always with `</>;`, but we can't be sure 1.0
    // servers will accept it, because it's defined in 1.1.1 TS.
    const char *prefix = (version > ANJAY_LWM2M_VERSION_1_0) ? "</>;" : "";
    return avs_is_ok(
                   avs_stream_write_f(stream, "%slwm2m=\"%s\"", prefix,
                                      _anjay_lwm2m_version_as_string(version)))
                   ? 0
                   : -1;
}

static int
bootstrap_discover_object_instance(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_iid_t iid,
                                   void *stream_) {
    avs_stream_t *stream = (avs_stream_t *) stream_;
    int result = 0;
    (void) ((result = print_separator(stream))
            || (result = print_discovered_instance(
                        stream, obj, iid, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY)));
    if (result) {
        return result;
    }
    if ((*obj)->oid == ANJAY_DM_OID_SECURITY) {
        anjay_ssid_t ssid;
        int query_result = _anjay_ssid_from_security_iid(anjay, iid, &ssid);
        if (!query_result && ssid != ANJAY_SSID_BOOTSTRAP) {
            result = print_ssid_attr(stream, ssid);
        }
    } else if ((*obj)->oid == ANJAY_DM_OID_SERVER) {
        anjay_ssid_t ssid;
        int query_result = _anjay_ssid_from_server_iid(anjay, iid, &ssid);
        if (!query_result) {
            result = print_ssid_attr(stream, ssid);
        }
    }
    return result;
}

static int bootstrap_discover_object(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     void *stream_) {
    avs_stream_t *stream = (avs_stream_t *) stream_;
    int result;
    (void) ((result = print_separator(stream))
            || (result = print_discovered_object(
                        stream, obj, &ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY))
            || (result = _anjay_dm_foreach_instance(
                        anjay, obj, bootstrap_discover_object_instance,
                        stream)));
    return result;
}

int _anjay_bootstrap_discover(anjay_t *anjay,
                              avs_stream_t *stream,
                              anjay_oid_t oid) {
    const anjay_dm_object_def_t *const *obj = NULL;
    if (oid != ANJAY_ID_INVALID) {
        obj = _anjay_dm_find_object_by_oid(anjay, oid);
        if (!obj) {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    int result = print_enabler_version(stream, current_lwm2m_version(anjay));
    if (result) {
        return result;
    }
    if (obj) {
        return bootstrap_discover_object(anjay, obj, stream);
    } else {
        return _anjay_dm_foreach_object(anjay, bootstrap_discover_object,
                                        stream);
    }
}
#    endif

#endif // ANJAY_WITH_DISCOVER
