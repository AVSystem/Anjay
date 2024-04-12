/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef ANJAY_DM_DM_IO_H
#define ANJAY_DM_DM_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>

#include <anj/anj_config.h>

typedef struct dm_list_ctx_struct dm_list_ctx_t;
typedef struct dm_resource_list_ctx_struct dm_resource_list_ctx_t;
typedef struct dm_output_ctx_struct dm_output_ctx_t;
typedef struct dm_input_ctx_struct dm_input_ctx_t;
typedef struct dm_execute_ctx_struct dm_execute_ctx_t;
typedef struct dm_register_ctx_struct dm_register_ctx_t;
typedef struct dm_discover_ctx_struct dm_discover_ctx_t;

/**
 * Returns a blob of data from the data model handler.
 *
 * Note: this should be used only for small, self-contained chunks of data.
 * See @ref dm_ret_external_bytes documentation for a recommended method of
 * returning large data blobs.
 *
 * WARNING: The data behind the pointer is not being copied. The pointer is
 * passed verbatim to @ref dm_output_ctx_cb_t callback implemented by the
 * user and it must stay valid until that time. User is free to either copy
 * the data immediately and discard/free the pointer, or take ownership of it,
 * which extends the expected lifetime of the pointer further.
 *
 * @param ctx    Context to operate on.
 * @param data   Data buffer.
 * @param data_len Number of bytes available in the @p data buffer.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_ret_bytes(dm_output_ctx_t *ctx, void *data, size_t data_len);

/**
 * Returns a null-terminated string from the data model handler.
 *
 * Note: this should be used only for small, self-contained strings.
 * See @ref dm_ret_external_string documentation for a recommended method of
 * returning large strings.
 *
 * WARNING: The data behind the pointer is not being copied. The pointer is
 * passed verbatim to @ref dm_output_ctx_cb_t callback implemented by the
 * user and it must stay valid until that time. User is free to either copy
 * the data immediately and discard/free the pointer, or take ownership of it,
 * which extends the expected lifetime of the pointer further.
 *
 * @param ctx   Output context to operate on.
 * @param value Null-terminated string to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_ret_string(dm_output_ctx_t *ctx, char *value);

/**
 * Returns a blob of data from the data model handler.
 *
 * NOTE: The difference between this function and @ref dm_ret_bytes is that
 * this function allows the data model handler to return large data blobs which
 * is realized in multiple chunks AND by user callback that allows the data to
 * be not self-contained (i.e. it may be stored in a file, or in a database,
 * external memory, etc.).
 *
 * This method uses a @ref fluf_get_external_data_t callback for multiple
 * calls to get the data. The callback is called until the whole data is
 * returned. The callback is called with the same @p user_args pointer as passed
 * to this function.
 *
 * WARNING: <c>get_external_data</c> callback is passed to @ref
 * dm_output_ctx_cb_t callback and it's user's responsibility to ensure that any
 * context required by <c>get_external_data</c> (including <c>user_args</c>
 * pointers) remains valid until the last call to it.
 *
 * @param ctx               Output context to operate on.
 * @param get_external_data Callback to get the data.
 * @param user_args         User arguments passed to the callback.
 * @param length            Length of the data to be returned.
 *
 * @return 0 in case of success, negative value in case of error.
 */
int dm_ret_external_bytes(dm_output_ctx_t *ctx,
                          fluf_get_external_data_t *get_external_data,
                          void *user_args,
                          size_t length);

/**
 * Returns a null-terminated string from the data model handler.
 *
 * NOTE: The difference between this function and @ref dm_ret_string is that
 * this function allows the data model handler to return large strings which
 * is realized in multiple chunks AND by user callback thst allows the data to
 * be not self-contained (i.e. it may be stored in a file, or in a database,
 * external memory, etc.).
 *
 * This method uses a @ref fluf_get_external_data_t callback for multiple
 * calls to get the data. The callback is called until the whole data is
 * returned. The callback is called with the same @p user_args pointer as passed
 * to this function.
 *
 * WARNING: <c>get_external_data</c> callback is passed to @ref
 * dm_output_ctx_cb_t callback and it's user's responsibility to ensure that any
 * context required by <c>get_external_data</c> (including <c>user_args</c>
 * pointers) remains valid until the last call to it.
 *
 * @param ctx               Output context to operate on.
 * @param get_external_data Callback to get the data.
 * @param user_args         User arguments passed to the callback.
 * @param length            Length of the data to be returned.
 *
 * @return 0 in case of success, negative value in case of error.
 */
