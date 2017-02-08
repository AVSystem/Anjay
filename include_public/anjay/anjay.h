/*
 * Copyright 2017 AVSystem <avsystem@avsystem.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANJAY_INCLUDE_ANJAY_ANJAY_H
#define ANJAY_INCLUDE_ANJAY_ANJAY_H

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <sys/types.h>

#include <avsystem/commons/net.h>
#include <avsystem/commons/list.h>

#ifdef __cplusplus
extern "C" {
#endif

/** LwM2M Enabler Version */
#define ANJAY_SUPPORTED_ENABLER_VERSION "1.0"

/** Anjay object containing all information required for LwM2M communication. */
typedef struct anjay_struct anjay_t;

typedef struct anjay_configuration {
    /** Endpoint name as presented to the LwM2M server. If not set, defaults
     * to ANJAY_DEFAULT_ENDPOINT_NAME. */
    const char *endpoint_name;
    /** UDP port number that all listening sockets will be bound to. It may be
     * left at 0 - in that case, connection with each server will use a freshly
     * generated ephemeral port number. */
    uint16_t udp_listen_port;
    /** DTLS version to use for communication */
    avs_net_ssl_version_t dtls_version;

    /** Maximum size of a single incoming CoAP message. Decreasing this value
     * reduces memory usage, but packets bigger than this value will
     * be dropped. */
    size_t in_buffer_size;

    /** Maximum size of a single outgoing CoAP message. If the message exceeds
     * this size, the library performs the block-wise CoAP transfer
     * ( https://datatracker.ietf.org/doc/draft-ietf-core-block/ ).
     * NOTE: in case of block-wise transfers, this value limits the payload size
     * for a single block, not the size of a whole packet. */
    size_t out_buffer_size;
} anjay_configuration_t;

/**
 * @returns pointer to the string representing current version of the library.
 */
const char *anjay_get_version(void);

/**
 * Creates a new Anjay object.
 *
 * @param config Initial configuration. For details, see
 *               @ref anjay_configuration_t .
 *
 * @returns Created Anjay object on success, NULL in case of error.
 */
anjay_t *anjay_new(const anjay_configuration_t *config);

/**
 * Cleans up all resources and releases the Anjay object.
 *
 * NOTE: It shall be called <strong>before</strong> freeing LwM2M Objects
 * registered within the <c>anjay</c> object.
 *
 * @param anjay Anjay object to delete.
 */
void anjay_delete(anjay_t *anjay);

/**
 * Retrieves a list of sockets used for communication with LwM2M servers.
 * Returned list must not be freed nor modified.
 *
 * Example usage: poll()-based application loop
 *
 * @code
 * struct pollfd poll_fd = { .events = POLLIN, .fd = -1 };
 *
 * while (true) {
 *     AVS_LIST(avs_net_abstract_socket_t*) sockets = anjay_get_sockets(anjay);
 *     if (sockets) {
 *         // assuming there is only one socket
 *         poll_fd.fd = *(const int*)avs_net_socket_get_system(*sockets);
 *     } else {
 *         // sockets not initialized yet
 *         poll_fd.fd = -1;
 *     }
 *     if (poll(&poll_fd, 1, 1000) > 0) {
 *          if (poll_fd.revents & POLLIN) {
 *              if (anjay_serve(anjay, *sockets)) {
 *                  log("anjay_serve failed");
 *              }
 *          }
 *     }
 * }
 * @endcode
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns A list of valid server sockets on success,
 *          NULL when the device is not connected to any server.
 */
AVS_LIST(avs_net_abstract_socket_t *const) anjay_get_sockets(anjay_t *anjay);

/**
 * Reads a message from given @p ready_socket and handles it appropriately.
 *
 * @param anjay        Anjay object to operate on.
 * @param ready_socket A socket to read the message from.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_serve(anjay_t *anjay,
                avs_net_abstract_socket_t *ready_socket);

/** Short Server ID type. */
typedef uint16_t anjay_ssid_t;

/** A constant that may be used in @ref anjay_schedule_registration_update
 * call instead of Short Server ID to send Update messages to all connected
 * servers. */
#define ANJAY_SSID_ANY 0

/** An SSID value reserved by LwM2M to refer to the Bootstrap Server.
 * NOTE: The value of a "Short Server ID" Resource in the Security Object
 * Instance referring to the Bootstrap Server is irrelevant and cannot be used
 * to identify the Bootstrap Server. */
#define ANJAY_SSID_BOOTSTRAP UINT16_MAX

/** Object ID */
typedef uint16_t anjay_oid_t;

/** Object Instance ID */
typedef uint16_t anjay_iid_t;

/** Object Instance ID value reserved by the LwM2M spec */
#define ANJAY_IID_INVALID UINT16_MAX

/** Resource ID */
typedef uint16_t anjay_rid_t;

/** Resource Instance ID */
typedef uint16_t anjay_riid_t;

/** Helper macro used to define ANJAY_ERR_ constants.
 * Generated values are valid CoAP Status Codes encoded as a single byte. */
#define ANJAY_COAP_STATUS(Maj, Min) ((uint8_t) ((Maj << 5) | (Min & 0x1F)))

/** Error values that may be returned from data model handlers. @{ */
#define ANJAY_ERR_BAD_REQUEST                (-ANJAY_COAP_STATUS(4,  0))
#define ANJAY_ERR_UNAUTHORIZED               (-ANJAY_COAP_STATUS(4,  1))
#define ANJAY_ERR_BAD_OPTION                 (-ANJAY_COAP_STATUS(4,  2))
#define ANJAY_ERR_NOT_FOUND                  (-ANJAY_COAP_STATUS(4,  4))
#define ANJAY_ERR_METHOD_NOT_ALLOWED         (-ANJAY_COAP_STATUS(4,  5))
#define ANJAY_ERR_NOT_ACCEPTABLE             (-ANJAY_COAP_STATUS(4,  6))
#define ANJAY_ERR_REQUEST_ENTITY_INCOMPLETE  (-ANJAY_COAP_STATUS(4,  8))
#define ANJAY_ERR_CONFLICT                   (-ANJAY_COAP_STATUS(4,  9))

#define ANJAY_ERR_INTERNAL                   (-ANJAY_COAP_STATUS(5,  0))
#define ANJAY_ERR_NOT_IMPLEMENTED            (-ANJAY_COAP_STATUS(5,  1))
/** @} */

/** Type used to return some content in response to a RPC. */
typedef struct anjay_output_ctx_struct anjay_output_ctx_t;

/** Type used to return a chunked blob of data in response to a RPC. Useful in
 * cases where the application needs to send more data than it can fit in
 * the memory. */
