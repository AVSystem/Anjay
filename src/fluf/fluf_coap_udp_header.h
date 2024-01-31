/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_COAP_SRC_UDP_UDP_HEADER_H
#define FLUF_COAP_SRC_UDP_UDP_HEADER_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <avsystem/commons/avs_defs.h>
#include <avsystem/commons/avs_utils.h>

#include <fluf/fluf_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _FLUF_FIELD_GET(field, mask, shift) (((field) & (mask)) >> (shift))
#define _FLUF_FIELD_SET(field, mask, shift, value) \
    ((field) = (uint8_t) (((field) & ~(mask))      \
                          | (uint8_t) (((value) << (shift)) & (mask))))

/**
 * Magic value defined in RFC7252, used internally when constructing/parsing
 * CoAP packets.
 */
#define _FLUF_COAP_PAYLOAD_MARKER ((uint8_t) { 0xFF })

#define _FLUF_COAP_MESSAGE_ID_LEN 2

typedef struct fluf_coap_udp_header {

    uint8_t version_type_token_length;

    uint8_t code;

    uint8_t message_id[_FLUF_COAP_MESSAGE_ID_LEN];
} fluf_coap_udp_header_t;

AVS_STATIC_ASSERT(AVS_ALIGNOF(fluf_coap_udp_header_t) == 1,
                  fluf_coap_udp_header_t_must_always_be_properly_aligned);

/** @{
 * Sanity checks that ensure no padding is inserted anywhere inside
 * @ref fluf_coap_udp_header_t .
 */
AVS_STATIC_ASSERT(offsetof(fluf_coap_udp_header_t, version_type_token_length)
                          == 0,
                  vttl_field_is_at_start_of_fluf_coap_udp_header_t);
AVS_STATIC_ASSERT(offsetof(fluf_coap_udp_header_t, code) == 1,
                  no_padding_before_code_field_of_fluf_coap_udp_header_t);
AVS_STATIC_ASSERT(offsetof(fluf_coap_udp_header_t, message_id) == 2,
                  no_padding_before_message_id_field_of_fluf_coap_udp_header_t);
AVS_STATIC_ASSERT(sizeof(fluf_coap_udp_header_t) == 4,
                  no_padding_in_fluf_coap_udp_header_t);
/** @} */

#define _FLUF_COAP_UDP_HEADER_VERSION_MASK 0xC0
#define _FLUF_COAP_UDP_HEADER_VERSION_SHIFT 6

static inline uint8_t
_fluf_coap_udp_header_get_version(const fluf_coap_udp_header_t *hdr) {
    int val = _FLUF_FIELD_GET(hdr->version_type_token_length,
                              _FLUF_COAP_UDP_HEADER_VERSION_MASK,
                              _FLUF_COAP_UDP_HEADER_VERSION_SHIFT);
    assert(val >= 0 && val <= 3);
    return (uint8_t) val;
}

static inline void
_fluf_coap_udp_header_set_version(fluf_coap_udp_header_t *hdr,
                                  uint8_t version) {
    assert(version <= 3);
    _FLUF_FIELD_SET(hdr->version_type_token_length,
                    _FLUF_COAP_UDP_HEADER_VERSION_MASK,
                    _FLUF_COAP_UDP_HEADER_VERSION_SHIFT, version);
}

#define _FLUF_COAP_UDP_HEADER_TOKEN_LENGTH_MASK 0x0F
#define _FLUF_COAP_UDP_HEADER_TOKEN_LENGTH_SHIFT 0

static inline uint8_t
_fluf_coap_udp_header_get_token_length(const fluf_coap_udp_header_t *hdr) {
    int val = _FLUF_FIELD_GET(hdr->version_type_token_length,
                              _FLUF_COAP_UDP_HEADER_TOKEN_LENGTH_MASK,
                              _FLUF_COAP_UDP_HEADER_TOKEN_LENGTH_SHIFT);
    assert(val >= 0 && val <= _FLUF_COAP_UDP_HEADER_TOKEN_LENGTH_MASK);
    return (uint8_t) val;
}

static inline void
_fluf_coap_udp_header_set_token_length(fluf_coap_udp_header_t *hdr,
                                       uint8_t token_length) {
    assert(token_length <= FLUF_COAP_MAX_TOKEN_LENGTH);
    _FLUF_FIELD_SET(hdr->version_type_token_length,
                    _FLUF_COAP_UDP_HEADER_TOKEN_LENGTH_MASK,
                    _FLUF_COAP_UDP_HEADER_TOKEN_LENGTH_SHIFT, token_length);
}

#define _FLUF_COAP_UDP_HEADER_TYPE_MASK 0x30
#define _FLUF_COAP_UDP_HEADER_TYPE_SHIFT 4

#define _FLUF_COAP_UDP_TYPE_FIRST FLUF_COAP_UDP_TYPE_CONFIRMABLE
#define _FLUF_COAP_UDP_TYPE_LAST FLUF_COAP_UDP_TYPE_RESET

static inline fluf_coap_udp_type_t
_fluf_coap_udp_header_get_type(const fluf_coap_udp_header_t *hdr) {
    int val = _FLUF_FIELD_GET(hdr->version_type_token_length,
                              _FLUF_COAP_UDP_HEADER_TYPE_MASK,
                              _FLUF_COAP_UDP_HEADER_TYPE_SHIFT);
    assert(val >= _FLUF_COAP_UDP_TYPE_FIRST && val <= _FLUF_COAP_UDP_TYPE_LAST);
    return (fluf_coap_udp_type_t) val;
}

static inline void _fluf_coap_udp_header_set_type(fluf_coap_udp_header_t *hdr,
                                                  fluf_coap_udp_type_t type) {
    _FLUF_FIELD_SET(hdr->version_type_token_length,
                    _FLUF_COAP_UDP_HEADER_TYPE_MASK,
                    _FLUF_COAP_UDP_HEADER_TYPE_SHIFT, type);
}

static inline uint16_t extract_u16(const uint8_t *data) {
    uint16_t result;
    memcpy(&result, data, sizeof(uint16_t));
    return avs_convert_be16(result);
}

static inline uint16_t
_fluf_coap_udp_header_get_id(const fluf_coap_udp_header_t *hdr) {
    return extract_u16(hdr->message_id);
}

static inline void _fluf_coap_udp_header_set_id(fluf_coap_udp_header_t *hdr,
                                                uint16_t msg_id) {
    uint16_t msg_id_nbo = avs_convert_be16(msg_id);
    memcpy(hdr->message_id, &msg_id_nbo, sizeof(msg_id_nbo));
}

static inline void _fluf_coap_udp_header_set(fluf_coap_udp_header_t *hdr,
                                             fluf_coap_udp_type_t type,
                                             uint8_t token_length,
                                             uint8_t code,
                                             uint16_t id) {
    _fluf_coap_udp_header_set_version(hdr, 1);
    _fluf_coap_udp_header_set_type(hdr, type);
    _fluf_coap_udp_header_set_token_length(hdr, token_length);
    hdr->code = code;
    _fluf_coap_udp_header_set_id(hdr, id);
}

static inline fluf_coap_udp_header_t
_fluf_coap_udp_header_init(fluf_coap_udp_type_t type,
                           uint8_t token_length,
                           uint8_t code,
                           uint16_t id) {
    fluf_coap_udp_header_t hdr = { 0 };
    _fluf_coap_udp_header_set(&hdr, type, token_length, code, id);
    return hdr;
}

#ifdef __cplusplus
}
#endif

#endif // FLUF_COAP_SRC_UDP_UDP_HEADER_H
