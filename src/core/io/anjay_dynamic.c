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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <avsystem/commons/avs_stream.h>

#include "../anjay_core.h"
#include "../anjay_dm_core.h"
#include "../anjay_io_core.h"
#include "../coap/anjay_content_format.h"

#include "anjay_vtable.h"

VISIBILITY_SOURCE_BEGIN

/////////////////////////////////////////////////////////////////////// ENCODING

static anjay_output_ctx_t *spawn_opaque(avs_stream_t *stream,
                                        const anjay_uri_path_t *uri) {
    (void) uri;
    return _anjay_output_opaque_create(stream);
}

#ifndef ANJAY_WITHOUT_PLAINTEXT
static anjay_output_ctx_t *spawn_text(avs_stream_t *stream,
                                      const anjay_uri_path_t *uri) {
    (void) uri;
    return _anjay_output_text_create(stream);
}
#endif // ANJAY_WITHOUT_PLAINTEXT

#ifndef ANJAY_WITHOUT_TLV
static anjay_output_ctx_t *spawn_tlv(avs_stream_t *stream,
                                     const anjay_uri_path_t *uri) {
    return _anjay_output_tlv_create(stream, uri);
}
#endif // ANJAY_WITHOUT_TLV

#ifdef ANJAY_WITH_LWM2M_JSON
static anjay_output_ctx_t *spawn_json(avs_stream_t *stream,
                                      const anjay_uri_path_t *uri) {
    return _anjay_output_senml_like_create(stream, uri,
                                           AVS_COAP_FORMAT_OMA_LWM2M_JSON);
}
#endif // ANJAY_WITH_LWM2M_JSON

typedef struct {
    uint16_t format;
    anjay_input_ctx_constructor_t *input_ctx_constructor;
    anjay_output_ctx_t *(*output_ctx_spawn_func)(avs_stream_t *stream,
                                                 const anjay_uri_path_t *uri);
} dynamic_format_def_t;

static const dynamic_format_def_t SUPPORTED_SIMPLE_FORMATS[] = {
    { AVS_COAP_FORMAT_OCTET_STREAM, _anjay_input_opaque_create, spawn_opaque },
#ifndef ANJAY_WITHOUT_PLAINTEXT
    { AVS_COAP_FORMAT_PLAINTEXT, _anjay_input_text_create, spawn_text },
#endif // ANJAY_WITHOUT_PLAINTEXT
    { AVS_COAP_FORMAT_NONE, NULL, NULL }
};

static const dynamic_format_def_t SUPPORTED_HIERARCHICAL_FORMATS[] = {
#ifndef ANJAY_WITHOUT_TLV
    { AVS_COAP_FORMAT_OMA_LWM2M_TLV, _anjay_input_tlv_create, spawn_tlv },
#endif // ANJAY_WITHOUT_TLV
#ifdef ANJAY_WITH_LWM2M_JSON
    { AVS_COAP_FORMAT_OMA_LWM2M_JSON, NULL, spawn_json },
#endif // ANJAY_WITH_LWM2M_JSON
    { AVS_COAP_FORMAT_NONE, NULL, NULL }
};

AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(SUPPORTED_HIERARCHICAL_FORMATS) > 1,
                  at_least_one_hierarchical_format_must_be_enabled);

static const dynamic_format_def_t *
find_format(const dynamic_format_def_t *supported_formats, uint16_t format) {
    for (const dynamic_format_def_t *candidate = supported_formats;
         candidate->format != AVS_COAP_FORMAT_NONE;
         ++candidate) {
        if (_anjay_translate_legacy_content_format(format)
                == candidate->format) {
            return candidate;
        }
    }
    return NULL;
}

uint16_t _anjay_default_hierarchical_format(anjay_lwm2m_version_t version) {
    (void) version;
    return AVS_COAP_FORMAT_OMA_LWM2M_TLV;
}

uint16_t _anjay_default_simple_format(anjay_t *anjay,
                                      anjay_lwm2m_version_t version) {
    if (anjay->prefer_hierarchical_formats) {
        return _anjay_default_hierarchical_format(version);
    }

    return AVS_COAP_FORMAT_PLAINTEXT;
}

int _anjay_output_dynamic_construct(anjay_output_ctx_t **out_ctx,
                                    avs_stream_t *stream,
                                    const anjay_uri_path_t *uri,
                                    uint16_t format,
                                    anjay_request_action_t action) {
    if (format == AVS_COAP_FORMAT_NONE) {
        return -1;
    }

    const dynamic_format_def_t *def = NULL;
    switch (action) {
    case ANJAY_ACTION_READ:
        (void) ((def = find_format(SUPPORTED_SIMPLE_FORMATS, format))
                || (def = find_format(SUPPORTED_HIERARCHICAL_FORMATS, format)));
        break;
    default:
        break;
    }

    if (!def || !def->output_ctx_spawn_func) {
        anjay_log(DEBUG,
                  _("Could not find an appropriate output context for "
                    "format: ") "%" PRIu16,
                  format);
        return ANJAY_ERR_NOT_ACCEPTABLE;
    }

    if (!(*out_ctx = def->output_ctx_spawn_func(stream, uri))) {
        return ANJAY_ERR_INTERNAL;
    }
    return 0;
}

/////////////////////////////////////////////////////////////////////// DECODING

int _anjay_input_dynamic_construct(anjay_input_ctx_t **out,
                                   avs_stream_t *stream,
                                   const anjay_request_t *request) {
    uint16_t format = request->content_format;
    if (request->content_format == AVS_COAP_FORMAT_NONE) {
        format = AVS_COAP_FORMAT_PLAINTEXT;
    }
    anjay_input_ctx_constructor_t *constructor = NULL;
    const dynamic_format_def_t *def;
    switch (request->action) {
    case ANJAY_ACTION_WRITE:
    case ANJAY_ACTION_WRITE_UPDATE:
    case ANJAY_ACTION_CREATE: {
        if ((def = find_format(SUPPORTED_SIMPLE_FORMATS, format))
                || (def = find_format(SUPPORTED_HIERARCHICAL_FORMATS,
                                      format))) {
            constructor = def->input_ctx_constructor;
        }
        break;
    }
    case ANJAY_ACTION_EXECUTE:
        *out = NULL;
        return 0;
    default:
        // Nothing to prepare - the action does not need an input context.
        return 0;
    }
    if (constructor) {
        return constructor(out, &stream, &request->uri);
    }
    return ANJAY_ERR_UNSUPPORTED_CONTENT_FORMAT;
}

#ifdef ANJAY_TEST
#    include "tests/core/io/dynamic.c"
#endif