typedef struct anjay_ret_bytes_ctx_struct anjay_ret_bytes_ctx_t;

/**
 * Marks the beginning of raw data returned from the data model handler. Used
 * in conjunction with @ref anjay_ret_bytes_append to return a large blob of
 * data in multiple chunks.
 *
 * Example: file content in a RPC response.
 *
 * @code
 * FILE *file;
 * size_t filesize;
 * // initialize file and filesize
 *
 * anjay_ret_bytes_ctx_t *bytes_ctx = anjay_ret_bytes_begin(ctx, filesize);
 * if (!bytes_ctx) {
 *     // handle error
 * }
 *
 * ssize_t bytes_read;
 * char buffer[1024];
 * while ((bytes_read = fread(buffer, 1, sizeof(buffer), file)) > 0) {
 *     if (anjay_ret_bytes_append(bytes_ctx, buffer, bytes_read)) {
 *         // handle error
 *     }
 * }
 *
 * @endcode
 *
 * @param ctx    Output context to write data into.
 * @param length Size of the data to be written.
 *
 * @returns Output context used to return the data or NULL in case of error.
 */
anjay_ret_bytes_ctx_t *anjay_ret_bytes_begin(anjay_output_ctx_t *ctx,
                                             size_t length);

/**
 * Appends a chunk of the data blob to the response message.
 *
 * Note: total number of bytes returned by multiple consecutive successful calls
 * to this function must be equal to the value passed as the length parameter to
 * @ref anjay_ret_bytes_begin that initialized the @p ctx, otherwise the
 * behavior is undefined.
 *
 * @param ctx    Context to operate on.
 * @param data   Data buffer.
 * @param length Number of bytes available in the @p data buffer.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_bytes_append(anjay_ret_bytes_ctx_t *ctx,
                           const void *data,
                           size_t length);

/**
 * Returns a blob of data from the data model handler.
 *
 * Note: this should be used only for small, self-contained chunks of data.
 * See @ref anjay_ret_bytes_begin documentation for a recommended method of
 * returning large data blobs.
 *
 * @param ctx    Context to operate on.
 * @param data   Data buffer.
 * @param length Number of bytes available in the @p data buffer.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_bytes(anjay_output_ctx_t *ctx, const void *data, size_t length);

/**
 * Returns a null-terminated string from the data model handler.
 *
 * @param ctx   Output context to operate on.
 * @param value Null-terminated string to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_string(anjay_output_ctx_t *ctx, const char *value);

/**
 * Returns a 32-bit signed integer from the data model handler.
 *
 * Note: the only difference between @p anjay_ret_i32 and @p anjay_ret_i64 is
 * the size of the @p value parameter. Actual number of bytes sent on the wire
 * depends on the @p value.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_i32(anjay_output_ctx_t *ctx, int32_t value);

/**
 * Returns a 64-bit signed integer from the data model handler.
 *
 * Note: the only difference between @p anjay_ret_i32 and @p anjay_ret_i64 is
 * the size of the @p value parameter. Actual number of bytes sent on the wire
 * depends on the @p value.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_i64(anjay_output_ctx_t *ctx, int64_t value);

/**
 * Returns a 32-bit floating-point value from the data model handler.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_float(anjay_output_ctx_t *ctx, float value);

/**
 * Returns a 64-bit floating-point value from the data model handler.
 *
 * Note: the @p value will be sent as a 32-bit floating-point value if it is
 * exactly representable as such.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_double(anjay_output_ctx_t *ctx, double value);

/**
 * Returns a boolean value from the data model handler.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_bool(anjay_output_ctx_t *ctx, bool value);

/**
 * Returns a object link (Object ID/Instance ID pair) from the
 * data model handler.
 *
 * @param ctx Output context to operate on.
 * @param oid Object ID part of the link.
 * @param iid Object Instance ID part of the link.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_objlnk(anjay_output_ctx_t *ctx, anjay_oid_t oid, anjay_iid_t iid);

/**
 * Begins returing an array of values from the data model handler.
 *
 * A single array may contain values of multiple types.
 *
 * Example usage:
 * @code
 * anjay_output_ctx_t *array_ctx = anjay_ret_array_start(ctx);
 * if (!array_ctx
 *         || anjay_ret_array_index(array_ctx, 0)
 *         || anjay_ret_i32(array_ctx, 42)
 *         || anjay_ret_array_index(array_ctx, 1)
 *         || anjay_ret_string(array_ctx, "foo")
 *         || anjay_ret_array_finish(array_ctx)) {
 *     return ANJAY_ERR_INTERNAL;
 * }
 * @endcode
 *
 * Note: the handler does not need to call @p anjay_ret_array_finish on
 * the context returned by this function if any anjay_ret_* call on the array
 * context returns an error value. In that case the library will automatically
 * free all resources allocated by @ref anjay_ret_array_start after the data
 * model handler finishes.
 *
 * @param ctx Output context to operate on.
 *
 * @returns Created array context on success, NULL in case of error.
 */
anjay_output_ctx_t *anjay_ret_array_start(anjay_output_ctx_t *ctx);

/**
 * Assigns an index to the next value returned using one of the
 * anjay_ret_* functions.
 *
 * For an example usage, see @ref anjay_ret_array_start .
 *
 * @param array_ctx Array context to operate on.
 * @param index     Array index to assign.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_array_index(anjay_output_ctx_t *array_ctx, anjay_riid_t index);

/**
 * Finished an array of values returned from the data model and cleans up
 * the @p array_ctx .
 *
 * @param array_ctx Array context to clean up.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_ret_array_finish(anjay_output_ctx_t *array_ctx);

/** Type used to retrieve RPC content. */
typedef struct anjay_input_ctx_struct anjay_input_ctx_t;

#define ANJAY_EXECUTE_GET_ARG_END 1
/** Type used to retrieve execute command. */
typedef struct anjay_execute_ctx_struct anjay_execute_ctx_t;

/**
 * Reads next argument from execute request content.
 *
 * Negative return value indicate an error, which can be caused either by
 * internal error in anjay, or by malformed message. In case of an error all
 * data read up to the point when an error occurs should be considered invalid.
 *
 * User not interested in argument value (or interested in ignoring the value after
 * reading some part of it), can safely call this function to skip tail of the
 * value and get next argument or an EOF information.
 *
 * @param ctx execute context
 * @param out_arg obtained argument id
 * @param out_has_value true if argument has a value, false otherwise
 * @return 0 on success, negative value in case of an error (described above),
 *         ANJAY_EXECUTE_GET_ARG_END in case of end of message
 *         (in which case out_arg is set to -1, and out_has_value to false)
 */
