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

#ifndef ANJAY_IO_H
#define	ANJAY_IO_H

#include <avsystem/commons/stream.h>
#include <avsystem/commons/stream/stream_outbuf.h>

#include <anjay/anjay.h>

#include <anjay_modules/io.h>

#include "coap/stream.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    ANJAY_ID_IID,
    ANJAY_ID_RID,
    ANJAY_ID_RIID
} anjay_id_type_t;

anjay_input_ctx_constructor_t _anjay_input_dynamic_create;
anjay_input_ctx_constructor_t _anjay_input_opaque_create;
anjay_input_ctx_constructor_t _anjay_input_text_create;

#ifdef WITH_LEGACY_CONTENT_FORMAT_SUPPORT
uint16_t _anjay_translate_legacy_content_format(uint16_t format);
#else
#define _anjay_translate_legacy_content_format(fmt) (fmt)
#endif

int _anjay_handle_requested_format(uint16_t *out_ptr,
                                   uint16_t requested_format);

#define ANJAY_OUTCTXERR_FORMAT_MISMATCH        (-0xCE0)
#define ANJAY_OUTCTXERR_METHOD_NOT_IMPLEMENTED (-0xCE1)
/* returned from _anjay_output_ctx_destroy if no anjay_ret_* function was
 * called, making it impossible to determine actual resource format */
#define ANJAY_OUTCTXERR_ANJAY_RET_NOT_CALLED   (-0xCE2)

anjay_output_ctx_t *
_anjay_output_dynamic_create(avs_stream_abstract_t *stream,
                             int *errno_ptr,
                             anjay_msg_details_t *details_template);

anjay_output_ctx_t *
_anjay_output_opaque_create(avs_stream_abstract_t *stream,
                            int *errno_ptr,
                            anjay_msg_details_t *inout_details);

anjay_output_ctx_t *
_anjay_output_text_create(avs_stream_abstract_t *stream,
                          int *errno_ptr,
                          anjay_msg_details_t *inout_details);

anjay_output_ctx_t *
_anjay_output_raw_tlv_create(avs_stream_abstract_t *stream);

anjay_output_ctx_t *
_anjay_output_tlv_create(avs_stream_abstract_t *stream,
                         int *errno_ptr,
                         anjay_msg_details_t *inout_details);

int *_anjay_output_ctx_errno_ptr(anjay_output_ctx_t *ctx);
anjay_output_ctx_t * _anjay_output_object_start(anjay_output_ctx_t *ctx);
int _anjay_output_object_finish(anjay_output_ctx_t *ctx);
int _anjay_output_set_id(anjay_output_ctx_t *ctx,
                         anjay_id_type_t type, uint16_t id);
int _anjay_output_ctx_destroy(anjay_output_ctx_t **ctx_ptr);

avs_stream_abstract_t *_anjay_input_bytes_stream(anjay_input_ctx_t *ctx);
int _anjay_input_attach_child(anjay_input_ctx_t *ctx,
                              anjay_input_ctx_t *child);
anjay_input_ctx_t *_anjay_input_nested_ctx(anjay_input_ctx_t *ctx);
int _anjay_input_get_id(anjay_input_ctx_t *ctx,
                        anjay_id_type_t *out_type, uint16_t *out_id);
int _anjay_input_next_entry(anjay_input_ctx_t *ctx);

typedef struct anjay_output_buf_ctx {
    const void *vtable;
    const void *ret_bytes_vtable;
    avs_stream_outbuf_t *stream;
} anjay_output_buf_ctx_t;

anjay_output_buf_ctx_t _anjay_output_buf_ctx_init(avs_stream_outbuf_t *stream);

VISIBILITY_PRIVATE_HEADER_END

#endif	/* ANJAY_IO_H */
