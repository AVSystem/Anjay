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

#include <fluf/fluf_config.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_io_ctx.h>
#include <fluf/fluf_utils.h>

#ifdef FLUF_WITH_PLAINTEXT

typedef struct {
    fluf_io_out_ctx_t ctx;
    fluf_io_out_entry_t entry;
    char buf[800];
    size_t buffer_length;
    size_t copied_bytes;
} text_test_env_t;

typedef struct {
    const char *data;
    size_t size;
} test_data_t;

static void text_test_setup(text_test_env_t *env) {
    memset(env, 0, sizeof(text_test_env_t));
    env->buffer_length = sizeof(env->buf);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_init(&env->ctx, FLUF_OP_DM_READ,
                                                 &FLUF_MAKE_ROOT_PATH(), 1,
                                                 FLUF_COAP_FORMAT_PLAINTEXT));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_format(&env->ctx),
                          FLUF_COAP_FORMAT_PLAINTEXT);
}

static void verify_bytes(text_test_env_t *env, test_data_t *expected_data) {
    AVS_UNIT_ASSERT_EQUAL(env->copied_bytes, expected_data->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env->buf, expected_data->data,
                                      expected_data->size);
}

static void test_bytes(test_data_t *expected_data, fluf_io_out_entry_t *input) {
    text_test_env_t env;
    text_test_setup(&env);

    env.entry = *input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.copied_bytes));
    verify_bytes(&env, expected_data);
}

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

#    define MAKE_TEST_DATA(Data)     \
        (test_data_t) {              \
            .data = Data,            \
            .size = sizeof(Data) - 1 \
        }

AVS_UNIT_TEST(text_encoder, entry_already_added) {
    text_test_env_t env;
    text_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_INT;
    input.value.int_value = 1;
    env.entry = input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(text_encoder, format_type_not_set) {

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_NULL;
    text_test_env_t env;
    text_test_setup(&env);
    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(text_encoder, no_remaining_bytes_int) {
    text_test_env_t env;
    text_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_INT;
    env.entry = input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    env.ctx._buff.remaining_bytes = 0;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_payload(&env.ctx, env.buf,
                                                      env.buffer_length,
                                                      &env.copied_bytes),
                          FLUF_IO_ERR_LOGIC);
}

AVS_UNIT_TEST(text_encoder, no_remaining_bytes_bytes) {
    text_test_env_t env;
    text_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    char BytesInput[] = "String input";
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

AVS_UNIT_TEST(text_encoder, no_remaining_bytes_string) {
    text_test_env_t env;
    text_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    char *StringInput = "String input";
    input.type = FLUF_DATA_TYPE_STRING;
    input.value.bytes_or_string.chunk_length = 0;
    input.value.bytes_or_string.data = StringInput;
    input.value.bytes_or_string.offset = 0;
    env.entry = input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    env.ctx._buff.remaining_bytes = 0;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.copied_bytes));
    AVS_UNIT_ASSERT_EQUAL(env.copied_bytes, 0);
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

AVS_UNIT_TEST(text_encoder, external_bytes_handler_error) {
    text_test_env_t env;
    text_test_setup(&env);

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

AVS_UNIT_TEST(text_encoder, external_bytes_handler_null) {
    text_test_env_t env;
    text_test_setup(&env);

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

AVS_UNIT_TEST(text_encoder, external_bytes_handler_null_length_set) {
    text_test_env_t env;
    text_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;
    input.value.external_data.get_external_data = NULL;
    input.value.external_data.length = 50;
    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_INPUT_ARG);
}

AVS_UNIT_TEST(text_encoder, external_string_handler_null_length_set) {
    text_test_env_t env;
    text_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_EXTERNAL_STRING;
    input.value.external_data.get_external_data = NULL;
    input.value.external_data.length = 50;
    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_INPUT_ARG);
}

AVS_UNIT_TEST(text_encoder, external_string_handler_error) {
    text_test_env_t env;
    text_test_setup(&env);

    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_EXTERNAL_STRING;
    input.value.external_data.get_external_data = external_data_handler_failure;
    input.value.external_data.length = 50;
    env.entry = input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_payload(&env.ctx, env.buf,
                                                      env.buffer_length,
                                                      &env.copied_bytes),
                          -1);
}

