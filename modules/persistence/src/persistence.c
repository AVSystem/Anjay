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

#include <config.h>

#include <stdio.h>
#include <netinet/in.h>

#include <anjay/persistence.h>

#include <anjay_modules/io.h>
#include <anjay_modules/utils.h>

VISIBILITY_SOURCE_BEGIN

#define persistence_log(...) _anjay_log(anjay_persistence, __VA_ARGS__)

typedef int persistence_handler_u16_t(anjay_persistence_context_t *ctx,
                                      uint16_t *value);
typedef int persistence_handler_u32_t(anjay_persistence_context_t *ctx,
                                      uint32_t *value);
typedef int persistence_handler_bool_t(anjay_persistence_context_t *ctx,
                                       bool *value);
typedef int persistence_handler_bytes_t(anjay_persistence_context_t *ctx,
                                        uint8_t *buffer,
                                        size_t buffer_size);
typedef int persistence_handler_time_t(anjay_persistence_context_t *ctx,
                                       time_t *value);
typedef int persistence_handler_double_t(anjay_persistence_context_t *ctx,
                                         double *value);
typedef int
persistence_handler_list_t(anjay_persistence_context_t *ctx,
                           AVS_LIST(void) *list_ptr,
                           size_t element_size,
                           anjay_persistence_handler_collection_element_t *handler);
typedef int
persistence_handler_tree_t(anjay_persistence_context_t *ctx,
                           AVS_RBTREE(void) tree,
                           size_t element_size,
                           anjay_persistence_handler_collection_element_t *handler,
                           anjay_persistence_cleanup_collection_element_t *cleanup);

struct anjay_persistence_context_struct {
    persistence_handler_u16_t *handle_u16;
    persistence_handler_u32_t *handle_u32;
    persistence_handler_bool_t *handle_bool;
    persistence_handler_bytes_t *handle_bytes;
    persistence_handler_time_t *handle_time;
    persistence_handler_double_t *handle_double;
    persistence_handler_list_t *handle_list;
    persistence_handler_tree_t *handle_tree;
    avs_stream_abstract_t *stream;
};

//// PERSIST ///////////////////////////////////////////////////////////////////

static int persist_bool(anjay_persistence_context_t *ctx, bool *value) {
    AVS_STATIC_ASSERT(sizeof(*value) == 1, bool_is_1byte);
    return avs_stream_write(ctx->stream, value, 1);
}

static int persist_bytes(anjay_persistence_context_t *ctx,
                         uint8_t *buffer,
                         size_t buffer_size) {
    return avs_stream_write(ctx->stream, buffer, buffer_size);
}

static int persist_u16(anjay_persistence_context_t *ctx, uint16_t *value) {
    AVS_STATIC_ASSERT(sizeof(*value) == 2, u16_is_2bytes);
    uint16_t tmp = htons(*value);
    return avs_stream_write(ctx->stream, &tmp, 2);
}

static int persist_u32(anjay_persistence_context_t *ctx, uint32_t *value) {
    AVS_STATIC_ASSERT(sizeof(*value) == 4, u32_is_4bytes);
    uint32_t tmp = htonl(*value);
    return avs_stream_write(ctx->stream, &tmp, 4);
}

static int persist_time(anjay_persistence_context_t *ctx, time_t *value) {
    // time is stored as 32 bits for cross-platform portability
    int64_t value64 = (int64_t) *value;
    uint32_t value32 = (uint32_t) *(uint64_t *) &value64;
    return persist_u32(ctx, &value32);
}

static int persist_double(anjay_persistence_context_t *ctx, double *value) {
    uint64_t value_be = _anjay_htond(*value);
    AVS_STATIC_ASSERT(sizeof(*value) == sizeof(value_be), double_is_64);
    AVS_STATIC_ASSERT(sizeof(value_be) == 8, u64_is_8bytes);
    return avs_stream_write(ctx->stream, &value_be, 8);
}

static int persist_list(anjay_persistence_context_t *ctx,
                        AVS_LIST(void) *list_ptr,
                        size_t element_size,
                        anjay_persistence_handler_collection_element_t *handler) {
    (void) element_size;
    size_t count = AVS_LIST_SIZE(*list_ptr);
    uint32_t count32 = (uint32_t) count;
    if (count != count32) {
        return -1;
    }
    int retval = persist_u32(ctx, &count32);
    if (!retval) {
        AVS_LIST(void) element;
        AVS_LIST_FOREACH(element, *list_ptr) {
            if ((retval = handler(ctx, element))) {
                break;
            }
        }
    }
    return retval;
}

