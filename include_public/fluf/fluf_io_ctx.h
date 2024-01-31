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

#include <fluf/fluf.h>
#include <fluf/fluf_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

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

#define _FLUF_IO_BOOT_DISC_RECORD_MAX_LENGTH \
    sizeof("</>;lwm2m=1.2,</0/65534>;ssid=65534;uri=\"")

#define _FLUF_IO_REGISTER_RECORD_MAX_LENGTH sizeof(",</65534>;ver=9.9")

#define _FLUF_IO_ATTRIBUTE_RECORD_MAX_LEN sizeof(";gt=-2.2250738585072014E-308")

#define _FLUF_IO_DISCOVER_RECORD_MAX_LEN \
    sizeof(",</65534/65534/65534>;dim=65534")

// TODO: expand at new encoders
#define _FLUF_IO_CTX_BUFFER_LENGTH _FLUF_IO_SENML_CBOR_SIMPLE_RECORD_MAX_LENGTH

AVS_STATIC_ASSERT(_FLUF_IO_CTX_BUFFER_LENGTH
                                  >= _FLUF_IO_CBOR_SIMPLE_RECORD_MAX_LENGTH
                          && _FLUF_IO_CTX_BUFFER_LENGTH
                                     >= _FLUF_IO_BOOT_DISC_RECORD_MAX_LENGTH
                          && _FLUF_IO_CTX_BUFFER_LENGTH
                                     >= _FLUF_IO_REGISTER_RECORD_MAX_LENGTH
                          && _FLUF_IO_CTX_BUFFER_LENGTH
                                     >= _FLUF_IO_ATTRIBUTE_RECORD_MAX_LEN
                          && _FLUF_IO_CTX_BUFFER_LENGTH
                                     >= _FLUF_IO_DISCOVER_RECORD_MAX_LEN,
                  internal_buff_badly_defined);

typedef struct {
    size_t remaining_bytes;
    size_t offset;
    size_t bytes_in_internal_buff;
    bool is_extended_type;
    uint8_t internal_buff[_FLUF_IO_CTX_BUFFER_LENGTH];
} fluf_io_buff_t;

typedef struct {
    bool entry_added;
} fluf_internal_cbor_encoder_t;

typedef struct {
    bool encode_time;
    double last_timestamp;
    size_t items_count;
    fluf_uri_path_t base_path;
    size_t base_path_len;
    bool first_entry_added;
} fluf_internal_senml_cbor_encoder_t;

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
