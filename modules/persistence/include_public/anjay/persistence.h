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

#ifndef ANJAY_INCLUDE_ANJAY_PERSISTENCE_H
#define ANJAY_INCLUDE_ANJAY_PERSISTENCE_H

#include <avsystem/commons/rbtree.h>
#include <avsystem/commons/stream.h>

#include <anjay/anjay.h>

#ifdef __cplusplus
extern "C" {
#endif

struct anjay_persistence_context_struct;
typedef struct anjay_persistence_context_struct anjay_persistence_context_t;

typedef int
anjay_persistence_handler_collection_element_t(anjay_persistence_context_t *ctx,
                                               void *element);

typedef int anjay_persistence_cleanup_collection_element_t(void *element);

/**
 * Creates context where each underlying operation writes passed value to the
 * stream.
 * @param stream    stream to operate on
 * @return          NULL on error during context construction, valid pointer
 *                  otherwise
 */
anjay_persistence_context_t *
anjay_persistence_store_context_new(avs_stream_abstract_t *stream);

/**
 * Creates context where each underlying operation reads value from the stream
 * and writes it under an address passed by the user.
 * @param stream    stream to operate on
 * @return          NULL on error during context construction, valid pointer
 *                  otherwise
 */
anjay_persistence_context_t *
anjay_persistence_restore_context_new(avs_stream_abstract_t *stream);

/**
 * Creates context where each underlying operation skips value.
 * @param stream    stream to operate on
 * @return          NULL on error during context construction, valid pointer
 *                  otherwise
 */
anjay_persistence_context_t *
anjay_persistence_ignore_context_new(avs_stream_abstract_t *stream);

/**
 * Deletes @p ctx and frees memory associated with it.
 * Note: stream used to initialize context is not closed.
 * @param ctx       pointer to the context to be deleted
 */
void anjay_persistence_context_delete(anjay_persistence_context_t *ctx);

/**
 * Performs operation (depending on the @p ctx) on bool.
 * @param ctx   context that determines the actual operation
 * @param value pointer of value passed to the underlying operation
 * @return 0 in case of success, negative value in case of failure
 */
int anjay_persistence_bool(anjay_persistence_context_t *ctx, bool *value);

/**
 * Performs operation (depending on the @p ctx) on byte sequence.
 *
 * On restore context behavior:
 *  - @p buffer is a pointer to the user-allocated buffer.
 *  - @p buffer_size is a size of the user-allocated buffer.
 *  - If the data cannot be fit into the @p buffer, then an error is returned.
 *
 * On persist context behavior:
 *  - @p buffer is a pointer to the user-allocated buffer.
 *  - @p buffer_size is the amount of bytes to store.
 *  - If the data cannot be stored, then an error is returned.
 *
 * On ignore context behavior:
 *  - @p buffer is optional, might be NULL.
 *  - @p buffer_size is the amount of bytes to be ignored.
 *
 * Example usage:
 * @code
 *  char buffer[1024];
 *  // Some logic that fills the buffer
 *  ...
 *  uint32_t buffer_size = ...;
 *
 *  // Store buffer size so that it can be restored later.
 *  int retval = anjay_persistence_u32(persist_ctx, &buffer_size);
 *  if (retval) {
 *      return retval;
 *  }
 *
 *  // Store the buffer itself.
 *  return anjay_persistence_bytes(persist_ctx, buffer, buffer_size);
 * @endcode
 *
 * @param ctx           Context that determines actual operation.
 * @param buffer        Pointer to the user-allocated buffer or NULL.
 * @param buffer_size   Size of the user-allocated buffer or 0.
 * @return 0 in case of success, negative value in case of failure.
 */
int anjay_persistence_bytes(anjay_persistence_context_t *ctx,
                            uint8_t *buffer,
                            size_t buffer_size);

/**
 * Performs operation (depending on the @p ctx) on uint16_t.
 * @param ctx   context that determines the actual operation
 * @param value pointer of value passed to the underlying operation
 * @return 0 in case of success, negative value in case of failure
 */
int anjay_persistence_u16(anjay_persistence_context_t *ctx,
                          uint16_t *value);

/**
 * Performs operation (depending on the @p ctx) on uint32_t.
 * @param ctx   context that determines the actual operation
 * @param value pointer of value passed to the underlying operation
 * @return 0 in case of success, negative value in case of failure
 */
int anjay_persistence_u32(anjay_persistence_context_t *ctx,
                          uint32_t *value);

/**
 * Performs operation (depending on the @p ctx) on time_t.
 * Note: for cross-platform compability time_t is stored as unsgined 32bit
 *       integer, therefore values larger than UINT32_MAX will get truncated
 *       in a way it is usually done on unsigned integers.
 *
 * @param ctx   context that determines the actual operation
 * @param value pointer of value passed to the underlying operation
 * @return 0 in case of success, negative value in case of failure
 */
int anjay_persistence_time(anjay_persistence_context_t *ctx,
                           time_t *value);

/**
 * Performs operation (depending on the @p ctx) on double.
 * @param ctx   context that determines the actual operation
 * @param value pointer of value passed to the underlying operation
 * @return 0 in case of success, negative value in case of failure
 */
int anjay_persistence_double(anjay_persistence_context_t *ctx,
                             double *value);

/**
 * Performs a operation (depending on the @p ctx) on a @p list_ptr, using
 * @p handler for each element.
 *
 * @param ctx           context that determines the actual operation
 * @param list_ptr      pointer to the list containing the data
 * @param element_size  size of single element in the list
 * @param handler       function called for each element of the list
 * @return 0 in case of success, negative value in case of failure
 */
int anjay_persistence_list(
        anjay_persistence_context_t *ctx,
        AVS_LIST(void) *list_ptr,
        size_t element_size,
        anjay_persistence_handler_collection_element_t *handler);

/**
 * Performs a operation (depending on the @p ctx) on a @p tree, using
 * @p handler for each element.
 *
 * @param ctx           context that determines the actual operation
 * @param tree          tree containing the data
 * @param element_size  size of single element in the tree
 * @param handler       function called for each element of the tree
 * @param cleanup       function called on an element if it could not be
 *                      restored in entirety
 * @return 0 in case of success, negative value in case of failure
 */
int anjay_persistence_tree(
        anjay_persistence_context_t *ctx,
        AVS_RBTREE(void) tree,
        size_t element_size,
        anjay_persistence_handler_collection_element_t *handler,
        anjay_persistence_cleanup_collection_element_t *cleanup);

#ifdef __cplusplus
}
#endif

#endif /* ANJAY_INCLUDE_ANJAY_PERSISTENCE_H */
