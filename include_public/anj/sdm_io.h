/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_IO_H
#define SDM_IO_H

#include <fluf/fluf.h>
#include <fluf/fluf_defs.h>

#include <anj/anj_config.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Allows to handling writing of bytes in @ref sdm_res_write_t handler. Data
 * are copied to the @p Buffer. In case of overflow, this macro returns @ref
 * SDM_ERR_BAD_REQUEST. For last chunk @p Bytes_len will be set.
 */
#define SDM_RES_WRITE_HANDLING_BYTES(Value, Buffer, Buffer_len, Bytes_len) \
    do {                                                                   \
        if ((Value)->bytes_or_string.offset                                \
                        + (Value)->bytes_or_string.chunk_length            \
                > (Buffer_len)) {                                          \
            return SDM_ERR_BAD_REQUEST;                                    \
        }                                                                  \
        memcpy(&(Buffer)[(Value)->bytes_or_string.offset],                 \
               (Value)->bytes_or_string.data,                              \
               (Value)->bytes_or_string.chunk_length);                     \
        if ((Value)->bytes_or_string.offset                                \
                        + (Value)->bytes_or_string.chunk_length            \
                == (Value)->bytes_or_string.full_length_hint) {            \
            (Bytes_len) = (Value)->bytes_or_string.full_length_hint;       \
        }                                                                  \
    } while (0)

/** Allows to handling writing of string in @ref sdm_res_write_t handler. Data
 * are copied to the @p Buffer. In case of overflow, this macro returns @ref
 * SDM_ERR_BAD_REQUEST. For the last chunk, null character is added.
 */
#define SDM_RES_WRITE_HANDLING_STRING(Value, Buffer, Buffer_len)        \
    do {                                                                \
        if ((Value)->bytes_or_string.offset                             \
                        + (Value)->bytes_or_string.chunk_length         \
                > ((Buffer_len) -1)) {                                  \
            return SDM_ERR_BAD_REQUEST;                                 \
        }                                                               \
        memcpy(&(Buffer)[(Value)->bytes_or_string.offset],              \
               (Value)->bytes_or_string.data,                           \
               (Value)->bytes_or_string.chunk_length);                  \
        if ((Value)->bytes_or_string.offset                             \
                        + (Value)->bytes_or_string.chunk_length         \
                == (Value)->bytes_or_string.full_length_hint) {         \
            (Buffer)[(Value)->bytes_or_string.full_length_hint] = '\0'; \
        }                                                               \
    } while (0)

/** Allows to create a Resource. If @p Handlers with appropriate callbacks is
 * given then @p Res_val can be NULL. If the Resource is created as a global
 * variable then inside the call of this macro @ref SDM_MAKE_RES_SPEC, @ref
 * SDM_MAKE_RES_VALUE, or @ref SDM_MAKE_RES_VALUE_WITH_INITIALIZE can be called.
 *
 * IMPORTANT: If Resource is not created as a global variable then you CANNOT
 * use any of the macros that create Res_val.
 *
 * Below are two examples of how to create a SSID Resource of a Server Object
 * @code
 * // Create a Resource with Handlers pointer set to NULL and Res_val defined
 * // and initialized to a value of 2.
 * sdm_res_t ssid_res = SDM_MAKE_RES(
 *       &SDM_MAKE_RES_SPEC(0, FLUF_DATA_TYPE_INT, SDM_RES_R),
 *       NULL,
 *       &SDM_MAKE_RES_VALUE_WITH_INITIALIZE(0, SDM_INIT_RES_VAL_I64(2)));
 *
 * // Create a Resource with Res_val pointer set to NULL and Handlers defined.
 * sdm_res_t ssid_res = SDM_MAKE_RES(
 *       &SDM_MAKE_RES_SPEC(0, FLUF_DATA_TYPE_INT, SDM_RES_R),
 *       &ssid_res_handlers, NULL);
 * @endcode
 */
#define SDM_MAKE_RES(Res_spec, Handlers, Res_val) \
    ((sdm_res_t) {                                \
        .res_spec = (Res_spec),                   \
        .res_handlers = (Handlers),               \
        .value.res_value = (Res_val)              \
    })

/** Allows to create a Multiple Resource. If @p Handlers with appropriate
 * callbacks is given then @p Res_val can be NULL. If the Resource is created as
 * a global variable then inside the call of this macro @ref SDM_MAKE_RES_SPEC
 * can be called. */