static int persist_tree(anjay_persistence_context_t *ctx,
                        AVS_RBTREE(void) tree,
                        size_t element_size,
                        anjay_persistence_handler_collection_element_t *handler,
                        anjay_persistence_cleanup_collection_element_t *cleanup) {
    (void) element_size; (void) cleanup;
    size_t count = AVS_RBTREE_SIZE(tree);
    uint32_t count32 = (uint32_t) count;
    if (count != count32) {
        return -1;
    }
    int retval = persist_u32(ctx, &count32);
    if (!retval) {
        AVS_RBTREE_ELEM(void) element;
        AVS_RBTREE_FOREACH(element, tree) {
            if ((retval = handler(ctx, element))) {
                break;
            }
        }
    }
    return retval;
}

#define INIT_STORE_CONTEXT(Stream) { \
            .handle_u16 = persist_u16, \
            .handle_u32 = persist_u32, \
            .handle_bool = persist_bool, \
            .handle_bytes = persist_bytes, \
            .handle_time = persist_time, \
            .handle_double = persist_double, \
            .handle_list = persist_list, \
            .handle_tree = persist_tree, \
            .stream = Stream \
        }

//// RESTORE ///////////////////////////////////////////////////////////////////

static int restore_bool(anjay_persistence_context_t *ctx, bool *out) {
    AVS_STATIC_ASSERT(sizeof(*out) == 1, bool_is_1byte);
    return avs_stream_read_reliably(ctx->stream, out, 1);
}

static int restore_bytes(anjay_persistence_context_t *ctx,
                         uint8_t *buffer,
                         size_t buffer_size) {
    return avs_stream_read_reliably(ctx->stream, buffer, buffer_size);
}

static int restore_u16(anjay_persistence_context_t *ctx, uint16_t *out) {
    uint16_t tmp;
    AVS_STATIC_ASSERT(sizeof(*out) == 2, u16_is_2bytes);
    int retval = avs_stream_read_reliably(ctx->stream, &tmp, 2);
    if (!retval && out) {
        *out = ntohs(tmp);
    }
    return retval;
}

static int restore_u32(anjay_persistence_context_t *ctx, uint32_t *out) {
    uint32_t tmp;
    AVS_STATIC_ASSERT(sizeof(*out) == 4, u32_is_4bytes);
    int retval = avs_stream_read_reliably(ctx->stream, &tmp, 4);
    if (!retval) {
        *out = ntohl(tmp);
    }
    return retval;
}

static int restore_time(anjay_persistence_context_t *ctx, time_t *out) {
    // time is stored as 32 bits for cross-platform portability
    int32_t value32;
    int retval = restore_u32(ctx, (uint32_t *) &value32);
    if (!retval) {
        *out = (time_t) value32;
    }
    return retval;
}

static int restore_double(anjay_persistence_context_t *ctx, double *out) {
    uint64_t tmp;
    AVS_STATIC_ASSERT(sizeof(*out) == sizeof(tmp), double_is_64);
    AVS_STATIC_ASSERT(sizeof(tmp) == 8, u64_is_8bytes);
    int retval = avs_stream_read_reliably(ctx->stream, &tmp, 8);
    if (!retval) {
        *out = _anjay_ntohd(tmp);
    }
    return retval;
}

static int restore_list(anjay_persistence_context_t *ctx,
                        AVS_LIST(void) *list_ptr,
                        size_t element_size,
                        anjay_persistence_handler_collection_element_t *handler) {
    uint32_t count;
    int retval = restore_u32(ctx, &count);
    if (!retval) {
        AVS_LIST(void) *insert_ptr = list_ptr;
        while (count--) {
            AVS_LIST(void) element = AVS_LIST_NEW_BUFFER(element_size);
            if (!element) {
                persistence_log(ERROR, "Out of memory");
                return -1;
            }
            AVS_LIST_INSERT(insert_ptr, element);
            insert_ptr = AVS_LIST_NEXT_PTR(insert_ptr);
            if ((retval = handler(ctx, element))) {
                return retval;
            }
        }
    }
    return retval;
}

