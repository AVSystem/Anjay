/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifdef FLUF_WITH_CBOR

#    include <string.h>

#    include <avsystem/commons/avs_unit_test.h>

#    include <fluf/fluf_io.h>
#    include <fluf/fluf_utils.h>

typedef struct {
    fluf_io_out_ctx_t ctx;
    fluf_io_out_entry_t entry;
    char buf[300];
    size_t buffer_length;
    size_t out_length;
} cbor_test_env_t;

static void cbor_test_setup(cbor_test_env_t *env) {
    memset(env, 0, sizeof(cbor_test_env_t));
    env->buffer_length = sizeof(env->buf);
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_init(&env->ctx, FLUF_OP_DM_READ,
                                                 &FLUF_MAKE_ROOT_PATH(), 1,
                                                 FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_EQUAL(fluf_io_out_ctx_get_format(&env->ctx),
                          FLUF_COAP_FORMAT_CBOR);
}

typedef struct {
    const char *data;
    size_t size;
} test_data_t;

#    define MAKE_TEST_DATA(Data)     \
        (test_data_t) {              \
            .data = Data,            \
            .size = sizeof(Data) - 1 \
        }

static void verify_bytes(cbor_test_env_t *env, test_data_t *data) {
    AVS_UNIT_ASSERT_EQUAL(env->out_length, data->size);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env->buf, data->data, data->size);
}

static void test_simple_variable(test_data_t *data,
                                 fluf_io_out_entry_t *value) {
    cbor_test_env_t env;
    cbor_test_setup(&env);

    env.entry = *value;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    verify_bytes(&env, data);
}

#    define TEST_INT_IMPL(Name, Num, Data)           \
        AVS_UNIT_TEST(cbor_encoder, Name) {          \
            test_data_t data = MAKE_TEST_DATA(Data); \
            fluf_io_out_entry_t value = { 0 };       \
            value.type = FLUF_DATA_TYPE_INT;         \
            value.value.int_value = Num;             \
            test_simple_variable(&data, &value);     \
        }

#    define TEST_INT(Num, Data) \
        TEST_INT_IMPL(AVS_CONCAT(int, __LINE__), Num, Data);

TEST_INT(0, "\x00")
TEST_INT(1, "\x01")
TEST_INT(10, "\x0A")
TEST_INT(23, "\x17")
TEST_INT(24, "\x18\x18")
TEST_INT(25, "\x18\x19")
TEST_INT(100, "\x18\x64")
TEST_INT(221, "\x18\xDD")
TEST_INT(1000, "\x19\x03\xE8")
TEST_INT(INT16_MAX, "\x19\x7F\xFF")
TEST_INT(INT16_MAX + 1, "\x19\x80\x00")
TEST_INT(UINT16_MAX, "\x19\xFF\xFF")
TEST_INT(UINT16_MAX + 1, "\x1A\x00\x01\x00\x00")
TEST_INT(1000000, "\x1A\x00\x0F\x42\x40")
TEST_INT(INT32_MAX, "\x1A\x7F\xFF\xFF\xFF")
TEST_INT((int64_t) INT32_MAX + 1, "\x1A\x80\x00\x00\x00")
TEST_INT(UINT32_MAX, "\x1A\xFF\xFF\xFF\xFF")
TEST_INT((int64_t) UINT32_MAX + 1, "\x1B\x00\x00\x00\x01\x00\x00\x00\x00")
TEST_INT(INT64_MAX, "\x1B\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF")

TEST_INT(-1, "\x20")
TEST_INT(-10, "\x29")
TEST_INT(-24, "\x37")
TEST_INT(-25, "\x38\x18")
TEST_INT(-100, "\x38\x63")
TEST_INT(-256, "\x38\xFF")
TEST_INT(-257, "\x39\x01\x00")
TEST_INT(-1000, "\x39\x03\xE7")
TEST_INT(INT64_MIN, "\x3B\x7F\xFF\xFF\xFF\xFF\xFF\xFF\xFF")

#    define TEST_TIME_IMPL(Name, Num, Data)          \
        AVS_UNIT_TEST(cbor_encoder, Name) {          \
            test_data_t data = MAKE_TEST_DATA(Data); \
            fluf_io_out_entry_t value = { 0 };       \
            value.type = FLUF_DATA_TYPE_TIME;        \
            value.value.time_value = Num;            \
            test_simple_variable(&data, &value);     \
        }