#define SDM_MAKE_MULTI_RES(                                   \
        Res_spec, Handlers, Inst, Inst_count, Max_inst_count) \
    ((sdm_res_t) {                                            \
        .res_spec = (Res_spec),                               \
        .res_handlers = (Handlers),                           \
        .value.res_inst.insts = (Inst),                       \
        .value.res_inst.inst_count = (Inst_count),            \
        .value.res_inst.max_inst_count = (Max_inst_count)     \
    })

/**
 * Allows to create a Resource Instance, Res_val can be NULL. If the Resource
 * Instance is created as a global variable then inside the call of this macro
 * @ref SDM_MAKE_RES_VALUE, or @ref SDM_MAKE_RES_VALUE_WITH_INITIALIZE can be
 * called.
 *
 * IMPORTANT: If Resource Instance is not created as a global variable then you
 * CANNOT use any of the macros that create Res_val.
 */
#define SDM_MAKE_RES_INST(Riid, Res_val) \
    ((sdm_res_inst_t) {                  \
        .riid = (Riid),                  \
        .res_value = (Res_val)           \
    })

/** Allows to initialize a @ref sdm_res_spec_t struct. */
#define SDM_MAKE_RES_SPEC(Rid, Data_type, Operation_type) \
    ((const sdm_res_spec_t) {                             \
        .rid = (Rid),                                     \
        .type = (Data_type),                              \
        .operation = (Operation_type)                     \
    })

/** Allows to initialize a @ref sdm_res_value_t struct. Set Buff_size to zero if
 * the variable is not of type bytes or string, res_value field can be
 * set using a macro from the SDM_SET_RES_VAL_ group. */
#define SDM_MAKE_RES_VALUE(Buff_size)       \
    ((sdm_res_value_t) {                    \
        .resource_buffer_size = (Buff_size) \
    })

/** Allows initialization of @ref sdm_res_value_t struct with assignment of
 * initial values. Set Buff_size to zero if the variable is not of type bytes or
 * string. To use, it is necessary to use one of the macros from the
 * SDM_INIT_RES_VAL_ group. */
#define SDM_MAKE_RES_VALUE_WITH_INITIALIZE(Buff_size, Sdm_init_res_val_macro) \
    ((sdm_res_value_t) {                                                      \
        .resource_buffer_size = (Buff_size), Sdm_init_res_val_macro           \
    })

/** Group of macros that allow to init the value of a resource
 * stored in @ref res_value, intended for use only
 * with @ref SDM_MAKE_RES_VALUE_WITH_INITIALIZE. */
#define SDM_INIT_RES_VAL_U64(U64) .value.uint_value = (U64)
#define SDM_INIT_RES_VAL_I64(I64) .value.int_value = (I64)
#define SDM_INIT_RES_VAL_BOOL(Bool) .value.bool_value = (Bool)
#define SDM_INIT_RES_VAL_DOUBLE(Double) .value.double_value = (Double)
#define SDM_INIT_RES_VAL_OBJLNK(Oid, Iid) \
    .value.objlnk.oid = (Oid),            \
    .value.objlnk.iid = (Iid)
#define SDM_INIT_RES_VAL_TIME_VAL(Time) .value.time_value = (Time)
#define SDM_INIT_RES_VAL_STRING(String) .value.bytes_or_string.data = (String)
#define SDM_INIT_RES_VAL_BYTES(Bytes, Bytes_len) \
    .value.bytes_or_string.data = (Bytes),       \
    .value.bytes_or_string.chunk_length = (Bytes_len)

typedef struct sdm_res_inst_struct sdm_res_inst_t;
typedef struct sdm_res_struct sdm_res_t;
typedef struct sdm_obj_inst_struct sdm_obj_inst_t;
typedef struct sdm_obj_struct sdm_obj_t;

/** Error values that may be returned from data model handlers. @{ */
/**
 * Request sent by the LwM2M Server was malformed or contained an invalid
 * value.
 */
#define SDM_ERR_BAD_REQUEST (-(int) FLUF_COAP_CODE_BAD_REQUEST)
/**
 * LwM2M Server is not allowed to perform the operation due to lack of
 * necessary access rights.
 */
#define SDM_ERR_UNAUTHORIZED (-(int) FLUF_COAP_CODE_UNAUTHORIZED)

