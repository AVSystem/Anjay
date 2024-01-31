/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SDM_IMPL_H
#define SDM_IMPL_H

#include <fluf/fluf.h>
#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>

#include <anj/sdm.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The buffer is full, the function must be called again to retrieve the rest
 * of the message. */
#define SDM_IMPL_BLOCK_TRANSFER_NEEDED 1

/**
 * Used for block transfers. All records have been read. Call @ref sdm_process
 * again with new message to continue parsing. If no more data is available,
 * this shall be treated as an error.*/
#define SDM_IMPL_WANT_NEXT_MSG 2

/**
 * SDM implementation context, do not modify its fields.
 */
typedef struct {
    union {
        fluf_io_out_ctx_t out_ctx;
        fluf_io_in_ctx_t in_ctx;
        fluf_io_register_ctx_t register_ctx;
        fluf_io_discover_ctx_t discover_ctx;
        fluf_io_bootstrap_discover_ctx_t bootstrap_discover_ctx;
    } fluf_io;
    bool in_progress;
    bool data_to_copy;
    fluf_op_t op;
    uint32_t block_number;
} sdm_process_ctx_t;

/**
 * Model implementation of <c>SDM API</c> handling. Call it after @ref
 * fluf_msg_decode call. Processes all LwM2M requests related to the data model.
 * For all operations that read data from data model, the read values encoded in
 * the format indicated in @p msg will be written to the @p out_buff. This
 * function is designed to handle block operations (RFC7252). If the request
 * comes in several packets, call it separately for each @p msg. If the response
 * does not fit in the @p out_buff then @ref SDM_IMPL_BLOCK_TRANSFER_NEEDED will
 * be returned, in which case send the response as a single block and call the
 * function again. After message handling @p in_out_msg is used to prepare the
 * answer. If response payload doesn't fit in @p out_buff, block option will be
 * added to the @p in_out_msg.
 *
 * IMPORTANT: In Block-Wise Transfer in CoAP single block size is always
 * power of two. If @p out_buff_len does not meet this condition and the entire
 * payload does not fit in @p out_buff then an error will be returned.
 *
 * IMPORTANT: If for some reason you want to handle data model related requests
 * by yourself, then use @ref sdm_process as a reference. However, in most
 * applications, the use of this function meets all requirements.
 *
 * @param      ctx                      SDM implementation context.
 * @param      dm                       Data model context.
 * @param      in_out_msg               LwM2M Server request or
 *                                      @ref LWM2M_OP_REGISTER message, used for
 *                                      response preparing.
 * @param      is_bootstrap_server_call Indicate source of request.
 * @param[out] out_buff                 Buffer for the data read from the data
 *                                      model.
 * @param      out_buff_len             Length of payload buffer. Must be
 *                                      power of two in order to support
 *                                      block operations.
 *
 * @returns
 * - 0 on success,
 * - @ref SDM_IMPL_BLOCK_TRANSFER_NEEDED if @p out_buff is full and the function
 *   must be called again,
 * - @ref SDM_IMPL_WANT_NEXT_MSG if the next block message is expected,
 * - a negative value in case of error.
 */
int sdm_process(sdm_process_ctx_t *ctx,
                sdm_data_model_t *dm,
                fluf_data_t *in_out_msg,
                bool is_bootstrap_server_call,
                char *out_buff,
                size_t out_buff_len);

/**
 * Can be used to cancel ongoing operation. There are 2 main usage scenarios:
 *  - the lack of support for block operations,
 *  - transaction is cancelled.
 *
 * @param ctx  SDM implementation context.
 * @param dm   Data model context.
 *
 * @returns
 * - 0 on success,
 * - a negative value in case of error.
 */
int sdm_process_stop(sdm_process_ctx_t *ctx, sdm_data_model_t *dm);

#ifdef __cplusplus
}
#endif

#endif // SDM_IMPL_H
