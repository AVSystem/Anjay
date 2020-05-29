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

#include <avs_coap_init.h>

#ifdef WITH_AVS_COAP_OBSERVE

#    include <avsystem/commons/avs_errno.h>
#    include <avsystem/commons/avs_persistence.h>

#    include <avsystem/coap/observe.h>

#    include "avs_coap_code_utils.h"

#    define MODULE_NAME coap
#    include <avs_coap_x_log_config.h>

#    include "avs_coap_ctx.h"
#    include "options/avs_coap_options.h"

VISIBILITY_SOURCE_BEGIN

static AVS_LIST(avs_coap_observe_t)
create_observe(avs_coap_observe_id_t id,
               const avs_coap_request_header_t *req,
               avs_coap_observe_cancel_handler_t *cancel_handler,
               void *handler_arg) {
    const size_t options_capacity =
            _avs_coap_options_request_key_size(&req->options);

    AVS_LIST(avs_coap_observe_t) observe =
            (AVS_LIST(avs_coap_observe_t)) AVS_LIST_NEW_BUFFER(
                    sizeof(avs_coap_observe_t) + options_capacity);
    if (!observe) {
        LOG(ERROR, _("out of memory"));
        return NULL;
    }

    *observe = (avs_coap_observe_t) {
        .id = id,
        .cancel_handler = cancel_handler,
        .cancel_handler_arg = handler_arg,
        .request_code = req->code,
        .request_key = _avs_coap_options_copy_request_key(
                &req->options, observe->options_storage, options_capacity)
    };

    // BLOCK1 option is not necessary; we won't receive any request payload
    // while sending Notify
    avs_coap_options_remove_by_number(&observe->request_key,
                                      AVS_COAP_OPTION_BLOCK1);

    return observe;
}

static AVS_LIST(avs_coap_observe_t) *
find_observe_ptr_by_id(avs_coap_ctx_t *ctx, const avs_coap_observe_id_t *id) {
    AVS_LIST(avs_coap_observe_t) *observe_ptr;
    AVS_LIST_FOREACH_PTR(observe_ptr, &_avs_coap_get_base(ctx)->observes) {
        if (avs_coap_token_equal(&(*observe_ptr)->id.token, &id->token)) {
            return observe_ptr;
        }
    }

    return NULL;
}

avs_error_t
avs_coap_observe_start(avs_coap_ctx_t *ctx,
                       avs_coap_observe_id_t id,
                       const avs_coap_request_header_t *req,
                       avs_coap_observe_cancel_handler_t *cancel_handler,
                       void *handler_arg) {
    if (!avs_coap_code_is_request(req->code)) {
        LOG(ERROR, "%s" _(" is not a valid request code"),
            AVS_COAP_CODE_STRING(req->code));
        return avs_errno(AVS_EINVAL);
    }

    AVS_LIST(avs_coap_observe_t) observe =
            create_observe(id, req, cancel_handler, handler_arg);
    if (!observe) {
        return avs_errno(AVS_ENOMEM);
    }

    avs_error_t err = ctx->vtable->accept_observation(ctx, observe);

    if (avs_is_err(err)) {
        avs_free(observe);
        return err;
    }

    // make sure to *replace* existing observation with same ID if one exists
    _avs_coap_observe_cancel(ctx, &id);

    LOG(DEBUG, _("Observe start: ") "%s", AVS_COAP_TOKEN_HEX(&id.token));

    AVS_LIST_INSERT(&_avs_coap_get_base(ctx)->observes, observe);
    return AVS_OK;
}

static avs_coap_observe_t *find_observe_by_id(avs_coap_ctx_t *ctx,
                                              const avs_coap_observe_id_t *id) {
    AVS_LIST(avs_coap_observe_t) *observe_ptr = find_observe_ptr_by_id(ctx, id);
    return observe_ptr ? *observe_ptr : NULL;
}

