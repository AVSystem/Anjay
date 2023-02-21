/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
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

static anjay_unlocked_output_ctx_t *spawn_opaque(avs_stream_t *stream,
                                                 const anjay_uri_path_t *uri) {
    (void) uri;
    return _anjay_output_opaque_create(stream);
}

#ifndef ANJAY_WITHOUT_PLAINTEXT
static anjay_unlocked_output_ctx_t *spawn_text(avs_stream_t *stream,
                                               const anjay_uri_path_t *uri) {
    (void) uri;
    return _anjay_output_text_create(stream);
}
#endif // ANJAY_WITHOUT_PLAINTEXT

#ifndef ANJAY_WITHOUT_TLV
static anjay_unlocked_output_ctx_t *spawn_tlv(avs_stream_t *stream,
                                              const anjay_uri_path_t *uri) {
    return _anjay_output_tlv_create(stream, uri);
}
#endif // ANJAY_WITHOUT_TLV

#ifdef ANJAY_WITH_LWM2M_JSON
static anjay_unlocked_output_ctx_t *spawn_json(avs_stream_t *stream,
                                               const anjay_uri_path_t *uri) {
    return _anjay_output_senml_like_create(stream, uri,
                                           AVS_COAP_FORMAT_OMA_LWM2M_JSON);
}
#endif // ANJAY_WITH_LWM2M_JSON

#ifdef ANJAY_WITH_SENML_JSON
static anjay_unlocked_output_ctx_t *
spawn_senml_json(avs_stream_t *stream, const anjay_uri_path_t *uri) {
    return _anjay_output_senml_like_create(stream, uri,
                                           AVS_COAP_FORMAT_SENML_JSON);
}
#endif // ANJAY_WITH_SENML_JSON

#ifdef ANJAY_WITH_CBOR
static anjay_unlocked_output_ctx_t *
spawn_senml_cbor(avs_stream_t *stream, const anjay_uri_path_t *uri) {
    return _anjay_output_senml_like_create(stream, uri,
                                           AVS_COAP_FORMAT_SENML_CBOR);
}

static anjay_unlocked_output_ctx_t *spawn_cbor(avs_stream_t *stream,
                                               const anjay_uri_path_t *uri) {
    (void) uri;
    return _anjay_output_cbor_create(stream);
}

#endif // ANJAY_WITH_CBOR

typedef struct {
    uint16_t format;
    anjay_input_ctx_constructor_t *input_ctx_constructor;
    anjay_unlocked_output_ctx_t *(*output_ctx_spawn_func)(
            avs_stream_t *stream, const anjay_uri_path_t *uri);
} dynamic_format_def_t;

static const dynamic_format_def_t SUPPORTED_SIMPLE_FORMATS[] = {
    { AVS_COAP_FORMAT_OCTET_STREAM, _anjay_input_opaque_create, spawn_opaque },
#ifndef ANJAY_WITHOUT_PLAINTEXT
    { AVS_COAP_FORMAT_PLAINTEXT, _anjay_input_text_create, spawn_text },
#endif // ANJAY_WITHOUT_PLAINTEXT
#ifdef ANJAY_WITH_CBOR
    { AVS_COAP_FORMAT_CBOR, _anjay_input_cbor_create, spawn_cbor },
#endif // ANJAY_WITH_CBOR
    { AVS_COAP_FORMAT_NONE, NULL, NULL }
};

static const dynamic_format_def_t SUPPORTED_HIERARCHICAL_FORMATS[] = {
#ifndef ANJAY_WITHOUT_TLV
    { AVS_COAP_FORMAT_OMA_LWM2M_TLV, _anjay_input_tlv_create, spawn_tlv },
#endif // ANJAY_WITHOUT_TLV
#ifdef ANJAY_WITH_LWM2M_JSON
    { AVS_COAP_FORMAT_OMA_LWM2M_JSON, NULL, spawn_json },
#endif // ANJAY_WITH_LWM2M_JSON
#ifdef ANJAY_WITH_SENML_JSON
    { AVS_COAP_FORMAT_SENML_JSON, _anjay_input_json_create, spawn_senml_json },
#endif // ANJAY_WITH_SENML_JSON
#ifdef ANJAY_WITH_CBOR
    { AVS_COAP_FORMAT_SENML_CBOR, _anjay_input_senml_cbor_create,
      spawn_senml_cbor },
#endif // ANJAY_WITH_CBOR
    { AVS_COAP_FORMAT_NONE, NULL, NULL }
};

