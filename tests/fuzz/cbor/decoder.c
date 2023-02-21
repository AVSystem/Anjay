/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <anjay_init.h>

#include <avsystem/commons/avs_defs.h>
#include <avsystem/commons/avs_stream_file.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "src/core/io/cbor/anjay_json_like_cbor_decoder.h"

static int decode_value(anjay_json_like_decoder_t *decoder);

static int decode_number(anjay_json_like_decoder_t *decoder,
                         anjay_json_like_value_type_t type) {
    anjay_json_like_number_t number;
    memset(&number, 0, sizeof(number));
    if (_anjay_json_like_decoder_number(decoder, &number)) {
        return -1;
    }
    if (number.type != type) {
        abort();
    }
    return 0;
}

static int decode_string(anjay_json_like_decoder_t *decoder) {
    anjay_io_cbor_bytes_ctx_t bytes = { 0 };
    if (_anjay_io_cbor_get_bytes_ctx(decoder, &bytes)) {
        return -1;
    }
    size_t remaining = bytes.bytes_available;

    uint8_t buffer[1024];
    bool finished = false;
    while (!finished) {
        size_t expected_bytes_count = AVS_MIN(sizeof(buffer), remaining);
        size_t bytes_read;
        if (_anjay_io_cbor_get_some_bytes(decoder, &bytes, buffer,
                                          sizeof(buffer), &bytes_read,
                                          &finished)) {
            return -1;
        }
        assert(bytes_read == expected_bytes_count);
        remaining -= bytes_read;
        assert(finished == !remaining);
    }
    return 0;
}

static int decode_map(anjay_json_like_decoder_t *decoder) {
    size_t outer_level = _anjay_json_like_decoder_nesting_level(decoder);
    if (_anjay_json_like_decoder_enter_map(decoder)) {
        return -1;
    }
    while (_anjay_json_like_decoder_nesting_level(decoder) > outer_level) {
        // decode key and value
        if (decode_value(decoder) || decode_value(decoder)) {
            return -1;
        }
    }
    return 0;
}

static int decode_array(anjay_json_like_decoder_t *decoder) {
    size_t outer_level = _anjay_json_like_decoder_nesting_level(decoder);
    if (_anjay_json_like_decoder_enter_array(decoder)) {
        return -1;
    }
    while (_anjay_json_like_decoder_nesting_level(decoder) > outer_level) {
        if (decode_value(decoder)) {
            return -1;
        }
    }
    return 0;
}

static int decode_value(anjay_json_like_decoder_t *decoder) {
    anjay_json_like_value_type_t type;
    if (_anjay_json_like_decoder_current_value_type(decoder, &type)) {
        return -1;
    }
    switch (type) {
    case ANJAY_JSON_LIKE_VALUE_BOOL: {
        bool value;
        (void) value;
        return _anjay_json_like_decoder_bool(decoder, &value);
    }
    case ANJAY_JSON_LIKE_VALUE_DOUBLE:
    case ANJAY_JSON_LIKE_VALUE_FLOAT:
    case ANJAY_JSON_LIKE_VALUE_NEGATIVE_INT:
    case ANJAY_JSON_LIKE_VALUE_UINT:
        return decode_number(decoder, type);
    case ANJAY_JSON_LIKE_VALUE_BYTE_STRING:
    case ANJAY_JSON_LIKE_VALUE_TEXT_STRING:
        return decode_string(decoder);
    case ANJAY_JSON_LIKE_VALUE_MAP:
        return decode_map(decoder);
    case ANJAY_JSON_LIKE_VALUE_ARRAY:
        return decode_array(decoder);
    }
    return 0;
}

static int decode_all(anjay_json_like_decoder_t *decoder) {
    int result = 0;
    while (!result) {
        result = decode_value(decoder);
    }
    return result;
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    avs_stream_t *fp =
            avs_stream_file_create("/dev/stdin", AVS_STREAM_FILE_READ);
    if (!fp) {
        return -1;
    }
    anjay_json_like_decoder_t *decoder = _anjay_cbor_decoder_new(fp);
    if (!decoder) {
        avs_stream_cleanup(&fp);
        return -1;
    }
    int result = decode_all(decoder);
    _anjay_json_like_decoder_delete(&decoder);
    avs_stream_cleanup(&fp);
    return result;
}