avs_error_t
_avs_coap_observe_setup_notify(avs_coap_ctx_t *ctx,
                               const avs_coap_observe_id_t *id,
                               avs_coap_observe_notify_t *out_notify) {
    avs_coap_observe_t *observe = find_observe_by_id(ctx, id);
    if (!observe) {
        LOG(DEBUG, _("observation ") "%s" _(" does not exist"),
            AVS_COAP_TOKEN_HEX(&id->token));
        return avs_errno(AVS_EINVAL);
    }

    *out_notify = (avs_coap_observe_notify_t) {
        .request_code = observe->request_code,
        .request_key = observe->request_key,
        .observe_option_value = ++observe->last_observe_option_value
    };
    return AVS_OK;
}

void _avs_coap_observe_cancel(avs_coap_ctx_t *ctx,
                              const avs_coap_observe_id_t *id) {
    AVS_LIST(avs_coap_observe_t) *observe_ptr = find_observe_ptr_by_id(ctx, id);
    if (!observe_ptr) {
        LOG(TRACE, _("observation ") "%s" _(" does not exist"),
            AVS_COAP_TOKEN_HEX(&id->token));
        return;
    }

    LOG(DEBUG, _("Observe cancel: ") "%s", AVS_COAP_TOKEN_HEX(&id->token));

    avs_coap_observe_t *observe = AVS_LIST_DETACH(observe_ptr);
    if (observe->cancel_handler) {
        observe->cancel_handler(*id, observe->cancel_handler_arg);
    }
    AVS_LIST_DELETE(&observe);
}

#    ifdef WITH_AVS_COAP_OBSERVE_PERSISTENCE

static const char OBSERVE_ENTRY_MAGIC[] = { 'O', 'B', 'S', '\0' };

static avs_error_t
persistence_common_fields(avs_persistence_context_t *persistence,
                          avs_coap_token_t *token,
                          uint32_t *last_observe_option_value,
                          uint8_t *request_code,
                          uint16_t *options_size) {
    avs_error_t err;
    (void) (avs_is_err((err = avs_persistence_magic(
                                persistence, OBSERVE_ENTRY_MAGIC,
                                sizeof(OBSERVE_ENTRY_MAGIC))))
            || avs_is_err((err = avs_persistence_u8(persistence, &token->size)))
            || avs_is_err((err = (avs_coap_token_valid(token)
                                          ? AVS_OK
                                          : avs_errno(AVS_EBADMSG))))
            || avs_is_err((err = avs_persistence_bytes(
                                   persistence, token->bytes, token->size)))
            || avs_is_err((err = avs_persistence_u32(
                                   persistence, last_observe_option_value)))
            || avs_is_err((err = avs_persistence_u8(persistence, request_code)))
            || avs_is_err(
                       (err = avs_persistence_u16(persistence, options_size))));
    return err;
}

avs_error_t avs_coap_observe_persist(avs_coap_ctx_t *ctx,
                                     avs_coap_observe_id_t id,
                                     avs_persistence_context_t *persistence) {
    if (avs_persistence_direction(persistence) != AVS_PERSISTENCE_STORE) {
        return avs_errno(AVS_EINVAL);
    }
    avs_coap_observe_t *observe = find_observe_by_id(ctx, &id);
    if (!observe) {
        LOG(ERROR,
            _("Cannot persist observation ") "%s" _(": it does not exist"),
            AVS_COAP_TOKEN_HEX(&id.token));
        return avs_errno(AVS_EINVAL);
    }
    uint16_t options_size = (uint16_t) observe->request_key.size;
    if (options_size != observe->request_key.size) {
        LOG(ERROR, _("Options longer than ") "%u" _(" are not supported"),
            (unsigned) UINT16_MAX);
        return _avs_coap_err(AVS_COAP_ERR_NOT_IMPLEMENTED);
    }

    avs_error_t err =
            persistence_common_fields(persistence, &id.token,
                                      &observe->last_observe_option_value,
                                      &observe->request_code, &options_size);
    if (avs_is_ok(err)) {
        err = avs_persistence_bytes(persistence, observe->request_key.begin,
                                    options_size);
    }
    return err;
}

