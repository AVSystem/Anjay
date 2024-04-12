/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_IO_CTX_H
#define FLUF_IO_CTX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <avsystem/commons/avs_defs.h>
#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf.h>
#include <fluf/fluf_cbor_decoder_ll.h>
#include <fluf/fluf_config.h>
#include <fluf/fluf_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(FLUF_WITH_LWM2M_CBOR) && !defined(FLUF_WITH_LWM2M12)
#    error "FLUF_WITH_LWM2M_CBOR requires FLUF_WITH_LWM2M12 enabled"
#endif // defined(FLUF_WITH_LWM2M_CBOR) && !defined(FLUF_WITH_LWM2M12)

#if !defined(FLUF_WITH_SENML_CBOR) && !defined(FLUF_WITH_LWM2M_CBOR)
#    error "At least one of FLUF_WITH_SENML_CBOR or FLUF_WITH_LWM2M_CBOR must be enabled."
#endif // !defined(FLUF_WITH_SENML_CBOR) && !defined(FLUF_WITH_LWM2M_CBOR)

#define _FLUF_IO_MAX_PATH_STRING_SIZE sizeof("/65535/65535/65535/65535")

#define _FLUF_IO_CBOR_MAX_OBJLNK_STRING_SIZE sizeof("65535:65535")
// objlink is the largest possible simple variable + 1 byte for header
#define _FLUF_IO_CBOR_SIMPLE_RECORD_MAX_LENGTH \
    (_FLUF_IO_CBOR_MAX_OBJLNK_STRING_SIZE + 1)

// max 3 bytes for array UINT16_MAX elements
// 1 byte for map
// 14 bytes for basename 21 65 2F36353533352F3635353334 as /65534/65534
// 14 bytes for name 00 63 2F36353533352F3635353334 as /65534/65534
// 10 bytes for basetime 22 FB 1122334455667788
// 4 bytes for objlink header
// 1 bytes for string value header
// resource with objlink is biggest possible value that can be directly written
// into the internal_buff
#define _FLUF_IO_SENML_CBOR_SIMPLE_RECORD_MAX_LENGTH \
    (3 + 1 + 14 + 14 + 10 + 4 + 1 + _FLUF_IO_CBOR_MAX_OBJLNK_STRING_SIZE)

/**
 * Largest possible single LwM2M CBOR record, starts with closing maps of the
 * previous record and contains an objlink:
 *
 *     FF         map end
 *    FF          map end
 *   FF           map end
 *  19 FF FE      oid: 65534
 *  BF            map begin
 *   19 FF FE     iid: 65534
 *   BF           map begin
 *    19 FF FE    rid: 65534
 *    BF          map begin
 *     19 FF FE   riid: 65534
 *     6B 36 35 35 33 34 3A 36 35 35 33 34  objlink
 */
#define _FLUF_IO_LWM2M_CBOR_SIMPLE_RECORD_MAX_LENGTH (30)

#define _FLUF_IO_BOOT_DISC_RECORD_MAX_LENGTH \
    sizeof("</>;lwm2m=1.2,</0/65534>;ssid=65534;uri=\"")

#define _FLUF_IO_REGISTER_RECORD_MAX_LENGTH sizeof(",</65534>;ver=9.9")

#define _FLUF_IO_ATTRIBUTE_RECORD_MAX_LEN sizeof(";gt=-2.2250738585072014E-308")

#define _FLUF_IO_DISCOVER_RECORD_MAX_LEN \
    sizeof(",</65534/65534/65534>;dim=65534")

#define _FLUF_IO_PLAINTEXT_SIMPLE_RECORD_MAX_LENGTH FLUF_DOUBLE_STR_MAX_LEN

// TODO: expand at new encoders
#define _FLUF_IO_CTX_BUFFER_LENGTH _FLUF_IO_SENML_CBOR_SIMPLE_RECORD_MAX_LENGTH

// According to IEEE 754-1985, the longest notation for value represented by
// double type is 24 characters
#define _FLUF_IO_CTX_DOUBLE_BUFF_STR_SIZE 24

AVS_STATIC_ASSERT(
        _FLUF_IO_CTX_BUFFER_LENGTH >= _FLUF_IO_CBOR_SIMPLE_RECORD_MAX_LENGTH
                && _FLUF_IO_CTX_BUFFER_LENGTH
                           >= _FLUF_IO_LWM2M_CBOR_SIMPLE_RECORD_MAX_LENGTH
                && _FLUF_IO_CTX_BUFFER_LENGTH
                           >= _FLUF_IO_BOOT_DISC_RECORD_MAX_LENGTH
                && _FLUF_IO_CTX_BUFFER_LENGTH
                           >= _FLUF_IO_REGISTER_RECORD_MAX_LENGTH
                && _FLUF_IO_CTX_BUFFER_LENGTH
                           >= _FLUF_IO_ATTRIBUTE_RECORD_MAX_LEN
                && _FLUF_IO_CTX_BUFFER_LENGTH
                           >= _FLUF_IO_DISCOVER_RECORD_MAX_LEN
                && _FLUF_IO_CTX_BUFFER_LENGTH
                           >= _FLUF_IO_PLAINTEXT_SIMPLE_RECORD_MAX_LENGTH,
        internal_buff_badly_defined);

