/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <string.h>

#include <avsystem/commons/avs_unit_test.h>

#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#ifdef FLUF_WITH_OPAQUE

typedef struct {
    fluf_io_out_ctx_t ctx;
    fluf_io_out_entry_t entry;
    char buf[400];
    size_t buffer_length;
    size_t copied_bytes;
} opaque_test_env_t;

typedef struct {
    const char *data;
    size_t size;
} test_data_t;

static void opaque_test_setup(opaque_test_env_t *env) {
    memset(env, 0, sizeof(opaque_test_env_t));
    env->buffer_length = sizeof(env->buf);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_init(
            &env->ctx, FLUF_OP_DM_READ, &FLUF_MAKE_ROOT_PATH(), 1,
            FLUF_COAP_FORMAT_OPAQUE_STREAM));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_format(&env->ctx),
                          FLUF_COAP_FORMAT_OPAQUE_STREAM);
}

AVS_UNIT_TEST(opaque_out, entry_already_added) {
    opaque_test_env_t env;
    opaque_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_BYTES;
    env.entry = input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(opaque_out, format_type_not_set) {

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_NULL;
    opaque_test_env_t env;
    opaque_test_setup(&env);
    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(opaque_out, no_remaining_bytes_bytes) {
    opaque_test_env_t env;
    opaque_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    char BytesInput[] = "Bytes input";
    input.type = FLUF_DATA_TYPE_BYTES;
    env.entry = input;
    input.value.bytes_or_string.chunk_length = 0;
    input.value.bytes_or_string.data = BytesInput;
    input.value.bytes_or_string.offset = 0;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    env.ctx._buff.remaining_bytes = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.copied_bytes));
    AVS_UNIT_ASSERT_EQUAL(env.copied_bytes, 0);
}

AVS_UNIT_TEST(opaque_out, unsupported_format_type) {

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_INT;
    opaque_test_env_t env;
    opaque_test_setup(&env);
    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_FORMAT);
}

static int external_data_handler_failure(void *buffer,
                                         size_t bytes_to_copy,
                                         size_t offset,
                                         void *args) {
    (void) buffer;
    (void) bytes_to_copy;
    (void) offset;
    (void) args;

    return -1;
}

AVS_UNIT_TEST(opaque_out, external_bytes_handler_error) {
    opaque_test_env_t env;
    opaque_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;
    input.value.external_data.get_external_data = external_data_handler_failure;
    input.value.external_data.length = 50;
    env.entry = input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_payload(&env.ctx, env.buf,
                                                      env.buffer_length,
                                                      &env.copied_bytes),
                          -1);
}

AVS_UNIT_TEST(opaque_out, external_bytes_handler_null) {
    opaque_test_env_t env;
    opaque_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;
    input.value.external_data.get_external_data = NULL;
    input.value.external_data.length = 0;
    env.entry = input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_payload(&env.ctx, env.buf,
                                                      env.buffer_length,
                                                      &env.copied_bytes),
                          0);
}

AVS_UNIT_TEST(opaque_out, external_bytes_handler_null_length_set) {
    opaque_test_env_t env;
    opaque_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;
    input.value.external_data.get_external_data = NULL;
    input.value.external_data.length = 50;
    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_INPUT_ARG);
}

#    define MAKE_TEST_DATA(Data)     \
        (test_data_t) {              \
            .data = Data,            \
            .size = sizeof(Data) - 1 \
        }

static void verify_bytes(opaque_test_env_t *env, test_data_t *expected_data) {
    AVS_UNIT_ASSERT_EQUAL(env->copied_bytes, expected_data->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env->buf, expected_data->data,
                                      expected_data->size);
}

static void test_bytes(test_data_t *expected_data, fluf_io_out_entry_t *input) {
    opaque_test_env_t env;
    opaque_test_setup(&env);

    env.entry = *input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.copied_bytes));
    verify_bytes(&env, expected_data);
}

#    define TEST_BYTES(Name, DataInput)                                       \
        AVS_UNIT_TEST(opaque_out, bytes_##Name) {                             \
            fluf_io_out_entry_t input = { 0 };                                \
            input.type = FLUF_DATA_TYPE_BYTES;                                \
            input.value.bytes_or_string.chunk_length = sizeof(DataInput) - 1; \
            input.value.bytes_or_string.data = DataInput;                     \
            input.value.bytes_or_string.offset = 0;                           \
            test_data_t expected = MAKE_TEST_DATA(DataInput);                 \
            test_bytes(&expected, &input);                                    \
        }