#    define TEST_BYTES(Name, DataInput, DataEncoded)                          \
        AVS_UNIT_TEST(text_encoder, bytes_##Name) {                           \
            fluf_io_out_entry_t input = { 0 };                                \
            input.type = FLUF_DATA_TYPE_BYTES;                                \
            input.value.bytes_or_string.chunk_length = sizeof(DataInput) - 1; \
            input.value.bytes_or_string.data = DataInput;                     \
            input.value.bytes_or_string.offset = 0;                           \
            test_data_t expected = MAKE_TEST_DATA(DataEncoded);               \
            test_bytes(&expected, &input);                                    \
        }

TEST_BYTES(4bytes, "\x01\x02\x03\x04", "AQIDBA==")
TEST_BYTES(5bytes, "\x64\x49\x45\x54\x46", "ZElFVEY=")
TEST_BYTES(23bytes,
           "\x84\x11\xDB\xB8\xAA\xF7\xC3\xEF\xBA\xC0\x2F\x50\xC2\x88\xAF\x1B"
           "\x8F\xD2\xE4\xC9\x5A\xD7\xEC",
           "hBHbuKr3w++6wC9QwoivG4/S5Mla1+w=")
TEST_BYTES(24bytes,
           "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
           "\x3F\xAC\x35\x03\x16\x1E\x32\x0A",
           "RgoALcBo1OWN3Ddd8IPN2D+sNQMWHjIK")
TEST_BYTES(60bytes,
           "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
           "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x46\x0A\x00\x2D\xC0\x68\xD4\xE5"
           "\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
           "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x3F\xAC\x35\x03\x16\x1E\x32\x0A"
           "\x46\x0A\x00\x2D",
           "RgoALcBo1OWN3Ddd8IPN2D+sNQMWHjIKRgoALcBo1OWN3Ddd8IPN2D+"
           "sNQMWHjIKP6w1AxYeMgpGCgAt")
TEST_BYTES(
        61bytes,
        "\x0F\x34\x21\x26\xCD\xB5\x30\xEE\xC5\x48\xBB\x6F\x03\x62\xC2\x7B\x21"
        "\x52\xB6\xEA\xFA\x4E\x09\xD3\xB8\x40\x85\x7D\xDA\xB1\xC8\xFF\x65\xB7"
        "\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\xFF\xA9\xAB\x9E\x67\x04\x0A\x3A\x1B\xE7\x77\x53\x9A\xA1\x6D\xDA\xA0"
        "\x0A\x00\x2D\x23",
        "DzQhJs21MO7FSLtvA2LCeyFStur6TgnTuECFfdqxyP9lt9w3XfCDzdj/"
        "qaueZwQKOhvnd1OaoW3aoAoALSM=")
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
        "\xBF\x46\x19\x74\x1D\xC4\x7B\xFB\x52\xA4\x32\x47\xA7\x5C\xA1\x5C\x1A",
        "1vsggM5EMTvhY9mJNpAGVpz2TCQENOqN8/FA6jpB4Vf/"
        "ksyuQhAnSEdufBGbWiFaUfdFsF47gSbp"
        "sIrxk8qms9fgFuy/"
        "9SEWx1BsmqiOSanxWYzDgA80ISbNtTDuxUi7bwNiwnshYAjiWNPgZDpLWRb9"
        "jgVBRr37yHtNwzgBlDFQ/"
        "Oe+etrWVnQcf3WxWRVOho5xsP9pYNy8Urbq+k4J07hAhX3ascj/Zbf/"
        "qaueZwQKOhvnd1OaoW3aoLvAkaE4kw4z30uegwz0cx7Wg5JUPXMf7MrZH+"
        "I9V9F8VIj7Ps9+iimY"
        "iUq7L+WxNiuLj79GGXQdxHv7UqQyR6dcoVwa")
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
           "\x97\x56\x82\x29\xFF\x8F\x61\x6C\xA5\xD0\x08\x20\xAE\x49\x5B\x04",
           "2OLm7ZAFKTsXrI0zk1LZa/"
           "L7IHQ+nO+tuwPODsW9DS9CbRzW2yn49qSWPXqK7ubyVhy+znEwO+zJ"
           "hnGWhlGiyiOKCx1nPFC4ZkxkjDHNEQXKVku7eRiPW/"
           "HgHoU4vnpvMEr9sxupUrQOlXODpTOfDAQu"
           "M7PVC24CDMcNGhpIDJIbYoPPwVyQvIM7kr+"
           "OznzWmXfyZpIMxgoRgL4DWSOJ9u86Wgfr70fwH/C0"
           "lgEb6VFAcBbdspvrQqxuReauj86axMsJ5yzkSIbwnFYs7xvQjpLUYRVGdhky35+"
           "YwAr3rqnXYeyL"
           "eOWqxgtdmB2G5ldnl1aCKf+PYWyl0AggrklbBA==")