/** Target of the operation (Object/Instance/Resource) does not exist. */
#define SDM_ERR_NOT_FOUND (-(int) FLUF_COAP_CODE_NOT_FOUND)
/**
 * Operation is not allowed in current device state or the attempted operation
 * is invalid for this target (Object/Instance/Resource)
 */
#define SDM_ERR_METHOD_NOT_ALLOWED (-(int) FLUF_COAP_CODE_METHOD_NOT_ALLOWED)

/** Unspecified error, no other error code was suitable. */
#define SDM_ERR_INTERNAL (-(int) FLUF_COAP_CODE_INTERNAL_SERVER_ERROR)
/** Operation is not implemented by the LwM2M Client. */
#define SDM_ERR_NOT_IMPLEMENTED (-(int) FLUF_COAP_CODE_NOT_IMPLEMENTED)
/**
 * LwM2M Client is busy processing some other request; LwM2M Server may retry
 * sending the same request after some delay.
 */
#define SDM_ERR_SERVICE_UNAVAILABLE (-(int) FLUF_COAP_CODE_SERVICE_UNAVAILABLE)
/** @} */

/**
 * Data model operation result.
 */
typedef enum {
    /** Operation success. Object has been changed. */
    SDM_OP_RESULT_SUCCESS_MODIFIED,
    /** Read-only operation success. */
    SDM_OP_RESULT_SUCCESS_NOT_MODIFIED,
    /** The operation has failed. */
    SDM_OP_RESULT_FAILURE
} sdm_op_result_t;

/**
 * Resource operation types.
 */
typedef enum {
    /**
     * Read-only Single-Instance Resource. Bootstrap Server might attempt to
     * write to it anyway.
     */
    SDM_RES_R,
    /**
     * Read-only Multiple Instance Resource. Bootstrap Server might attempt to
     * write to it anyway.
     */
    SDM_RES_RM,
    /** Write-only Single-Instance Resource. */
    SDM_RES_W,
    /** Write-only Multiple Instance Resource. */
    SDM_RES_WM,
    /** Read/Write Single-Instance Resource. */
    SDM_RES_RW,
    /** Read/Write Multiple Instance Resource. */
    SDM_RES_RWM,
    /** Executable Resource. */
    SDM_RES_E,
    /** Resource that can be read/written only by Bootstrap server. */
    SDM_RES_BS_RW
} sdm_res_operation_t;

/**
 * Basic information about Resource. Can be used by the same Resource in
 * different Instances of an Object.
 */
typedef struct {
    /** Resource ID number. */
    fluf_rid_t rid;
    /** Resource data type as defined in Appendix C of the LwM2M spec. */
    fluf_data_type_t type;
    /** Operation allowed on the Resource. */
    sdm_res_operation_t operation;
} sdm_res_spec_t;