int anjay_execute_get_next_arg(anjay_execute_ctx_t *ctx, int *out_arg,
                               bool *out_has_value);

/**
 * Attempts to read currently processed argument's value (or part of it).
 * Read data is written as null-terminated string into @p out_buf.
 *
 * Function might return 0 when there is nothing more to read or because argument
 * does not have associated value with it, or because the value has already been
 * read / skipeed entirely.
 *
 * If the function returns buf_size-1, then there might be more data to read.
 *
 * Error is reported (as -1 return value) in the following cases:
 * 1. buf_size < 2
 * 2. out_buf is NULL
 * 3. In case of malformed message or when an internal error occurs.
 *    In such cases all data read up to this point should be considered invalid.
 *
 * @param ctx execute context
 * @param out_buf buffer where read bytes will be stored
 * @param buf_size size of the buffer
 * @return number of bytes read, or a negative value in case of an error
 */
ssize_t anjay_execute_get_arg_value(anjay_execute_ctx_t *ctx, char* out_buf,
                                    ssize_t buf_size);

/**
 * Reads a chunk of data blob from the RPC request message.
 *
 * Consecutive calls to this function will return successive chunks of
 * the data blob. Reaching end of the data is signaled by setting the
 * @p out_message_finished flag.
 *
 * A call to this function will always attempt to read as much data as possible.
 *
 * Example: writing a large data blob to file.
 *
 * @code
 * FILE *file;
 * // initialize file
 *
 * bool finished;
 * size_t bytes_read;
 * char buf[1024];
 *
 * do {
 *     if (anjay_get_bytes(ctx, &bytes_read, &finished, buf, sizeof(buf))
 *             || fwrite(buf, 1, bytes_read, file) < bytes_read) {
 *         // handle error
 *     }
 * } while (!finished);
 *
 * @endcode
 *
 * @param      ctx                  Input context to operate on.
 * @param[out] out_bytes_read       Number of bytes read.
 * @param[out] out_message_finished Set to true if there is no more data
 *                                  to read.
 * @param[out] out_buf              Buffer to read data into.
 * @param      buf_size             Number of bytes available in @p out_buf .
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_get_bytes(anjay_input_ctx_t *ctx,
                    size_t *out_bytes_read,
                    bool *out_message_finished,
                    void *out_buf,
                    size_t buf_size);

#define ANJAY_BUFFER_TOO_SHORT 1
/**
 * Reads a null-terminated string from the RPC request content. On success,
 * the content inside @p out_buf is always null-terminated. On failure, the
 * contents of @p out_buf are undefined.
 *
 * When the input buffer is not big enough to contain whole message content +
 * terminating nullbyte, ANJAY_BUFFER_TOO_SHORT is returned, after which further
 * calls can be made, to retrieve more data.
 *
 * @param      ctx                  Input context to operate on.
 * @param[out] out_buf              Buffer to read data into.
 * @param      buf_size             Number of bytes available in @p out_buf .
 *
 * @returns 0 on success, a negative value in case of error, ANJAY_BUFFER_TOO_SHORT
 *          if the buffer is not big enough to contain whole message content +
 *          terminating nullbyte.
 */
int anjay_get_string(anjay_input_ctx_t *ctx, char *out_buf, size_t buf_size);

/**
 * Reads an integer as a 32-bit signed value from the RPC request content.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_get_i32(anjay_input_ctx_t *ctx, int32_t *out);

/**
 * Reads an integer as a 64-bit signed value from the RPC request content.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_get_i64(anjay_input_ctx_t *ctx, int64_t *out);

/**
 * Reads a floating-point value as a float from the RPC request content.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_get_float(anjay_input_ctx_t *ctx, float *out);

/**
 * Reads a floating-point value as a double from the RPC request content.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_get_double(anjay_input_ctx_t *ctx, double *out);

/**
 * Reads a boolean value from the RPC request content.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_get_bool(anjay_input_ctx_t *ctx, bool *out);

/**
 * Reads an object link (Object ID/Object Instance ID pair) from the RPC
 * request content.
 *
 * @param      ctx     Input context to operate on.
 * @param[out] out_oid Object ID part of the returned value.
 * @param[out] out_iid Object Instance ID part of the returned value.
 *
 * @returns 0 on success, a negative value in case of error.
 *
 * In case of error, <c>out_oid</c> and <c>out_iid</c> are guaranteed to be left
 * untouched.
 */
int anjay_get_objlnk(anjay_input_ctx_t *ctx,
                     anjay_oid_t *out_oid, anjay_iid_t *out_iid);

/**
 * Begins reading an array of values (also known as Multiple Resource Instances)
 * from the RPC request content.
 *
 * Example usage:
 * @code
 * anjay_input_ctx_t *array_ctx = anjay_get_array(ctx);
 * if (!array_ctx) {
 *     return ANJAY_ERR_INTERNAL;
 * }
 *
 * anjay_riid_t index;
 * int32_t value;
 * if (anjay_get_array_index(array_ctx, &index)
 *         || anjay_get_i32(array_ctx, &value)) {
 *     return ANJAY_ERR_BAD_REQUEST;
 * }
 *
 * char buffer[256];
 * if (anjay_get_array_index(array_ctx, &index)
 *         || anjay_get_string(array_ctx, buffer, sizeof(buffer))) {
 *     return ANJAY_ERR_BAD_REQUEST;
 * }
 *
 * if (anjay_get_array_index(array_ctx, &index)
 *         || index != ANJAY_GET_INDEX_END) {
 *     return ANJAY_ERR_BAD_REQUEST;
 * }
 *
 * // continue processing the original ctx
 * @endcode
 *
 * WARNING: An RPC request content may contain Resource Instances with repeating
 * identifiers. LwM2M Specification requires Resource Instance ID to uniquely
 * identify the Resource Instance, but it does not specify however, what action
 * should be taken upon receiving such request. Therefore it is up to the user,
 * to decide whether such RPC shall be rejected or accepted in any way.
 *
 * Note: created context does not need to be released.
 *
 * @param ctx Input context to operate on.
 *
 * @returns Created array context on success, NULL in case of error.
 */
anjay_input_ctx_t *anjay_get_array(anjay_input_ctx_t *ctx);

/** A value returned from @ref anjay_get_array_index to indicate end of
 * the array. */
#define ANJAY_GET_INDEX_END 1

/**
 * Reads an index of the next array entry.
 *
 * @param      array_ctx Array context to operate on.
 * @param[out] out_index Index of the next array entry.
 *
 * @returns:
 * - 0 on success,
 * - ANJAY_GET_INDEX_END when there are no more entries in the array,
 * - a negative value in case of error.
 */
