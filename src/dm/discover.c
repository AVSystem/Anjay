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

#include <config.h>

#include <inttypes.h>

#include <anjay_modules/time.h>

#include "discover.h"
#include "query.h"

#include "../dm.h"
#include "../anjay.h"

VISIBILITY_SOURCE_BEGIN

static int
print_time_attr(avs_stream_abstract_t *stream, const char *name, time_t t) {
    if (t < 0) {
        return 0;
    }
    return avs_stream_write_f(stream, ";%s=%lu", name, (unsigned long) t);
}

static int print_double_attr(avs_stream_abstract_t *stream,
                             const char *name,
                             double value) {
    if (isnan(value)) {
        return 0;
    }
    return avs_stream_write_f(stream, ";%s=%.17g", name, value);
}

static int print_attrs(avs_stream_abstract_t *stream,
                       const anjay_dm_attributes_t *attrs) {
    int result = 0;
    (void) ((result = print_time_attr(stream, ANJAY_ATTR_PMIN,
                                      attrs->min_period))
            || (result = print_time_attr(stream, ANJAY_ATTR_PMAX,
                                         attrs->max_period))
            || (result = print_double_attr(stream, ANJAY_ATTR_GT,
                                           attrs->greater_than))
            || (result = print_double_attr(stream, ANJAY_ATTR_LT,
                                           attrs->less_than))
            || (result = print_double_attr(stream, ANJAY_ATTR_ST,
                                           attrs->step)));
    return result;
}

static int print_resource_attrs(avs_stream_abstract_t *stream,
                                int32_t resource_dim,
                                const anjay_dm_attributes_t *attrs) {
    int result = 0;
    if (resource_dim >= 0) {
        result = avs_stream_write_f(stream, ";dim=%" PRIu32,
                                    (uint32_t) resource_dim);
    }
    if (result) {
        return result;
    }
    return print_attrs(stream, attrs);
}

static int print_discovered_object(avs_stream_abstract_t *stream,
                                   const anjay_dm_object_def_t *const *obj,
                                   const anjay_dm_attributes_t *attrs) {
    int retval = avs_stream_write_f(stream, "</%" PRIu16 ">", (*obj)->oid);
    if (retval) {
        return retval;
    }
    return print_attrs(stream, attrs);
}

static int print_discovered_instance(avs_stream_abstract_t *stream,
                                     const anjay_dm_object_def_t *const *obj,
                                     anjay_iid_t iid,
                                     const anjay_dm_attributes_t *attrs) {
    int retval = avs_stream_write_f(stream, "</%" PRIu16 "/%" PRIu16 ">",
                                    (*obj)->oid, iid);
    if (retval) {
        return retval;
    }
    return print_attrs(stream, attrs);
}

static int print_discovered_resource(avs_stream_abstract_t *stream,
                                     const anjay_dm_object_def_t *const *obj,
                                     anjay_iid_t iid,
                                     anjay_rid_t rid,
                                     int32_t resource_dim,
                                     const anjay_dm_attributes_t *attrs) {
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
                                        anjay_ssid_t ssid,
                                        anjay_dm_attributes_t *out) {
    *out = ANJAY_DM_ATTRIBS_EMPTY;
    return _anjay_dm_object_read_default_attrs(anjay, obj, ssid, out);
}

static int
read_instance_level_attributes(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj,
                               anjay_iid_t iid,
                               anjay_ssid_t ssid,
                               anjay_dm_attributes_t *out) {
    *out = ANJAY_DM_ATTRIBS_EMPTY;
    return _anjay_dm_read_combined_instance_attrs(anjay, obj, iid, ssid, out);
}