/**
 * A handler that reads the Resource or Resource Instance value, called only if
 * the Resource or Resource Instance is one of the @ref SDM_RES_R, @ref
 * SDM_RES_RW or @ref SDM_RES_BS_RW, @ref SDM_RES_RM, @ref SDM_RES_RWM,
 *
 * @param      obj        Object definition pointer.
 * @param      obj_inst   Object Instance pointer.
 * @param      res        Resource pointer.
 * @param      res_inst   Resource Instance pointer NULL in case
 *                        of a single Resource.
 * @param[out] out_value  Returned Resource value.
 *
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_res_read_t(sdm_obj_t *obj,
                           sdm_obj_inst_t *obj_inst,
                           sdm_res_t *res,
                           sdm_res_inst_t *res_inst,
                           fluf_res_value_t *out_value);

/**
 * A handler that writes the Resource or Resource Instance value, called only if
 * the Resource or Resource Instance is PRESENT and is one of the @ref
 * SDM_RES_W, @ref SDM_RES_RW, @ref SDM_RES_WM, @ref SDM_RES_RWM, @ref
 * SDM_RES_BS_RW.
 *
 * For values of type @ref FLUF_DATA_TYPE_BYTES and @ref FLUF_DATA_TYPE_STRING,
 * in case of the block operation, handler can be called several times, with
 * consecutive chunks of value - @ref offset value in @ref bytes_or_string will
 * be changing.
 *
 * IMPORTANT: For value of type @ref FLUF_DATA_TYPE_STRING always use
 * <c>chunk_length</c> to determine the length of the string, never use the
 * <c>strlen()</c> function - pointer to string data points directly to CoAP
 * message payload.
 *
 * @param obj      Object definition pointer.
 * @param obj_inst Object Instance pointer.
 * @param res      Resource pointer.
 * @param res_inst Resource Instance pointer NULL in case
 *                 of a single Resource.
 * @param value    Resource value.
 *
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_res_write_t(sdm_obj_t *obj,
                            sdm_obj_inst_t *obj_inst,
                            sdm_res_t *res,
                            sdm_res_inst_t *res_inst,
                            const fluf_res_value_t *value);

/**
 * A handler that performs the Execute action on given Resource, called only if
 * the Resource is PRESENT and is @ref SDM_RES_E kind.
 *
 * @param obj             Object definition pointer.
 * @param obj_inst        Object Instance pointer.
 * @param res             Resource pointer.
 * @param execute_arg     Payload provided in EXECUTE request, NULL if not
 *                        present.
 * @param execute_arg_len Execute payload length.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_res_execute_t(sdm_obj_t *obj,
                              sdm_obj_inst_t *obj_inst,
                              sdm_res_t *res,
                              const char *execute_arg,
                              size_t execute_arg_len);

/**
 * A handler called in order to create a new Instance of the Resource. If the
 * creation of the Instance succeeds but @ref sdm_operation_end_t returns
 * information about the failure of the transaction, the user is responsible for
 * deleting the Instance.
 *
 * @param       obj          Object definition pointer.
 * @param       obj_inst     Object Instance pointer.
 * @param       res          Resource pointer.
 * @param [out] out_res_inst Points to the Instances array field in @ref
 *                           sdm_res_t. Must be filled in this call. After
 *                           successfull call the array will be reorganised if
 *                           needed to keep the ascending order of the
 *                           Instances.
 * @param       riid         New Resource Instance ID.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_res_inst_create_t(sdm_obj_t *obj,
                                  sdm_obj_inst_t *obj_inst,
                                  sdm_res_t *res,
                                  sdm_res_inst_t **out_res_inst,
                                  fluf_riid_t riid);

/**
 * A handler called in order to delete an Instance of the Resource. After this
 * call @p res_inst will be removed from the instances array in the @p res and
 * iid field of this Instance will be set to @ref FLUF_ID_INVALID. If @ref
 * sdm_res_inst_delete_t call was successful but @ref sdm_operation_end_t
 * returned information about the failure of the transaction, the user is
 * responsible for restoring the Instance.
 *
 * @param obj       Object definition pointer.
 * @param obj_inst  Object Instance pointer.
 * @param res       Resource pointer.
 * @param res_inst  Resource Instance pointer.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_res_inst_delete_t(sdm_obj_t *obj,
                                  sdm_obj_inst_t *obj_inst,
                                  sdm_res_t *res,
                                  sdm_res_inst_t *res_inst);

/**
 * A handler that creates an Object Instance. If the creation of the Instance
 * succeeds but @ref sdm_operation_end_t returns information about the failure
 * of the transaction, the user is responsible for deleting the Instance. To
 * achieve this, the user should call @ref sdm_remove_obj_inst.
 *
 * @param       obj          Object definition pointer.
 * @param [out] out_obj_inst Points to the Instances array field in @ref
 *                           sdm_obj_t. Must be filled in this call. After
 *                           successfull call the array will be reorganised if
 *                           needed to keep the ascending order of the
 *                           Instances.
 * @param       iid          New object Instance ID.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_inst_create_t(sdm_obj_t *obj,
                              sdm_obj_inst_t **out_obj_inst,
                              fluf_iid_t iid);

/**
 * A handler that deletes an Object Instance. After this call @p obj_inst will
 * be removed from the Instances array in the @p obj and iid field of this
 * Instance will be set to @ref FLUF_ID_INVALID. If @ref sdm_inst_delete_t call
 * was successful but @ref sdm_operation_end_t returned information about the
 * failure of the transaction, the user is responsible for restoring the
 * Instance.
 *
 * @param obj       Object definition pointer.
 * @param obj_inst  Object Instance pointer.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_inst_delete_t(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst);

/**
 * A handler that shall reset Object Instance to its default (after creational)
 * state. In this call remove all resource instances of this @p obj_inst that
 * are writable. New values will be provided. This handler is used in the LwM2M
 * WRITE_REPLACE operation.
 *
 * @param obj       Object definition pointer.
 * @param obj_inst  Object Instance pointer.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_inst_reset_t(sdm_obj_t *obj, sdm_obj_inst_t *obj_inst);

/**
 * It is called when a request from the LwM2M server involves an Object
 * associated with this handler.
 *
 * @param obj        Object definition pointer.
 * @param operation  Data model operation type.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_operation_begin_t(sdm_obj_t *obj, fluf_op_t operation);

/**
 * A handler that is called after transaction is finished, but before
 * @ref sdm_operation_end_t is called. It is used to check whether the
 * operation can be completed successfully. It is called when a request
 * from the LwM2M server involves an Object associated with this handler and
 * modifies it (CREATE, WRITE and DELETE operations).
 *
 * @param obj   Object definition pointer.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of SDM_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int sdm_operation_validate_t(sdm_obj_t *obj);

/**
 * It is called after handling a request from the LwM2M server. If @p status is
 * @ref SDM_OP_RESULT_FAILURE user is supposed to restore previous @p obj state.
 *
 * @param obj     Object definition pointer.
 * @param result  Status of the LwM2M operation.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error.
 */
