/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_IO_CBOR_ENCODER_LL_H
#define FLUF_IO_CBOR_ENCODER_LL_H

#include <stdbool.h>
#include <stdint.h>

#include <fluf/fluf_config.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * This is a stateless low-level CBOR encoder.
 *
 * The correctness of cbor message construction is not checked here.
 *
 * Important notice!
 * All functions return the number of bytes written to the buffer. The maximum
 * number of bytes that can be written to the buffer during a single function
 * call is 9. Therefore, this is the minimum size of the input buffer for which
 * an overflow will never occur. To keep the code simple here, the buffer size
 * is not checked. Ensuring the security of the operation is on the side of the
 * user.
 *
 * With string and bytes, only the header with size and type is written, the
 * data stream must be written outside of this encoder after that.
 */

#define FLUF_CBOR_LL_SINGLE_CALL_MAX_LEN 9

size_t fluf_cbor_ll_encode_uint(void *buff, uint64_t value);

size_t fluf_cbor_ll_encode_int(void *buff, int64_t value);

size_t fluf_cbor_ll_encode_bool(void *buff, bool value);

size_t fluf_cbor_ll_encode_float(void *buff, float value);

size_t fluf_cbor_ll_encode_double(void *buff, double value);

size_t fluf_cbor_ll_encode_tag(void *buff, uint64_t value);

size_t fluf_cbor_ll_string_begin(void *buff, size_t size);

size_t fluf_cbor_ll_bytes_begin(void *buff, size_t size);

size_t fluf_cbor_ll_definite_map_begin(void *buff, size_t items_count);

size_t fluf_cbor_ll_definite_array_begin(void *buff, size_t items_count);

size_t fluf_cbor_ll_indefinite_map_begin(void *buff);

size_t fluf_cbor_ll_indefinite_map_end(void *buff);

#ifdef __cplusplus
}
#endif

#endif // FLUF_IO_CBOR_ENCODER_LL_H