#    define TEST_TIME(Num, Data) \
        TEST_TIME_IMPL(AVS_CONCAT(time_, __LINE__), Num, Data);

TEST_TIME(24, "\xC1\x18\x18")
TEST_TIME(UINT32_MAX, "\xC1\x1A\xFF\xFF\xFF\xFF")

#    define TEST_BOOL_IMPL(Name, Num, Data)          \
        AVS_UNIT_TEST(cbor_encoder, Name) {          \
            test_data_t data = MAKE_TEST_DATA(Data); \
            fluf_io_out_entry_t value = { 0 };       \
            value.type = FLUF_DATA_TYPE_BOOL;        \
            value.value.bool_value = Num;            \
            test_simple_variable(&data, &value);     \
        }

#    define TEST_BOOL(Num, Data) \
        TEST_BOOL_IMPL(AVS_CONCAT(bool, __LINE__), Num, Data);

TEST_BOOL(true, "\xF5")
TEST_BOOL(false, "\xF4")

#    define TEST_DOUBLE_IMPL(Name, Num, Data)        \
        AVS_UNIT_TEST(cbor_encoder, Name) {          \
            test_data_t data = MAKE_TEST_DATA(Data); \
            fluf_io_out_entry_t value = { 0 };       \
            value.type = FLUF_DATA_TYPE_DOUBLE;      \
            value.value.double_value = Num;          \
            test_simple_variable(&data, &value);     \
        }

#    define TEST_DOUBLE(Num, Data) \
        TEST_DOUBLE_IMPL(AVS_CONCAT(double, __LINE__), Num, Data);

TEST_DOUBLE(-0.0, "\xFA\x80\x00\x00\x00")
TEST_DOUBLE(100000.0, "\xFA\x47\xC3\x50\x00")

TEST_DOUBLE(1.1, "\xFB\x3F\xF1\x99\x99\x99\x99\x99\x9A")
TEST_DOUBLE(100000.0, "\xFA\x47\xC3\x50\x00")
TEST_DOUBLE(1.0e+300, "\xFB\x7E\x37\xE4\x3C\x88\x00\x75\x9C")
TEST_DOUBLE(-4.1, "\xFB\xC0\x10\x66\x66\x66\x66\x66\x66")

#    define TEST_OBJLINK_IMPL(Name, Oid, Iid, Data)  \
        AVS_UNIT_TEST(cbor_encoder, Name) {          \
            test_data_t data = MAKE_TEST_DATA(Data); \
            fluf_io_out_entry_t value = { 0 };       \
            value.type = FLUF_DATA_TYPE_OBJLNK;      \
            value.value.objlnk.oid = Oid;            \
            value.value.objlnk.iid = Iid;            \
            test_simple_variable(&data, &value);     \
        }

#    define TEST_OBJLINK(Oid, Iid, Data) \
        TEST_OBJLINK_IMPL(AVS_CONCAT(objlink, __LINE__), Oid, Iid, Data);

TEST_OBJLINK(0, 0, "\x63\x30\x3A\x30");
TEST_OBJLINK(1, 1, "\x63\x31\x3A\x31");
TEST_OBJLINK(2, 0, "\x63\x32\x3A\x30");
TEST_OBJLINK(0, 5, "\x63\x30\x3A\x35");
TEST_OBJLINK(2, 13, "\x64\x32\x3A\x31\x33");
TEST_OBJLINK(21, 37, "\x65\x32\x31\x3A\x33\x37");
TEST_OBJLINK(2137, 1, "\x66\x32\x31\x33\x37\x3A\x31");
TEST_OBJLINK(1111, 2222, "\x69\x31\x31\x31\x31\x3A\x32\x32\x32\x32");
TEST_OBJLINK(11111, 50001, "\x6B\x31\x31\x31\x31\x31\x3A\x35\x30\x30\x30\x31");
TEST_OBJLINK(0, 60001, "\x67\x30\x3A\x36\x30\x30\x30\x31");

static void test_string(test_data_t *data, fluf_io_out_entry_t *value) {
    cbor_test_env_t env;
    cbor_test_setup(&env);

    env.entry = *value;

    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_out_ctx_get_payload(
            &env.ctx, env.buf, env.buffer_length, &env.out_length));
    verify_bytes(&env, data);
};