TEST_BYTES(4bytes, "\x01\x02\x03\x04")
TEST_BYTES(5bytes, "\x64\x49\x45\x54\x46")
TEST_BYTES(23bytes,
           "\x84\x11\xDB\xB8\xAA\xF7\xC3\xEF\xBA\xC0\x2F\x50\xC2\x88\xAF\x1B"
           "\x8F\xD2\xE4\xC9\x5A\xD7\xEC")
TEST_BYTES(24bytes,
           "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
           "\x3F\xAC\x35\x03\x16\x1E\x32\x0A")
TEST_BYTES(60bytes,
           "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
           "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x46\x0A\x00\x2D\xC0\x68\xD4\xE5"
           "\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
           "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x3F\xAC\x35\x03\x16\x1E\x32\x0A"
           "\x46\x0A\x00\x2D")
TEST_BYTES(
        61bytes,
        "\x0F\x34\x21\x26\xCD\xB5\x30\xEE\xC5\x48\xBB\x6F\x03\x62\xC2\x7B\x21"
        "\x52\xB6\xEA\xFA\x4E\x09\xD3\xB8\x40\x85\x7D\xDA\xB1\xC8\xFF\x65\xB7"
        "\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\xFF\xA9\xAB\x9E\x67\x04\x0A\x3A\x1B\xE7\x77\x53\x9A\xA1\x6D\xDA\xA0"
        "\x0A\x00\x2D\x23")
TEST_BYTES(
        255bytes,
        "\xD6\xFB\x20\x80\xCE\x44\x31\x3B\xE1\x63\xD9\x89\x36\x90\x06\x56\x9C"
        "\xF6\x4C\x24\x04\x34\xEA\x8D\xF3\xF1\x40\xEA\x3A\x41\xE1\x57\xFF\x92"
        "\xCC\xAE\x42\x10\x27\x48\x47\x6E\x7C\x11\x9B\x5A\x21\x5A\x51\xF7\x45"
        "\xB0\x5E\x3B\x81\x26\xE9\xB0\x8A\xF1\x93\xCA\xA6\xB3\xD7\xE0\x16\xEC"
        "\xBF\xF5\x21\x16\xC7\x50\x6C\x9A\xA8\x8E\x49\xA9\xF1\x59\x8C\xC3\x80"
        "\x0F\x34\x21\x26\xCD\xB5\x30\xEE\xC5\x48\xBB\x6F\x03\x62\xC2\x7B\x21"
        "\x60\x08\xE2\x58\xD3\xE0\x64\x3A\x4B\x59\x16\xFD\x8E\x05\x41\x46\xBD"
        "\xFB\xC8\x7B\x4D\xC3\x38\x01\x94\x31\x50\xFC\xE7\xBE\x7A\xDA\xD6\x56"
        "\x74\x1C\x7F\x75\xB1\x59\x15\x4E\x86\x8E\x71\xB0\xFF\x69\x60\xDC\xBC"
        "\x52\xB6\xEA\xFA\x4E\x09\xD3\xB8\x40\x85\x7D\xDA\xB1\xC8\xFF\x65\xB7"
        "\xFF\xA9\xAB\x9E\x67\x04\x0A\x3A\x1B\xE7\x77\x53\x9A\xA1\x6D\xDA\xA0"
        "\xBB\xC0\x91\xA1\x38\x93\x0E\x33\xDF\x4B\x9E\x83\x0C\xF4\x73\x1E\xD6"
        "\x83\x92\x54\x3D\x73\x1F\xEC\xCA\xD9\x1F\xE2\x3D\x57\xD1\x7C\x54\x88"
        "\xFB\x3E\xCF\x7E\x8A\x29\x98\x89\x4A\xBB\x2F\xE5\xB1\x36\x2B\x8B\x8F"
        "\xBF\x46\x19\x74\x1D\xC4\x7B\xFB\x52\xA4\x32\x47\xA7\x5C\xA1\x5C\x1A")