#    define TEST_BYTES_EXT(Name, DataInput, DataEncoded)              \
        AVS_UNIT_TEST(text_encoder, bytes_ext_##Name) {               \
            fluf_io_out_entry_t input = { 0 };                        \
            input.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;               \
            input.value.external_data.get_external_data =             \
                    external_data_handler;                            \
            input.value.external_data.length = sizeof(DataInput) - 1; \
            ptr_for_callback = DataInput;                             \
            test_data_t expected = MAKE_TEST_DATA(DataEncoded);       \
            test_bytes(&expected, &input);                            \
        }

TEST_BYTES_EXT(4bytes, "\x01\x02\x03\x04", "AQIDBA==")
TEST_BYTES_EXT(5bytes, "\x64\x49\x45\x54\x46", "ZElFVEY=")
TEST_BYTES_EXT(
        23bytes,
        "\x84\x11\xDB\xB8\xAA\xF7\xC3\xEF\xBA\xC0\x2F\x50\xC2\x88\xAF\x1B"
        "\x8F\xD2\xE4\xC9\x5A\xD7\xEC",
        "hBHbuKr3w++6wC9QwoivG4/S5Mla1+w=")
TEST_BYTES_EXT(
        24bytes,
        "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\x3F\xAC\x35\x03\x16\x1E\x32\x0A",
        "RgoALcBo1OWN3Ddd8IPN2D+sNQMWHjIK")
TEST_BYTES_EXT(
        60bytes,
        "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x46\x0A\x00\x2D\xC0\x68\xD4\xE5"
        "\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\x3F\xAC\x35\x03\x16\x1E\x32\x0A\x3F\xAC\x35\x03\x16\x1E\x32\x0A"
        "\x46\x0A\x00\x2D",
        "RgoALcBo1OWN3Ddd8IPN2D+sNQMWHjIKRgoALcBo1OWN3Ddd8IPN2D+"
        "sNQMWHjIKP6w1AxYeMgpGCgAt")
TEST_BYTES_EXT(
        61bytes,
        "\x0F\x34\x21\x26\xCD\xB5\x30\xEE\xC5\x48\xBB\x6F\x03\x62\xC2\x7B\x21"
        "\x52\xB6\xEA\xFA\x4E\x09\xD3\xB8\x40\x85\x7D\xDA\xB1\xC8\xFF\x65\xB7"
        "\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\xFF\xA9\xAB\x9E\x67\x04\x0A\x3A\x1B\xE7\x77\x53\x9A\xA1\x6D\xDA\xA0"
        "\x0A\x00\x2D\x23",
        "DzQhJs21MO7FSLtvA2LCeyFStur6TgnTuECFfdqxyP9lt9w3XfCDzdj/"
        "qaueZwQKOhvnd1OaoW3aoAoALSM=")

