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

#include <anjay_config.h>

#include <inttypes.h>

#include <anjay_modules/time_defs.h>

#include "discover.h"
#include "query.h"

#include "../anjay_core.h"
#include "../dm_core.h"

VISIBILITY_SOURCE_BEGIN

static int
print_period_attr(avs_stream_abstract_t *stream, const char *name, int32_t t) {
    if (t < 0) {
        return 0;
    }
    return avs_stream_write_f(stream, ";%s=%lu", name, (unsigned long) t);
}

#ifdef WITH_CON_ATTR
static int print_con_attr(avs_stream_abstract_t *stream,
                          anjay_dm_con_attr_t value) {
    if (value < 0) {
        return 0;
    }
    return avs_stream_write_f(stream, ";" ANJAY_CUSTOM_ATTR_CON "=%d",
                              (int) value);
}
#else // WITH_CON_ATTR
#    define print_con_attr(...) 0
#endif // WITH_CON_ATTR

static int print_double_attr(avs_stream_abstract_t *stream,
                             const char *name,
                             double value) {
    if (isnan(value)) {
        return 0;
    }
    return avs_stream_write_f(stream, ";%s=%.17g", name, value);
}

static int print_attrs(avs_stream_abstract_t *stream,
                       const anjay_dm_internal_attrs_t *attrs) {
    int result = 0;
    (void) ((result = print_period_attr(stream, ANJAY_ATTR_PMIN,
                                        attrs->standard.min_period))
            || (result = print_period_attr(stream, ANJAY_ATTR_PMAX,
                                           attrs->standard.max_period))
            || (result = print_con_attr(stream, attrs->custom.data.con)));
    return result;
}

static int print_resource_attrs(avs_stream_abstract_t *stream,
                                int32_t resource_dim,
                                const anjay_dm_internal_res_attrs_t *attrs) {
    int result = 0;
    if (resource_dim >= 0) {
        result = avs_stream_write_f(stream, ";dim=%" PRIu32,
                                    (uint32_t) resource_dim);
    }
    if (!result) {
        (void) ((result = print_attrs(stream,
                                      _anjay_dm_get_internal_attrs_const(
                                              &attrs->standard.common)))
                || (result = print_double_attr(stream, ANJAY_ATTR_GT,
                                               attrs->standard.greater_than))
                || (result = print_double_attr(stream, ANJAY_ATTR_LT,
                                               attrs->standard.less_than))
                || (result = print_double_attr(stream, ANJAY_ATTR_ST,
                                               attrs->standard.step)));
    }
    return result;
}

static int print_discovered_object(avs_stream_abstract_t *stream,
                                   const anjay_dm_object_def_t *const *obj,
                                   const anjay_dm_internal_attrs_t *attrs) {
    int retval = avs_stream_write_f(stream, "</%" PRIu16 ">", (*obj)->oid);
    if (retval) {
        return retval;
    }
    if ((*obj)->version) {
        if ((retval = avs_stream_write_f(stream, ";ver=\"%s\"",
                                         (*obj)->version))) {
            return retval;
        }
    }
    return print_attrs(stream, attrs);
}

static int print_discovered_instance(avs_stream_abstract_t *stream,
                                     const anjay_dm_object_def_t *const *obj,
                                     anjay_iid_t iid,
                                     const anjay_dm_internal_attrs_t *attrs) {
    int retval = avs_stream_write_f(stream, "</%" PRIu16 "/%" PRIu16 ">",
                                    (*obj)->oid, iid);
    if (retval) {
        return retval;
    }
    return print_attrs(stream, attrs);
}

static int
print_discovered_resource(avs_stream_abstract_t *stream,
                          const anjay_dm_object_def_t *const *obj,
                          anjay_iid_t iid,
                          anjay_rid_t rid,
                          int32_t resource_dim,
                          const anjay_dm_internal_res_attrs_t *attrs) {
    int retval =
            avs_stream_write_f(stream, "</%" PRIu16 "/%" PRIu16 "/%" PRIu16 ">",
                               (*obj)->oid, iid, rid);
    if (retval) {
        return retval;
    }
    return print_resource_attrs(stream, resource_dim, attrs);
}