TEST_BYTES(256bytes,
           "\xD8\xE2\xE6\xED\x90\x05\x29\x3B\x17\xAC\x8D\x33\x93\x52\xD9\x6B"
           "\xF2\xFB\x20\x74\x3E\x9C\xEF\xAD\xBB\x03\xCE\x0E\xC5\xBD\x0D\x2F"
           "\x42\x6D\x1C\xD6\xDB\x29\xF8\xF6\xA4\x96\x3D\x7A\x8A\xEE\xE6\xF2"
           "\x56\x1C\xBE\xCE\x71\x30\x3B\xEC\xC9\x86\x71\x96\x86\x51\xA2\xCA"
           "\x23\x8A\x0B\x1D\x67\x3C\x50\xB8\x66\x4C\x64\x8C\x31\xCD\x11\x05"
           "\xCA\x56\x4B\xBB\x79\x18\x8F\x5B\xF1\xE0\x1E\x85\x38\xBE\x7A\x6F"
           "\x30\x4A\xFD\xB3\x1B\xA9\x52\xB4\x0E\x95\x73\x83\xA5\x33\x9F\x0C"
           "\x04\x2E\x33\xB3\xD5\x0B\x6E\x02\x0C\xC7\x0D\x1A\x1A\x48\x0C\x92"
           "\x1B\x62\x83\xCF\xC1\x5C\x90\xBC\x83\x3B\x92\xBF\x8E\xCE\x7C\xD6"
           "\x99\x77\xF2\x66\x92\x0C\xC6\x0A\x11\x80\xBE\x03\x59\x23\x89\xF6"
           "\xEF\x3A\x5A\x07\xEB\xEF\x47\xF0\x1F\xF0\xB4\x96\x01\x1B\xE9\x51"
           "\x40\x70\x16\xDD\xB2\x9B\xEB\x42\xAC\x6E\x45\xE6\xAE\x8F\xCE\x9A"
           "\xC4\xCB\x09\xE7\x2C\xE4\x48\x86\xF0\x9C\x56\x2C\xEF\x1B\xD0\x8E"
           "\x92\xD4\x61\x15\x46\x76\x19\x32\xDF\x9F\x98\xC0\x0A\xF7\xAE\xA9"
           "\xD7\x61\xEC\x8B\x78\xE5\xAA\xC6\x0B\x5D\x98\x1D\x86\xE6\x57\x67"
           "\x97\x56\x82\x29\xFF\x8F\x61\x6C\xA5\xD0\x08\x20\xAE\x49\x5B\x04")

static char *ptr_for_callback = NULL;
static int external_data_handler(void *buffer,
                                 size_t bytes_to_copy,
                                 size_t offset,
                                 void *args) {
    (void) args;
    assert(&ptr_for_callback);
    memcpy(buffer, &ptr_for_callback[offset], bytes_to_copy);
    return 0;
}

#    define TEST_BYTES_EXT(Name, DataInput)                           \
        AVS_UNIT_TEST(opaque_out, bytes_ext_##Name) {                 \
            fluf_io_out_entry_t input = { 0 };                        \
            input.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;               \
            input.value.external_data.get_external_data =             \
                    external_data_handler;                            \
            input.value.external_data.length = sizeof(DataInput) - 1; \
            ptr_for_callback = DataInput;                             \
            test_data_t expected = MAKE_TEST_DATA(DataInput);         \
            test_bytes(&expected, &input);                            \
        }

TEST_BYTES_EXT(4bytes, "\x01\x02\x03\x04")
TEST_BYTES_EXT(5bytes, "\x64\x49\x45\x54\x46")
TEST_BYTES_EXT(
        23bytes,
        "\x84\x11\xDB\xB8\xAA\xF7\xC3\xEF\xBA\xC0\x2F\x50\xC2\x88\xAF\x1B"
        "\x8F\xD2\xE4\xC9\x5A\xD7\xEC")
TEST_BYTES_EXT(
        24bytes,
        "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\x3F\xAC\x35\x03\x16\x1E\x32\x0A")
TEST_BYTES_EXT(
        60bytes,
        "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x46\x0A\x00\x2D\xC0\x68\xD4\xE5"
        "\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x3F\xAC\x35\x03\x16\x1E\x32\x0A"
        "\x46\x0A\x00\x2D")