#    define TEST_BYTES_EMPTY_CHUNK_LENGTH(Name, DataInput, DataEncoded) \
        AVS_UNIT_TEST(text_encoder, bytes_empty_ch_len_##Name) {        \
            fluf_io_out_entry_t input = { 0 };                          \
            input.type = FLUF_DATA_TYPE_BYTES;                          \
            input.value.bytes_or_string.chunk_length = 0;               \
            input.value.bytes_or_string.data = DataInput;               \
            input.value.bytes_or_string.offset = 0;                     \
            test_data_t expected = MAKE_TEST_DATA(DataEncoded);         \
            test_bytes(&expected, &input);                              \
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
AVS_UNIT_TEST(text_encoder, bytes_offset) {
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
    text_test_env_t env;
    text_test_setup(&env);
    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry),
                          FLUF_IO_ERR_INPUT_ARG);
}

static void test_out_buff_smaller_than_internal_buff(text_test_env_t *env,
                                                     fluf_io_out_entry_t *input,
                                                     size_t buffer_length,
                                                     test_data_t *expected) {
    text_test_setup(env);
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
            AVS_UNIT_ASSERT_EQUAL(env->ctx._buff.b64_cache.cache_offset, 0);
            break;
        } else if (res == FLUF_IO_NEED_NEXT_CALL) {
            AVS_UNIT_ASSERT_EQUAL(env->copied_bytes, env->buffer_length);
        }
    }
    AVS_UNIT_ASSERT_EQUAL(copied_bytes, expected->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env->buf, expected->data, copied_bytes);
}

#    define TEST_BYTES_COMPLEX(Name, DataInput, DataEncoded, buffer_length)   \
        AVS_UNIT_TEST(text_encoder, bytes_complex_##Name) {                   \
            fluf_io_out_entry_t input = { 0 };                                \
            input.type = FLUF_DATA_TYPE_BYTES;                                \
            input.value.bytes_or_string.chunk_length = sizeof(DataInput) - 1; \
            input.value.bytes_or_string.data = DataInput;                     \
            input.value.bytes_or_string.offset = 0;                           \
                                                                              \
            text_test_env_t env = { 0 };                                      \
            test_data_t expected = MAKE_TEST_DATA(DataEncoded);               \
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
static char DataEncoded62[] =
        "DzQhJs21MO7FSLtvA2LCeyFStur6TgnTuECFfdqxyP9lt9w3XfCDzdj/"
        "qaueZwQKOhvnd1OaoW3aoAoOLSM=";
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
static char DataEncoded257[] =
        "2OLm7ZAFKTsXrI0zk1LZa/"
        "L7IHQ+nO+tuwPODsW9DS9CbRzW2yn49qSWPXqK7ubyVhy+znEwO+zJ"
        "hnGWhlGiyiOKCx1nPFC4ZkxkjDHNEQXKVku7eRiPW/"
        "HgHoU4vnpvMEr9sxupUrQOlXODpTOfDAQu"
        "M7PVC24CDMcNGhpIDJIbYoPPwVyQvIM7kr+"
        "OznzWmXfyZpIMxgoRgL4DWSOJ9u86Wgfr70fwH/C0"
        "lgEb6VFAcBbdspvrQqxuReauj86axMsJ5yzkSIbwnFYs7xvQjpLUYRVGdhky35+"
        "YwAr3rqnXYeyLeOWqxgtdmB2G5ldnl1aCKf+PYWyl0AggrklbBA==";
TEST_BYTES_COMPLEX(buf_len_1, DataToEncode62, DataEncoded62, 1);
TEST_BYTES_COMPLEX(buf_len_2, DataToEncode62, DataEncoded62, 2);
TEST_BYTES_COMPLEX(buf_len_20, DataToEncode62, DataEncoded62, 20);
TEST_BYTES_COMPLEX(buf_len_21, DataToEncode62, DataEncoded62, 21);
TEST_BYTES_COMPLEX(buf_len_22, DataToEncode62, DataEncoded62, 22);
TEST_BYTES_COMPLEX(buf_len_23, DataToEncode62, DataEncoded62, 23);
TEST_BYTES_COMPLEX(buf_len_24, DataToEncode62, DataEncoded62, 24);
TEST_BYTES_COMPLEX(buf_len_20_input_257, DataToEncode257, DataEncoded257, 20);
TEST_BYTES_COMPLEX(buf_len_21_input_257, DataToEncode257, DataEncoded257, 21);
TEST_BYTES_COMPLEX(buf_len_100_input_257, DataToEncode257, DataEncoded257, 100);
TEST_BYTES_COMPLEX(buf_len_101_input_257, DataToEncode257, DataEncoded257, 101);

#    define TEST_EXT_BYTES_COMPLEX(Name, DataInput, DataEncoded,      \
                                   buffer_length)                     \
        AVS_UNIT_TEST(text_encoder, bytes_ext_complex_##Name) {       \
            fluf_io_out_entry_t input = { 0 };                        \
            input.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;               \
            input.value.external_data.get_external_data =             \
                    external_data_handler;                            \
            input.value.external_data.length = sizeof(DataInput) - 1; \
            ptr_for_callback = DataInput;                             \
                                                                      \
            text_test_env_t env = { 0 };                              \
            test_data_t expected = MAKE_TEST_DATA(DataEncoded);       \
            test_out_buff_smaller_than_internal_buff(                 \
                    &env, &input, buffer_length, &expected);          \
        }