#define BASE64_ENCODED_MULTIPLIER 4
typedef struct {
    uint8_t buf[BASE64_ENCODED_MULTIPLIER];
    size_t cache_offset;
} fluf_internal_text_encoder_b64_cache_t;

typedef struct {
    size_t remaining_bytes;
    size_t offset;
    size_t bytes_in_internal_buff;
    bool is_extended_type;
    uint8_t internal_buff[_FLUF_IO_CTX_BUFFER_LENGTH];
    fluf_internal_text_encoder_b64_cache_t b64_cache;
} fluf_io_buff_t;

#ifdef FLUF_WITH_PLAINTEXT
typedef struct {
    bool entry_added;
} fluf_internal_text_encoder_t;
#endif // FLUF_WITH_PLAINTEXT

#ifdef FLUF_WITH_OPAQUE
typedef struct {
    bool entry_added;
} fluf_internal_opaque_encoder_t;
#endif // FLUF_WITH_OPAQUE

#ifdef FLUF_WITH_CBOR
typedef struct {
    bool entry_added;
} fluf_internal_cbor_encoder_t;
#endif // FLUF_WITH_CBOR

#ifdef FLUF_WITH_SENML_CBOR
typedef struct {
    bool encode_time;
    double last_timestamp;
    size_t items_count;
    fluf_uri_path_t base_path;
    size_t base_path_len;
    bool first_entry_added;
} fluf_internal_senml_cbor_encoder_t;
#endif // FLUF_WITH_SENML_CBOR

#ifdef FLUF_WITH_CBOR
typedef struct {
    fluf_cbor_ll_decoder_t ctx;
    fluf_cbor_ll_decoder_bytes_ctx_t *bytes_ctx;
    size_t bytes_consumed;
    char objlnk_buf[_FLUF_IO_CBOR_MAX_OBJLNK_STRING_SIZE];
    bool entry_parsed;
} fluf_internal_cbor_decoder_t;
#endif // FLUF_WITH_CBOR

#ifdef FLUF_WITH_LWM2M_CBOR
typedef struct {
    fluf_uri_path_t base_path;
    fluf_uri_path_t last_path;
    uint8_t maps_opened;
    size_t items_count;
} fluf_internal_lwm2m_cbor_encoder_t;
#endif // FLUF_WITH_LWM2M_CBOR

#ifdef FLUF_WITH_SENML_CBOR
typedef struct {
    bool map_entered : 1;
    bool has_name : 1;
    bool has_value : 1;
    bool has_basename : 1;
    bool path_processed : 1;
    bool label_ready : 1;

    char short_string_buf[_FLUF_IO_CBOR_MAX_OBJLNK_STRING_SIZE];
    int32_t label;

    ptrdiff_t pairs_remaining;

    fluf_cbor_ll_decoder_bytes_ctx_t *bytes_ctx;
    size_t bytes_consumed;
} fluf_internal_senml_entry_parse_state_t;

typedef struct {
    char path[_FLUF_IO_MAX_PATH_STRING_SIZE];
    fluf_data_type_t type;
    union {
        bool boolean;
        fluf_objlnk_value_t objlnk;
        fluf_cbor_ll_number_t number;
        fluf_bytes_or_string_value_t bytes;
    } value;
} fluf_internal_senml_cached_entry_t;

typedef struct {
    fluf_cbor_ll_decoder_t ctx;

    bool composite_read : 1;
    bool toplevel_array_entered : 1;

    ptrdiff_t entry_count;

    /* Currently processed entry - shared between entire context chain. */
    fluf_internal_senml_entry_parse_state_t entry_parse;
    fluf_internal_senml_cached_entry_t entry;
    /* Current basename set in the payload. */
    char basename[_FLUF_IO_MAX_PATH_STRING_SIZE];
    /* A path which must be a prefix of the currently processed `path`. */
    fluf_uri_path_t base;

} fluf_internal_senml_cbor_decoder_t;
#endif // FLUF_WITH_SENML_CBOR

#ifdef FLUF_WITH_LWM2M_CBOR
typedef struct {
    fluf_uri_path_t path;
    uint8_t relative_paths_lengths[FLUF_URI_PATH_MAX_LENGTH];
    uint8_t relative_paths_num;
} fluf_internal_lwm2m_cbor_path_stack_t;