TEST_BYTES_EXT(
        61bytes,
        "\x0F\x34\x21\x26\xCD\xB5\x30\xEE\xC5\x48\xBB\x6F\x03\x62\xC2\x7B\x21"
        "\x52\xB6\xEA\xFA\x4E\x09\xD3\xB8\x40\x85\x7D\xDA\xB1\xC8\xFF\x65\xB7"
        "\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\xFF\xA9\xAB\x9E\x67\x04\x0A\x3A\x1B\xE7\x77\x53\x9A\xA1\x6D\xDA\xA0"
        "\x0A\x00\x2D\x23")

#    define TEST_BYTES_EMPTY_CHUNK_LENGTH(Name, DataInput, DataExpected) \
        AVS_UNIT_TEST(opaque_out, bytes_empty_ch_len_##Name) {           \
            fluf_io_out_entry_t input = { 0 };                           \
            input.type = FLUF_DATA_TYPE_BYTES;                           \
            input.value.bytes_or_string.chunk_length = 0;                \
            input.value.bytes_or_string.data = DataInput;                \
            input.value.bytes_or_string.offset = 0;                      \
            test_data_t expected = MAKE_TEST_DATA(DataExpected);         \
            test_bytes(&expected, &input);                               \
        }

TEST_BYTES_EMPTY_CHUNK_LENGTH(4bytes, "\x01\x02\x03\x04", "")
TEST_BYTES_EMPTY_CHUNK_LENGTH(5bytes, "\x64\x49\x45\x54\x46", "")
TEST_BYTES_EMPTY_CHUNK_LENGTH(
        256bytes,
        "\xD8\xE2\xE6\xED\x90\x05\x29\x3B\x17\xAC\x8D\x33\x93\x52\xD9\x6B"
        "\xF2\xFB\x20\x74\x3E\x9C\xEF\xAD\xBB\x03\xCE\x0E\xC5\xBD\x0D\x2F"
        "\x42\x6D\x1C\xD6\xDB\x29\xF8\xF6\xA4\x96\x3D\x7A\x8A\xEE\xE6\xF2"
        "\x56\x1C\xBE\xCE\x71\x30\x3B\xEC\xC9\x86\x71\x96\x86\x51\xA2\xCA"
        "\x23\x8A\x0B\x1D\x67\x3C\x50\xB8\x66\x4C\x64\x8C\x31\xCD\x11\x05"
        "\xCA\x56\x4B\xBB\x79\x18\x8F\x5B\xF1\xE0\x1E\x85\x38\xBE\x7A\x6F"
        "\x30\x4A\xFD\xB3\x1B\xA9\x52\xB4\x0E\x95\x73\x83\xA5\x33\x9F\x0C"
        "\x04\x2E\x33\xB3\xD5\x0B\x6E\x02\x0C\xC7\x0D\x1A\x1A\x48\x0C\x92"
        "\x1B\x62\x83\xCF\xC1\x5C\x90\xBC\x83\x3B\x92\xBF\x8E\xCE\x7C\xD6"
        "\x99\x77\xF2\x66\x92\x0C\xC6\x0A\x11\x80\xBE\x03\x59\x23\x89\xF6"
        "\xEF\x3A\x5A\x07\xEB\xEF\x47\xF0\x1F\xF0\xB4\x96\x01\x1B\xE9\x51"
        "\x40\x70\x16\xDD\xB2\x9B\xEB\x42\xAC\x6E\x45\xE6\xAE\x8F\xCE\x9A"
        "\xC4\xCB\x09\xE7\x2C\xE4\x48\x86\xF0\x9C\x56\x2C\xEF\x1B\xD0\x8E"
        "\x92\xD4\x61\x15\x46\x76\x19\x32\xDF\x9F\x98\xC0\x0A\xF7\xAE\xA9"
        "\xD7\x61\xEC\x8B\x78\xE5\xAA\xC6\x0B\x5D\x98\x1D\x86\xE6\x57\x67"
        "\x97\x56\x82\x29\xFF\x8F\x61\x6C\xA5\xD0\x08\x20\xAE\x49\x5B\x04",
        "")

/* Incorrectly value.bytes_or_string.offset set to 100. Value is ignored and
 * fluf_io_out_ctx_new_entry returns success */
AVS_UNIT_TEST(opaque_out, bytes_offset) {
    char DataInput[] =
            "\x46\x0A\xAE\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
            "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x46\x0A\x0E\x2D\xC0\x68\xD4\xE5"
            "\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
            "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x3F\xAC\x35\x03\x16\x1E\x32\x0A"
            "\x46\x0A\xEE\x2D";
    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_BYTES;
    input.value.bytes_or_string.chunk_length = sizeof(DataInput) - 1;
    input.value.bytes_or_string.data = DataInput;
    input.value.bytes_or_string.offset = 100;
    opaque_test_env_t env;
    opaque_test_setup(&env);
    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_INPUT_ARG);
}