static int restore_tree(anjay_persistence_context_t *ctx,
                        AVS_RBTREE(void) tree,
                        size_t element_size,
                        anjay_persistence_handler_collection_element_t *handler,
                        anjay_persistence_cleanup_collection_element_t *cleanup) {
    uint32_t count;
    int retval = restore_u32(ctx, &count);
    if (!retval) {
        while (count--) {
            AVS_RBTREE_ELEM(void) element =
                    AVS_RBTREE_ELEM_NEW_BUFFER(element_size);
            if (!element) {
                persistence_log(ERROR, "Out of memory");
                return -1;
            }
            if (!(retval = handler(ctx, element))) {
                if (AVS_RBTREE_INSERT(tree, element) != element) {
                    retval = -1;
                }
            }
            if (retval) {
                cleanup(element);
                AVS_RBTREE_ELEM_DELETE_DETACHED(&element);
                break;
            }
        }
    }
    return retval;
}

#define INIT_RESTORE_CONTEXT(Stream) { \
            .handle_u16 = restore_u16, \
            .handle_u32 = restore_u32, \
            .handle_bool = restore_bool, \
            .handle_bytes = restore_bytes, \
            .handle_time = restore_time, \
            .handle_double = restore_double, \
            .handle_list = restore_list, \
            .handle_tree = restore_tree, \
            .stream = Stream \
        }

//// IGNORE ////////////////////////////////////////////////////////////////////

static int ignore_bool(anjay_persistence_context_t *ctx, bool *out) {
    (void) out;
    bool tmp;
    AVS_STATIC_ASSERT(sizeof(*out) == 1, bool_is_1byte);
    return avs_stream_read_reliably(ctx->stream, &tmp, 1);
}

#define PERSISTENCE_IGNORE_BYTES_BUFSIZE 512

static int ignore_bytes(anjay_persistence_context_t *ctx,
                        uint8_t *buffer,
                        size_t buffer_size) {
    (void) buffer;
    uint8_t buf[PERSISTENCE_IGNORE_BYTES_BUFSIZE];
    while (buffer_size > 0) {
        size_t chunk_to_ignore =
                buffer_size < sizeof(buf) ? buffer_size : sizeof(buf);
        int retval =
                avs_stream_read_reliably(ctx->stream, buf, chunk_to_ignore);
        if (retval) {
            return retval;
        }
        buffer_size -= chunk_to_ignore;
    }
    return 0;
}

static int ignore_u16(anjay_persistence_context_t *ctx, uint16_t *out) {
    (void) out;
    uint16_t tmp;
    AVS_STATIC_ASSERT(sizeof(*out) == 2, u16_is_2bytes);
    return avs_stream_read_reliably(ctx->stream, &tmp, 2);
}

static int ignore_u32(anjay_persistence_context_t *ctx, uint32_t *out) {
    (void) out;
    uint32_t tmp;
    AVS_STATIC_ASSERT(sizeof(*out) == 4, u32_is_4bytes);
    return avs_stream_read_reliably(ctx->stream, &tmp, 4);
}

static int ignore_double(anjay_persistence_context_t *ctx, double *out) {
    (void) out;
    uint64_t tmp;
    AVS_STATIC_ASSERT(sizeof(*out) == sizeof(tmp), double_is_64);
    AVS_STATIC_ASSERT(sizeof(tmp) == 8, u64_is_8bytes);
    return avs_stream_read_reliably(ctx->stream, &tmp, 8);
}

static int ignore_time(anjay_persistence_context_t *ctx, time_t *out) {
    (void) out;
    return ignore_u32(ctx, NULL);
}

static int ignore_collection(
        anjay_persistence_context_t *ctx,
        anjay_persistence_handler_collection_element_t *handler) {
    uint32_t count;
    int retval = restore_u32(ctx, &count);
    while (!retval && count--) {
        retval = handler(ctx, NULL);
    }
    return retval;
}

static int ignore_list(anjay_persistence_context_t *ctx,
                       AVS_LIST(void) *list_ptr,
                       size_t element_size,
                       anjay_persistence_handler_collection_element_t *handler) {
    (void) list_ptr; (void) element_size;
    return ignore_collection(ctx, handler);
}