TEST_EXT_BYTES_COMPLEX(buf_len_1, DataToEncode62, DataEncoded62, 1);
TEST_EXT_BYTES_COMPLEX(buf_len_2, DataToEncode62, DataEncoded62, 2);
TEST_EXT_BYTES_COMPLEX(buf_len_20, DataToEncode62, DataEncoded62, 20);
TEST_EXT_BYTES_COMPLEX(buf_len_21, DataToEncode62, DataEncoded62, 21);
TEST_EXT_BYTES_COMPLEX(buf_len_22, DataToEncode62, DataEncoded62, 22);
TEST_EXT_BYTES_COMPLEX(buf_len_23, DataToEncode62, DataEncoded62, 23);
TEST_EXT_BYTES_COMPLEX(buf_len_24, DataToEncode62, DataEncoded62, 24);
TEST_EXT_BYTES_COMPLEX(buf_len_20_input_257,
                       DataToEncode257,
                       DataEncoded257,
                       20);
TEST_EXT_BYTES_COMPLEX(buf_len_21_input_257,
                       DataToEncode257,
                       DataEncoded257,
                       21);
TEST_EXT_BYTES_COMPLEX(buf_len_100_input_257,
                       DataToEncode257,
                       DataEncoded257,
                       100);
TEST_EXT_BYTES_COMPLEX(buf_len_101_input_257,
                       DataToEncode257,
                       DataEncoded257,
                       101);
TEST_EXT_BYTES_COMPLEX(buf_len_250_input_257,
                       DataToEncode257,
                       DataEncoded257,
                       250);
TEST_EXT_BYTES_COMPLEX(buf_len_251_input_257,
                       DataToEncode257,
                       DataEncoded257,
                       251);

AVS_UNIT_TEST(text_encoder, bytes_empty_input) {
    char DataInput[] = "";
    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_BYTES;
    input.value.bytes_or_string.chunk_length = sizeof(DataInput) - 1;
    input.value.bytes_or_string.data = DataInput;
    input.value.bytes_or_string.offset = 0;

    text_test_env_t env;
    text_test_setup(&env);

    env.entry = input;

    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry), 0);
    AVS_UNIT_ASSERT_EQUAL(env.ctx._encoder._text.entry_added, true);
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_payload(&env.ctx, env.buf,
                                                      env.buffer_length,
                                                      &env.copied_bytes),
                          0);
    AVS_UNIT_ASSERT_EQUAL(0, env.copied_bytes);
}