static int print_separator(avs_stream_abstract_t *stream) {
    return avs_stream_write(stream, ",", 1);
}

static int read_object_level_attributes(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj,
                                        anjay_dm_internal_attrs_t *out) {
    *out = ANJAY_DM_INTERNAL_ATTRS_EMPTY;
    return _anjay_dm_object_read_default_attrs(
            anjay, obj, _anjay_dm_current_ssid(anjay), out, NULL);
}

static int
read_instance_level_attributes(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_iid_t iid,
                               anjay_dm_internal_attrs_t *out) {
    *out = ANJAY_DM_INTERNAL_ATTRS_EMPTY;
    return _anjay_dm_read_combined_instance_attrs(
            anjay, obj, iid, _anjay_dm_current_ssid(anjay), out);
}

static int read_resource_dim(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             int32_t *out_dim) {
    int result = _anjay_dm_resource_dim(anjay, obj, iid, rid, NULL);
    if (result == ANJAY_DM_DIM_INVALID || result == ANJAY_ERR_METHOD_NOT_ALLOWED
            || result == ANJAY_ERR_NOT_IMPLEMENTED) {
        *out_dim = -1;
    } else if (result < 0) {
        return result;
    } else {
        *out_dim = result;
    }
    return 0;
}

typedef enum {
    /* no attributes */
    NO_ATTRIBS = 0,
    /* dim attribute and all attributes assigned directly to a resource */
    WITH_RESOURCE_ATTRIBS = 1,
    /* dim attribute and all attributes (including inherited ones) */
    WITH_INHERITED_ATTRIBS = 2
} discover_resource_hint_t;

static int discover_resource(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             discover_resource_hint_t hint) {
    int32_t resource_dim = -1;
    int result = 0;

    if (hint >= WITH_RESOURCE_ATTRIBS) {
        result = read_resource_dim(anjay, obj, iid, rid, &resource_dim);
    }

    anjay_dm_internal_res_attrs_t resource_attributes =
            ANJAY_DM_INTERNAL_RES_ATTRS_EMPTY;
    if (hint == WITH_RESOURCE_ATTRIBS) {
        result = _anjay_dm_resource_read_attrs(anjay, obj, iid, rid,
                                               _anjay_dm_current_ssid(anjay),
                                               &resource_attributes, NULL);
    } else if (hint == WITH_INHERITED_ATTRIBS) {
        anjay_dm_attrs_query_details_t details =
                (anjay_dm_attrs_query_details_t) {
                    .obj = obj,
                    .iid = iid,
                    .rid = rid,
                    .ssid = _anjay_dm_current_ssid(anjay),
                    /**
                     * Spec says we care about inherited attributes from Object
                     * and Instance levels only.
                     */
                    .with_server_level_attrs = false,
                };
        result = _anjay_dm_effective_attrs(anjay, &details,
                                           &resource_attributes);
    }
    if (result) {
        return result;
    }
    return print_discovered_resource(anjay->comm_stream, obj, iid, rid,
                                     resource_dim, &resource_attributes);
}

static int discover_instance_resources(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj,
                                       anjay_iid_t iid,
                                       discover_resource_hint_t hint) {
    int result = 0;
    for (size_t i = 0; i < (*obj)->supported_rids.count; ++i) {
        result = _anjay_dm_resource_present(
                anjay, obj, iid, (*obj)->supported_rids.rids[i], NULL);
        if (result <= 0) {
            continue;
        }
        result = print_separator(anjay->comm_stream);
        if (!result) {
            result = discover_resource(anjay, obj, iid,
                                       (*obj)->supported_rids.rids[i], hint);
        }
    }
    return result;
}

