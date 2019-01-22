/*
 * Copyright 2017-2019 AVSystem <avsystem@avsystem.com>
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

#ifndef ANJAY_INCLUDE_ANJAY_IO_H
#define ANJAY_INCLUDE_ANJAY_IO_H

#include <anjay/core.h>

#ifdef __cplusplus
extern "C" {
#endif

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
 * Example: file content in an RPC response.
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
 * If a zero-length value is to be returned, it is safe both not to call
 * @ref anjay_ret_bytes_append at all, or to call it any number of times with
 * a <c>length</c> argument equal to zero.
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
 * User not interested in argument value (or interested in ignoring the value
 * after reading some part of it), can safely call this function to skip tail of
 * the value and get next argument or an EOF information.
 *
 * @param ctx           Execute context
 * @param out_arg       Obtained argument id
 * @param out_has_value true if argument has a value, false otherwise
 *
 * @returns 0 on success, negative value in case of an error (described above),
 *          ANJAY_EXECUTE_GET_ARG_END in case of end of message
 *          (in which case out_arg is set to -1, and out_has_value to false)
 */
int anjay_execute_get_next_arg(anjay_execute_ctx_t *ctx,
                               int *out_arg,
                               bool *out_has_value);

/**
 * Attempts to read currently processed argument's value (or part of it).
 * Read data is written as null-terminated string into @p out_buf.
 *
 * Function might return 0 when there is nothing more to read or because
 * argument does not have associated value with it, or because the value has
 * already been read / skipped entirely.
 *
 * If the function returns buf_size-1, then there might be more data to read.
 *
 * Error is reported (as -1 return value) in the following cases:
 * 1. buf_size < 2
 * 2. out_buf is NULL
 * 3. In case of malformed message or when an internal error occurs.
 *    In such cases all data read up to this point should be considered invalid.
 *
 * @param ctx       Execute context
 * @param out_buf   Buffer where read bytes will be stored
 * @param buf_size  Size of the buffer
 *
 * @returns number of bytes read, or a negative value in case of an error
 */
ssize_t anjay_execute_get_arg_value(anjay_execute_ctx_t *ctx,
                                    char *out_buf,
                                    size_t buf_size);

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
 * @returns 0 on success, a negative value in case of error,
 *          ANJAY_BUFFER_TOO_SHORT if the buffer is not big enough to contain
 *          whole message content + terminating nullbyte.
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
                     anjay_oid_t *out_oid,
                     anjay_iid_t *out_iid);

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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ANJAY_INCLUDE_ANJAY_IO_H*/