#    define TEST_INT_IMPL(Name, Num, DataEncoded)               \
        AVS_UNIT_TEST(text_encoder, Name) {                     \
            fluf_io_out_entry_t input = { 0 };                  \
            input.type = FLUF_DATA_TYPE_INT;                    \
            input.value.int_value = Num;                        \
            test_data_t expected = MAKE_TEST_DATA(DataEncoded); \
            test_bytes(&expected, &input);                      \
        }

#    define TEST_INT(Num, Data) \
        TEST_INT_IMPL(AVS_CONCAT(int, __LINE__), Num, Data);

TEST_INT(0, "0")
TEST_INT(1, "1")
TEST_INT(10, "10")
TEST_INT(23, "23")
TEST_INT(24, "24")
TEST_INT(25, "25")
TEST_INT(100, "100")
TEST_INT(221, "221")
TEST_INT(1000, "1000")
TEST_INT(INT16_MAX, "32767")
TEST_INT(INT16_MAX + 1, "32768")
TEST_INT(UINT16_MAX, "65535")
TEST_INT(UINT16_MAX + 1, "65536")
TEST_INT(1000000, "1000000")
TEST_INT(INT32_MAX, "2147483647")
TEST_INT((int64_t) INT32_MAX + 1, "2147483648")
TEST_INT(UINT32_MAX, "4294967295")
TEST_INT((int64_t) UINT32_MAX + 1, "4294967296")
TEST_INT(INT64_MAX, "9223372036854775807")

TEST_INT(-1, "-1")
TEST_INT(-10, "-10")
TEST_INT(-23, "-23")
TEST_INT(-24, "-24")
TEST_INT(-25, "-25")
TEST_INT(-100, "-100")
TEST_INT(-221, "-221")
TEST_INT(-1000, "-1000")
TEST_INT(INT64_MIN, "-9223372036854775808")

AVS_UNIT_TEST(text_encoder, int_out_buff_smaller_than_internal_buff) {
    char DataEncoded[] = "92233720368547758";
    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_INT;
    input.value.int_value = 92233720368547758;

    text_test_env_t env = { 0 };
    test_data_t expected = MAKE_TEST_DATA(DataEncoded);
    test_out_buff_smaller_than_internal_buff(&env, &input, 20, &expected);
}

#    define TEST_UINT_IMPL(Name, Num, DataEncoded)              \
        AVS_UNIT_TEST(text_encoder, Name) {                     \
            fluf_io_out_entry_t input = { 0 };                  \
            input.type = FLUF_DATA_TYPE_UINT;                   \
            input.value.uint_value = Num;                       \
            test_data_t expected = MAKE_TEST_DATA(DataEncoded); \
            test_bytes(&expected, &input);                      \
        }

#    define TEST_UINT(Num, Data) \
        TEST_INT_IMPL(AVS_CONCAT(int, __LINE__), Num, Data);

TEST_UINT(0, "0")
TEST_UINT(1, "1")
TEST_UINT(10, "10")
TEST_UINT(23, "23")
TEST_UINT(24, "24")
TEST_UINT(25, "25")
TEST_UINT(100, "100")
TEST_UINT(221, "221")
TEST_UINT(1000, "1000")
TEST_UINT(INT16_MAX, "32767")
TEST_UINT(INT16_MAX + 1, "32768")
TEST_UINT(UINT16_MAX, "65535")
TEST_UINT(UINT16_MAX + 1, "65536")
TEST_UINT(1000000, "1000000")
TEST_UINT(INT32_MAX, "2147483647")
TEST_UINT((int64_t) INT32_MAX + 1, "2147483648")
TEST_UINT(UINT32_MAX, "4294967295")
TEST_UINT((int64_t) UINT32_MAX + 1, "4294967296")
TEST_UINT(INT64_MAX, "9223372036854775807")