typedef struct {
    fluf_cbor_ll_decoder_t ctx;

    bool toplevel_map_entered : 1;
    bool path_parsed : 1;
    bool in_path_array : 1;
    bool expects_map : 1;

    fluf_uri_path_t base;
    fluf_internal_lwm2m_cbor_path_stack_t path_stack;

    fluf_cbor_ll_decoder_bytes_ctx_t *bytes_ctx;
    size_t bytes_consumed;
    char objlnk_buf[_FLUF_IO_CBOR_MAX_OBJLNK_STRING_SIZE];
} fluf_internal_lwm2m_cbor_decoder_t;
#endif // FLUF_WITH_LWM2M_CBOR

typedef struct {
    fluf_id_type_t type;
    size_t length;
    size_t bytes_read;
} tlv_entry_t;

#define FLUF_TLV_MAX_DEPTH 3
typedef struct {
    bool want_payload;
    bool want_disambiguation;
    /** buffer provided by fluf_io_in_ctx_feed_payload */
    void *buff;
    size_t buff_size;
    size_t buff_offset;
    bool payload_finished;

    fluf_uri_path_t uri_path;

    // Currently processed path
    bool has_path;
    fluf_uri_path_t current_path;

    uint8_t type_field;
    size_t id_length_buff_bytes_need;
    uint8_t id_length_buff[5];
    size_t id_length_buff_read_offset;
    size_t id_length_buff_write_offset;

    tlv_entry_t *entries;
    tlv_entry_t entries_block[FLUF_TLV_MAX_DEPTH];
} fluf_internal_tlv_decoder_t;

#ifdef FLUF_WITH_PLAINTEXT
typedef struct {
    /** auxiliary buffer used for accumulate data for decoding */
    union {
        /** general purpose auxiliary buffer */
        struct {
            char buf[AVS_MAX(_FLUF_IO_CTX_DOUBLE_BUFF_STR_SIZE,
                             AVS_MAX(AVS_INT_STR_BUF_SIZE(int64_t),
                                     AVS_UINT_STR_BUF_SIZE(uint64_t)))];
            size_t size;
        } abuf;

        /** auxiliary buffer used for accumulate data for decoding used for
         *  base64 */
        struct {
            /** if input is not divisible by 4, residual is stored in this
             * buffer */
            char res_buf[3];
            size_t res_buf_size;

            /** the general idea is to use input buffer as output buffer
             * BUT it is needed to use 9 bytes long auxiliary buffer */
            char out_buf[9];
            size_t out_buf_size;
        } abuf_b64;
    } aux;

    /** buffer provided by fluf_io_in_ctx_feed_payload */
    void *buff;
    size_t buff_size;
    bool payload_finished : 1;

    bool want_payload : 1;
    bool return_eof_next_time : 1;
    bool eof_already_returned : 1;
    bool padding_detected : 1;
} fluf_internal_text_decoder_t;
#endif // FLUF_WITH_PLAINTEXT

#ifdef FLUF_WITH_OPAQUE
typedef struct {
    bool want_payload : 1;
    bool payload_finished : 1;
    bool eof_already_returned : 1;
} fluf_internal_opaque_decoder_t;
#endif // FLUF_WITH_OPAQUE

#ifndef FLUF_WITHOUT_REGISTER_CTX
/** Register payload context
 *  Do not modify this structure directly, its fields are changed during fluf_io
 * api calls.
 */
typedef struct {
    fluf_io_buff_t buff;
    fluf_uri_path_t last_path;
    bool first_record_added;
} fluf_io_register_ctx_t;
#endif // FLUF_WITHOUT_REGISTER_CTX

#ifndef FLUF_WITHOUT_BOOTSTRAP_DISCOVER_CTX
/** Bootstrap-Discovery payload context
 *  Do not modify this structure directly, its fields are changed during fluf_io
 * api calls.
 */
typedef struct {
    fluf_io_buff_t buff;
    fluf_uri_path_t last_path;
    fluf_uri_path_t base_path;
    bool first_record_added;
    const char *uri;
} fluf_io_bootstrap_discover_ctx_t;
#endif // FLUF_WITHOUT_BOOTSTRAP_DISCOVER_CTX

#ifndef FLUF_WITHOUT_DISCOVER_CTX
/** Discovery payload context
 *  Do not modify this structure directly, its fields are changed during fluf_io
 * api calls.
 */
typedef struct {
    fluf_io_buff_t buff;
    fluf_uri_path_t last_path;
    fluf_uri_path_t base_path;
    uint8_t depth;
    uint16_t dim_counter;
    bool first_record_added;
    fluf_attr_notification_t attr;
    size_t attr_record_len;
    size_t attr_record_offset;
} fluf_io_discover_ctx_t;
#endif // FLUF_WITHOUT_DISCOVER_CTX

#ifdef __cplusplus
}
#endif

#endif /* FLUF_IO_CTX_H */
