/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */
#ifndef ANJAY_IO_CBOR_TYPES_H
#define ANJAY_IO_CBOR_TYPES_H

VISIBILITY_PRIVATE_HEADER_BEGIN

/* See "2.1.  Major Types" in RFC 7049 */
typedef enum {
    _CBOR_MAJOR_TYPE_BEGIN = 0,
    CBOR_MAJOR_TYPE_UINT = 0,
    CBOR_MAJOR_TYPE_NEGATIVE_INT = 1,
    CBOR_MAJOR_TYPE_BYTE_STRING = 2,
    CBOR_MAJOR_TYPE_TEXT_STRING = 3,
    CBOR_MAJOR_TYPE_ARRAY = 4,
    CBOR_MAJOR_TYPE_MAP = 5,
    CBOR_MAJOR_TYPE_TAG = 6,
    CBOR_MAJOR_TYPE_FLOAT_OR_SIMPLE_VALUE = 7,
    _CBOR_MAJOR_TYPE_END
} cbor_major_type_t;

typedef enum {
    /**
     * Section "2.  Specification of the CBOR Encoding":
     *
     * > When it [5 lower bits of major type] is 24 to 27, the additional
     * > bytes for a variable-length integer immediately follow; the values 24
     * > to 27 of the additional information specify that its length is a 1-,
     * > 2-, 4-, or 8-byte unsigned integer, respectively.
     *
     * > Additional informationvalue 31 is used for indefinite-length items,
     * > described in Section 2.2.  Additional information values 28 to 30 are
     * > reserved for future expansion.
     */
    CBOR_EXT_LENGTH_1BYTE = 24,
    CBOR_EXT_LENGTH_2BYTE = 25,
    CBOR_EXT_LENGTH_4BYTE = 26,
    CBOR_EXT_LENGTH_8BYTE = 27,
    CBOR_EXT_LENGTH_INDEFINITE = 31
} cbor_ext_length_t;

/**
 * This enum is for:
 *
 * > Major type 7:  floating-point numbers and simple data types that need
 * > no content, as well as the "break" stop code.  See Section 2.3.
 */
typedef enum {
    /* See "2.3.  Floating-Point Numbers and Values with No Content" */
    CBOR_VALUE_BOOL_FALSE = 20,
    CBOR_VALUE_BOOL_TRUE = 21,
    CBOR_VALUE_NULL = 22,
    CBOR_VALUE_UNDEFINED = 23,
    CBOR_VALUE_IN_NEXT_BYTE = CBOR_EXT_LENGTH_1BYTE,
    CBOR_VALUE_FLOAT_16 = CBOR_EXT_LENGTH_2BYTE,
    CBOR_VALUE_FLOAT_32 = CBOR_EXT_LENGTH_4BYTE,
    CBOR_VALUE_FLOAT_64 = CBOR_EXT_LENGTH_8BYTE
} cbor_primitive_value_t;

#define CBOR_INDEFINITE_STRUCTURE_BREAK 0xFF

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_IO_CBOR_TYPES_H */