#    define TEST_STRING_IMPL(Name, StringInput, DataExpected)    \
        AVS_UNIT_TEST(text_encoder, Name) {                      \
            fluf_io_out_entry_t input = { 0 };                   \
            input.type = FLUF_DATA_TYPE_STRING;                  \
            input.value.bytes_or_string.chunk_length =           \
                    sizeof(StringInput) - 1;                     \
            input.value.bytes_or_string.data = StringInput;      \
            input.value.bytes_or_string.offset = 0;              \
            test_data_t expected = MAKE_TEST_DATA(DataExpected); \
            test_bytes(&expected, &input);                       \
        }

#    define TEST_STRING(StringInput, DataExpected)                  \
        TEST_STRING_IMPL(AVS_CONCAT(string, __LINE__), StringInput, \
                         DataExpected);

TEST_STRING("Anjay4.0", "Anjay4.0")
TEST_STRING("Anjay4.0 is going to be lighter than original Anjay",
            "Anjay4.0 is going to be lighter than original Anjay")
TEST_STRING("Anjay4.0 is going to be lighter than original Anjay."
            "Anjay4.0 is going to be lighter than original Anjay."
            "Anjay4.0 is going to be lighter than original Anjay."
            "Anjay4.0 is going to be lighter than original Anjay.",
            "Anjay4.0 is going to be lighter than original Anjay."
            "Anjay4.0 is going to be lighter than original Anjay."
            "Anjay4.0 is going to be lighter than original Anjay."
            "Anjay4.0 is going to be lighter than original Anjay.")

AVS_UNIT_TEST(text_encoder, string_out_buff_smaller_than_input) {
    char DataInputOutput[] =
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay";
    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_STRING;
    input.value.bytes_or_string.chunk_length = sizeof(DataInputOutput) - 1;
    input.value.bytes_or_string.data = DataInputOutput;
    input.value.bytes_or_string.offset = 0;

    text_test_env_t env = { 0 };
    test_data_t expected = MAKE_TEST_DATA(DataInputOutput);
    test_out_buff_smaller_than_internal_buff(&env, &input, 100, &expected);
}

#    define TEST_STRING_EXT_IMPL(Name, StringInput, DataExpected)       \
        AVS_UNIT_TEST(text_encoder, Name) {                             \
            fluf_io_out_entry_t input = { 0 };                          \
            input.type = FLUF_DATA_TYPE_EXTERNAL_STRING;                \
            input.value.external_data.get_external_data =               \
                    external_data_handler;                              \
            input.value.external_data.length = sizeof(StringInput) - 1; \
            ptr_for_callback = StringInput;                             \
            test_data_t expected = MAKE_TEST_DATA(DataExpected);        \
            test_bytes(&expected, &input);                              \
        }

#    define TEST_STRING_EXT(StringInput, DataExpected)                  \
        TEST_STRING_EXT_IMPL(AVS_CONCAT(string, __LINE__), StringInput, \
                             DataExpected);

TEST_STRING_EXT("Anjay4.0", "Anjay4.0")
TEST_STRING_EXT("Anjay4.0 is going to be lighter than original Anjay",
                "Anjay4.0 is going to be lighter than original Anjay")
TEST_STRING_EXT("Anjay4.0 is going to be lighter than original Anjay."
                "Anjay4.0 is going to be lighter than original Anjay."
                "Anjay4.0 is going to be lighter than original Anjay."
                "Anjay4.0 is going to be lighter than original Anjay.",
                "Anjay4.0 is going to be lighter than original Anjay."
                "Anjay4.0 is going to be lighter than original Anjay."
                "Anjay4.0 is going to be lighter than original Anjay."
                "Anjay4.0 is going to be lighter than original Anjay.")

AVS_UNIT_TEST(text_encoder, string_ext_out_buff_smaller_than_input) {
    char DataInputOutput[] =
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay"
            "Anjay4.0 is going to be lighter than original Anjay";
    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_EXTERNAL_STRING;
    input.value.external_data.get_external_data = external_data_handler;
    input.value.external_data.length = sizeof(DataInputOutput) - 1;
    ptr_for_callback = DataInputOutput;

    text_test_env_t env = { 0 };
    test_data_t expected = MAKE_TEST_DATA(DataInputOutput);
    test_out_buff_smaller_than_internal_buff(&env, &input, 100, &expected);
}