static void test_out_buff_smaller_than_internal_buff(opaque_test_env_t *env,
                                                     fluf_io_out_entry_t *input,
                                                     size_t buffer_length,
                                                     test_data_t *expected) {
    opaque_test_setup(env);
    assert(env->buffer_length >= expected->size);
    /* env->buffer_length is made smaller than it is indeed for test purposes */
    env->buffer_length = buffer_length;
    env->entry = *input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env->ctx, &env->entry));

    size_t copied_bytes = 0;
    while (1) {
        int res =
                fluf_io_out_ctx_get_payload(&env->ctx, &env->buf[copied_bytes],
                                            env->buffer_length,
                                            &env->copied_bytes);
        AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env->buf + copied_bytes,
                                          expected->data + copied_bytes,
                                          env->copied_bytes);
        copied_bytes += env->copied_bytes;

        if (!res) {
            AVS_UNIT_ASSERT_EQUAL(env->ctx._buff.remaining_bytes, 0);
            break;
        } else if (res == FLUF_IO_NEED_NEXT_CALL) {
            AVS_UNIT_ASSERT_EQUAL(env->copied_bytes, env->buffer_length);
        }
    }
    AVS_UNIT_ASSERT_EQUAL(copied_bytes, expected->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env->buf, expected->data, copied_bytes);
}

#    define TEST_BYTES_COMPLEX(Name, DataInput, buffer_length)                \
        AVS_UNIT_TEST(opaque_out, bytes_complex_##Name) {                     \
            fluf_io_out_entry_t input = { 0 };                                \
            input.type = FLUF_DATA_TYPE_BYTES;                                \
            input.value.bytes_or_string.chunk_length = sizeof(DataInput) - 1; \
            input.value.bytes_or_string.data = DataInput;                     \
            input.value.bytes_or_string.offset = 0;                           \
                                                                              \
            opaque_test_env_t env = { 0 };                                    \
            test_data_t expected = MAKE_TEST_DATA(DataInput);                 \
            test_out_buff_smaller_than_internal_buff(                         \
                    &env, &input, buffer_length, &expected);                  \
        }

static char DataToEncode62[] =
        "\x0F\x34\x21\x26\xCD\xB5\x30\xEE\xC5\x48\xBB\x6F\x03"
        "\x62\xC2\x7B\x21"
        "\x52\xB6\xEA\xFA\x4E\x09\xD3\xB8\x40\x85\x7D\xDA\xB1"
        "\xC8\xFF\x65\xB7"
        "\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\xFF\xA9\xAB\x9E\x67\x04\x0A\x3A\x1B\xE7\x77\x53\x9A"
        "\xA1\x6D\xDA\xA0"
        "\x0A\x0E\x2D\x23";
static char DataToEncode257[] =
        "\xD8\xE2\xE6\xED\x90\x05\x29\x3B\x17\xAC\x8D\x33\x93\x52\xD9\x6B"
        "\xF2\xFB\x20\x74\x3E\x9C\xEF\xAD\xBB\x03\xCE\x0E\xC5\xBD\x0D\x2F"
        "\x42\x6D\x1C\xD6\xDB\x29\xF8\xF6\xA4\x96\x3D\x7A\x8A\xEE\xE6\xF2"
        "\x56\x1C\xBE\xCE\x71\x30\x3B\xEC\xC9\x86\x71\x96\x86\x51\xA2\xCA"
        "\x23\x8A\x0B\x1D\x67\x3C\x50\xB8\x66\x4C\x64\x8C\x31\xCD\x11\x05"
        "\xCA\x56\x4B\xBB\x79\x18\x8F\x5B\xF1\xE0\x1E\x85\x38\xBE\x7A\x6F"
        "\x30\x4A\xFD\xB3\x1B\xA9\x52\xB4\x0E\x95\x73\x83\xA5\x33\x9F\x0C"
        "\x04\x2E\x33\xB3\xD5\x0B\x6E\x02\x0C\xC7\x0D\x1A\x1A\x48\x0C\x92"
        "\x1B\x62\x83\xCF\xC1\x5C\x90\xBC\x83\x3B\x92\xBF\x8E\xCE\x7C\xD6"
        "\x99\x77\xF2\x66\x92\x0C\xC6\x0A\x11\x80\xBE\x03\x59\x23\x89\xF6"
        "\xEF\x3A\x5A\x07\xEB\xEF\x47\xF0\x1F\xF0\xB4\x96\x01\x1B\xE9\x51"
        "\x40\x70\x16\xDD\xB2\x9B\xEB\x42\xAC\x6E\x45\xE6\xAE\x8F\xCE\x9A"
        "\xC4\xCB\x09\xE7\x2C\xE4\x48\x86\xF0\x9C\x56\x2C\xEF\x1B\xD0\x8E"
        "\x92\xD4\x61\x15\x46\x76\x19\x32\xDF\x9F\x98\xC0\x0A\xF7\xAE\xA9"
        "\xD7\x61\xEC\x8B\x78\xE5\xAA\xC6\x0B\x5D\x98\x1D\x86\xE6\x57\x67"
        "\x97\x56\x82\x29\xFF\x8F\x61\x6C\xA5\xD0\x08\x20\xAE\x49\x5B\x04";

