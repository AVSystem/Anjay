/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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
print_integer_attr(avs_stream_t *stream, const char *name, int32_t t) {
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
                          const anjay_dm_oi_attributes_t *attrs) {
    int result = 0;
    (void) ((result = print_integer_attr(stream, ANJAY_ATTR_PMIN,
                                         attrs->min_period))
            || (result = print_integer_attr(stream, ANJAY_ATTR_PMAX,
                                            attrs->max_period))
            || (result = print_integer_attr(stream, ANJAY_ATTR_EPMIN,
                                            attrs->min_eval_period))
            || (result = print_integer_attr(stream, ANJAY_ATTR_EPMAX,
                                            attrs->max_eval_period))
            || (result = print_con_attr(stream, attrs->con)));
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
                         const anjay_dm_r_attributes_t *attrs) {
    int result;
    (void) ((result = print_oi_attrs(stream, &attrs->common))
            || (result = print_double_attr(stream, ANJAY_ATTR_GT,
                                           attrs->greater_than))
            || (result = print_double_attr(stream, ANJAY_ATTR_LT,
                                           attrs->less_than))
            || (result =
                        print_double_attr(stream, ANJAY_ATTR_ST, attrs->step)));
    return result;
}

static int print_discovered_object(avs_stream_t *stream,
                                   const anjay_dm_installed_object_t *obj,
                                   const anjay_dm_oi_attributes_t *attrs,
                                   anjay_lwm2m_version_t version) {
    if (avs_is_err(avs_stream_write_f(stream, "</%" PRIu16 ">",
                                      _anjay_dm_installed_object_oid(obj)))) {
        return -1;
    }
    (void) version;
    const char *format = ";ver=\"%s\"";
#    ifdef ANJAY_WITH_LWM2M11
    if (version > ANJAY_LWM2M_VERSION_1_0) {
        format = ";ver=%s";
    }
#    endif // ANJAY_WITH_LWM2M11
    if (_anjay_dm_installed_object_version(obj)
            && avs_is_err(avs_stream_write_f(stream, format,
                                             _anjay_dm_installed_object_version(
                                                     obj)))) {
        return -1;
    }
    return print_oi_attrs(stream, attrs);
}

static int print_discovered_instance(avs_stream_t *stream,
                                     const anjay_dm_installed_object_t *obj,
                                     anjay_iid_t iid,
                                     const anjay_dm_oi_attributes_t *attrs) {
    if (avs_is_err(avs_stream_write_f(stream, "</%" PRIu16 "/%" PRIu16 ">",
                                      _anjay_dm_installed_object_oid(obj),
                                      iid))) {
        return -1;
    }
    return print_oi_attrs(stream, attrs);
}

static int print_discovered_resource(avs_stream_t *stream,
                                     const anjay_dm_installed_object_t *obj,
                                     anjay_iid_t iid,
                                     anjay_rid_t rid,
                                     int32_t resource_dim,
                                     const anjay_dm_r_attributes_t *attrs) {
    if (avs_is_err(avs_stream_write_f(
                stream, "</%" PRIu16 "/%" PRIu16 "/%" PRIu16 ">",
                _anjay_dm_installed_object_oid(obj), iid, rid))
            || print_resource_dim(stream, resource_dim)
            || print_r_attrs(stream, attrs)) {
        return -1;
    }
    return 0;
}

static int print_separator(avs_stream_t *stream) {
    return avs_is_ok(avs_stream_write(stream, ",", 1)) ? 0 : -1;
}