int dm_ret_external_string(dm_output_ctx_t *ctx,
                           fluf_get_external_data_t *get_external_data,
                           void *user_args,
                           size_t length);

/**
 * Returns a 64-bit signed integer from the data model handler.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_ret_i64(dm_output_ctx_t *ctx, int64_t value);

/**
 * Returns a 64-bit floating-point value from the data model handler.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_ret_double(dm_output_ctx_t *ctx, double value);

/**
 * Returns a boolean value from the data model handler.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_ret_bool(dm_output_ctx_t *ctx, bool value);

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
int dm_ret_objlnk(dm_output_ctx_t *ctx, fluf_oid_t oid, fluf_iid_t iid);

/**
 * Returns a 64-bit unsigned integer from the data model handler.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_ret_u64(dm_output_ctx_t *ctx, uint64_t value);

/**
 * Returns a time value from the data model handler. It is 64-bit signed integer
 * Unix Time, representing the number of seconds since Jan 1st, 1970 in the UTC
 * time zone.
 *
 * @param ctx   Output context to operate on.
 * @param value The value to return.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_ret_time(dm_output_ctx_t *ctx, uint64_t time);

/**
 * Reads a chunk of data blob from the @ref dm_write operation.
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
 *     if (dm_get_bytes(ctx, &bytes_read, &finished, buf, sizeof(buf))
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
int dm_get_bytes(dm_input_ctx_t *ctx,
                 size_t *out_bytes_read,
                 bool *out_message_finished,
                 void *out_buf,
                 size_t buf_size);

#define DM_BUFFER_TOO_SHORT 1
/**
 * Reads a null-terminated string from @ref dm_write operation. On success or
 * even when @ref DM_BUFFER_TOO_SHORT is returned, the content inside @p out_buf
 * is always null-terminated. On failure, the contents of @p out_buf are
 * undefined.
 *
 * When the input buffer is not big enough to contain whole message content +
 * terminating nullbyte, DM_BUFFER_TOO_SHORT is returned, after which further
 * calls can be made, to retrieve more data.
 *
 * @param      ctx                  Input context to operate on.
 * @param[out] out_buf              Buffer to read data into.
 * @param      buf_size             Number of bytes available in @p out_buf .
 *                                  Must be at least 1.
 *
 * @returns 0 on success, a negative value in case of error,
 *          @ref DM_BUFFER_TOO_SHORT if the buffer is not big enough to
 *          contain whole message content + terminating nullbyte.
 */
int dm_get_string(dm_input_ctx_t *ctx, char *out_buf, size_t buf_size);

/**
 * Reads a chunk of data blob from the @ref dm_write operation. The data is
 * returned by calling the @p fluf_get_external_data_t callback.
 *
 * The difference between this function and @ref dm_get_bytes is that this
 * function allows to read from @ref dm_write operation large data blobs which
 * is realized in multiple chunks AND could be stored outside of data model
 * (i.e. in a file, or in a database, external memory, etc.).
 *
 * During the call to this function, @param out_get_external_data will be set to
 * the callback function, which then should be called by the user to get the
 * data. The callback is called until the whole data is returned.
 *
 * NOTE: Call to this function will set @param out_user_args which HAS TO be
 * passed to the callback function.
 *
 * @param ctx               Context to operate on.
 * @param out_get_external_data Pointer to which set the internal callback.
 * @param out_user_args         Pointer to arguments to be passed to the
 * callback.
 * @param out_length            Pointer to which set the length of the data to
 * be returned.
 *
 * @return 0 in case of success, negative value in case of error.
 */
int dm_get_external_bytes(dm_input_ctx_t *ctx,
                          fluf_get_external_data_t **out_get_external_data,
                          void **out_user_args,
                          size_t *out_length);

/**
 * Reads a null-terminated string from @ref dm_write operation. The data is
 * returned by calling the @p fluf_get_external_data_t callback.
 *
 * The difference between this function and @ref dm_get_string is that this
 * function allows to read from @ref dm_write operation large strings which
 * is realized in multiple chunks AND could be stored outside of data model
 * (i.e. in a file, or in a database, external memory, etc.).
 *
 * During the call to this function, @param out_get_external_data will be set to
 * the callback function, which then should be called by the user to get the
 * data. The callback is called until the whole data is returned.
 *
 * NOTE: Call to this function will set @param out_user_args which HAS TO be
 * passed to the callback function.
 *
 * @param ctx               Context to operate on.
 * @param out_get_external_data Pointer to which set the internal callback.
 * @param out_user_args         Pointer to arguments to be passed to the
 * callback.
 * @param out_length            Pointer to which set the length of the data to
 * be returned.
 *
 * @return 0 in case of success, negative value in case of error.
 */
