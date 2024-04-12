/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_H
#define SDM_H

#include <fluf/fluf.h>
#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>

#include <anj/anj_config.h>
#include <anj/sdm_io.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * There is no more data to read from data model.
 * This value can be returned by:
 *      - @ref sdm_get_read_entry
 *      - @ref sdm_get_composite_read_entry
 *      - @ref sdm_get_register_record
 *      - @ref sdm_get_discover_record
 *      - @ref sdm_get_bootstrap_discover_record
 */
#define SDM_LAST_RECORD 1

/**
 * A group of error codes resulting from incorrect API usage or memory
 * issues. If this occurs, the @ref FLUF_COAP_CODE_INTERNAL_SERVER_ERROR should
 * be returned in response.
 */

/** Invalid input arguments. */
#define SDM_ERR_INPUT_ARG (-1)
/** Not enough space in buffer or array. */
#define SDM_ERR_MEMORY (-2)
/** Invalid call. */
#define SDM_ERR_LOGIC (-3)

/**
 * Must be called at the beginning of each operation on the data model. It is to
 * be called only once, even if the message is divided into several blocks.
 * Data model operations are:
 *      - FLUF_OP_REGISTER,
 *      - FLUF_OP_UPDATE,
 *      - FLUF_OP_DM_READ,
 *      - FLUF_OP_DM_READ_COMP,
 *      - FLUF_OP_DM_DISCOVER,
 *      - FLUF_OP_DM_WRITE_REPLACE,
 *      - FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
 *      - FLUF_OP_DM_WRITE_COMP,
 *      - FLUF_OP_DM_EXECUTE,
 *      - FLUF_OP_DM_CREATE,
 *      - FLUF_OP_DM_DELETE.
 *
 * @param dm                    Data model to operate on.
 * @param operation             Data model operation type.
 * @param is_bootstrap_request  Indicate source of request.
 * @param path                  Path from the request, if not specified should
 *                              be NULL.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_operation_begin(sdm_data_model_t *dm,
                        fluf_op_t operation,
                        bool is_bootstrap_request,
                        const fluf_uri_path_t *path);

/**
 * Called at the end of each operation on the data model. If during
 * operation any function returns error value this function must be called
 * immediately.
 *
 * @param dm Data model to operate on.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_operation_end(sdm_data_model_t *dm);

/**
 * Processes READ and BOOTSTRAP-READ operation. Should be repeatedly called
 * until it returns the @ref SDM_LAST_RECORD. Returns all @ref SDM_RES_R, @ref
 * SDM_RES_RW (and @ref SDM_RES_BS_RW for Bootstrap call) Resources/ Resource
 * Instances from path given in @ref sdm_operation_begin.
 *
 * @param      dm          Data model to operate on.
 * @param[out] out_record  Resource or Resource Instance record, with defined
 *                         type, value and path.
 *
 * @returns
 * - 0 on success,
 * - @ref SDM_LAST_RECORD when last record was read,
 * - a negative value in case of error.
 */
int sdm_get_read_entry(sdm_data_model_t *dm, fluf_io_out_entry_t *out_record);

/**
 * Returns information about the number of Resources and Resource Instances that
 * can be read for the READ operation currently in progress.
 *
 * IMPORTANT: Call this function only after a successful @ref
 * sdm_operation_begin call for @ref FLUF_OP_DM_READ operation.
 * If @p out_res_count is set to <c>0</c>, immediately call @ref
 * sdm_operation_end.
 *
 * @param      dm            Data model to operate on.
 * @param[out] out_res_count Return number of the readable Resources.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_get_readable_res_count(sdm_data_model_t *dm, size_t *out_res_count);

/**
 * Processes READ-COMPOSITE operation. For each record from the request should
 * be called until it returns the @ref SDM_LAST_RECORD. Returns all @ref
 * SDM_RES_R and @ref SDM_RES_RW Resources/ Resource Instances from given @p
 * path.
 *
 * @param dm               Data model to operate on.
 * @param path             Target Object, Object Instance, Resource,
 *                         or Resource Instance path.
 * @param[out] out_record  Resource or Resource Instance record, with defined
 *                         type, value and path.
 *
 * @returns
 * - 0 on success,
 * - @ref SDM_LAST_RECORD when last record was read,
 * - a negative value in case of error.
 */
int sdm_get_composite_read_entry(sdm_data_model_t *dm,
                                 const fluf_uri_path_t *path,
                                 fluf_io_out_entry_t *out_record);

