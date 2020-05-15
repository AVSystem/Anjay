/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

/**
 * Type used to return Object Instance or Resource Instance lists.
 */
typedef struct anjay_dm_list_ctx_struct anjay_dm_list_ctx_t;

/**
 * Used to return entries from @ref anjay_dm_list_instances_t or
 * @ref anjay_dm_list_resource_instances_t .
 *
 * @param ctx Context passed to the iteration handler.
 * @param id  ID of the returned Object Instance or Resource Instance. MUST NOT
 *            be <c>ANJAY_ID_INVALID</c> / <c>ANJAY_ID_INVALID</c> (65535).
 *
 * This function returns no value. Any errors that may occur are handled
 * internally by the library after the calling handler returns.
 */
void anjay_dm_emit(anjay_dm_list_ctx_t *ctx, uint16_t id);

/**
 * Type used to return Resource lists.
 */
typedef struct anjay_dm_resource_list_ctx_struct anjay_dm_resource_list_ctx_t;

/**
 * Kind of a Resource.
 */
typedef enum {
    /**
     * Read-only Single-Instance Resource. Bootstrap Server might attempt to
     * write to it anyway.
     */
    ANJAY_DM_RES_R,

    /**
     * Write-only Single-Instance Resource.
     */
    ANJAY_DM_RES_W,

    /**
     * Read/Write Single-Instance Resource.
     */
    ANJAY_DM_RES_RW,

    /**
     * Read-only Multiple Instance Resource. Bootstrap Server might attempt to
     * write to it anyway.
     */
    ANJAY_DM_RES_RM,

    /**
     * Write-only Multiple Instance Resource.
     */
    ANJAY_DM_RES_WM,

    /**
     * Read/Write Multiple Instance Resource.
     */
    ANJAY_DM_RES_RWM,

    /**
     * Executable Resource.
     */
    ANJAY_DM_RES_E,

    /**
     * Resource that can be read/written only by Bootstrap server.
     */
    ANJAY_DM_RES_BS_RW
} anjay_dm_resource_kind_t;

/**
 * Resource presentness flag.
 */
typedef enum {
    /**
     * Resource that is absent (not yet instantiable, but might be instantiated
     * e.g. using a Write operation).
     */
    ANJAY_DM_RES_ABSENT = 0,

    /**
     * Resource that is present.
     */
    ANJAY_DM_RES_PRESENT = 1
} anjay_dm_resource_presence_t;

/**
 * Used to return Resource entries from @ref anjay_dm_list_resources_t .
 *
 * @param ctx      Context passed to the iteration handler.
 * @param rid      ID of the returned Resource. MUST NOT be
 *                 <c>ANJAY_ID_INVALID</c> (65535).
 * @param kind     Kind of the returned Resource.
 * @param presence Flag that indicates whether the Resource is PRESENT.
 *
 * This function returns no value. Any errors that may occur are handled
 * internally by the library after the calling handler returns.
 */
void anjay_dm_emit_res(anjay_dm_resource_list_ctx_t *ctx,
                       anjay_rid_t rid,
                       anjay_dm_resource_kind_t kind,
                       anjay_dm_resource_presence_t presence);

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
 * size_t bytes_read;
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
static inline int anjay_ret_i32(anjay_output_ctx_t *ctx, int32_t value) {
    return anjay_ret_i64(ctx, value);
}

/*
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
 * Returns a 32-bit floating-point value from the data model handler.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
static inline int anjay_ret_float(anjay_output_ctx_t *ctx, float value) {
    return anjay_ret_double(ctx, value);
}

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

/** Type used to retrieve RPC content. */
typedef struct anjay_input_ctx_struct anjay_input_ctx_t;

#define ANJAY_EXECUTE_GET_ARG_END 1
/** Type used to retrieve execute command. */
typedef struct anjay_execute_ctx_struct anjay_execute_ctx_t;

/**
 * Reads next argument from execute request content.
 *
 * Returns @ref ANJAY_ERR_BAD_REQUEST to indicate the message is malformed and
 * user should forward this code as the return value of @ref
 * anjay_dm_resource_execute_t . Arguments are parsed sequentially so not
 * necessarily the first call of this function will return an error. In case of
 * an error all data read up to the point when an error occurs should be
 * considered invalid.
 *
 * User not interested in argument value (or interested in ignoring the value
 * after reading some part of it), can safely call this function to skip tail of
 * the value and get next argument or an EOF information.
 *
 * @param ctx           Execute context
 * @param out_arg       Obtained argument id
 * @param out_has_value true if argument has a value, false otherwise
 *
 * @returns 0 on success, @ref ANJAY_ERR_BAD_REQUEST in case of malformed
 *          message, @ref ANJAY_EXECUTE_GET_ARG_END in case of end of message
 *          (in which case @p out_arg is set to -1, and @p out_has_value to
 *          @c false)
 */
int anjay_execute_get_next_arg(anjay_execute_ctx_t *ctx,
                               int *out_arg,
                               bool *out_has_value);

/**
 * Attempts to read currently processed argument's value (or part of it).
 * Read data is written as null-terminated string into @p out_buf.
 *
 * Returns @ref ANJAY_ERR_BAD_REQUEST to indicate the message is malformed and
 * user should forward this code as the return value of @ref
 * anjay_dm_resource_execute_t .
 *
 * Function might return 0 when there is nothing more to read or because
 * argument does not have associated value with it, or because the value has
 * already been read / skipped entirely.
 *
 * When the output buffer is not big enough to contain whole message content +
 * terminating nullbyte, ANJAY_BUFFER_TOO_SHORT is returned, after which further
 * calls can be made, to retrieve more data.
 *
 * In case of an error following values are returned:
 * - -1 if buf_size < 2 or out_buf is NULL
 * - @ref ANJAY_ERR_BAD_REQUEST in case of malformed message
 *
 * In such cases all data read up to this point should be considered invalid.
 *
 * @param ctx            Execute context
 * @param out_bytes_read Pointer to a variable that, on successful exit, will be
 *                       set to the number of bytes read (not counting the
 *                       terminating null-byte). May be NULL if not needed.
 * @param out_buf        Buffer where read bytes will be stored
 * @param buf_size       Size of the buffer
 *
 * @returns 0 on success, a negative value in case of error,
 *          ANJAY_BUFFER_TOO_SHORT if the buffer is not big enough to contain
 *          whole message content + terminating nullbyte.
 */
int anjay_execute_get_arg_value(anjay_execute_ctx_t *ctx,
                                size_t *out_bytes_read,
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
 * Reads a null-terminated string from the RPC request content. On success or
 * even when @ref ANJAY_BUFFER_TOO_SHORT is returned, the content inside
 * @p out_buf is always null-terminated. On failure, the contents of @p out_buf
 * are undefined.
 *
 * When the input buffer is not big enough to contain whole message content +
 * terminating nullbyte, ANJAY_BUFFER_TOO_SHORT is returned, after which further
 * calls can be made, to retrieve more data.
 *
 * @param      ctx                  Input context to operate on.
 * @param[out] out_buf              Buffer to read data into.
 * @param      buf_size             Number of bytes available in @p out_buf .
 *                                  Must be at least 1.
 *
 * @returns 0 on success, a negative value in case of error,
 *          @ref ANJAY_BUFFER_TOO_SHORT if the buffer is not big enough to
 *          contain whole message content + terminating nullbyte.
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /*ANJAY_INCLUDE_ANJAY_IO_H*/