#    define TEST_STRING_NAMED(Name, Text, ExpectedHeader)               \
        AVS_UNIT_TEST(cbor_encoder, string_##Name) {                    \
            test_data_t expected = MAKE_TEST_DATA(ExpectedHeader Text); \
            fluf_io_out_entry_t value = { 0 };                          \
            value.type = FLUF_DATA_TYPE_STRING;                         \
            value.value.bytes_or_string.data = Text;                    \
            test_string(&expected, &value);                             \
        }

#    define TEST_STRING(Text, ExpectedHeader) \
        TEST_STRING_NAMED(Text, AVS_QUOTE(Text), ExpectedHeader)

TEST_STRING(, "\x60")
TEST_STRING(a, "\x61")
TEST_STRING(1111, "\x64")
TEST_STRING_NAMED(dzborg, "DZBORG:DD", "\x69")
TEST_STRING_NAMED(escaped, "\"\\", "\x62")
TEST_STRING_NAMED(
        255chars,
        "oxazxnwrmthhloqwchkumektviptdztidxeelvgffcdoodpijsbikkkvrmtrxddmpidudj"
        "ptfmqqgfkjlrsqrmagculcyjjbmxombbiqdhimwafcfaswhmmykezictjpidmxtoqnjmja"
        "xzgvqdybtgneqsmlzhxqeuhibjopnregwykgpcdogguszhhffdeixispwfnwcufnmsxycy"
        "qxquiqsuqwgkwafkeedsacxvvjwhpokaabxelqxzqutwa",
        "\x78\xFF")
TEST_STRING_NAMED(
        256chars,
        "oqndmcvrgmvswuvcskllakhhersslftmmuwwwzirelnbtnlmvmezrqktqqnlpldqwyvtbv"
        "yryqcurqxnhzxoladzzmnumrifhqbcywuetmuyyjxpiwquzrekjxzgiknqcmwzwuzxvrxb"
        "zycnfrhyigwgkmbtlfyrhkolnsikvdelvkztkvonimtmvrivrnevgyxvjdjzvobsiufbwt"
        "atfqeavhvfdfbnsumtletbaheyacrkwgectlrdrizenuvi",
        "\x79\x01\x00")

#    define TEST_BYTES(Name, Data, ExpectedHeader)                       \
        AVS_UNIT_TEST(cbor_encoder, bytes_##Name) {                      \
            fluf_io_out_entry_t value = { 0 };                           \
            value.type = FLUF_DATA_TYPE_BYTES;                           \
            value.value.bytes_or_string.chunk_length = sizeof(Data) - 1; \
            value.value.bytes_or_string.data = Data;                     \
            test_data_t expected = MAKE_TEST_DATA(ExpectedHeader Data);  \
            test_string(&expected, &value);                              \
        }

TEST_BYTES(0bytes, "", "\x40")
TEST_BYTES(4bytes, "\x01\x02\x03\x04", "\x44")
TEST_BYTES(5bytes, "\x64\x49\x45\x54\x46", "\x45")
TEST_BYTES(23bytes,
           "\x84\x11\xDB\xB8\xAA\xF7\xC3\xEF\xBA\xC0\x2F\x50\xC2\x88\xAF\x1B"
           "\x8F\xD2\xE4\xC9\x5A\xD7\xEC",
           "\x57")
TEST_BYTES(24bytes,
           "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
           "\x3F\xAC\x35\x03\x16\x1E\x32\x0A",
           "\x58\x18")
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
        "\x58\xFF")
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
           "\x59\x01\x00")

static char *ptr_for_callback = NULL;
static int external_data_handler(void *buffer,
                                 size_t bytes_to_copy,
                                 size_t offset,
                                 void *args) {
    (void) args;
    memcpy(buffer, &ptr_for_callback[offset], bytes_to_copy);
    return 0;
}

#    define TEST_STRING_EXT(Name, Text, ExpectedHeader)                 \
        AVS_UNIT_TEST(cbor_encoder, string_ext_##Name) {                \
            test_data_t expected = MAKE_TEST_DATA(ExpectedHeader Text); \
            fluf_io_out_entry_t value = { 0 };                          \
            value.type = FLUF_DATA_TYPE_EXTERNAL_STRING;                \
            value.value.external_data.get_external_data =               \
                    external_data_handler;                              \
            value.value.external_data.length = sizeof(Text) - 1;        \
            ptr_for_callback = Text;                                    \
            test_string(&expected, &value);                             \
        }