TEST_BYTES_COMPLEX(buf_len_1, DataToEncode62, 1);
TEST_BYTES_COMPLEX(buf_len_2, DataToEncode62, 2);
TEST_BYTES_COMPLEX(buf_len_20, DataToEncode62, 20);
TEST_BYTES_COMPLEX(buf_len_21, DataToEncode62, 21);
TEST_BYTES_COMPLEX(buf_len_22, DataToEncode62, 22);
TEST_BYTES_COMPLEX(buf_len_23, DataToEncode62, 23);
TEST_BYTES_COMPLEX(buf_len_24, DataToEncode62, 24);
TEST_BYTES_COMPLEX(buf_len_20_input_257, DataToEncode257, 20);
TEST_BYTES_COMPLEX(buf_len_21_input_257, DataToEncode257, 21);
TEST_BYTES_COMPLEX(buf_len_100_input_257, DataToEncode257, 100);
TEST_BYTES_COMPLEX(buf_len_101_input_257, DataToEncode257, 101);

#    define TEST_EXT_BYTES_COMPLEX(Name, DataInput, buffer_length)    \
        AVS_UNIT_TEST(opaque_out, bytes_ext_complex_##Name) {         \
            fluf_io_out_entry_t input = { 0 };                        \
            input.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;               \
            input.value.external_data.get_external_data =             \
                    external_data_handler;                            \
            input.value.external_data.length = sizeof(DataInput) - 1; \
            ptr_for_callback = DataInput;                             \
                                                                      \
            opaque_test_env_t env = { 0 };                            \
            test_data_t expected = MAKE_TEST_DATA(DataInput);         \
            test_out_buff_smaller_than_internal_buff(                 \
                    &env, &input, buffer_length, &expected);          \
        }

TEST_EXT_BYTES_COMPLEX(buf_len_1, DataToEncode62, 1);
TEST_EXT_BYTES_COMPLEX(buf_len_2, DataToEncode62, 2);
TEST_EXT_BYTES_COMPLEX(buf_len_20, DataToEncode62, 20);
TEST_EXT_BYTES_COMPLEX(buf_len_21, DataToEncode62, 21);
TEST_EXT_BYTES_COMPLEX(buf_len_22, DataToEncode62, 22);
TEST_EXT_BYTES_COMPLEX(buf_len_23, DataToEncode62, 23);
TEST_EXT_BYTES_COMPLEX(buf_len_24, DataToEncode62, 24);
TEST_EXT_BYTES_COMPLEX(buf_len_20_input_257, DataToEncode257, 20);
TEST_EXT_BYTES_COMPLEX(buf_len_21_input_257, DataToEncode257, 21);
TEST_EXT_BYTES_COMPLEX(buf_len_100_input_257, DataToEncode257, 100);
TEST_EXT_BYTES_COMPLEX(buf_len_101_input_257, DataToEncode257, 101);
TEST_EXT_BYTES_COMPLEX(buf_len_250_input_257, DataToEncode257, 250);
TEST_EXT_BYTES_COMPLEX(buf_len_251_input_257, DataToEncode257, 251);

#endif // FLUF_WITH_OPAQUE