typedef int sdm_operation_end_t(sdm_obj_t *obj, sdm_op_result_t result);

/**
 * A struct containing pointers to Resource handlers. A single Instance can be
 * used by many Resources.
 */
typedef struct {
    /**
     * Get Resource value, required for LwM2M READ operation.
     * If NULL and the Resource supports READ operation, then the value will be
     * read directly from @ref fluf_res_value_t
     */
    sdm_res_read_t *res_read;

    /**
     * Set Resource value, required for LwM2M WRITE operation.
     * If NULL and the Resource supports WRITE operation, then the value will be
     * written directly to @ref fluf_res_value_t
     */
    sdm_res_write_t *res_write;

    /**
     * Required for LwM2M EXECUTE operation. Must be defined for a Resource of
     * type @ref SDM_RES_E.
     */
    sdm_res_execute_t *res_execute;

    /**
     * Create a Resource Instance in a multi-instance Resource.
     *
     * Required for handling LwM2M WRITE operation. If not defined and the
     * operation requires the creation of a new Instance then an error will be
     * returned to the LwM2M server.
     */
    sdm_res_inst_create_t *res_inst_create;

    /**
     * Delete a Resource Instance from a multi-instance Resource.
     *
     * Required for handling LwM2M DELETE operation performed on Resource
     * Instances and for LwM2M WRITE operation in replace mode, which can remove
     * all Resource Instances. If not defined and the operation requires
     * deleting the Instance, an error will be returned to the LwM2M server.
     */
    sdm_res_inst_delete_t *res_inst_delete;
} sdm_res_handlers_t;

/** Resource value struct. */
typedef struct {
    /** Resource value. */
    fluf_res_value_t value;
    /**
     * For a Resource of type @ref FLUF_DATA_TYPE_BYTES or @ref
     * FLUF_DATA_TYPE_STRING describes the size of the buffer pointed to by the
     * value.bytes_or_string.data.
     *
     * Must be set if Resource supports WRITE operation and @ref res_write
     * handler is not defined.
     */
    size_t resource_buffer_size;
} sdm_res_value_t;

/** A struct defining a value of Resource Instance. */
struct sdm_res_inst_struct {
    /**
     * Resource Instance value. If not set, and the resource is writable or
     * readable @ref res_handlers will be used instead.
     */
    sdm_res_value_t *res_value;
    /** Resource Instance ID number. */
    fluf_riid_t riid;
};