#    define TEST_BYTES_EXT(Name, Text, ExpectedHeader)                  \
        AVS_UNIT_TEST(cbor_encoder, bytes_ext_##Name) {                 \
            test_data_t expected = MAKE_TEST_DATA(ExpectedHeader Text); \
            fluf_io_out_entry_t value = { 0 };                          \
            value.type = FLUF_DATA_TYPE_EXTERNAL_BYTES;                 \
            value.value.external_data.get_external_data =               \
                    external_data_handler;                              \
            value.value.external_data.length = sizeof(Text) - 1;        \
            ptr_for_callback = Text;                                    \
            test_string(&expected, &value);                             \
        }

TEST_STRING_EXT(empty, "", "\x60")
TEST_STRING_EXT(a, "a", "\x61")
TEST_STRING_EXT(ononeone, "1111", "\x64")
TEST_STRING_EXT(dzborg, "DZBORG:DD", "\x69")
TEST_STRING_EXT(escaped, "\"\\", "\x62")
TEST_STRING_EXT(
        255chars,
        "oxazxnwrmthhloqwchkumektviptdztidxeelvgffcdoodpijsbikkkvrmtrxddmpidudj"
        "ptfmqqgfkjlrsqrmagculcyjjbmxombbiqdhimwafcfaswhmmykezictjpidmxtoqnjmja"
        "xzgvqdybtgneqsmlzhxqeuhibjopnregwykgpcdogguszhhffdeixispwfnwcufnmsxycy"
        "qxquiqsuqwgkwafkeedsacxvvjwhpokaabxelqxzqutwa",
        "\x78\xFF")
TEST_STRING_EXT(
        256chars,
        "oqndmcvrgmvswuvcskllakhhersslftmmuwwwzirelnbtnlmvmezrqktqqnlpldqwyvtbv"
        "yryqcurqxnhzxoladzzmnumrifhqbcywuetmuyyjxpiwquzrekjxzgiknqcmwzwuzxvrxb"
        "zycnfrhyigwgkmbtlfyrhkolnsikvdelvkztkvonimtmvrivrnevgyxvjdjzvobsiufbwt"
        "atfqeavhvfdfbnsumtletbaheyacrkwgectlrdrizenuvi",
        "\x79\x01\x00")

TEST_BYTES_EXT(0bytes, "", "\x40")
TEST_BYTES_EXT(4bytes, "\x01\x02\x03\x04", "\x44")
TEST_BYTES_EXT(5bytes, "\x64\x49\x45\x54\x46", "\x45")
TEST_BYTES_EXT(
        23bytes,
        "\x84\x11\xDB\xB8\xAA\xF7\xC3\xEF\xBA\xC0\x2F\x50\xC2\x88\xAF\x1B"
        "\x8F\xD2\xE4\xC9\x5A\xD7\xEC",
        "\x57")
TEST_BYTES_EXT(
        24bytes,
        "\x46\x0A\x00\x2D\xC0\x68\xD4\xE5\x8D\xDC\x37\x5D\xF0\x83\xCD\xD8"
        "\x3F\xAC\x35\x03\x16\x1E\x32\x0A",
        "\x58\x18")
TEST_BYTES_EXT(
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
        "\x58\xFF")
TEST_BYTES_EXT(
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
        "\x59\x01\x00")