int anjay_get_array_index(anjay_input_ctx_t *array_ctx,
                          anjay_riid_t *out_index);

typedef struct anjay_dm_object_def_struct anjay_dm_object_def_t;

/** Object/Object Instance/Resource Attributes */
typedef struct {
    time_t min_period;   //< Minimum Period as defined by LwM2M spec
    time_t max_period;   //< Maximum Period as defined by LwM2M spec
    double greater_than; //< Greater Than attribute as defined by LwM2M spec
    double less_than;    //< Less Than attribute as defined by LwM2M spec
    double step;         //< Step attribute as defined by LwM2M spec
} anjay_dm_attributes_t;

/** A value indicating that the Min/Max Period attribute is not set */
#define ANJAY_ATTRIB_PERIOD_NONE (-1)

/** A value indicating that the Less Than/Greater Than/Step attribute
 * is not set */
#define ANJAY_ATTRIB_VALUE_NONE (NAN)

/** Convenience Object/Object Instance/Resource attributes constant, filled with
 * "attribute not set" values */
extern const anjay_dm_attributes_t ANJAY_DM_ATTRIBS_EMPTY;

/**
 * A handler that returns default attribute values set for the Object.
 *
 * @param      anjay   Anjay object to operate on.
 * @param      obj_ptr Object definition pointer, as passed to
 *                     @ref anjay_register_object .
 * @param      ssid    Short Server ID of the server requesting the RPC.
 * @param[out] out     Attributes struct to be filled by the handler.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_object_read_default_attrs_t(anjay_t *anjay,
                                                 const anjay_dm_object_def_t *const *obj_ptr,
                                                 anjay_ssid_t ssid,
                                                 anjay_dm_attributes_t *out);

/**
 * A handler that sets default attribute values for the Object.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param ssid    Short Server ID of the server requesting the RPC.
 * @param attrs   Attributes struct to be set for the Object.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_object_write_default_attrs_t(anjay_t *anjay,
                                                  const anjay_dm_object_def_t *const *obj_ptr,
                                                  anjay_ssid_t ssid,
                                                  const anjay_dm_attributes_t *attrs);

/**
 * A handler that enumerates all Object Instances for the Object.
 *
 * Example usage:
 * @code
 * static int instance_it_handler(anjay_t *anjay,
 *                                const anjay_dm_object_def_t *const *obj_ptr,
 *                                anjay_iid_t *out,
 *                                void **cookie) {
 *     // assuming there are NUM_INSTANCES Object Instances with consecutive
 *     // Instance IDs, starting with 0
 *
 *     int curr_iid = (int)(intptr_t)*cookie;
 *     if (curr_iid >= NUM_INSTANCES) {
 *         *out = ANJAY_IID_INVALID;
 *     } else {
 *         *out = (anjay_iid_t)curr_iid;
 *         *cookie = (void*)(intptr_t)(curr_iid + 1);
 *     }
 *     return 0;
 * }
 * @endcode
 *
 * @param        anjay   Anjay object to operate on.
 * @param        obj_ptr Object definition pointer, as passed to
 *                       @ref anjay_register_object .
 * @param[out]   out     Instance ID of the next valid Object Instance
 *                       or ANJAY_IID_INVALID when there are no more instances.
 * @param[inout] cookie  Opaque pointer that may be used to store iteration
 *                       state. At first call to the handler it is set to NULL.
 *                       Since the iteration may be stopped at any point, the
 *                       value stored in @p cookie must not require cleanup.
 *                       The implementation is free to invalidate any such
 *                       cookies during a call to
 *                       @ref anjay_dm_instance_remove_t or
 *                       @ref anjay_dm_instance_create_t - so it is legal to use
 *                       the cookie to store e.g. an array index cast to a
 *                       pointer type.
 *
 * During the same call to Anjay library from top-level user code (e.g.
 * @ref anjay_sched_run or @ref anjay_serve), subsequent or concurrent
 * iterations on the same Object MUST return the Instance IDs in the same order,
 * provided that the set of existing Instances did not change in between.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_it_t(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t *out,
                                   void **cookie);

/**
 * Convenience function to use as the instance_it handler in Single Instance
 * objects.
 *
 * Implements a valid iteration that returns a single Instance ID: 0.
 */
int anjay_dm_instance_it_SINGLE(anjay_t *anjay,
                                const anjay_dm_object_def_t *const *obj_ptr,
                                anjay_iid_t *out,
                                void **cookie);

/**
 * A handler that checks if an Object Instance with given Instance ID exists.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Checked Object Instance ID.
 *
 * @returns This handler should return:
 * - 1 if the Object Instance exists,
 * - 0 if the Object Instance does not exist,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_present_t(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_iid_t iid);

/**
 * Convenience function to use as the instance_present handler in Single
 * Instance objects.
 *
 * @returns 1 (true) if <c>iid == 0</c>, 0 (false) otherwise.
 */
int anjay_dm_instance_present_SINGLE(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_iid_t iid);