/** Main Resource struct. */
struct sdm_res_struct {
    /** Resource specification, can't be NULL. */
    const sdm_res_spec_t *res_spec;
    /**
     * Resource handlers, can be NULL, unless the Resource is of type @ref
     * SDM_RES_E, or @ref res_value is not set, in this case, @ref res_read is
     * required if the resource is readable and @ref res_write if it is
     * writable.
     */
    const sdm_res_handlers_t *res_handlers;
    /**
     * For READ and WRITE operations if the corresponding handlers are not
     * defined, the value of the Resource/Resource Instance will be entered or
     * taken directly from here.
     */
    union {
        struct {
            /**
             * Pointer to the array of the pointers to the Resource Instances.
             * @ref max_allowed_inst_number defined the size of the array.
             *
             * During any type of CREATE or WTIRE operations array of Instances
             * might be modified, so if @ref sdm_res_inst_create_t or @ref
             * sdm_res_inst_delete_t are defined for the Resource @p insts can't
             * points to the const array.
             *
             * When @ref sdm_add_obj is called, the Instances inside the array
             * must be ordered in ascending order of Resource Instance ID
             * number.
             *
             * Example:
             * @code
             * #define RES_INST_MAX_COUNT 3
             * sdm_res_inst_t res_inst_1 = {.riid = 1};
             * sdm_res_inst_t res_inst_2 = {.riid = 2};
             * sdm_res_inst_t *res_insts[RES_INST_MAX_COUNT] = {
             *              &res_inst_1,
             *              &res_inst_2};
             * sdm_res_t custom_res = {
             *      .res_spec = &custom_res_spec,
             *      .value.res_inst.insts = res_insts,
             *      .value.res_inst.inst_count = 2,
             *      .value.res_inst.max_inst_count = RES_INST_MAX_COUNT};
             * @endcode
             */
            sdm_res_inst_t **insts;
            /** Max allowed number of Instances of this Resource. */
            uint16_t max_inst_count;
            /** Number of the Resource Instances. */
            uint16_t inst_count;
        } res_inst;
        /**
         * For single-instance Resource stores the value of the Resource.
         * If not set, and the resource is writable or readable @ref
         * res_handlers will be used instead.
         */
        sdm_res_value_t *res_value;
    } value;
};

/** A struct defining an Object Instance. */
struct sdm_obj_inst_struct {
    /** Object Instance ID number. */
    fluf_iid_t iid;
    /** Pointer to the array of the Resources. */
    sdm_res_t *resources;
    /** Number of Resources of this Object Instance. */
    uint16_t res_count;
};

/**
 * A struct containing pointers to Object handlers.
 */
typedef struct {
    /**
     * Create an Object Instance. Required for handling LwM2M CREATE operation.
     */
    sdm_inst_create_t *inst_create;
    /**
     * Delete an Object Instance. Required for handling LwM2M DELETE operation.
     */
    sdm_inst_delete_t *inst_delete;
    /**
     * Reset an Object Instance. Required for handling LwM2M WRITE operation in
     * replace mode - if it's not present then this operation will fail.
     */
    sdm_inst_reset_t *inst_reset;

    /**
     * If defined then it's called before any kind of LwM2M operation that
     * involves this Object.
     *
     * Until @ref operation_end call, must not change due to factors other than
     * the data model handler calls.
     */
    sdm_operation_begin_t *operation_begin;
    /**
     * If defined then it's called after any kind of LwM2M operation
     * that modifies the Object. Return value of this handler determines
     * if Object's state is valid or not.
     */
    sdm_operation_validate_t *operation_validate;
    /**
     * If defined then it's called on the end of any kind of LwM2M
     * operation that involves this Object. Gives information about operation
     * result.
     */
    sdm_operation_end_t *operation_end;
} sdm_obj_handlers_t;

/** A struct defining an Object. */
struct sdm_obj_struct {
    /** Object ID number. */
    fluf_oid_t oid;
    /**
     * Object version: a string with static lifetime, containing two digits
     * separated by a dot (for example: "1.1"). If left NULL, LwM2M client will
     * not include the "ver=" attribute in Register and Discover messages, which
     * implies version 1.0.
     */
    const char *version;
    /**
     * Object handlers, if NULL every CREATE and DELETE operation will fail.
     */
    const sdm_obj_handlers_t *obj_handlers;
    /**
     * Pointer to the array of the pointers to the Object Instances.
     * @ref max_allowed_inst_number defined the size of the array.
     * When @ref sdm_add_obj is called, the Instances inside the array must be
     * ordered in ascending order of Object Instance ID number.
     *
     * During LwM2M CREATE and DELETE operations array of Instances will be
     * modified, so if @ref sdm_inst_create_t or @ref sdm_inst_delete_t are
     * defined for the Object @p insts can't points to the const array.
     *
     * Example:
     * @code
     * #define INST_MAX_COUNT 3
     * sdm_obj_inst_t inst_1 = {.iid = 1};
     * sdm_obj_inst_t inst_2 = {.iid = 2};
     * sdm_obj_inst_t *insts[INST_MAX_COUNT] = {&inst_1, &inst_2};
     * sdm_obj_t custom_obj = {
     *              .oid = CUSTOM_OID,
     *              .insts = insts,
     *              .inst_count = 2,
     *              .max_inst_count = INST_MAX_COUNT};
     * @endcode
     */
    sdm_obj_inst_t **insts;
    /** Max allowed number of Instances of this Object. */
    uint16_t max_inst_count;
    /** Number of Instances of this Object. */
    uint16_t inst_count;
    /**
     * Indicates an ongoing operation, if true the user should not modify any
     * field of the Object.
     */
    bool in_transaction;
};