AVS_STATIC_ASSERT(AVS_ARRAY_SIZE(SUPPORTED_HIERARCHICAL_FORMATS) > 1,
                  at_least_one_hierarchical_format_must_be_enabled);

#ifdef ANJAY_WITH_LWM2M11
static const dynamic_format_def_t SUPPORTED_COMPOSITE_READ_FORMATS[] = {
#    ifdef ANJAY_WITH_CBOR
    { AVS_COAP_FORMAT_SENML_CBOR, _anjay_input_senml_cbor_composite_read_create,
      spawn_senml_cbor },
#    endif // ANJAY_WITH_CBOR
#    ifdef ANJAY_WITH_SENML_JSON
    { AVS_COAP_FORMAT_SENML_JSON, _anjay_input_json_composite_read_create,
      spawn_senml_json },
#    endif // ANJAY_WITH_SENML_JSON
    { AVS_COAP_FORMAT_NONE, NULL, NULL }
};

static const dynamic_format_def_t SUPPORTED_COMPOSITE_WRITE_FORMATS[] = {
#    ifdef ANJAY_WITH_CBOR
    { AVS_COAP_FORMAT_SENML_CBOR, _anjay_input_senml_cbor_create, NULL },
#    endif // ANJAY_WITH_CBOR
#    ifdef ANJAY_WITH_SENML_JSON
    { AVS_COAP_FORMAT_SENML_JSON, _anjay_input_json_create, NULL },
#    endif // ANJAY_WITH_SENML_JSON
    { AVS_COAP_FORMAT_NONE, NULL, NULL }
};
#endif // ANJAY_WITH_LWM2M11

#ifdef ANJAY_WITH_SEND
static const dynamic_format_def_t SUPPORTED_SEND_FORMATS[] = {
#    ifdef ANJAY_WITH_CBOR
    { AVS_COAP_FORMAT_SENML_CBOR, _anjay_input_senml_cbor_composite_read_create,
      spawn_senml_cbor },
#    endif // ANJAY_WITH_CBOR
#    ifdef ANJAY_WITH_SENML_JSON
    { AVS_COAP_FORMAT_SENML_JSON, _anjay_input_json_composite_read_create,
      spawn_senml_json },
#    endif // ANJAY_WITH_SENML_JSON
    { AVS_COAP_FORMAT_NONE, NULL, NULL }
};
#endif // ANJAY_WITH_SEND

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

static int spawn_output_ctx(anjay_unlocked_output_ctx_t **out_ctx,
                            avs_stream_t *stream,
                            const anjay_uri_path_t *uri,
                            uint16_t format,
                            const dynamic_format_def_t *def) {
    if (!def || !def->output_ctx_spawn_func) {
        anjay_log(DEBUG,
                  _("Could not find an appropriate output context for "
                    "format: ") "%" PRIu16,
                  format);
        return ANJAY_ERR_NOT_ACCEPTABLE;
    }

    if (!(*out_ctx = def->output_ctx_spawn_func(stream, uri))) {
        anjay_log(DEBUG, _("Failed to spawn output context"));
        return ANJAY_ERR_INTERNAL;
    }

    return 0;
}

uint16_t _anjay_default_hierarchical_format(anjay_lwm2m_version_t version) {
#ifdef ANJAY_WITH_LWM2M11
    switch (version) {
    case ANJAY_LWM2M_VERSION_1_1:
#    if defined(ANJAY_WITH_CBOR)
        return AVS_COAP_FORMAT_SENML_CBOR;
#    elif defined(ANJAY_WITH_SENML_JSON)
        return AVS_COAP_FORMAT_SENML_JSON;
#    endif
        /* fall-through */
    case ANJAY_LWM2M_VERSION_1_0:
        return AVS_COAP_FORMAT_OMA_LWM2M_TLV;
    }
    AVS_UNREACHABLE("The switch statement above is supposed to be exhaustive");
#else  // ANJAY_WITH_LWM2M11
    (void) version;
#endif // ANJAY_WITH_LWM2M11
    return AVS_COAP_FORMAT_OMA_LWM2M_TLV;
}

