/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef FLUF_H
#define FLUF_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fluf/fluf_config.h>
#include <fluf/fluf_defs.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef FLUF_WITH_LWM2M12
#    define FLUF_LWM2M_VERSION_STR "1.2"
#else
#    define FLUF_LWM2M_VERSION_STR "1.1"
#endif // FLUF_WITH_LWM2M12

/** Invalid input arguments. */
#define FLUF_ERR_INPUT_ARG (-1)
/** Not supported binding type */
#define FLUF_ERR_BINDING (-2)
/** Options array is not big enough */
#define FLUF_ERR_OPTIONS_ARRAY (-3)
/** FLUF_ATTR_OPTION_MAX_SIZE is too small */
#define FLUF_ERR_ATTR_BUFF (-4)
/** Malformed CoAP message. */
#define FLUF_ERR_MALFORMED_MESSAGE (-5)
/** No space in buffer. */
#define FLUF_ERR_BUFF (-6)
/** COAP message not supported or not recognized. */
#define FLUF_ERR_COAP_BAD_MSG (-6)
/** Location paths number oversizes FLUL_MAX_ALLOWED_LOCATION_PATHS_NUMBER*/
#define FLUF_ERR_LOCATION_PATHS_NUMBER (-7)

/**
 * LWM2M operations.
 */
typedef enum {
    // Bootstrap Interface
    FLUF_OP_BOOTSTRAP_REQ,
    FLUF_OP_BOOTSTRAP_FINISH,
    FLUF_OP_BOOTSTRAP_PACK_REQ,
    // Registration Interface
    FLUF_OP_REGISTER,
    FLUF_OP_UPDATE,
    FLUF_OP_DEREGISTER,
    // DM Interface,
    FLUF_OP_DM_READ,
    FLUF_OP_DM_READ_COMP,
    FLUF_OP_DM_DISCOVER,
    FLUF_OP_DM_WRITE_REPLACE,
    FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
    FLUF_OP_DM_WRITE_ATTR,
    FLUF_OP_DM_WRITE_COMP,
    FLUF_OP_DM_EXECUTE,
    FLUF_OP_DM_CREATE,
    FLUF_OP_DM_DELETE,
    // Information reporting interface
    FLUF_OP_INF_OBSERVE,
    FLUF_OP_INF_OBSERVE_COMP,
    FLUF_OP_INF_CANCEL_OBSERVE,
    FLUF_OP_INF_CANCEL_OBSERVE_COMP,
    FLUF_OP_INF_CON_NOTIFY,
    FLUF_OP_INF_NON_CON_NOTIFY,
    FLUF_OP_INF_SEND,
    // client/server ACK Piggybacked/non-con/con response
    FLUF_OP_RESPONSE,
    // CoAP related messages
    FLUF_OP_COAP_RESET,
    FLUF_OP_COAP_PING
} fluf_op_t;

/**
 * Defines CoAP transport binding.
 */
typedef enum {
    FLUF_BINDING_UDP,
    FLUF_BINDING_DTLS_PSK,
    FLUF_BINDING_TCP,
    FLUF_BINDING_LORAWAN,
    FLUF_BINDING_NIDD,
    FLUF_BINDING_SMS,
} fluf_binding_type_t;

/**
 * CoAP block option type.
 */
typedef enum {
    FLUF_OPTION_BLOCK_NOT_DEFINED,
    FLUF_OPTION_BLOCK_1,
    FLUF_OPTION_BLOCK_2
} fluf_block_option_t;

/**
 * CoAP block option struct.
 */
typedef struct {
    fluf_block_option_t block_type;
    bool more_flag;
    uint32_t number;
    uint32_t size;
} fluf_block_t;

/**
 * Notification attributes.
 */
typedef struct {
    bool has_min_period;
    bool has_max_period;
    bool has_greater_than;
    bool has_less_than;
    bool has_step;
    bool has_min_eval_period;
    bool has_max_eval_period;

    uint32_t min_period;
    uint32_t max_period;
    double greater_than;
    double less_than;
    double step;
    uint32_t min_eval_period;
    uint32_t max_eval_period;

#ifdef FLUF_WITH_LWM2M12
    bool has_edge;
    bool has_con;
    bool has_hqmax;
    uint32_t edge;
    uint32_t con;
    uint32_t hqmax;
#endif // FLUF_WITH_LWM2M12
} fluf_attr_notification_t;

/**
 * DISCOVER operation attribute - depth parameter.
 */
typedef struct {
    bool has_depth;
    uint32_t depth;
} fluf_attr_discover_t;

/**
 * REGISTER operation attributes.
 */
typedef struct {
    bool has_Q;
    bool has_endpoint;
    bool has_lifetime;
    bool has_lwm2m_ver;
    bool has_binding;
    bool has_sms_number;

    char *endpoint;
    uint32_t lifetime;
    char *lwm2m_ver;
    char *binding;
    char *sms_number;
} fluf_attr_register_t;

/**
 * BOOTSTRAP-REQUEST operation attributes.
 */
typedef struct {
    bool has_endpoint;
    bool has_pct;

    char *endpoint;
    uint16_t pct;
} fluf_attr_bootstrap_t;

/**
 * Location-Path from REGISTER operation response. If the number of
 * Location-Paths exceeds @ref FLUL_MAX_ALLOWED_LOCATION_PATHS_NUMBER then @ref
 * fluf_msg_decode returns a @ref FLUF_ERR_LOCATION_PATHS_NUMBER error. For
 * every @ref fluf_msg_prepare calls for UPDATE and DEREGISTER operations, this
 * structure must be filled. After @ref fluf_msg_prepare @p location points to
 * message buffer, so they have to be copied into user memory.
 */