/** REGISTER operation context, do not modify this structure directly. */
typedef struct {
    uint16_t obj_idx;
    uint16_t inst_idx;
    fluf_id_type_t level;
} _sdm_reg_ctx_t;

/** DISCOVER operation context, do not modify this structure directly. */
typedef struct {
    uint16_t ssid;
    uint16_t obj_idx;
    uint16_t inst_idx;
    uint16_t res_idx;
    uint16_t res_inst_idx;
    fluf_id_type_t level;
} _sdm_disc_ctx_t;

/** WRITE operation context, do not modify this structure directly. */
typedef struct {
    fluf_uri_path_t path;
    bool instance_creation_attempted;
} _sdm_write_ctx_t;

/** READ operation context, do not modify this structure directly. */
typedef struct {
    uint16_t inst_idx;
    uint16_t res_idx;
    uint16_t res_inst_idx;
    size_t total_op_count;
    fluf_id_type_t base_level;
    fluf_uri_path_t path;
} _sdm_read_ctx_t;

/** Set of pointers related with operation, do not modify this structure
 * directly. */
typedef struct {
    sdm_obj_t *obj;
    sdm_obj_inst_t *inst;
    sdm_res_t *res;
    sdm_res_inst_t *res_inst;
} _sdm_entity_ptrs_t;

/**
 * Data model context, do not modify this structure directly, its fields are
 * changed during sdm API calls. Initialize it by calling @ref
 * sdm_dm_initialize. Objects can be added using @ref sdm_add_obj and removed
 * with @ref sdm_remove_obj.
 */
typedef struct {
    sdm_obj_t **objs;
    uint16_t objs_count;
    uint16_t max_allowed_objs_number;

    union {
        _sdm_reg_ctx_t reg_ctx;
        _sdm_disc_ctx_t disc_ctx;
        _sdm_write_ctx_t write_ctx;
        _sdm_read_ctx_t read_ctx;
    } op_ctx;
    _sdm_entity_ptrs_t entity_ptrs;
    int result;
    bool boostrap_operation;
    bool is_transactional;
    size_t op_count;
    bool op_in_progress;
    fluf_op_t operation;
} sdm_data_model_t;

/**
 * Assigns @p objs_array to @p dm. Every @ref sdm_add_obj call will add Object
 * to @p objs_array until @p objs_array_size is reached. @p objs_array must not
 * contain any objects.
 *
 * @param dm              Data model.
 * @param objs_array      Pointer to the array of the pointers to the Objects.
 * @param objs_array_size Determines maximum allowed number of Objects.
 */
void sdm_initialize(sdm_data_model_t *dm,
                    sdm_obj_t **objs_array,
                    uint16_t objs_array_size);

/**
 * Add Object to the data model and validates it. Remember that resources and
 * instances have to be stored in ascending order due to the ID value.
 *
 * @param dm   Data model to which @p obj will be added.
 * @param obj  Pointer to the Object definition struct.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int sdm_add_obj(sdm_data_model_t *dm, sdm_obj_t *obj);

/**
 * Removes Object from the data model.
 *
 * @param dm   Data model from which @p obj will be removed.
 * @param oid  ID number of the Object to be removed.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int sdm_remove_obj(sdm_data_model_t *dm, fluf_oid_t oid);

/**
 * Removes Instance with given @p iid from Object. The function will remove an
 * instance from the <c>insts array</c> set its <c>iid</c> to @ref
 * FLUF_ID_INVALID, update <c>inst_count</c> and reorganize the indexes of the
 * remaining instances.
 *
 * @param obj  Pointer to the Object definition struct.
 * @param iid  ID number of the Instance to be removed.
 *
 * @returns 0 on success, a negative value if the Instance with given @p iid
 *          does not exist.
 */
int sdm_remove_obj_inst(sdm_obj_t *obj, fluf_iid_t iid);

#ifdef __cplusplus
}
#endif

#endif // SDM_IO_H