/**
 * A handler that shall reset Object Instance to its default (after creational)
 * state.
 *
 * Note: if this handler is not implemented, then non-partial write on the
 * Object Instance (@p iid) will not succeed.
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @param iid       Instance ID to reset.
 *
 * @return This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_reset_t(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr,
                                      anjay_iid_t iid);

/**
 * A handler that removes an Object Instance with given Instance ID.
 *
 * The library will not attempt to interleave calls to
 * @ref anjay_dm_instance_it_t with deleting instances, so the implementation
 * is free to invalidate any iteration cookies during instance removal.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Checked Object Instance ID.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_remove_t(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t iid);

/**
 * A handler that creates an Object Instance.
 *
 * The library will not attempt to interleave calls to
 * @ref anjay_dm_instance_it_t with creating instances, so the implementation
 * is free to invalidate any iteration cookies during instance creation.
 *
 * @param        anjay     Anjay object to operate on.
 * @param        obj_ptr   Object definition pointer, as passed to
 *                         @ref anjay_register_object .
 * @param[inout] inout_iid Instance ID to create if it was specified by the
 *                         server or @ref ANJAY_IID_INVALID otherwise.
 *                         The handler should set this value to an ID of the
 *                         created Object Instance.
 * @param        ssid      Short Server ID of the Object Instance creator.
 *                         @ref ANJAY_SSID_BOOTSTRAP may be used when an
 *                         instance is created by the Bootstrap Write request.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_create_t(anjay_t *anjay,
                                       const anjay_dm_object_def_t *const *obj_ptr,
                                       anjay_iid_t *inout_iid,
                                       anjay_ssid_t ssid);

/**
 * A handler that returns default attributes set for the Object Instance.
 *
 * @param      anjay   Anjay object to operate on.
 * @param      obj_ptr Object definition pointer, as passed to
 *                     @ref anjay_register_object .
 * @param      iid     Checked Object Instance ID.
 * @param      ssid    Short Server ID of the server requesting the RPC.
 * @param[out] out     Returned attributes.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_read_default_attrs_t(anjay_t *anjay,
                                                   const anjay_dm_object_def_t *const *obj_ptr,
                                                   anjay_iid_t iid,
                                                   anjay_ssid_t ssid,
                                                   anjay_dm_attributes_t *out);

/**
 * A handler that sets default attributes for the Object Instance.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Checked Object Instance ID.
 * @param ssid    Short Server ID of the server requesting the RPC.
 * @param attrs   Attributes to set for the Object Instance.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_instance_write_default_attrs_t(anjay_t *anjay,
                                                    const anjay_dm_object_def_t *const *obj_ptr,
                                                    anjay_iid_t iid,
                                                    anjay_ssid_t ssid,
                                                    const anjay_dm_attributes_t *attrs);

/**
 * A handler that checks if a Resource has been instantiated in Object Instance,
 * called only if Resource is SUPPORTED (see @ref anjay_dm_resource_supported_t).
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Checked Instance ID.
 * @param rid     Checked Resource ID.
 *
 * @returns This handler should return:
 * - 1 if the Resource is supported,
 * - 0 if the Resource is not supported,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_resource_present_t(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid);

/**
 * Convenience function to use as the resource_present handler in objects that
 * implement all possible Resources.
 *
 * @returns Always 1.
 */
int anjay_dm_resource_present_TRUE(anjay_t *anjay,
                                   const anjay_dm_object_def_t *const *obj_ptr,
                                   anjay_iid_t iid,
                                   anjay_rid_t rid);

/**
 * A handler that checks if a Resource is supported by the Object (@p obj_ptr).
 *
 * @param anjay     Anjay object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @param rid       Checked Resource ID.
 *
 * @returns This handler should return:
 * - 1 if the Resource is supported,
 * - 0 if the Resource is not supported,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_resource_supported_t(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr,
                                          anjay_rid_t rid);

typedef enum {
    ANJAY_DM_RESOURCE_OP_BIT_R = (1 << 0),
    ANJAY_DM_RESOURCE_OP_BIT_W = (1 << 1),
    ANJAY_DM_RESOURCE_OP_BIT_E = (1 << 2),
} anjay_dm_resource_op_bit_t;

typedef uint16_t anjay_dm_resource_op_mask_t;

#define ANJAY_DM_RESOURCE_OP_NONE ((anjay_dm_resource_op_mask_t) 0)

/**
 * A handler that returns supported non-Bootstrap operations by the client's
 * implementation for a specified Resource.
 *
 * Note: If not implemented, all operations on resource are supported.
 *
 * @param anjay     Anjay object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @param rid       Resource being queried.
 * @param out       Combination of @ref anjay_dm_resource_op_bit_t or
 *                  @ref ANJAY_DM_RESOURCE_OP_NONE if no operation is supported.
 * @return This handler should return:
 * - 0 if it can be queried
 * - a negative value if for some reason resource cannot be queried. If it
 *   returns one of ANJAY_ERR_* constants, the response message will have an
 *   appropriate CoAP response code. Otherwise, the device will respond with
 *   an unspecified (but valid) error code.
 */
typedef int
anjay_dm_resource_operations_t(anjay_t *anjay,
                               const anjay_dm_object_def_t *const *obj_ptr,
                               anjay_rid_t rid,
                               anjay_dm_resource_op_mask_t *out);

/**
 * Convenience function to use as the resource_support handler in objects that
 * implement all possible Resources.
 *
 * @returns Always 1.
 */
int anjay_dm_resource_supported_TRUE(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_rid_t rid);

/**
 * A handler that reads the Resource value, called only if the Resource is
 * PRESENT (see @ref anjay_dm_resource_present_t).
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Output context to write the resource value to using the
 *                anjay_ret_* function family.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, it will be used as a hint for the CoAP response code to use. The
 *   library may decide to override the returned value in case of a more
 *   specific internal error (e.g. 4.06 Not Acceptable in response to an invalid
 *   Accept option).
 *
 *   Note that the CoAP response sent by the library will always be valid.
 *   If the value returned is a negative number that is not any of the
 *   ANJAY_ERR_ constant, the normal fallback response is
 *   5.00 Internal Server Error.
 */
typedef int anjay_dm_resource_read_t(anjay_t *anjay,
                                     const anjay_dm_object_def_t *const *obj_ptr,
                                     anjay_iid_t iid,
                                     anjay_rid_t rid,
                                     anjay_output_ctx_t *ctx);

/**
 * A handler that writes the Resource value.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Input context to read the resource value from using the
 *                anjay_get_* function family.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_resource_write_t(anjay_t *anjay,
                                      const anjay_dm_object_def_t *const *obj_ptr,
                                      anjay_iid_t iid,
                                      anjay_rid_t rid,
                                      anjay_input_ctx_t *ctx);

/**
 * A handler that performs the Execute action on given Resource.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ctx     Execute context to read the execution arguments from, using the
 *                anjay_execute_get_* function family.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_resource_execute_t(anjay_t *anjay,
                                        const anjay_dm_object_def_t *const *obj_ptr,
                                        anjay_iid_t iid,
                                        anjay_rid_t rid,
                                        anjay_execute_ctx_t *ctx);

#define ANJAY_DM_DIM_INVALID ANJAY_ERR_NOT_IMPLEMENTED

/**
 * A handler that returns number of instances in a Multiple Resource.
 *
 * @param      anjay   Anjay object to operate on.
 * @param      obj_ptr Object definition pointer, as passed to
 *                     @ref anjay_register_object .
 * @param      iid     Object Instance ID.
 * @param      rid     Resource ID.
 *
 * @returns This handler should return:
 *
 * - @ref ANJAY_DM_DIM_INVALID if the queried Resource is not a
 *   Multiple Resource, or querying its size is not supported,
 * - a non-negative value equal to the number of instances on success,
 * - a negative value in case of a fatal error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_resource_dim_t(anjay_t *anjay,
                                    const anjay_dm_object_def_t *const *obj_ptr,
                                    anjay_iid_t iid,
                                    anjay_rid_t rid);

/**
 * A handler that returns Resource attributes.
 *
 * @param      anjay   Anjay object to operate on.
 * @param      obj_ptr Object definition pointer, as passed to
 *                     @ref anjay_register_object .
 * @param      iid     Object Instance ID.
 * @param      rid     Resource ID.
 * @param      ssid    Short Server ID of the LwM2M Server issuing the request.
 * @param[out] out     Returned Resource attributes.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_resource_read_attrs_t(anjay_t *anjay,
                                           const anjay_dm_object_def_t *const *obj_ptr,
                                           anjay_iid_t iid,
                                           anjay_rid_t rid,
                                           anjay_ssid_t ssid,
                                           anjay_dm_attributes_t *out);

/**
 * A handler that sets attributes for given Resource.
 *
 * @param anjay   Anjay object to operate on.
 * @param obj_ptr Object definition pointer, as passed to
 *                @ref anjay_register_object .
 * @param iid     Object Instance ID.
 * @param rid     Resource ID.
 * @param ssid    Short Server ID of the LwM2M Server issuing the request.
 * @param attrs   Attributes to set for this Resource.
 *
 * @returns This handler should return:
 * - 0 on success,
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_resource_write_attrs_t(anjay_t *anjay,
                                            const anjay_dm_object_def_t *const *obj_ptr,
                                            anjay_iid_t iid,
                                            anjay_rid_t rid,
                                            anjay_ssid_t ssid,
                                            const anjay_dm_attributes_t *attrs);

/**
 * A handler that is called while registering an object with @ref anjay_register_object,
 * it may be used to perform additional registration tasks.
 *
 * @param anjay     Anjay object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @return 0 on success, negative value on error in which case the object
 *         @p obj_ptr will not be registered.
 */