/**
 * Returns information about the number of Resources and Resource Instances that
 * can be read from @p path. Use it in order to process READ-COMPOSITE
 * operation.
 *
 * IMPORTANT: Call this function only after a successful @ref
 * sdm_operation_begin call for @ref FLUF_OP_DM_READ_COMP operation.
 * If @p out_res_count is set to <c>0</c>, immediately call @ref
 * sdm_operation_end or process the next record.
 *
 * @param      dm            Data model to operate on.
 * @param      path          Target uri path.
 * @param[out] out_res_count Return number of the readable Resources.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_get_composite_readable_res_count(sdm_data_model_t *dm,
                                         const fluf_uri_path_t *path,
                                         size_t *out_res_count);

/**
 * Creates a new instance of the object. Call this function only after a
 * successful @ref sdm_operation_begin call for @ref FLUF_OP_DM_CREATE operation
 * and before any @ref sdm_write_entry call.
 *
 * @param dm   Data model to operate on.
 * @param iid  New instance ID. Set to FLUF_ID_INVALID if the value was not
 *             specified in the LwM2M server request, in which case the first
 *             free value will be selected.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_create_object_instance(sdm_data_model_t *dm, fluf_iid_t iid);

/**
 * Adds another record during any kind of WRITE and CREATE operation. Depending
 * on the value of @ref sdm_op_t we specified when calling @ref
 * sdm_operation_begin this function can be used to handle the following
 * operations:
 *      - FLUF_OP_DM_WRITE_REPLACE,
 *      - FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
 *      - FLUF_OP_DM_WRITE_COMP,
 *      - FLUF_OP_DM_CREATE.
 *
 * @param dm      Data model to operate on.
 * @param record  Resource or Resource Instance record, with defined type, value
 *                and path.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_write_entry(sdm_data_model_t *dm, fluf_io_out_entry_t *record);

/**
 * Returns information Resource value type, might be useful when payload format
 * does not contain information about the type of data.
 *
 * IMPORTANT: Call this function only after a successful @ref
 * sdm_operation_begin call, which involved the object which you're accessing.
 *
 * @param      dm        Data model to operate on.
 * @param      path      Resource or Resource Instance path.
 * @param[out] out_type  Resource or Resource Instance value type.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_get_resource_type(sdm_data_model_t *dm,
                          const fluf_uri_path_t *path,
                          fluf_data_type_t *out_type);

/**
 * Processes REGISTER operation. Should be repeatedly called until it returns
 * the @ref SDM_LAST_RECORD. Provides information about Objects and Object
 * Instances of the data model.
 *
 * @param      dm          Data model to operate on.
 * @param[out] out_path    Object or Object Instance path.
 * @param[out] out_version Object version, provided for Object @p out_path, set
 *                         to NULL if not present.
 *
 * @returns
 * - 0 on success,
 * - @ref SDM_LAST_RECORD when last record was read,
 * - a negative value in case of error.
 */
int sdm_get_register_record(sdm_data_model_t *dm,
                            fluf_uri_path_t *out_path,
                            const char **out_version);

/**
 * Processes DISCOVER operation. Should be repeatedly called until it returns
 * the @ref SDM_LAST_RECORD. Provides all elements of the data model
 * included in path specified in @ref sdm_operation_begin.
 *
 * @param      dm          Data model to operate on.
 * @param[out] out_path    Object, Object Instance, Resource or Resource
 *                         Instance path.
 * @param[out] out_version Object version, provided for Object @p out_path, set
 *                         to NULL if not present.
 * @param[out] out_dim     Relevant only if @p out_path is Resource, contains
 *                         the number of the Resource Instances. For
 *                         Single-Instance Resources set to NULL.
 *
 * @returns
 * - 0 on success,
 * - @ref SDM_LAST_RECORD when last record was read,
 * - a negative value in case of error.
 */
int sdm_get_discover_record(sdm_data_model_t *dm,
                            fluf_uri_path_t *out_path,
                            const char **out_version,
                            const uint16_t **out_dim);

/**
 * Processes BOOTSTRAP-DISCOVER operation. Should be repeatedly called until it
 * returns the @ref SDM_LAST_RECORD. Provides all elements of the data model
 * included in path specified in @ref sdm_operation_begin.
 *
 * @param      dm          Data model to operate on.
 * @param[out] out_path    Object, Object Instance, Resource or Resource
 *                         Instance path.
 * @param[out] out_version Object version, provided for Object @p out_path, set
 *                         to NULL if not present.
 * @param[out] out_ssid    Short server ID of Object Instance, relevant for
 *                         Security, OSCORE and Servers Object Instances. Set to
 *                         NULL if not present.
 * @param[out] out_uri     Server URI relevant for Security Object Instances.
 *                         Set to NULL if not present.
 *
 * @returns
 * - 0 on success,
 * - @ref SDM_LAST_RECORD when last record was read,
 * - a negative value in case of error.
 */
int sdm_get_bootstrap_discover_record(sdm_data_model_t *dm,
                                      fluf_uri_path_t *out_path,
                                      const char **out_version,
                                      const uint16_t **ssid,
                                      const char **uri);
/**
 * Processes EXECUTE operation, on the Resource pointed to by path specified in
 * @ref sdm_operation_begin. If there is a payload in the request then pass it
 * through @p execute_arg.
 *
 * @param dm              Data model to operate on.
 * @param execute_arg     Payload provided in EXECUTE request, set to NULL if
 *                        not present in COAP request.
 * @param execute_arg_len Execute payload length.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_execute(sdm_data_model_t *dm,
                const char *execute_arg,
                size_t execute_arg_len);

#ifdef __cplusplus
}
#endif

#endif // SDM_H