typedef struct {
    // doesn't point to /rd location-path - it's obligatory
    const char *location[FLUL_MAX_ALLOWED_LOCATION_PATHS_NUMBER];
    size_t location_len[FLUL_MAX_ALLOWED_LOCATION_PATHS_NUMBER];
    size_t location_count;
} fluf_location_path_t;

/** Maximum size of ETag option, as defined in RFC7252. */
#define FLUF_MAX_ETAG_LENGTH 8

/**
 * CoAP ETag option.
 */
typedef struct {
    uint8_t size;
    uint8_t bytes[FLUF_MAX_ETAG_LENGTH];
} fluf_etag_t;

/**
 * Contains all details of CoAP LWM2M message, used with @ref fluf_msg_decode
 * and @ref fluf_msg_prepare. During CoAP message preparing all fields related
 * with given @p opeartion will be used. @ref fluf_msg_decode extracts all
 * possible information, so user doesn't have to use CoAP related function
 * directly.
 */
typedef struct {
    /**
     * LWM2M operation type. Must be defined before @ref fluf_msg_prepare
     * call.
     */
    fluf_op_t operation;

    /**
     * Pointer to CoAP msg payload. Set in @ref fluf_msg_decode, @ref
     * fluf_msg_prepare copies payload directly to message buffer.
     *
     * IMPORTANT: Payload is not encoded or decoded by FLUF functions, use
     * FLUF_IO API to achieve this.
     */
    void *payload;

    /** Payload length. */
    size_t payload_size;

    /**
     * Stores the value of Content Format option. If payload is present it
     * describes its format. In @ref fluf_msg_decode set to
     * FLUF_COAP_FORMAT_NOT_DEFINED if not present. If message contains payload,
     * must be set before @ref fluf_msg_prepare call.
     */
    uint16_t content_format;

    /**
     * Stores the value of Accept option. It describes response payload
     * preferred format. Set to FLUF_COAP_FORMAT_NOT_DEFINED if not present.
     */
    uint16_t accept;

    /**
     * Observation number. Have to be incremented with every Notify message.
     */
    uint64_t observe_number;

    /**
     * Stores the value of Uri Path options. Contains information about data
     * model path.
     */
    fluf_uri_path_t uri;

    /**
     * Stores the value of Block option. If block type is defined
     * @ref fluf_msg_prepare will add block option to the message.
     */
    fluf_block_t block;

    /** Stores the value of ETag option. */
    fluf_etag_t etag;

    /**
     * Location path is send in respone to the REGISTER message and then have to
     * be used in UPDATE and DEREGISTER requests.
     */
    fluf_location_path_t location_path;

    /**
     * Attributes are optional and stored in Uri Query options.
     */
    union {
        fluf_attr_notification_t notification_attr;
        fluf_attr_discover_t discover_attr;
        fluf_attr_register_t register_attr;
        fluf_attr_bootstrap_t bootstrap_attr;
    } attr;

    /**
     * Coap msg code. Must be set before @ref fluf_msg_prepare call if message
     * is any kind of response.
     */
    uint8_t msg_code;

    /** Binding type - defines communication channel. **/
    fluf_binding_type_t binding;

    /**
     * Contains communication channel dependend informations that allows to
     * prepare or identify response.
     */
    fluf_coap_msg_t coap;
} fluf_data_t;

/**
 * Based on @p msg decodes CoAP message, compliant with the LwM2M version 1.1
 * or 1.2 (check @ref FLUF_WITH_LWM2M12 config flag). All information from
 * message are decoded and stored in the @p data. Each possible option, has its
 * own field in @ref fluf_data_t and if present in the message then it is
 * decoded. In order to be able to send the response, the data that must be in
 * the CoAP header (such as token or message id) are copied to @ref
 * fluf_coap_msg_t.
 *
 * @param      msg       LWM2M/CoAP message.
 * @param      msg_size  Length of the message.
 * @param      binding   Defined source of the message.
 * @param[out] data      Empty LwM2M data instance.
 *
 * NOTES: Check tests/lwm2m_decode.c to see the examples of usage.
 *
 * @return 0 on success, or an one of the error codes defined at the top of this
 * file.
 */
int fluf_msg_decode(uint8_t *msg,
                    size_t msg_size,
                    fluf_binding_type_t binding,
                    fluf_data_t *data);

/**
 * Based on @p data prepares CoAP message, compliant with the LwM2M version 1.1
 * or 1.2 (check @ref FLUF_WITH_LWM2M12 config flag). All information related
 * with given @ref fluf_op_t are placed into the message. After function call
 * @p out_buff contains a CoAP packet ready to be sent.
 *
 * @param      data          LwM2M data instance.
 * @param[out] out_buff      Buffer for LwM2M message.
 * @param      out_buff_size Buffer size.
 * @param[out] out_msg_size  Size of the prepared message.
 *
 * NOTES: Check tests/lwm2m_prepare.c to see the examples of usage.
 *
 * @return 0 on success, or an one of the error codes defined at the top of this
 * file.
 */
int fluf_msg_prepare(fluf_data_t *data,
                     uint8_t *out_buff,
                     size_t out_buff_size,
                     size_t *out_msg_size);

/**
 * Should be called once to initialize the module.
 *
 * @param random_seed  PRNG seed value, used in CoAP token generation process.
 */
void fluf_init(uint32_t random_seed);

#ifdef __cplusplus
}
#endif

#endif // FLUF_H