typedef int anjay_dm_object_on_register_t(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr);

/**
 * A handler that is called when there is a request that might modify an Object
 * and fail. Such situation often requires to rollback changes, and this handler
 * shall implement logic that prepares for possible failure in the future.
 *
 * Handlers listed below are NOT called without begining transaction in the
 * first place (note that if an Object does not implement transaction handlers,
 * then it will not be possible to perform operations listed below):
 *  - @ref anjay_dm_instance_create_t
 *  - @ref anjay_dm_instance_remove_t
 *  - @ref anjay_dm_instance_reset_t
 *  - @ref anjay_dm_resource_write_t
 *  - @ref anjay_dm_transaction_commit_t
 *  - @ref anjay_dm_transaction_rollback_t
 *
 * Note: if an error occurs during a transaction (i.e. after successful call of
 * this function) then the rollback handler @ref anjay_dm_transaction_rollback_t
 * will be executed by the library.
 *
 * @param anjay     Anjay object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 *
 * @return
 * - 0 on success
 * - a negative value in case of error
 */
typedef int anjay_dm_transaction_begin_t(anjay_t *anjay,
                                         const anjay_dm_object_def_t *const *obj_ptr);

/**
 * A handler that is called after transaction is finished, but before
 * @ref anjay_dm_transaction_commit_t is called. It is used to check whether the
 * commit operation may be successfully performed.
 *
 * Any validation of the object's state shall be performed in this function,
 * rather than in the commit handler. If there is a need to commit changes to
 * multiple objects at once, this handler is called on all modified objects
 * first, to avoid potential inconsistencies that may arise from a failing
 * commit operation.
 *
 * Returning success from this handler means that the corresponding commit
 * function shall subsequently execute successfully. The commit handler may
 * nevertheless fail, but if and only if a fatal, unpredictable and
 * irrecoverable error (e.g. physical write error) occurs.
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @return
 * - 0 on success
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_transaction_validate_t(anjay_t *anjay,
                                            const anjay_dm_object_def_t *const *obj_ptr);

/**
 * A handler that is called after transaction is finished. If it fails then
 * @ref anjay_dm_transaction_rollback_t handler must be called by the user
 * code if it is necessary.
 *
 * NOTE: If this function fails, the data model will be left in an inconsistent
 * state. For this reason, it may return an error value if and only if a fatal,
 * unpredictable and irrecoverable error (e.g. physical write error) occurs.
 * All other errors (such as invalid object state) shall be reported via
 * @ref anjay_dm_transaction_validate_t .
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @return
 * - 0 on success
 * - a negative value in case of error. If it returns one of ANJAY_ERR_
 *   constants, the response message will have an appropriate CoAP response
 *   code. Otherwise, the device will respond with an unspecified (but valid)
 *   error code.
 */