uint16_t _anjay_default_simple_format(anjay_unlocked_t *anjay,
                                      anjay_lwm2m_version_t version) {
    if (anjay->prefer_hierarchical_formats) {
        return _anjay_default_hierarchical_format(version);
    }

#ifdef ANJAY_WITHOUT_PLAINTEXT
#    ifdef ANJAY_WITH_CBOR
    if (version >= ANJAY_LWM2M_VERSION_1_1) {
        return AVS_COAP_FORMAT_CBOR;
    }
#    endif // ANJAY_WITH_CBOR
    return _anjay_default_hierarchical_format(version);
#else  // ANJAY_WITHOUT_PLAINTEXT
    return AVS_COAP_FORMAT_PLAINTEXT;
#endif // ANJAY_WITHOUT_PLAINTEXT
}

int _anjay_output_dynamic_construct(anjay_unlocked_output_ctx_t **out_ctx,
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
#ifdef ANJAY_WITH_LWM2M11
    case ANJAY_ACTION_READ_COMPOSITE:
        def = find_format(SUPPORTED_COMPOSITE_READ_FORMATS, format);
        break;
#endif // ANJAY_WITH_LWM2M11
    default:
        break;
    }
    return spawn_output_ctx(out_ctx, stream, uri, format, def);
}

/////////////////////////////////////////////////////////////////////// DECODING

int _anjay_input_dynamic_construct_raw(anjay_unlocked_input_ctx_t **out,
                                       avs_stream_t *stream,
                                       uint16_t format,
                                       anjay_request_action_t action,
                                       const anjay_uri_path_t *uri) {
    if (format == AVS_COAP_FORMAT_NONE) {
        format = AVS_COAP_FORMAT_PLAINTEXT;
    }
    anjay_input_ctx_constructor_t *constructor = NULL;
    const dynamic_format_def_t *def;
    switch (action) {
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
#ifdef ANJAY_WITH_LWM2M11
    case ANJAY_ACTION_WRITE_COMPOSITE:
        if ((def = find_format(SUPPORTED_COMPOSITE_WRITE_FORMATS, format))) {
            constructor = def->input_ctx_constructor;
        }
        break;
    case ANJAY_ACTION_READ_COMPOSITE:
        if ((def = find_format(SUPPORTED_COMPOSITE_READ_FORMATS, format))) {
            constructor = def->input_ctx_constructor;
        }
        break;
#endif // ANJAY_WITH_LWM2M11
    default:
        // Nothing to prepare - the action does not need an input context.
        return 0;
    }
    if (constructor) {
        return constructor(out, stream, uri);
    }
    return ANJAY_ERR_UNSUPPORTED_CONTENT_FORMAT;
}

int _anjay_input_dynamic_construct(anjay_unlocked_input_ctx_t **out,
                                   avs_stream_t *stream,
                                   const anjay_request_t *request) {
    return _anjay_input_dynamic_construct_raw(out, stream,
                                              request->content_format,
                                              request->action, &request->uri);
}

#ifdef ANJAY_WITH_SEND
int _anjay_output_dynamic_send_construct(anjay_unlocked_output_ctx_t **out_ctx,
                                         avs_stream_t *stream,
                                         const anjay_uri_path_t *uri,
                                         uint16_t format) {
    if (format == AVS_COAP_FORMAT_NONE) {
        return -1;
    }

    const dynamic_format_def_t *def =
            find_format(SUPPORTED_SEND_FORMATS, format);

    return spawn_output_ctx(out_ctx, stream, uri, format, def);
}
#endif // ANJAY_WITH_SEND

#ifdef ANJAY_TEST
#    include "tests/core/io/dynamic.c"
#endif