static int read_resource_dim_clb(anjay_unlocked_t *anjay,
                                 const anjay_dm_installed_object_t *obj,
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

static int read_resource_dim(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t *obj,
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

static int read_attrs(anjay_unlocked_t *anjay,
                      const anjay_dm_installed_object_t *obj,
                      anjay_iid_t iid,
                      anjay_rid_t rid,
                      anjay_riid_t riid,
                      anjay_ssid_t ssid,
                      anjay_lwm2m_version_t lwm2m_version,
                      anjay_id_type_t root_path_type,
                      anjay_dm_r_attributes_t *out) {
    (void) riid;
    (void) lwm2m_version;
    *out = ANJAY_DM_R_ATTRIBUTES_EMPTY;
    if (iid == ANJAY_ID_INVALID) {
        return _anjay_dm_call_object_read_default_attrs(anjay, obj, ssid,
                                                        &out->common);
    }
    if (root_path_type == ANJAY_ID_OID) {
        // When Discover is issued on an Object,
        // attributes from lower levels are not reported in LwM2M <=1.1
        return 0;
    }
    if (root_path_type == ANJAY_ID_RIID
            || (root_path_type == ANJAY_ID_RID && riid == ANJAY_ID_INVALID)
            || (root_path_type == ANJAY_ID_IID && rid == ANJAY_ID_INVALID)) {
        // Read all attached attributes
        return _anjay_dm_effective_attrs(
                anjay,
                &(const anjay_dm_attrs_query_details_t) {
                    .obj = obj,
                    .iid = iid,
                    .rid = rid,
                    .riid = riid,
                    .ssid = ssid,
                    /**
                     * Spec says we care about inherited attributes only.
                     */
                    .with_server_level_attrs = false
                },
                out);
    }
#    ifdef ANJAY_WITH_LWM2M11
    if (riid != ANJAY_ID_INVALID) {
        return _anjay_dm_call_resource_instance_read_attrs(anjay, obj, iid, rid,
                                                           riid, ssid, out);
    }
#    endif // ANJAY_WITH_LWM2M11
    if (rid != ANJAY_ID_INVALID) {
        return _anjay_dm_call_resource_read_attrs(anjay, obj, iid, rid, ssid,
                                                  out);
    }
    return _anjay_dm_call_instance_read_default_attrs(anjay, obj, iid, ssid,
                                                      &out->common);
}

typedef struct {
    avs_stream_t *stream;
    anjay_ssid_t ssid;
    anjay_lwm2m_version_t lwm2m_version;
    anjay_id_type_t root_path_type;
    anjay_id_type_t leaf_path_type;
} discover_clb_args_t;

#    ifdef ANJAY_WITH_LWM2M11
static int
print_discovered_resource_instance(avs_stream_t *stream,
                                   const anjay_dm_installed_object_t *obj,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid,
                                   anjay_riid_t riid,
                                   const anjay_dm_r_attributes_t *attrs) {
    if (avs_is_err(avs_stream_write_f(
                stream, "</%" PRIu16 "/%" PRIu16 "/%" PRIu16 "/%" PRIu16 ">",
                _anjay_dm_installed_object_oid(obj), iid, rid, riid))
            || print_r_attrs(stream, attrs)) {
        return -1;
    }
    return 0;
}

static int
discover_resource_instance_clb(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t *obj,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_riid_t riid,
                               void *args_) {
    discover_clb_args_t *args = (discover_clb_args_t *) args_;

    anjay_dm_r_attributes_t attributes = ANJAY_DM_R_ATTRIBUTES_EMPTY;
    int result;
    (void) ((result = read_attrs(anjay, obj, iid, rid, riid, args->ssid,
                                 args->lwm2m_version, args->root_path_type,
                                 &attributes))
            || (result = print_separator(args->stream))
            || (result = print_discovered_resource_instance(
                        args->stream, obj, iid, rid, riid, &attributes)));
    return result;
}
#    endif // ANJAY_WITH_LWM2M11

static int discover_resource(anjay_unlocked_t *anjay,
                             avs_stream_t *stream,
                             const anjay_dm_installed_object_t *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_ssid_t ssid,
                             anjay_lwm2m_version_t lwm2m_version,
                             anjay_dm_resource_kind_t kind,
                             anjay_id_type_t root_path_type,
                             anjay_id_type_t leaf_path_type) {
    int32_t resource_dim = -1;
    int result = 0;

    if (_anjay_dm_res_kind_multiple(kind) && (root_path_type != ANJAY_ID_OID)
            && (result = read_resource_dim(anjay, obj, iid, rid,
                                           &resource_dim))) {
        return result;
    }

    anjay_dm_r_attributes_t attributes;
    result = read_attrs(anjay, obj, iid, rid, ANJAY_ID_INVALID, ssid,
                        lwm2m_version, root_path_type, &attributes);
    if (!result) {
        result = print_discovered_resource(stream, obj, iid, rid, resource_dim,
                                           &attributes);
    }
#    ifdef ANJAY_WITH_LWM2M11
    if (!result && leaf_path_type > ANJAY_ID_RID
            && lwm2m_version >= ANJAY_LWM2M_VERSION_1_1
            && _anjay_dm_res_kind_multiple(kind)) {
        result = _anjay_dm_foreach_resource_instance(
                anjay, obj, iid, rid, discover_resource_instance_clb,
                &(discover_clb_args_t) {
                    .stream = stream,
                    .ssid = ssid,
                    .lwm2m_version = lwm2m_version,
                    .root_path_type = root_path_type,
                    .leaf_path_type = leaf_path_type
                });
    }
#    else  // ANJAY_WITH_LWM2M11
    (void) leaf_path_type;
#    endif // ANJAY_WITH_LWM2M11
    return result;
}

static int
discover_instance_resource_clb(anjay_unlocked_t *anjay,
                               const anjay_dm_installed_object_t *obj,
                               anjay_iid_t iid,
                               anjay_rid_t rid,
                               anjay_dm_resource_kind_t kind,
                               anjay_dm_resource_presence_t presence,
                               void *args_) {
    discover_clb_args_t *args = (discover_clb_args_t *) args_;
    int result = 0;
    if (presence != ANJAY_DM_RES_ABSENT
            && !(result = print_separator(args->stream))) {
        result = discover_resource(anjay, args->stream, obj, iid, rid,
                                   args->ssid, args->lwm2m_version, kind,
                                   args->root_path_type, args->leaf_path_type);
    }
    return result;
}
static int discover_instance(anjay_unlocked_t *anjay,
                             avs_stream_t *stream,
                             const anjay_dm_installed_object_t *obj,
                             anjay_iid_t iid,
                             anjay_ssid_t ssid,
                             anjay_lwm2m_version_t lwm2m_version,
                             anjay_id_type_t root_path_type,
                             anjay_id_type_t leaf_path_type) {
    anjay_dm_r_attributes_t attributes;
    int result = 0;
    (void) ((result = read_attrs(anjay, obj, iid, ANJAY_ID_INVALID,
                                 ANJAY_ID_INVALID, ssid, lwm2m_version,
                                 root_path_type, &attributes))
            || (result = print_discovered_instance(stream, obj, iid,
                                                   &attributes.common)));
    if (!result && leaf_path_type > ANJAY_ID_IID) {
        result =
                _anjay_dm_foreach_resource(anjay, obj, iid,
                                           discover_instance_resource_clb,
                                           &(discover_clb_args_t) {
                                               .stream = stream,
                                               .ssid = ssid,
                                               .lwm2m_version = lwm2m_version,
                                               .root_path_type = root_path_type,
                                               .leaf_path_type = leaf_path_type
                                           });
    }
    return result;
}

static int discover_object_instance_clb(anjay_unlocked_t *anjay,
                                        const anjay_dm_installed_object_t *obj,
                                        anjay_iid_t iid,
                                        void *args_) {
    discover_clb_args_t *args = (discover_clb_args_t *) args_;
    int result = 0;
    (void) ((result = print_separator(args->stream))
            || (result = discover_instance(anjay, args->stream, obj, iid,
                                           args->ssid, args->lwm2m_version,
                                           args->root_path_type,
                                           args->leaf_path_type)));
    return result;
}

static int discover_object(anjay_unlocked_t *anjay,
                           avs_stream_t *stream,
                           const anjay_dm_installed_object_t *obj,
                           anjay_ssid_t ssid,
                           anjay_lwm2m_version_t lwm2m_version,
                           anjay_id_type_t root_path_type,
                           anjay_id_type_t leaf_path_type) {
    anjay_dm_r_attributes_t attributes = ANJAY_DM_R_ATTRIBUTES_EMPTY;
    int result = 0;
    (void) ((result = read_attrs(anjay, obj, ANJAY_ID_INVALID, ANJAY_ID_INVALID,
                                 ANJAY_ID_INVALID, ssid, lwm2m_version,
                                 root_path_type, &attributes))
            || (result = print_discovered_object(
                        stream, obj, &attributes.common, lwm2m_version)));
    if (!result && leaf_path_type > ANJAY_ID_OID) {
        result =
                _anjay_dm_foreach_instance(anjay, obj,
                                           discover_object_instance_clb,
                                           &(discover_clb_args_t) {
                                               .stream = stream,
                                               .ssid = ssid,
                                               .lwm2m_version = lwm2m_version,
                                               .root_path_type = root_path_type,
                                               .leaf_path_type = leaf_path_type
                                           });
    }
    return result;
}

int _anjay_discover(anjay_unlocked_t *anjay,
                    avs_stream_t *stream,
                    const anjay_dm_installed_object_t *obj,
                    anjay_iid_t iid,
                    anjay_rid_t rid,
                    uint8_t depth,
                    anjay_ssid_t ssid,
                    anjay_lwm2m_version_t lwm2m_version) {
    assert(obj);

    if (iid == ANJAY_ID_INVALID) {
        return discover_object(
                anjay, stream, obj, ssid, lwm2m_version, ANJAY_ID_OID,
                (anjay_id_type_t) AVS_MIN(ANJAY_ID_OID + depth, ANJAY_ID_RIID));
    }

    int result = _anjay_dm_verify_instance_present(anjay, obj, iid);
    if (result) {
        return result;
    }

    const anjay_action_info_t info = {
        .oid = _anjay_dm_installed_object_oid(obj),
        .iid = iid,
        .ssid = ssid,
        .action = ANJAY_ACTION_DISCOVER
    };
    if (!_anjay_instance_action_allowed(anjay, &info)) {
        return ANJAY_ERR_UNAUTHORIZED;
    }

    if (rid == ANJAY_ID_INVALID) {
        return discover_instance(
                anjay, stream, obj, iid, ssid, lwm2m_version, ANJAY_ID_IID,
                (anjay_id_type_t) AVS_MIN(ANJAY_ID_IID + depth, ANJAY_ID_RIID));
    }

    anjay_dm_resource_kind_t kind;
    if ((result = _anjay_dm_verify_resource_present(anjay, obj, iid, rid,
                                                    &kind))) {
        return result;
    }

    return discover_resource(anjay, stream, obj, iid, rid, ssid, lwm2m_version,
                             kind, ANJAY_ID_RID,
                             (anjay_id_type_t) AVS_MIN(ANJAY_ID_RID + depth,
                                                       ANJAY_ID_RIID));
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
    (void) version;
    const char *format = "lwm2m=\"%s\"";
#        ifdef ANJAY_WITH_LWM2M11
    // Bug in specification.
    // Technically it should be always with `</>;`, but we can't be sure 1.0
    // servers will accept it, because it's defined in 1.1.1 TS.
    if (version > ANJAY_LWM2M_VERSION_1_0) {
        format = "</>;lwm2m=%s";
    }
#        endif // ANJAY_WITH_LWM2M11
    return avs_is_ok(avs_stream_write_f(
                   stream, format, _anjay_lwm2m_version_as_string(version)))
                   ? 0
                   : -1;
}

#        ifdef ANJAY_WITH_LWM2M11
static int print_uri_attr(avs_stream_t *stream, const char *uri) {
    avs_error_t err = avs_stream_write_f(stream, ";uri=\"");
    // escape '"' and
    for (const char *ch = uri; avs_is_ok(err) && *ch; ++ch) {
        if (*ch == '\\' || *ch == '"') {
            err = avs_stream_write(stream, "\\", 1);
        }
        if (avs_is_ok(err)) {
            err = avs_stream_write(stream, ch, 1);
        }
    }
    if (avs_is_ok(err)) {
        err = avs_stream_write(stream, "\"", 1);
    }
    return avs_is_ok(err) ? 0 : -1;
}
#        endif // ANJAY_WITH_LWM2M11

typedef struct {
    avs_stream_t *stream;
    anjay_lwm2m_version_t lwm2m_version;
} bootstrap_discover_object_instance_args_t;

static int
bootstrap_discover_object_instance(anjay_unlocked_t *anjay,
                                   const anjay_dm_installed_object_t *obj,
                                   anjay_iid_t iid,
                                   void *args_) {
    bootstrap_discover_object_instance_args_t *args =
            (bootstrap_discover_object_instance_args_t *) args_;
    int result = 0;
    (void) ((result = print_separator(args->stream))
            || (result = print_discovered_instance(
                        args->stream, obj, iid,
                        &ANJAY_DM_OI_ATTRIBUTES_EMPTY)));
    if (result) {
        return result;
    }
    if (_anjay_dm_installed_object_oid(obj) == ANJAY_DM_OID_SECURITY) {
        anjay_ssid_t ssid;
        int query_result = _anjay_ssid_from_security_iid(anjay, iid, &ssid);
        if (!query_result && ssid != ANJAY_SSID_BOOTSTRAP) {
            result = print_ssid_attr(args->stream, ssid);
        }
#        ifdef ANJAY_WITH_LWM2M11
        if (!result && args->lwm2m_version > ANJAY_LWM2M_VERSION_1_0) {
            char buffer[ANJAY_MAX_URL_RAW_LENGTH];
            query_result =
                    _anjay_server_uri_from_security_iid(anjay, iid, buffer,
                                                        sizeof(buffer));
            if (!query_result) {
                result = print_uri_attr(args->stream, buffer);
            }
        }
#        endif // ANJAY_WITH_LWM2M11
    } else if (_anjay_dm_installed_object_oid(obj) == ANJAY_DM_OID_SERVER) {
        anjay_ssid_t ssid;
        int query_result = _anjay_ssid_from_server_iid(anjay, iid, &ssid);
        if (!query_result) {
            result = print_ssid_attr(args->stream, ssid);
        }
    }
    return result;
}

typedef struct {
    avs_stream_t *stream;
    anjay_lwm2m_version_t lwm2m_version;
} bootstrap_discover_object_args_t;

static int bootstrap_discover_object(anjay_unlocked_t *anjay,
                                     const anjay_dm_installed_object_t *obj,
                                     void *args_) {
    bootstrap_discover_object_args_t *args =
            (bootstrap_discover_object_args_t *) args_;
    bootstrap_discover_object_instance_args_t instance_args = {
        .stream = args->stream,
        .lwm2m_version = args->lwm2m_version
    };
    int result;
    (void) ((result = print_separator(instance_args.stream))
            || (result = print_discovered_object(instance_args.stream, obj,
                                                 &ANJAY_DM_OI_ATTRIBUTES_EMPTY,
                                                 instance_args.lwm2m_version))
            || (result = _anjay_dm_foreach_instance(
                        anjay, obj, bootstrap_discover_object_instance,
                        &instance_args)));
    return result;
}

int _anjay_bootstrap_discover(anjay_unlocked_t *anjay,
                              avs_stream_t *stream,
                              anjay_oid_t oid,
                              anjay_lwm2m_version_t lwm2m_version) {
    const anjay_dm_installed_object_t *obj = NULL;
    if (oid != ANJAY_ID_INVALID) {
        obj = _anjay_dm_find_object_by_oid(anjay, oid);
        if (!obj) {
            return ANJAY_ERR_NOT_FOUND;
        }
    }
    int result = print_enabler_version(stream, lwm2m_version);
    if (result) {
        return result;
    }
    bootstrap_discover_object_args_t args = {
        .stream = stream,
        .lwm2m_version = lwm2m_version
    };
    if (obj) {
        return bootstrap_discover_object(anjay, obj, &args);
    } else {
        return _anjay_dm_foreach_object(anjay, bootstrap_discover_object,
                                        &args);
    }
}
#    endif

#endif // ANJAY_WITH_DISCOVER