int dm_get_external_string(dm_input_ctx_t *ctx,
                           fluf_get_external_data_t **out_get_external_data,
                           void **out_user_args,
                           size_t *out_length);

/**
 * Reads an integer as a 64-bit signed value from the @ref dm_write operation.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_get_i64(dm_input_ctx_t *ctx, int64_t *out);

/**
 * Reads an unsigned integer as a 32-bit unsigned value from the @ref
 * dm_write operation.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_get_u32(dm_input_ctx_t *ctx, uint32_t *out);

/**
 * Reads a floating-point value as a double from the @ref dm_write operation.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_get_double(dm_input_ctx_t *ctx, double *out);

/**
 * Reads a boolean value from the @ref dm_write operation.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_get_bool(dm_input_ctx_t *ctx, bool *out);

/**
 * Reads an object link (Object ID/Object Instance ID pair) from the @ref
 * dm_write operation.
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
int dm_get_objlnk(dm_input_ctx_t *ctx,
                  fluf_oid_t *out_oid,
                  fluf_iid_t *out_iid);

/**
 * Reads an unsigned integer as a 64-bit unsigned value from the @ref dm_write
 * operation.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_get_u64(dm_input_ctx_t *ctx, uint64_t *out);

/**
 * Reads a time value from the @ref dm_write operation. It is 64-bit signed
 * integer Unix Time, representing the number of seconds since Jan 1st, 1970 in
 * the UTC time zone.
 *
 * @param      ctx Input context to operate on.
 * @param[out] out Returned value. If the call is not successful, it is
 *                 guaranteed to be left untouched.
 *
 * @returns 0 on success, a negative value in case of error.
 */
int dm_get_time(dm_input_ctx_t *ctx, int64_t *time);

/**
 * Used to return entries from @ref dm_list_instances_t or
 * @ref dm_list_resource_instances_t .
 *
 * @param ctx Context passed to the iteration handler.
 * @param id  ID of the returned Object Instance or Resource Instance. MUST NOT
 *            be <c>FLUF_ID_INVALID</c> / <c>FLUF_ID_INVALID</c> (65535).
 *
 * This function returns no value. Any errors that may occur are handled
 * internally by the library after the calling handler returns.
 */
void dm_emit(dm_list_ctx_t *ctx, uint16_t id);

/**
 * Kind of a Resource.
 */
typedef enum {
    /**
     * Read-only Single-Instance Resource.
     */
    DM_RES_R,

    /**
     * Write-only Single-Instance Resource.
     */
    DM_RES_W,

    /**
     * Read/Write Single-Instance Resource.
     */
    DM_RES_RW,

    /**
     * Read-only Multiple Instance Resource.
     */
    DM_RES_RM,

    /**
     * Write-only Multiple Instance Resource.
     */
    DM_RES_WM,

    /**
     * Read/Write Multiple Instance Resource.
     */
    DM_RES_RWM,

    /**
     * Executable Resource.
     */
    DM_RES_E
} dm_resource_kind_t;

/**
 * Resource presentness flag.
 */
typedef enum {
    /**
     * Resource that is absent.
     */
    DM_RES_ABSENT = 0,

    /**
     * Resource that is present.
     */
    DM_RES_PRESENT = 1
} dm_resource_presence_t;

/**
 * Used to return Resource entries from @ref dm_list_resources_t .
 *
 * @param ctx      Context passed to the iteration handler.
 * @param rid      ID of the returned Resource. MUST NOT be
 *                 <c>FLUF_ID_INVALID</c> (65535).
 * @param kind     Kind of the returned Resource.
 * @param presence Flag that indicates whether the Resource is PRESENT.
 *
 * This function returns no value. Any errors that may occur are handled
 * internally by the library after the calling handler returns.
 */
void dm_emit_res(dm_resource_list_ctx_t *ctx,
                 fluf_rid_t rid,
                 dm_resource_kind_t kind,
                 dm_resource_presence_t presence);

#endif // ANJAY_DM_DM_IO_H
