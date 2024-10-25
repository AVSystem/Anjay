/*
 * Copyright 2017-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <assert.h>
#include <stdbool.h>

#include <anjay/core.h>
#include <anjay_init.h>

#include <anjay_modules/anjay_dm_utils.h>
#include <anjay_modules/anjay_utils_core.h>
#include <anjay_modules/dm/anjay_modules.h>

#include <avsystem/commons/avs_errno.h>
#include <avsystem/commons/avs_stream_membuf.h>

#include "../anjay_core.h"
#include "../anjay_utils_private.h"
#include "anjay_corelnk.h"

VISIBILITY_SOURCE_BEGIN

typedef struct {
    bool first;
    avs_stream_t *stream;
    anjay_lwm2m_version_t version;
} query_dm_args_t;

static int query_dm_instance(anjay_unlocked_t *anjay,
                             const anjay_dm_installed_object_t *obj,
                             anjay_iid_t iid,
                             void *args_) {
    (void) anjay;
    query_dm_args_t *args = (query_dm_args_t *) args_;
    avs_error_t err =
            avs_stream_write_f(args->stream, "%s</%u/%u>",
                               args->first ? "" : ",",
                               _anjay_dm_installed_object_oid(obj), iid);
    args->first = false;
    return avs_is_ok(err) ? 0 : -1;
}

static int query_dm_object(anjay_unlocked_t *anjay,
                           const anjay_dm_installed_object_t *obj,
                           void *args_) {
    anjay_oid_t oid = _anjay_dm_installed_object_oid(obj);
    if (oid == ANJAY_DM_OID_SECURITY) {
        /* LwM2M TS 1.1, 6.2.1. Register says that "The Security Object ID:0,
         * and OSCORE Object ID:21, if present, MUST NOT be part of the
         * Registration Objects and Object Instances list." */
        return 0;
    }

    query_dm_args_t *args = (query_dm_args_t *) args_;
    if (args->first) {
        args->first = false;
    } else if (avs_is_err(avs_stream_write(args->stream, ",", 1))) {
        return -1;
    }
    bool obj_written = false;
    const char *version = _anjay_dm_installed_object_version(obj);
    if (version) {
        const char *format = "</%u>;ver=\"%s\"";
#ifdef ANJAY_WITH_LWM2M11
        if (args->version > ANJAY_LWM2M_VERSION_1_0) {
            format = "</%u>;ver=%s";
        }
#endif // ANJAY_WITH_LWM2M11

        if (avs_is_err(
                    avs_stream_write_f(args->stream, format, oid, version))) {
            return -1;
        }
        obj_written = true;
    }
    query_dm_args_t instance_args = {
        .first = !obj_written,
        .stream = args->stream,
        .version = args->version
    };
    int result = _anjay_dm_foreach_instance(anjay, obj, query_dm_instance,
                                            &instance_args);
    if (result) {
        return result;
    }
    if (!instance_args.first) {
        obj_written = true;
    }
    if (!obj_written
            && avs_is_err(avs_stream_write_f(args->stream, "</%u>", oid))) {
        return -1;
    }
    return 0;
}

int _anjay_corelnk_query_dm(anjay_unlocked_t *anjay,
                            anjay_dm_t *dm,
                            anjay_lwm2m_version_t version,
                            char **buffer) {
    assert(buffer);
    assert(!*buffer);
    avs_stream_t *stream = avs_stream_membuf_create();
    if (!stream) {
        _anjay_log_oom();
        return -1;
    }
    int retval;
    if ((retval = _anjay_dm_foreach_object(anjay, dm, query_dm_object,
                                           &(query_dm_args_t) {
                                               .first = true,
                                               .stream = stream,
                                               .version = version
                                           }))
            || (retval =
                        (avs_is_ok(avs_stream_write(stream, "\0", 1)) ? 0 : -1))
            || (retval = (avs_is_ok(avs_stream_membuf_take_ownership(
                                  stream, (void **) buffer, NULL))
                                  ? 0
                                  : -1))) {
        anjay_log(ERROR, _("could not enumerate objects"));
    }
    avs_stream_cleanup(&stream);
    return retval;
}

#ifdef ANJAY_TEST
#    include "tests/core/io/corelnk.c"
#endif