avs_error_t
avs_coap_observe_restore(avs_coap_ctx_t *ctx,
                         avs_coap_observe_cancel_handler_t *cancel_handler,
                         void *handler_arg,
                         avs_persistence_context_t *persistence) {
    if (avs_persistence_direction(persistence) != AVS_PERSISTENCE_RESTORE) {
        return avs_errno(AVS_EINVAL);
    }
    avs_coap_base_t *coap_base = _avs_coap_get_base(ctx);
    if (coap_base->socket) {
        LOG(ERROR, _("CoAP context is already initialized, cannot restore "
                     "observation state"));
        return avs_errno(AVS_EINVAL);
    }

    avs_coap_observe_id_t id = { AVS_COAP_TOKEN_EMPTY };
    uint32_t last_observe_option_value;
    uint8_t request_code;
    uint16_t options_size = 0;
    AVS_LIST(avs_coap_observe_t) observe;
    avs_error_t err = persistence_common_fields(persistence, &id.token,
                                                &last_observe_option_value,
                                                &request_code, &options_size);
    if (avs_is_err(err)) {
        return err;
    }

    if (find_observe_by_id(ctx, &id)) {
        LOG(ERROR, _("Observe ") "%s" _(" already exists"),
            AVS_COAP_TOKEN_HEX(&id.token));
        // persistence data likely malformed
        return avs_errno(AVS_EBADMSG);
    }

    observe = (AVS_LIST(avs_coap_observe_t)) AVS_LIST_NEW_BUFFER(
            sizeof(avs_coap_observe_t) + options_size);
    if (!observe) {
        LOG(ERROR, _("Out of memory"));
        return avs_errno(AVS_ENOMEM);
    }
    *observe = (avs_coap_observe_t) {
        .id = id,
        .cancel_handler = cancel_handler,
        .cancel_handler_arg = handler_arg,
        .last_observe_option_value = last_observe_option_value,
        .request_code = request_code,
        .request_key = avs_coap_options_create_empty(observe->options_storage,
                                                     options_size)
    };
    observe->request_key.size = options_size;
    if (avs_is_err((err = avs_persistence_bytes(persistence,
                                                observe->options_storage,
                                                options_size)))) {
        AVS_LIST_DELETE(&observe);
        return err;
    }
    LOG(DEBUG, _("Observe (restored) start: ") "%s",
        AVS_COAP_TOKEN_HEX(&id.token));
    AVS_LIST_INSERT(&coap_base->observes, observe);

    return AVS_OK;
}

#    else // WITH_AVS_COAP_OBSERVE_PERSISTENCE

avs_error_t avs_coap_observe_persist(avs_coap_ctx_t *ctx,
                                     avs_coap_observe_id_t id,
                                     avs_persistence_context_t *persistence) {
    (void) ctx;
    (void) id;
    (void) persistence;

    LOG(WARNING, _("observe persistence not compiled in"));
    return _avs_coap_err(AVS_COAP_ERR_FEATURE_DISABLED);
}

avs_error_t
avs_coap_observe_restore(avs_coap_ctx_t *ctx,
                         avs_coap_observe_cancel_handler_t *cancel_handler,
                         void *handler_arg,
                         avs_persistence_context_t *persistence) {
    (void) ctx;
    (void) cancel_handler;
    (void) handler_arg;
    (void) persistence;

    LOG(WARNING, _("observe persistence not compiled in"));
    return _avs_coap_err(AVS_COAP_ERR_FEATURE_DISABLED);
}

#    endif // WITH_AVS_COAP_OBSERVE_PERSISTENCE

#endif // WITH_AVS_COAP_OBSERVE
