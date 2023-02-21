/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#include <anjay_init.h>

#ifdef ANJAY_WITH_MODULE_FACTORY_PROVISIONING

#    ifndef ANJAY_WITH_CBOR
#        error "CBOR content format must be enabled to use factory provisioning"
#    endif // ANJAY_WITH_CBOR

#    include <avsystem/commons/avs_stream_membuf.h>
#    include <avsystem/commons/avs_utils.h>

#    include <anjay_modules/anjay_bootstrap.h>
#    include <anjay_modules/anjay_dm_utils.h>
#    include <anjay_modules/anjay_io_utils.h>
#    include <anjay_modules/anjay_utils_core.h>

#    include <anjay/factory_provisioning.h>

#    include "core/dm/anjay_dm_write.h"
#    include <inttypes.h>

VISIBILITY_SOURCE_BEGIN

#    define provisioning_log(...) \
        _anjay_log(anjay_factory_provision, __VA_ARGS__)

static avs_error_t factory_provisioning_unlocked(anjay_unlocked_t *anjay,
                                                 avs_stream_t *data_stream) {
    if (_anjay_bootstrap_in_progress(anjay)) {
        provisioning_log(ERROR,
                         _("Transaction with LwM2M Bootstrap Server in "
                           "progress, refusing to perform local bootstrap"));
        return avs_errno(AVS_EAGAIN);
    }

    avs_error_t err = _anjay_bootstrap_delete_everything(anjay);
    if (avs_is_ok(err)) {
        anjay_unlocked_input_ctx_t *input_ctx;

        if (_anjay_input_senml_cbor_create(
                    &input_ctx, data_stream, &MAKE_ROOT_PATH())) {
            provisioning_log(ERROR, _("Cannot create CBOR context"));
            return avs_errno(AVS_ENOMEM);
        } else {
            if (_anjay_bootstrap_write_composite(anjay, input_ctx)) {
                err = avs_errno(AVS_EPROTO);
            }
            (void) _anjay_input_ctx_destroy(&input_ctx);
        }
    }

    if (avs_is_err(err)) {
        provisioning_log(
                ERROR, _("Error occured during writing bootstrap information"));
        return err;
    } else if (_anjay_bootstrap_finish(anjay)) {
        provisioning_log(ERROR, _("Could not apply bootstrap information"));
        return avs_errno(AVS_EBADMSG);
    }

    provisioning_log(INFO, _("Finished factory provisioning"));
    return AVS_OK;
}

avs_error_t anjay_factory_provision(anjay_t *anjay_locked,
                                    avs_stream_t *data_stream) {
    avs_error_t err = avs_errno(AVS_EINVAL);
    ANJAY_MUTEX_LOCK(anjay, anjay_locked);
    err = factory_provisioning_unlocked(anjay, data_stream);
    ANJAY_MUTEX_UNLOCK(anjay_locked);
    return err;
}
#endif // ANJAY_WITH_MODULE_FACTORY_PROVISIONING