static int discover_object_instance(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj,
                                    anjay_iid_t iid,
                                    void *dummy) {
    (void) dummy;
    int result = 0;
    (void) ((result = print_separator(anjay->comm_stream))
            || (result = print_discovered_instance(
                        anjay->comm_stream, obj, iid,
                        &ANJAY_DM_INTERNAL_ATTRS_EMPTY))
            || (result = discover_instance_resources(anjay, obj, iid,
                                                     NO_ATTRIBS)));
    return result;
}

int _anjay_discover_object(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj) {
    anjay_dm_internal_attrs_t object_attributes;
    int result = 0;
    (void) ((result = read_object_level_attributes(anjay, obj,
                                                   &object_attributes))
            || (result = print_discovered_object(anjay->comm_stream, obj,
                                                 &object_attributes)));
    if (result) {
        return result;
    }
    return _anjay_dm_foreach_instance(anjay, obj, discover_object_instance,
                                      NULL);
}

int _anjay_discover_instance(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid) {
    anjay_dm_internal_attrs_t instance_attributes;
    int result = 0;
    (void) ((result = read_instance_level_attributes(anjay, obj, iid,
                                                     &instance_attributes))
            || (result = print_discovered_instance(anjay->comm_stream, obj, iid,
                                                   &instance_attributes)));
    if (result) {
        return result;
    }
    return discover_instance_resources(anjay, obj, iid, WITH_RESOURCE_ATTRIBS);
}

int _anjay_discover_resource(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid) {
    return discover_resource(anjay, obj, iid, rid, WITH_INHERITED_ATTRIBS);
}

#ifdef WITH_BOOTSTRAP
static int print_ssid_attr(avs_stream_abstract_t *stream, uint16_t ssid) {
    return avs_stream_write_f(stream, ";" ANJAY_ATTR_SSID "=%" PRIu16, ssid);
}

static int print_enabler_version(avs_stream_abstract_t *stream) {
    return avs_stream_write_f(stream, "lwm2m=\"%s\"",
                              ANJAY_SUPPORTED_ENABLER_VERSION);
}

static int
bootstrap_discover_object_instance(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj,
                                   anjay_iid_t iid,
                                   void *dummy) {
    (void) dummy;
    int result = 0;
    (void) ((result = print_separator(anjay->comm_stream))
            || (result = print_discovered_instance(
                        anjay->comm_stream, obj, iid,
                        &ANJAY_DM_INTERNAL_ATTRS_EMPTY)));
    if (result) {
        return result;
    }
    if ((*obj)->oid == ANJAY_DM_OID_SECURITY) {
        anjay_ssid_t ssid;
        int query_result = _anjay_ssid_from_security_iid(anjay, iid, &ssid);
        if (!query_result && ssid != ANJAY_SSID_BOOTSTRAP) {
            result = print_ssid_attr(anjay->comm_stream, ssid);
        }
    }
    if ((*obj)->oid == ANJAY_DM_OID_SERVER) {
        anjay_ssid_t ssid;
        int query_result = _anjay_ssid_from_server_iid(anjay, iid, &ssid);
        if (!query_result) {
            result = print_ssid_attr(anjay->comm_stream, ssid);
        }
    }
    return result;
}

int _anjay_bootstrap_discover_object(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj) {
    int result = print_discovered_object(anjay->comm_stream, obj,
                                         &ANJAY_DM_INTERNAL_ATTRS_EMPTY);
    if (result) {
        return result;
    }
    return _anjay_dm_foreach_instance(anjay, obj,
                                      bootstrap_discover_object_instance, NULL);
}

static int bootstrap_discover_object(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     void *first_object_) {
    bool *first_object = (bool *) first_object_;
    int result = 0;
    if (*first_object) {
        *first_object = false;
        (void) ((result = print_enabler_version(anjay->comm_stream))
                || (result = print_separator(anjay->comm_stream)));
    } else {
        result = print_separator(anjay->comm_stream);
    }
    if (!result) {
        result = _anjay_bootstrap_discover_object(anjay, obj);
    }
    return result;
}

int _anjay_bootstrap_discover(anjay_t *anjay) {
    bool first_object = true;
    return _anjay_dm_foreach_object(anjay, bootstrap_discover_object,
                                    &first_object);
}
#endif