static int ignore_tree(anjay_persistence_context_t *ctx,
                       AVS_RBTREE(void) tree,
                       size_t element_size,
                       anjay_persistence_handler_collection_element_t *handler,
                       anjay_persistence_cleanup_collection_element_t *cleanup) {
    (void) tree; (void) element_size; (void) cleanup;
    return ignore_collection(ctx, handler);
}

#define INIT_IGNORE_CONTEXT(Stream) { \
            .handle_u16 = ignore_u16, \
            .handle_u32 = ignore_u32, \
            .handle_bool = ignore_bool, \
            .handle_bytes = ignore_bytes, \
            .handle_time = ignore_time, \
            .handle_double = ignore_double, \
            .handle_list = ignore_list, \
            .handle_tree = ignore_tree, \
            .stream = Stream \
        }

anjay_persistence_context_t *
anjay_persistence_store_context_new(avs_stream_abstract_t *stream) {
    if (!stream) {
        return NULL;
    }
    anjay_persistence_context_t *ctx = (anjay_persistence_context_t *)
            calloc(1, sizeof(anjay_persistence_context_t));
    if (ctx) {
        *ctx = (anjay_persistence_context_t) INIT_STORE_CONTEXT(stream);
    }
    return ctx;
}

anjay_persistence_context_t *
anjay_persistence_restore_context_new(avs_stream_abstract_t *stream) {
    if (!stream) {
        return NULL;
    }
    anjay_persistence_context_t *ctx = (anjay_persistence_context_t *)
            calloc(1, sizeof(anjay_persistence_context_t));
    if (ctx) {
        *ctx = (anjay_persistence_context_t) INIT_RESTORE_CONTEXT(stream);
    }
    return ctx;
}

anjay_persistence_context_t *
anjay_persistence_ignore_context_new(avs_stream_abstract_t *stream) {
    if (!stream) {
        return NULL;
    }
    anjay_persistence_context_t *ctx = (anjay_persistence_context_t *)
            calloc(1, sizeof(anjay_persistence_context_t));
    if (ctx) {
        *ctx = (anjay_persistence_context_t) INIT_IGNORE_CONTEXT(stream);
    }
    return ctx;
}

void anjay_persistence_context_delete(anjay_persistence_context_t *ctx) {
    free(ctx);
}

int anjay_persistence_u16(anjay_persistence_context_t *ctx,
                          uint16_t *value) {
    if (!ctx) {
        return -1;
    }
    return ctx->handle_u16(ctx, value);
}

int anjay_persistence_u32(anjay_persistence_context_t *ctx,
                          uint32_t *value) {
    if (!ctx) {
        return -1;
    }
    return ctx->handle_u32(ctx, value);
}

int anjay_persistence_bool(anjay_persistence_context_t *ctx, bool *value) {
    if (!ctx) {
        return -1;
    }
    return ctx->handle_bool(ctx, value);
}

int anjay_persistence_bytes(anjay_persistence_context_t *ctx,
                            uint8_t *buffer,
                            size_t buffer_size) {
    if (!ctx) {
        return -1;
    }
    return ctx->handle_bytes(ctx, buffer, buffer_size);
}

int anjay_persistence_time(anjay_persistence_context_t *ctx,
                           time_t *value) {
    if (!ctx) {
        return -1;
    }
    return ctx->handle_time(ctx, value);
}

int anjay_persistence_double(anjay_persistence_context_t *ctx,
                             double *value) {
    if (!ctx) {
        return -1;
    }
    return ctx->handle_double(ctx, value);
}

int anjay_persistence_list(anjay_persistence_context_t *ctx,
                           AVS_LIST(void) *list_ptr,
                           size_t element_size,
                           anjay_persistence_handler_collection_element_t *handler) {
    if (!ctx) {
        return -1;
    }
    return ctx->handle_list(ctx, list_ptr, element_size, handler);
}

int anjay_persistence_tree(
        anjay_persistence_context_t *ctx,
        AVS_RBTREE(void) tree,
        size_t element_size,
        anjay_persistence_handler_collection_element_t *handler,
        anjay_persistence_cleanup_collection_element_t *cleanup) {
    if (!ctx) {
        return -1;
    }
    return ctx->handle_tree(ctx, tree, element_size, handler, cleanup);
}

#ifdef ANJAY_TEST
#include "test/persistence.c"
#endif
