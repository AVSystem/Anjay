/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_DEFS_H
#define FLUF_DEFS_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <fluf/fluf_config.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * CoAP Content-Formats, as defined in "Constrained RESTful Environments (CoRE)
 * Parameters":
 * https://www.iana.org/assignments/core-parameters/core-parameters.xhtml
 * @{
 */
#define FLUF_COAP_FORMAT_NOT_DEFINED 0xFFFF

#define FLUF_COAP_FORMAT_PLAINTEXT 0
#define FLUF_COAP_FORMAT_LINK_FORMAT 40
#define FLUF_COAP_FORMAT_OPAQUE_STREAM 42
#define FLUF_COAP_FORMAT_CBOR 60
#define FLUF_COAP_FORMAT_SENML_JSON 110
#define FLUF_COAP_FORMAT_SENML_CBOR 112
#define FLUF_COAP_FORMAT_SENML_ETCH_JSON 320
#define FLUF_COAP_FORMAT_SENML_ETCH_CBOR 322
#define FLUF_COAP_FORMAT_OMA_LWM2M_TLV 11542
#define FLUF_COAP_FORMAT_OMA_LWM2M_JSON 11543
#define FLUF_COAP_FORMAT_OMA_LWM2M_CBOR 11544

#define FLUF_OBJ_ID_SECURITY 0U
#define FLUF_OBJ_ID_SERVER 1U
#define FLUF_OBJ_ID_ACCESS_CONTROL 2U
#define FLUF_OBJ_ID_DEVICE 3U
#define FLUF_OBJ_ID_OSCORE 21U

/** Object ID */
typedef uint16_t fluf_oid_t;

/** Object Instance ID */
typedef uint16_t fluf_iid_t;

/** Resource ID */
typedef uint16_t fluf_rid_t;

/** Resource Instance ID */
typedef uint16_t fluf_riid_t;

/**
 * Internal macros used for constructing/parsing CoAP codes.
 * @{
 */
#define _FLUF_COAP_CODE_CLASS_MASK 0xE0
#define _FLUF_COAP_CODE_CLASS_SHIFT 5
#define _FLUF_COAP_CODE_DETAIL_MASK 0x1F
#define _FLUF_COAP_CODE_DETAIL_SHIFT 0

#define FLUF_COAP_CODE(cls, detail)                                        \
    ((((cls) << _FLUF_COAP_CODE_CLASS_SHIFT) & _FLUF_COAP_CODE_CLASS_MASK) \
     | (((detail) << _FLUF_COAP_CODE_DETAIL_SHIFT)                         \
        & _FLUF_COAP_CODE_DETAIL_MASK))

#define FLUF_I64_STR_MAX_LEN (sizeof("-9223372036854775808") - 1)
#define FLUF_U16_STR_MAX_LEN (sizeof("65535") - 1)
#define FLUF_U32_STR_MAX_LEN (sizeof("4294967295") - 1)
#define FLUF_U64_STR_MAX_LEN (sizeof("18446744073709551615") - 1)
#define FLUF_DOUBLE_STR_MAX_LEN (sizeof("-2.2250738585072014E-308") - 1)

/**
 * @anchor fluf_coap_code_constants
 * @name fluf_coap_code_constants
 *
 * CoAP code constants, as defined in RFC7252/RFC7959.
 *
 * For detailed description of their semantics, refer to appropriate RFCs.
 * @{
 */
// clang-format off
#define FLUF_COAP_CODE_EMPTY  FLUF_COAP_CODE(0, 0)

#define FLUF_COAP_CODE_GET    FLUF_COAP_CODE(0, 1)
#define FLUF_COAP_CODE_POST   FLUF_COAP_CODE(0, 2)
#define FLUF_COAP_CODE_PUT    FLUF_COAP_CODE(0, 3)
#define FLUF_COAP_CODE_DELETE FLUF_COAP_CODE(0, 4)
/** https://tools.ietf.org/html/rfc8132#section-4 */
#define FLUF_COAP_CODE_FETCH  FLUF_COAP_CODE(0, 5)
#define FLUF_COAP_CODE_PATCH  FLUF_COAP_CODE(0, 6)
#define FLUF_COAP_CODE_IPATCH FLUF_COAP_CODE(0, 7)

#define FLUF_COAP_CODE_CREATED  FLUF_COAP_CODE(2, 1)
#define FLUF_COAP_CODE_DELETED  FLUF_COAP_CODE(2, 2)
#define FLUF_COAP_CODE_VALID    FLUF_COAP_CODE(2, 3)
#define FLUF_COAP_CODE_CHANGED  FLUF_COAP_CODE(2, 4)
#define FLUF_COAP_CODE_CONTENT  FLUF_COAP_CODE(2, 5)
#define FLUF_COAP_CODE_CONTINUE FLUF_COAP_CODE(2, 31)

#define FLUF_COAP_CODE_BAD_REQUEST                FLUF_COAP_CODE(4, 0)
#define FLUF_COAP_CODE_UNAUTHORIZED               FLUF_COAP_CODE(4, 1)
#define FLUF_COAP_CODE_BAD_OPTION                 FLUF_COAP_CODE(4, 2)
#define FLUF_COAP_CODE_FORBIDDEN                  FLUF_COAP_CODE(4, 3)
#define FLUF_COAP_CODE_NOT_FOUND                  FLUF_COAP_CODE(4, 4)
#define FLUF_COAP_CODE_METHOD_NOT_ALLOWED         FLUF_COAP_CODE(4, 5)
#define FLUF_COAP_CODE_NOT_ACCEPTABLE             FLUF_COAP_CODE(4, 6)
#define FLUF_COAP_CODE_REQUEST_ENTITY_INCOMPLETE  FLUF_COAP_CODE(4, 8)
#define FLUF_COAP_CODE_PRECONDITION_FAILED        FLUF_COAP_CODE(4, 12)
#define FLUF_COAP_CODE_REQUEST_ENTITY_TOO_LARGE   FLUF_COAP_CODE(4, 13)
#define FLUF_COAP_CODE_UNSUPPORTED_CONTENT_FORMAT FLUF_COAP_CODE(4, 15)

#define FLUF_COAP_CODE_INTERNAL_SERVER_ERROR  FLUF_COAP_CODE(5, 0)
#define FLUF_COAP_CODE_NOT_IMPLEMENTED        FLUF_COAP_CODE(5, 1)
#define FLUF_COAP_CODE_BAD_GATEWAY            FLUF_COAP_CODE(5, 2)
#define FLUF_COAP_CODE_SERVICE_UNAVAILABLE    FLUF_COAP_CODE(5, 3)
#define FLUF_COAP_CODE_GATEWAY_TIMEOUT        FLUF_COAP_CODE(5, 4)
#define FLUF_COAP_CODE_PROXYING_NOT_SUPPORTED FLUF_COAP_CODE(5, 5)
// clang-format on

#define FLUF_COAP_MAX_TOKEN_LENGTH 8

/** CoAP token object. */
typedef struct {
    uint8_t size;
    char bytes[FLUF_COAP_MAX_TOKEN_LENGTH];
} fluf_coap_token_t;

/**
 * CoAP message type, as defined in RFC 7252.
 */
typedef enum {
    FLUF_COAP_UDP_TYPE_CONFIRMABLE,
    FLUF_COAP_UDP_TYPE_NON_CONFIRMABLE,
    FLUF_COAP_UDP_TYPE_ACKNOWLEDGEMENT,
    FLUF_COAP_UDP_TYPE_RESET
} fluf_coap_udp_type_t;

typedef struct {
    fluf_coap_token_t token;
    uint16_t message_id;
    fluf_coap_udp_type_t type;
} fluf_coap_udp_t;

typedef union {
    fluf_coap_udp_t coap_udp;
} fluf_coap_msg_t;

typedef enum {
    FLUF_ID_OID,
    FLUF_ID_IID,
    FLUF_ID_RID,
    FLUF_ID_RIID,
    FLUF_URI_PATH_MAX_LENGTH
} fluf_id_type_t;

typedef struct {
    uint16_t ids[FLUF_URI_PATH_MAX_LENGTH];
    size_t uri_len;
} fluf_uri_path_t;

/** defines entry type */
typedef uint16_t fluf_data_type_t;

/**
 * Null data type. It will be returned by the input context in the following
 * situations:
 *
 * - when parsing a Composite-Read request payload
 * - when parsing a SenML-ETCH JSON/CBOR payload for a Write-Composite operation
 *   and an entry without a value, requesting a removal of a specific Resource
 *   Instance, is encountered
 * - when parsing a TLV or LwM2M CBOR payload and an aggregate (e.g. Object
 *   Instance or a multi-instance Resource) with zero nested elements is
 *   encountered
 *
 * @ref fluf_res_value_t is not used for null data.
 */
#define FLUF_DATA_TYPE_NULL ((fluf_data_type_t) 0)

/**
 * "Opaque" data type, as defined in Appendix C of the LwM2M spec.
 *
 * The <c>bytes_or_string</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_BYTES ((fluf_data_type_t) (1 << 0))

/**
 * "String" data type, as defined in Appendix C of the LwM2M spec.
 *
 * May also be used to represent the "Corelnk" type, as those two are
 * indistinguishable on the wire.
 *
 * The <c>bytes_or_string</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_STRING ((fluf_data_type_t) (1 << 1))

/**
 * "Integer" data type, as defined in Appendix C of the LwM2M spec.
 *
 * The <c>int_value</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_INT ((fluf_data_type_t) (1 << 2))

/**
 * "Float" data type, as defined in Appendix C of the LwM2M spec.
 *
 * The <c>double_value</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_DOUBLE ((fluf_data_type_t) (1 << 3))

/**
 * "Boolean" data type, as defined in Appendix C of the LwM2M spec.
 *
 * The <c>bool_value</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_BOOL ((fluf_data_type_t) (1 << 4))

/**
 * "Objlnk" data type, as defined in Appendix C of the LwM2M spec.
 *
 * The <c>objlnk</c> field of @ref fluf_res_value_t is used to pass the actual
 * data.
 */
#define FLUF_DATA_TYPE_OBJLNK ((fluf_data_type_t) (1 << 5))

/**
 * "Unsigned Integer" data type, as defined in Appendix C of the LwM2M spec.
 *
 * The <c>uint_value</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_UINT ((fluf_data_type_t) (1 << 6))

/**
 * "Time" data type, as defined in Appendix C of the LwM2M spec.
 *
 * The <c>time_value</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_TIME ((fluf_data_type_t) (1 << 7))

/**
 * When a bit mask of data types is applicable, this constant can be used to
 * specify all supported data types.
 *
 * Note that it does <strong>NOT</strong> include @ref
 * FLUF_DATA_TYPE_FLAG_EXTERNAL, and that @ref FLUF_DATA_TYPE_NULL, having a
 * a numeric value of 0, does not participate in bit masks.
 */
#define FLUF_DATA_TYPE_ANY                                             \
    ((fluf_data_type_t) (FLUF_DATA_TYPE_BYTES | FLUF_DATA_TYPE_STRING  \
                         | FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_DOUBLE  \
                         | FLUF_DATA_TYPE_BOOL | FLUF_DATA_TYPE_OBJLNK \
                         | FLUF_DATA_TYPE_UINT | FLUF_DATA_TYPE_TIME))

/**
 * A flag that can be ORed with either @ref FLUF_DATA_TYPE_BYTES or @ref
 * FLUF_DATA_TYPE_STRING to signify that the data is provided through an
 * external callback. Only valid for output contexts.
 *
 * The <c>external_data</c> field of @ref fluf_res_value_t is then used to pass
 * the actual data.
 */
#define FLUF_DATA_TYPE_FLAG_EXTERNAL ((fluf_data_type_t) (1 << 15))

/**
 * "Opaque" data type, as defined in Appendix C of the LwM2M spec, provided
 * through an external callback. Only valid for output contexts.
 *
 * The <c>external_data</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_EXTERNAL_BYTES \
    ((fluf_data_type_t) (FLUF_DATA_TYPE_BYTES | FLUF_DATA_TYPE_FLAG_EXTERNAL))

/**
 * "String" data type, as defined in Appendix C of the LwM2M spec, provided
 * through an external callback. Only valid for output contexts.
 *
 * May also be used to represent the "Corelnk" type, as those two are
 * indistinguishable on the wire.
 *
 * The <c>external_data</c> field of @ref fluf_res_value_t is used to pass the
 * actual data.
 */
#define FLUF_DATA_TYPE_EXTERNAL_STRING \
    ((fluf_data_type_t) (FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_FLAG_EXTERNAL))

/**
 * A handler that get binary/string data from external source.
 * Will be called if @ref fluf_data_type_t is set to @ref
 * FLUF_DATA_TYPE_EXTERNAL_BYTES or @ref FLUF_DATA_TYPE_EXTERNAL_STRING.
 * Can be called multiple times.
 *
 * @param buffer        Buffer to copy data.
 * @param bytes_to_copy Number of bytes to be copied.
 * @param offset        From which memory offset to start copying.
 * @param user_args     User data passed when the function is called.
 *
 * @return 0 on success, a negative value in case of error.
 */
typedef int fluf_get_external_data_t(void *buffer,
                                     size_t bytes_to_copy,
                                     size_t offset,
                                     void *user_args);

typedef struct {
    /**
     * Pointer to the data buffer.
     */
    void *data;

    /**
     * Offset, in bytes, of the entire resource value, that the <c>data</c>
     * field points to.
     *
     * For output contexts (e.g. responding to a Read operation), this currently
     * MUST be 0. Please use the <c>external_data</c> variant for outputting
     * large resources.
     *
     * For input contexts (e.g. parsing a Write operation payload), non-zero
     * values will be returned when parsing large resources that span multiple
     * data packets.
     */
    size_t offset;

    /**
     * Length, in bytes, of valid data at the buffer pointed to by <c>data</c>.
     *
     * For output contexts (e.g. responding to a Read operation) and resources
     * of type @ref FLUF_DATA_TYPE_STRING, if you leave both <c>chunk_length</c>
     * and <c>full_length_hint</c> as 0 and <c>data</c> is non-NULL, then
     * <c>data</c> will be assumed to point to a null-terminated string and
     * <c>strlen()</c> will be called to calculate its length instead.
     */
    size_t chunk_length;

    /**
     * Full length, in bytes, of the entire resource, if available. If all three
     * of <c>offset</c>, <c>chunk_length</c> and <c>full_length_hint</c> are
     * zero, this object refers to a zero-length resource. In all other cases,
     * a value of 0 signifies that information about total length is not
     * available.
     *
     * For output contexts (e.g. responding to a Read operation), this can be
     * set to either 0 or a value equal to <c>chunk_length</c>. Other values
     * will be treated as an error.
     *
     * For input contexts (e.g. parsing a Write operation payload), this will be
     * set to 0 when parsing content formats that do not provide length
     * information upfront (e.g. Plain Text or SenML JSON), until the entire
     * resource is parsed. End of resource will always be marked with
     * <c>full_length_hint</c> set to <c>offset + chunk_length</c>.
     */
    size_t full_length_hint;
} fluf_bytes_or_string_value_t;

typedef struct {
    fluf_oid_t oid;
    fluf_iid_t iid;
} fluf_objlnk_value_t;

/**
 * Stores a complete or partial value of a data model entry, check "Data Types"
 * appendix in LwM2M specification for more information.
 */
typedef union {
    /**
     * Chunk of information valid for when the underlying data type is
     * @ref FLUF_DATA_TYPE_BYTES or @ref FLUF_DATA_TYPE_STRING.
     */
    fluf_bytes_or_string_value_t bytes_or_string;

    /**
     * Configuration for resources generated using an external data callback,
     * valid for output contexts only, when the underlying data type is
     * @ref FLUF_DATA_TYPE_EXTERNAL_BYTES or
     * @ref FLUF_DATA_TYPE_EXTERNAL_STRING.
     */
    struct {
        /**
         * Callback that will be called to request a chunk of data.
         */
        fluf_get_external_data_t *get_external_data;

        /**
         * Opaque pointer that will be passed to <c>get_external_data</c> and
         * may be used by the user to pass additional context.
         */
        void *user_args;

        /**
         * Total length in bytes of the resource data. This is necessary for
         * some data types (e.g. CBOR-based ones) that require encoding the
         * length before the data. In case of string resources, this shall NOT
         * include any null terminator characters.
         */
        size_t length;
    } external_data;

    /**
     * Integer value, valid when the underlying data type is
     * @ref FLUF_DATA_TYPE_INT.
     */
    int64_t int_value;

    /**
     * Unsigned Integer value, valid when the underlying data type is
     * @ref FLUF_DATA_TYPE_UINT.
     */
    uint64_t uint_value;

    /**
     * Double-precision floating-point value, valid when the underlying data
     * type is @ref FLUF_DATA_TYPE_DOUBLE.
     */
    double double_value;

    /**
     * Boolean value, valid when the underlying data type is
     * @ref FLUF_DATA_TYPE_BOOL.
     */
    bool bool_value;

    /**
     * Objlnk value, valid when the underlying data type is
     * @ref FLUF_DATA_TYPE_OBJLNK.
     */
    fluf_objlnk_value_t objlnk;

    /**
     * Time value, expressed as a UNIX timestamp, valid when the underlying data
     * type is @ref FLUF_DATA_TYPE_TIME.
     */
    int64_t time_value;
} fluf_res_value_t;

#ifdef __cplusplus
}
#endif

#endif // FLUF_DEFS_H