typedef int anjay_dm_transaction_commit_t(anjay_t *anjay,
                                          const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Stub handler that can be substituted for any transaction operation. Does
 * nothing. It is <strong>NOT</strong> recommended for production usage.
 *
 * @return always 0
 */
int anjay_dm_transaction_NOOP(anjay_t *anjay,
                              const anjay_dm_object_def_t *const *obj_ptr);

/**
 * A handler that is called whenever there is a need to restore previous
 * Object state during a transaction or during commiting a transaction.
 *
 * @param anjay     Anjay Object to operate on.
 * @param obj_ptr   Object definition pointer, as passed to
 *                  @ref anjay_register_object .
 * @return
 * - 0 on success
 * - a negative value in case of error.
 */
typedef int anjay_dm_transaction_rollback_t(anjay_t *anjay,
                                            const anjay_dm_object_def_t *const *obj_ptr);

/** A struct defining an LwM2M Object and available operations. */
struct anjay_dm_object_def_struct {
    /** Object ID */
    anjay_oid_t oid;

    /** Smallest Resource ID that is invalid for this Object. All requests to
     * Resources with ID = @ref anjay_dm_object_def_struct#rid_bound
     * or bigger are discarded without calling the
     * @ref anjay_dm_object_def_struct#resource_present handler. */
    anjay_rid_t rid_bound;

    /** Get default Object attributes, @ref anjay_dm_object_read_default_attrs_t */
    anjay_dm_object_read_default_attrs_t *object_read_default_attrs;
    /** Set default Object attributes, @ref anjay_dm_object_write_default_attrs_t */
    anjay_dm_object_write_default_attrs_t *object_write_default_attrs;

    /** Enumerate available Object Instances, @ref anjay_dm_instance_it_t */
    anjay_dm_instance_it_t *instance_it;
    /** Check if an Object Instance exists, @ref anjay_dm_instance_present_t */
    anjay_dm_instance_present_t *instance_present;

    /** Resets an Object Instance, @ref anjay_dm_instance_reset_t */
    anjay_dm_instance_reset_t *instance_reset;
    /** Create an Object Instance, @ref anjay_dm_instance_create_t */
    anjay_dm_instance_create_t *instance_create;
    /** Delete an Object Instance, @ref anjay_dm_instance_remove_t */
    anjay_dm_instance_remove_t *instance_remove;

    /** Get default Object Instance attributes, @ref anjay_dm_instance_read_default_attrs_t */
    anjay_dm_instance_read_default_attrs_t *instance_read_default_attrs;
    /** Set default Object Instance attributes, @ref anjay_dm_instance_write_default_attrs_t */
    anjay_dm_instance_write_default_attrs_t *instance_write_default_attrs;

    /** Check if a Resource is present in given Object Instance, @ref anjay_dm_resource_present_t */
    anjay_dm_resource_present_t *resource_present;
    /** Check if a Resource is supported in given Object, @ref anjay_dm_resource_supported_t */
    anjay_dm_resource_supported_t *resource_supported;
    /** Returns a mask of supported operations on a given Resource, @ref anjay_dm_resource_operations_t */
    anjay_dm_resource_operations_t *resource_operations;

    /** Get Resource value, @ref anjay_dm_resource_read_t */
    anjay_dm_resource_read_t *resource_read;
    /** Set Resource value, @ref anjay_dm_resource_write_t */
    anjay_dm_resource_write_t *resource_write;
    /** Perform Execute action on a Resource, @ref anjay_dm_resource_execute_t */
    anjay_dm_resource_execute_t *resource_execute;

    /** Get number of Multiple Resource instances, @ref anjay_dm_resource_dim_t */
    anjay_dm_resource_dim_t *resource_dim;
    /** Get Resource attributes, @ref anjay_dm_resource_read_attrs_t */
    anjay_dm_resource_read_attrs_t *resource_read_attrs;
    /** Set Resource attributes, @ref anjay_dm_resource_write_attrs_t */
    anjay_dm_resource_write_attrs_t *resource_write_attrs;

    /** Perform additional registration operations, @ref anjay_dm_object_on_register_t */
    anjay_dm_object_on_register_t *on_register;

    /** Begin a transaction on this Object, @ref anjay_dm_transaction_begin_t */
    anjay_dm_transaction_begin_t *transaction_begin;
    /** Validate whether a transaction on this Object can be cleanly committed. See @ref anjay_dm_transaction_validate_t */
    anjay_dm_transaction_validate_t *transaction_validate;
    /** Commit changes made in a transaction, @ref anjay_dm_transaction_commit_t */
    anjay_dm_transaction_commit_t *transaction_commit;
    /** Rollback changes made in a transaction, @ref anjay_dm_transaction_rollback_t */
    anjay_dm_transaction_rollback_t *transaction_rollback;
};

/**
 * Notifies the library that the value of given Resource changed. It may trigger
 * an LwM2M Notify message, update server connections and perform other tasks,
 * as required for the specified Resource.
 *
 * Needs to be called for any Resource after its value is changed by means other
 * than LwM2M.
 *
 * Note that it should not be called after a Write performed by the LwM2M
 * server.
 *
 * @param anjay Anjay object to operate on.
 * @param oid   Object ID of the changed Resource.
 * @param iid   Object Instance ID of the changed Resource.
 * @param rid   Resource ID of the changed Resource.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_notify_changed(anjay_t *anjay,
                         anjay_oid_t oid,
                         anjay_iid_t iid,
                         anjay_rid_t rid);

/**
 * Notifies the library that the set of Instances existing in a given Object
 * changed. It may trigger an LwM2M Notify message, update server connections
 * and perform other tasks, as required for the specified Object ID.
 *
 * Needs to be called for each Object, after an Instance is created or removed
 * by means other than LwM2M.
 *
 * Note that it should not be called after a Create or Delete performed by the
 * LwM2M server.
 *
 * @param anjay Anjay object to operate on.
 * @param oid   Object ID of the changed Object.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_notify_instances_changed(anjay_t *anjay, anjay_oid_t oid);

/**
 * Determines time of next scheduled task.
 *
 * May be used to determine how long the device may wait before calling
 * @ref anjay_sched_run .
 *
 * @param      anjay     Anjay object to operate on.
 * @param[out] out_delay Relative time from now of next scheduled task.
 *
 * @returns 0 on success, or a negative value if no tasks are scheduled.
 */
int anjay_sched_time_to_next(anjay_t *anjay,
                             struct timespec *out_delay);

/**
 * Determines time of next scheduled task in milliseconds.
 *
 * This function is equivalent to @ref anjay_sched_time_to_next but, as a
 * convenience for users of system calls such as <c>poll()</c>, the result is
 * returned as a single integer number of milliseconds.
 *
 * @param      anjay        Anjay object to operate on.
 * @param[out] out_delay_ms Relative time from now of next scheduled task, in
 *                          milliseconds.
 *
 * @returns 0 on success, or a negative value if no tasks are scheduled.
 */
int anjay_sched_time_to_next_ms(anjay_t *anjay, int *out_delay_ms);

/**
 * Calculates time in milliseconds the client code may wait for incoming events
 * before the need to call @ref anjay_sched_run .
 *
 * This function combines @ref anjay_sched_time_to_next_ms with a user-provided
 * limit, so that a conclusive value will always be returned. It is provided as
 * a convenience for users of system calls such as <c>poll()</c>.
 *
 * @param anjay    Anjay object to operate on.
 * @param limit_ms The longest amount of time the function shall return.
 *
 * @returns Relative time from now of next scheduled task, in milliseconds, if
 *          such task exists and it's scheduled to run earlier than
 *          <c>limit_ms</c> seconds from now, or <c>limit_ms</c> otherwise.
 */
int anjay_sched_calculate_wait_time_ms(anjay_t *anjay, int limit_ms);

/**
 * Runs all scheduled events which need to be invoked at or before the time of
 * this function invocation.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_sched_run(anjay_t *anjay);

/**
 * Registers the Object in the data model, making it available for RPC calls.
 *
 * NOTE: <c>def_ptr</c> MUST stay valid for the entire lifetime of the
 * <c>anjay</c> object, including the call to @ref anjay_delete.
 *
 * @param anjay   Anjay object to operate on.
 * @param def_ptr Pointer to the Object definition struct. The exact value
 *                passed to this function will be forwarded to all data model
 *                handler calls.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_register_object(anjay_t *anjay,
                          const anjay_dm_object_def_t *const *def_ptr);

/**
 * Schedules sending an Update message to the server identified by given
 * Short Server ID.
 *
 * The Update will be sent during the next @ref anjay_sched_run call.
 *
 * Note: This function will not schedule registration update if Anjay is in
 * offline mode.
 *
 * @param anjay              Anjay object to operate on.
 * @param ssid               Short Server ID of the server to send Update to or
 *                           @ref ANJAY_SSID_ANY to send Updates to all
 *                           connected servers.
 *                           NOTE: Since Updates are not useful for the
 *                           Bootstrap Server, this function does not send one
 *                           for @ref ANJAY_SSID_BOOTSTRAP @p ssid .
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_schedule_registration_update(anjay_t *anjay,
                                       anjay_ssid_t ssid);

/**
 * Reconnects sockets associated with all connected servers. Should be called if
 * something related to the device's IP connection has changed.
 *
 * The reconnection will be performed during the next @ref anjay_sched_run call
 * and will trigger Registration Update.
 *
 * Note: This function makes Anjay enter online mode.
 *
 * @param anjay              Anjay object to operate on.
 */
int anjay_schedule_reconnect(anjay_t *anjay);

/**
 * This function shall be called when an LwM2M Server Object shall be disabled.
 * The standard case for this is when Execute is performed on the Disable
 * resource (/1/x/4).
 *
 * The server will be disabled for the period of time determined by the value
 * of the Disable Timeout resource (/1/x/5). The resource is read soon after
 * the invocation of this function (during next @ref anjay_sched_run) and is
 * <strong>not</strong> updated upon any subsequent Writes to that resource.
 *
 * @param anjay Anjay object to operate on.
 * @param ssid  Short Server ID of the server to put in a disabled state.
 *              NOTE: disabling a server requires a Server Object Instance
 *              to be present for given @p ssid . Because the Bootstrap Server
 *              does not have one, this function does nothing when called with
 *              @ref ANJAY_SSID_BOOTSTRAP .
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_disable_server(anjay_t *anjay, anjay_ssid_t ssid);


/**
 * Checks whether anjay is currently in offline state.
 *
 * @param anjay Anjay object to operate on.
 * @returns true if Anjay's instance is offline, false otherwise.
 */
bool anjay_is_offline(anjay_t *anjay);

/**
 * Puts the LwM2M client into offline mode. This should be done when the
 * Internet connection is deemed to be unavailable or lost.
 *
 * During the next call to @ref anjay_sched_run, Anjay will close all of its
 * sockets and stop attempting to make any contact with remote hosts. It will
 * remain in this state until the call to @ref anjay_exit_offline.
 *
 * User code shall still interface normally with the library while in the
 * offline state, which includes regular calls to @ref anjay_sched_run.
 * Notifications (as reported using @ref anjay_notify_changed and
 * @ref anjay_notify_instances_changed) continue to be tracked, and may be sent
 * after reconnecting, depending on values of the "Notification Storing When
 * Disabled or Offline" resource.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_enter_offline(anjay_t *anjay);

/**
 * Exits the offline state entered using the @ref anjay_enter_offline function.
 *
 * During subsequent calls to @ref anjay_sched_run, new connections to all
 * configured LwM2M Servers will be attempted, and Registration Update
 * (or Register, if the registration lifetime passed in the meantime) messages
 * will be sent.
 *
 * @param anjay Anjay object to operate on.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int anjay_exit_offline(anjay_t *anjay);

/**
 * Possible types of the LwM2M server binding mode.
 */
typedef enum {
    ANJAY_BINDING_NONE,
    ANJAY_BINDING_U,
    ANJAY_BINDING_UQ,
    ANJAY_BINDING_S,
    ANJAY_BINDING_SQ,
    ANJAY_BINDING_US,
    ANJAY_BINDING_UQS
} anjay_binding_mode_t;

/**
 * Attempts to parse c-string pointed by @p str as LwM2M Binding Mode. The
 * accepted string representations and their respective values are as follows:
 *
 * +------------------------+----------------------------+
 * | Textual representation | anjay_binding_mode_t value |
 * +------------------------+----------------------------+
 * |          "U"           |       ANJAY_BINDING_U      |
 * +------------------------+----------------------------+
 * |          "S"           |       ANJAY_BINDING_S      |
 * +------------------------+----------------------------+
 * |          "US"          |       ANJAY_BINDING_US     |
 * +------------------------+----------------------------+
 * |          "UQ"          |       ANJAY_BINDING_UQ     |
 * +------------------------+----------------------------+
 * |          "SQ"          |       ANJAY_BINDING_SQ     |
 * +------------------------+----------------------------+
 * |          "UQS"         |       ANJAY_BINDING_UQS    |
 * +------------------------+----------------------------+
 * |      anything else     |       ANJAY_BINDING_NONE   |
 * +------------------------+----------------------------+
 *
 * @returns one of the value of @ref anjay_binding_mode_t enum that matches textual
 * representation in the above table.
 */
anjay_binding_mode_t anjay_binding_mode_from_str(const char *str);

/**
 * Converts binding mode to a textual representation (as in the table in @ref
 * anjay_binding_mode_from_str) .
 *
 * WARNING: returned value MUST NOT be modified or freed in any way.
 *
 * @return NULL if @p binding_mode is @ref ANJAY_BINDING_NONE, otherwise a textual
 * representation as the table shows in @ref anjay_binding_mode_from_str documentation.
 */
const char *anjay_binding_mode_as_str(anjay_binding_mode_t binding_mode);

/**
 * Possible values of the Security Mode Resource, as described in the Security
 * Object definition.
 */
typedef enum {
    ANJAY_UDP_SECURITY_PSK = 0, //< Pre-Shared Key mode
    ANJAY_UDP_SECURITY_RPK = 1, //< Raw Public Key mode
    ANJAY_UDP_SECURITY_CERTIFICATE = 2, //< Certificate mode
    ANJAY_UDP_SECURITY_NOSEC = 3 //< NoSec mode
} anjay_udp_security_mode_t;

#define ANJAY_ACCESS_MASK_READ            (1U << 0)
#define ANJAY_ACCESS_MASK_WRITE           (1U << 1)
#define ANJAY_ACCESS_MASK_EXECUTE         (1U << 2)
#define ANJAY_ACCESS_MASK_DELETE          (1U << 3)
#define ANJAY_ACCESS_MASK_CREATE          (1U << 4)
#define ANJAY_ACCESS_MASK_FULL            (ANJAY_ACCESS_MASK_READ    |  \
                                           ANJAY_ACCESS_MASK_WRITE   |  \
                                           ANJAY_ACCESS_MASK_DELETE  |  \
                                           ANJAY_ACCESS_MASK_EXECUTE |  \
                                           ANJAY_ACCESS_MASK_CREATE)
#define ANJAY_ACCESS_MASK_NONE            0
#define ANJAY_ACCESS_LIST_OWNER_BOOTSTRAP UINT16_MAX

typedef uint16_t anjay_access_mask_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ANJAY_INCLUDE_ANJAY_ANJAY_H*/