#    define TEST_DOUBLE_IMPL(Name, Num, DataEncoded)            \
        AVS_UNIT_TEST(text_encoder, Name) {                     \
            fluf_io_out_entry_t input = { 0 };                  \
            input.type = FLUF_DATA_TYPE_DOUBLE;                 \
            input.value.double_value = Num;                     \
            test_data_t expected = MAKE_TEST_DATA(DataEncoded); \
            test_bytes(&expected, &input);                      \
        }

#    define TEST_DOUBLE(Num, Data) \
        TEST_DOUBLE_IMPL(AVS_CONCAT(double, __LINE__), Num, Data);

TEST_DOUBLE(-0.0, "0")
TEST_DOUBLE(100000.0, "100000")
TEST_DOUBLE(1.1, "1.1")
TEST_DOUBLE(1.0e+3, "1000")
TEST_DOUBLE(-4.1, "-4.1")
TEST_DOUBLE(10000.5, "10000.5");
TEST_DOUBLE(10000000000000.5, "10000000000000.5");
TEST_DOUBLE(3.26e218, "3.26e218");

static void test_bool(char *expected_data, fluf_io_out_entry_t *input) {
    text_test_env_t env;
    text_test_setup(&env);

    env.entry = *input;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.copied_bytes));
    AVS_UNIT_ASSERT_EQUAL(env.copied_bytes, 1);
    AVS_UNIT_ASSERT_EQUAL(env.buf[0], expected_data[0]);
}

AVS_UNIT_TEST(text_encoder, bool_false) {
    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_BOOL;
    input.value.bool_value = false;
    test_bool("0", &input);
}

AVS_UNIT_TEST(text_encoder, bool_true) {
    fluf_io_out_entry_t input = { 0 };
    input.type = FLUF_DATA_TYPE_BOOL;
    input.value.bool_value = true;
    test_bool("1", &input);
}

#    define TEST_OBJLINK_IMPL(Name, Oid, Iid, Data)  \
        AVS_UNIT_TEST(text_encoder, Name) {          \
            test_data_t data = MAKE_TEST_DATA(Data); \
            fluf_io_out_entry_t value = { 0 };       \
            value.type = FLUF_DATA_TYPE_OBJLNK;      \
            value.value.objlnk.oid = Oid;            \
            value.value.objlnk.iid = Iid;            \
            test_bytes(&data, &value);               \
        }

#    define TEST_OBJLINK(Oid, Iid, Data) \
        TEST_OBJLINK_IMPL(AVS_CONCAT(objlink, __LINE__), Oid, Iid, Data);

TEST_OBJLINK(0, 0, "0:0");
TEST_OBJLINK(1, 1, "1:1");
TEST_OBJLINK(2, 0, "2:0");
TEST_OBJLINK(0, 5, "0:5");
TEST_OBJLINK(2, 13, "2:13");
TEST_OBJLINK(21, 37, "21:37");
TEST_OBJLINK(2137, 1, "2137:1");
TEST_OBJLINK(1111, 2222, "1111:2222");
TEST_OBJLINK(11111, 50001, "11111:50001");
TEST_OBJLINK(0, 60001, "0:60001");

#    define TEST_TIME_IMPL(Name, Num, Data)          \
        AVS_UNIT_TEST(text_encoder, Name) {          \
            test_data_t data = MAKE_TEST_DATA(Data); \
            fluf_io_out_entry_t value = { 0 };       \
            value.type = FLUF_DATA_TYPE_TIME;        \
            value.value.time_value = Num;            \
            test_bytes(&data, &value);               \
        }

#    define TEST_TIME(Num, Data) \
        TEST_TIME_IMPL(AVS_CONCAT(time_, __LINE__), Num, Data);

TEST_TIME(24, "24")
TEST_TIME(UINT32_MAX, "4294967295")

#endif // FLUF_WITH_PLAINTEXT