AVS_UNIT_TEST(cbor_encoder, partial_read_string) {
    cbor_test_env_t env;
    cbor_test_setup(&env);

    char *test_str = "oqndmcvrgmvswuvcskllakhhersslftmmuwwwzirelnbtnlmvmezrqktq"
                     "qnlpldqwyvtbv"
                     "yryqcurqxnhzxoladzzmnumrifhqbcywuetmuyyjxpiwquzrekjxzgikn"
                     "qcmwzwuzxvrxb"
                     "zycnfrhyigwgkmbtlfyrhkolnsikvdelvkztkvonimtmvrivrnevgyxvj"
                     "djzvobsiufbwt"
                     "atfqeavhvfdfbnsumtletbaheyacrkwgectlrdrizenuvi";
    char *target_str = "\x79\x01\x00"
                       "oqndmcvrgmvswuvcskllakhhersslftmmuwwwzirelnbtnlmvmezrqk"
                       "tqqnlpldqwyvtbv"
                       "yryqcurqxnhzxoladzzmnumrifhqbcywuetmuyyjxpiwquzrekjxzgi"
                       "knqcmwzwuzxvrxb"
                       "zycnfrhyigwgkmbtlfyrhkolnsikvdelvkztkvonimtmvrivrnevgyx"
                       "vjdjzvobsiufbwt"
                       "atfqeavhvfdfbnsumtletbaheyacrkwgectlrdrizenuvi";

    env.entry.type = FLUF_DATA_TYPE_STRING;
    env.entry.value.bytes_or_string.data = test_str;

    size_t total_len = 0;
    int res = -1;
    for (env.buffer_length = 10; env.buffer_length < sizeof(env.buf);
         env.buffer_length += 10) {
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
        while (res) {
            res = fluf_io_out_ctx_get_payload(&env.ctx, &env.buf[total_len],
                                              env.buffer_length,
                                              &env.out_length);
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
            AVS_UNIT_ASSERT_TRUE(env.out_length <= env.buffer_length);
            total_len += env.out_length;
        }
        AVS_UNIT_ASSERT_EQUAL(total_len, strlen(test_str) + 3);
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env.buf, target_str, total_len);

        total_len = 0;
        memset(env.buf, 0, sizeof(env.buf));
        memset(&env.ctx, 0, sizeof(fluf_io_out_ctx_t));
        env.ctx._format = FLUF_COAP_FORMAT_CBOR;
        res = -1;
        env.ctx._encoder._cbor.entry_added = false;
    }
}

AVS_UNIT_TEST(cbor_encoder, partial_read_ext) {
    cbor_test_env_t env;
    cbor_test_setup(&env);

    char *test_str = "oqndmcvrgmvswuvcskllakhhersslftmmuwwwzirelnbtnlmvmezrqktq"
                     "qnlpldqwyvtbv"
                     "yryqcurqxnhzxoladzzmnumrifhqbcywuetmuyyjxpiwquzrekjxzgikn"
                     "qcmwzwuzxvrxb"
                     "zycnfrhyigwgkmbtlfyrhkolnsikvdelvkztkvonimtmvrivrnevgyxvj"
                     "djzvobsiufbwt"
                     "atfqeavhvfdfbnsumtletbaheyacrkwgectlrdrizenuvi";
    char *target_str = "\x79\x01\x00"
                       "oqndmcvrgmvswuvcskllakhhersslftmmuwwwzirelnbtnlmvmezrqk"
                       "tqqnlpldqwyvtbv"
                       "yryqcurqxnhzxoladzzmnumrifhqbcywuetmuyyjxpiwquzrekjxzgi"
                       "knqcmwzwuzxvrxb"
                       "zycnfrhyigwgkmbtlfyrhkolnsikvdelvkztkvonimtmvrivrnevgyx"
                       "vjdjzvobsiufbwt"
                       "atfqeavhvfdfbnsumtletbaheyacrkwgectlrdrizenuvi";

    env.entry.type = FLUF_DATA_TYPE_EXTERNAL_STRING;
    env.entry.value.external_data.get_external_data = external_data_handler;
    env.entry.value.external_data.length = strlen(test_str);
    ptr_for_callback = test_str;

    size_t total_len = 0;
    int res = -1;
    for (env.buffer_length = 10; env.buffer_length < sizeof(env.buf);
         env.buffer_length += 10) {
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_out_ctx_new_entry(&env.ctx, &env.entry));
        while (res) {
            res = fluf_io_out_ctx_get_payload(&env.ctx, &env.buf[total_len],
                                              env.buffer_length,
                                              &env.out_length);
            AVS_UNIT_ASSERT_TRUE(res == 0 || res == FLUF_IO_NEED_NEXT_CALL);
            AVS_UNIT_ASSERT_TRUE(env.out_length <= env.buffer_length);
            total_len += env.out_length;
        }
        AVS_UNIT_ASSERT_EQUAL(total_len, strlen(test_str) + 3);
        AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(env.buf, target_str, total_len);

        total_len = 0;
        memset(env.buf, 0, sizeof(env.buf));
        memset(&env.ctx, 0, sizeof(fluf_io_out_ctx_t));
        env.ctx._format = FLUF_COAP_FORMAT_CBOR;
        res = -1;
        env.ctx._encoder._cbor.entry_added = false;
    }
}
#endif // FLUF_WITH_CBOR