static int read_resource_dim(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             int32_t *out_dim) {
    int result = _anjay_dm_resource_dim(anjay, obj, iid, rid);
    if (result == ANJAY_DM_DIM_INVALID
            || result == ANJAY_ERR_METHOD_NOT_ALLOWED
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
                             anjay_ssid_t ssid,
                             avs_stream_abstract_t *stream,
                             discover_resource_hint_t hint) {
    int32_t resource_dim = -1;
    int result = 0;

    if (hint >= WITH_RESOURCE_ATTRIBS) {
        result = read_resource_dim(anjay, obj, iid, rid, &resource_dim);
    }

    anjay_dm_attributes_t resource_attributes = ANJAY_DM_ATTRIBS_EMPTY;
    if (hint == WITH_RESOURCE_ATTRIBS) {
        result = _anjay_dm_read_combined_resource_attrs(
                anjay, obj, iid, rid, ssid, &resource_attributes);
    } else if (hint == WITH_INHERITED_ATTRIBS) {
        anjay_dm_attrs_query_details_t details =
                (anjay_dm_attrs_query_details_t) {
                    .obj = obj,
                    .iid = iid,
                    .rid = rid,
                    .ssid = ssid,
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
    return print_discovered_resource(stream, obj, iid, rid, resource_dim,
                                     &resource_attributes);
}

static int discover_instance_resources(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj,
                                       anjay_iid_t iid,
                                       anjay_ssid_t ssid,
                                       avs_stream_abstract_t *stream,
                                       discover_resource_hint_t hint) {
    int result = 0;
    for (anjay_rid_t rid = 0; !result && rid < (*obj)->rid_bound; ++rid) {
        result = _anjay_dm_resource_supported_and_present(anjay, obj, iid, rid);
        if (result <= 0) {
            continue;
        }
        result = print_separator(stream);
        if (!result) {
            result =
                    discover_resource(anjay, obj, iid, rid, ssid, stream, hint);
        }
    }
    return result;
}

typedef struct {
    avs_stream_abstract_t *stream;
    anjay_ssid_t ssid;
} discover_object_instance_ctx_t;

static int discover_object_instance(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj,
                                    anjay_iid_t iid,
                                    void *data) {
    discover_object_instance_ctx_t *ctx =
            (discover_object_instance_ctx_t *) data;
    int result = 0;
    (void) ((result = print_separator(ctx->stream))
            || (result = print_discovered_instance(ctx->stream, obj, iid,
                                                   &ANJAY_DM_ATTRIBS_EMPTY))
            || (result = discover_instance_resources(anjay, obj, iid, ctx->ssid,
                                                     ctx->stream, NO_ATTRIBS)));
    return result;
}

int _anjay_discover_object(anjay_t *anjay,
                           const anjay_dm_object_def_t *const *obj,
                           anjay_ssid_t ssid,
                           avs_stream_abstract_t *stream) {
    anjay_dm_attributes_t object_attributes;
    int result = 0;
    (void) ((result = read_object_level_attributes(anjay, obj, ssid,
                                                   &object_attributes))
            || (result = print_discovered_object(stream, obj,
                                                 &object_attributes)));
    if (result) {
        return result;
    }
    discover_object_instance_ctx_t ctx = {
        .stream = stream,
        .ssid = ssid
    };
    return _anjay_dm_foreach_instance(anjay, obj, discover_object_instance,
                                      &ctx);
}

int _anjay_discover_instance(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_ssid_t ssid,
                             avs_stream_abstract_t *stream) {
    anjay_dm_attributes_t instance_attributes;
    int result = 0;
    (void) ((result = read_instance_level_attributes(anjay, obj, iid, ssid,
                                                     &instance_attributes))
            || (result = print_discovered_instance(stream, obj, iid,
                                                   &instance_attributes)));
    if (result) {
        return result;
    }
    return discover_instance_resources(anjay, obj, iid, ssid, stream,
                                       WITH_RESOURCE_ATTRIBS);
}

int _anjay_discover_resource(anjay_t *anjay,
                             const anjay_dm_object_def_t *const *obj,
                             anjay_iid_t iid,
                             anjay_rid_t rid,
                             anjay_ssid_t ssid,
                             avs_stream_abstract_t *stream) {
    return discover_resource(anjay, obj, iid, rid, ssid, stream,
                             WITH_INHERITED_ATTRIBS);
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
                                   void *data) {
    avs_stream_abstract_t *stream = (avs_stream_abstract_t *) data;
    int result = 0;
    (void) ((result = print_separator(stream))
            || (result = print_discovered_instance(stream, obj, iid,
                                                   &ANJAY_DM_ATTRIBS_EMPTY)));
    if (result) {
        return result;
    }
    if ((*obj)->oid == ANJAY_DM_OID_SECURITY) {
        anjay_ssid_t ssid;
        int query_result = _anjay_ssid_from_security_iid(anjay, iid, &ssid);
        if (!query_result && ssid != ANJAY_SSID_BOOTSTRAP) {
            result = print_ssid_attr(stream, ssid);
        }
    }
    if ((*obj)->oid == ANJAY_DM_OID_SERVER) {
        anjay_ssid_t ssid;
        int query_result = _anjay_ssid_from_server_iid(anjay, iid, &ssid);
        if (!query_result) {
            result = print_ssid_attr(stream, ssid);
        }
    }
    return result;
}

int _anjay_bootstrap_discover_object(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     avs_stream_abstract_t *stream) {
    int result = print_discovered_object(stream, obj, &ANJAY_DM_ATTRIBS_EMPTY);
    if (result) {
        return result;
    }
    return _anjay_dm_foreach_instance(
            anjay, obj, bootstrap_discover_object_instance, stream);
}

typedef struct {
    avs_stream_abstract_t *stream;
    bool first_object;
} bootstrap_discover_ctx_t;

static int bootstrap_discover_object(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj,
                                     void *data) {
    bootstrap_discover_ctx_t *ctx = (bootstrap_discover_ctx_t *) data;
    int result = 0;
    if (ctx->first_object) {
        ctx->first_object = false;
        (void) ((result = print_enabler_version(ctx->stream))
                || (result = print_separator(ctx->stream)));
    } else {
        result = print_separator(ctx->stream);
    }
    if (!result) {
        result = _anjay_bootstrap_discover_object(anjay, obj, ctx->stream);
    }
    return result;
}

int _anjay_bootstrap_discover(anjay_t *anjay, avs_stream_abstract_t *stream) {
    bootstrap_discover_ctx_t ctx = {
        .stream = stream,
        .first_object = true
    };
    return _anjay_dm_foreach_object(anjay, bootstrap_discover_object,
                                    &ctx);
}
#endif
