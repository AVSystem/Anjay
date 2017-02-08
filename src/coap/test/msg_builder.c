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

#include <endian.h>
#include <alloca.h>

#include <avsystem/commons/unit/test.h>

#define RANDOM_MSGID ((uint16_t)4)
#define RANDOM_MSGID_NBO ((uint16_t){htons(RANDOM_MSGID)})
#define RANDOM_MSGID_INIT { ((const uint8_t*)&RANDOM_MSGID_NBO)[0], \
                            ((const uint8_t*)&RANDOM_MSGID_NBO)[1] }

#define VTTL(version, type, token_length) \
    ((((version) & 0x03) << 6) | (((type) & 0x03) << 4) | ((token_length) & 0x07))

static anjay_coap_msg_t *make_msg_template(void *buffer,
                                           size_t buffer_size) {
    assert(buffer_size >= sizeof(anjay_coap_msg_t));

    anjay_coap_msg_t *msg = (anjay_coap_msg_t *)buffer;

    memset(buffer, 0, buffer_size);
    msg->length = (uint32_t)(buffer_size - sizeof(msg->length));
    msg->header = (anjay_coap_msg_header_t) {
        .version_type_token_length = VTTL(1, ANJAY_COAP_MSG_CONFIRMABLE, 0),
        .code = ANJAY_COAP_CODE_CONTENT,
        .message_id = RANDOM_MSGID_INIT
    };

    return msg;
}

static anjay_coap_msg_t *make_msg_template_with_data(void *buffer,
                                                     const void *data,
                                                     size_t data_size) {
    anjay_coap_msg_t *msg =
            make_msg_template(buffer, sizeof(anjay_coap_msg_t) + data_size);

    memcpy(msg->content, data, data_size);
    return msg;
}

#define MSG_TEMPLATE(DataSize) \
    make_msg_template(alloca(sizeof(anjay_coap_msg_t) + (DataSize)), \
                      sizeof(anjay_coap_msg_t) + DataSize)

#define MSG_TEMPLATE_WITH_DATA(Data, DataSize) \
    make_msg_template_with_data(alloca(sizeof(anjay_coap_msg_t) + (DataSize)), \
                                (Data), (DataSize))

#define INFO_WITH_DUMMY_HEADER \
    _anjay_coap_msg_info_init(); \
    info.type = ANJAY_COAP_MSG_CONFIRMABLE; \
    info.code = ANJAY_COAP_CODE_CONTENT; \
    info.identity.msg_id = 0

#define INFO_WITH_HEADER(HeaderPtr) \
    _anjay_coap_msg_info_init(); \
    info.type = _anjay_coap_msg_header_get_type(HeaderPtr); \
    info.code = (HeaderPtr)->code; \
    info.identity.msg_id = extract_u16((HeaderPtr)->message_id)

AVS_UNIT_TEST(coap_builder, header_only) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE(0);
    anjay_coap_msg_info_t info =
            INFO_WITH_HEADER(&msg_tpl->header);

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg_tpl));
    free(storage);
}

AVS_UNIT_TEST(coap_info, token) {
    anjay_coap_token_t TOKEN = {"A Token"};
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA(&TOKEN, ANJAY_COAP_MAX_TOKEN_LENGTH);
    _anjay_coap_msg_header_set_token_length(&msg_tpl->header, sizeof(TOKEN));

    anjay_coap_msg_info_t info =
            INFO_WITH_HEADER(&msg_tpl->header);
    info.identity.token = TOKEN;
    info.identity.token_size = sizeof(TOKEN);

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(msg_tpl->length) + msg_tpl->length);
    free(storage);
}

AVS_UNIT_TEST(coap_builder, option_empty) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA("\x00", 1);
    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_empty(&info, 0));

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg) + 1);

    _anjay_coap_msg_info_reset(&info);
    free(storage);
}

AVS_UNIT_TEST(coap_builder, option_opaque) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA("\x00" "foo", sizeof("\x00" "foo") - 1);
    _anjay_coap_opt_set_short_length((anjay_coap_opt_t *)msg_tpl->content, 3);

    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_opaque(&info, 0, "foo", sizeof("foo") - 1));

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg) + sizeof("\x00" "foo") - 1);

    _anjay_coap_msg_info_reset(&info);
    free(storage);
}

AVS_UNIT_TEST(coap_builder, option_multiple_ints) {
    uint32_t content_length = sizeof(uint8_t) + sizeof(uint8_t)
                            + sizeof(uint8_t) + sizeof(uint16_t)
                            + sizeof(uint8_t) + sizeof(uint32_t)
                            + sizeof(uint8_t) + sizeof(uint64_t)
                            + sizeof(uint8_t) + sizeof(uint8_t)
                            + sizeof(uint8_t);
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE(content_length);

    _anjay_coap_opt_set_short_length((anjay_coap_opt_t *)&msg_tpl->content[0], 1);
    memcpy(&msg_tpl->content[1], &(uint8_t){0x10}, 1);
    _anjay_coap_opt_set_short_length((anjay_coap_opt_t *)&msg_tpl->content[2], 2);
    memcpy(&msg_tpl->content[3], &(uint16_t){htobe16(0x2120)}, 2);
    _anjay_coap_opt_set_short_length((anjay_coap_opt_t *)&msg_tpl->content[5], 4);
    memcpy(&msg_tpl->content[6], &(uint32_t){htobe32(0x43424140)}, 4);
    _anjay_coap_opt_set_short_length((anjay_coap_opt_t *)&msg_tpl->content[10], 8);
    memcpy(&msg_tpl->content[11], &(uint64_t){htobe64(0x8786858483828180)}, 8);
    _anjay_coap_opt_set_short_length((anjay_coap_opt_t *)&msg_tpl->content[19], 1);
    memcpy(&msg_tpl->content[20], &(uint8_t){0xFF}, 1);
    _anjay_coap_opt_set_short_length((anjay_coap_opt_t *)&msg_tpl->content[21], 0);

    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_u8 (&info, 0, 0x10));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_u16(&info, 0, 0x2120));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_u32(&info, 0, 0x43424140));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_u64(&info, 0, 0x8786858483828180));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_u64(&info, 0, 0xFF));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_u64(&info, 0, 0));

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg) + content_length);

    _anjay_coap_msg_info_reset(&info);
    free(storage);
}

AVS_UNIT_TEST(coap_builder, option_content_format) {
    uint32_t content_length = sizeof(uint8_t) + sizeof(uint16_t);
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE(content_length);
    _anjay_coap_opt_set_short_length((anjay_coap_opt_t *)&msg_tpl->content[0], 2);
    _anjay_coap_opt_set_short_delta((anjay_coap_opt_t *)&msg_tpl->content[0], ANJAY_COAP_OPT_CONTENT_FORMAT);
    memcpy(&msg_tpl->content[1], &(uint16_t){htobe16(ANJAY_COAP_FORMAT_TLV)}, 2);

    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_content_format(&info, ANJAY_COAP_FORMAT_TLV));

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg) + content_length);

    _anjay_coap_msg_info_reset(&info);
    free(storage);
}

#define PAYLOAD "trololo"

AVS_UNIT_TEST(coap_builder, payload_only) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA("\xFF" PAYLOAD, sizeof("\xFF" PAYLOAD) - 1);
    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info)
                          + sizeof(ANJAY_COAP_PAYLOAD_MARKER)
                          + sizeof(PAYLOAD) - 1;
    void *storage = malloc(storage_size);

    anjay_coap_msg_builder_t builder;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_builder_init(
                &builder,
                _anjay_coap_ensure_aligned_buffer(storage),
                storage_size, &info));

    AVS_UNIT_ASSERT_EQUAL(sizeof(PAYLOAD) - 1, _anjay_coap_msg_builder_payload(&builder, PAYLOAD, sizeof(PAYLOAD) - 1));

    const anjay_coap_msg_t *msg = _anjay_coap_msg_builder_get_msg(&builder);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg_tpl) + sizeof("\xFF" PAYLOAD) - 1);
    free(storage);
}

#undef PAYLOAD

#define PAYLOAD1 "I can haz "
#define PAYLOAD2 "payload"
#define PAYLOAD_SIZE (sizeof(PAYLOAD1 PAYLOAD2) - 1)

AVS_UNIT_TEST(coap_builder, incremental_payload) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA("\xFF" PAYLOAD1 PAYLOAD2, 1 + PAYLOAD_SIZE);

    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info)
                          + sizeof(ANJAY_COAP_PAYLOAD_MARKER)
                          + PAYLOAD_SIZE;
    void *storage = malloc(storage_size);

    anjay_coap_msg_builder_t builder;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_builder_init(
                &builder,
                _anjay_coap_ensure_aligned_buffer(storage),
                storage_size, &info));

    AVS_UNIT_ASSERT_EQUAL(sizeof(PAYLOAD1) - 1, _anjay_coap_msg_builder_payload(&builder, PAYLOAD1, sizeof(PAYLOAD1) - 1));
    AVS_UNIT_ASSERT_EQUAL(sizeof(PAYLOAD2) - 1, _anjay_coap_msg_builder_payload(&builder, PAYLOAD2, sizeof(PAYLOAD2) - 1));

    const anjay_coap_msg_t *msg = _anjay_coap_msg_builder_get_msg(&builder);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg_tpl) + 1 + PAYLOAD_SIZE);
    free(storage);
}

#undef PAYLOAD1
#undef PAYLOAD2
#undef PAYLOAD_SIZE

#define OPT_EXT_DELTA1 "\xD0\x00"
#define OPT_EXT_DELTA2 "\xE0\x00\x00"
#define OPTS_SIZE (sizeof(OPT_EXT_DELTA1 OPT_EXT_DELTA2) - 1)

AVS_UNIT_TEST(coap_builder, option_ext_number) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA(OPT_EXT_DELTA1 OPT_EXT_DELTA2, OPTS_SIZE);
    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_empty(&info, 13));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_empty(&info, 13 + 269));

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg_tpl) + OPTS_SIZE);

    _anjay_coap_msg_info_reset(&info);
    free(storage);
}

#undef OPT_EXT_DELTA1
#undef OPT_EXT_DELTA2
#undef OPTS_SIZE

#define ZEROS_8 "\x00\x00\x00\x00\x00\x00\x00\x00"
#define ZEROS_64 ZEROS_8 ZEROS_8 ZEROS_8 ZEROS_8 ZEROS_8 ZEROS_8 ZEROS_8 ZEROS_8
#define ZEROS_256 ZEROS_64 ZEROS_64 ZEROS_64 ZEROS_64

#define ZEROS_13 ZEROS_8 "\x00\x00\x00\x00\x00"
#define ZEROS_269 ZEROS_256 ZEROS_13

#define OPT_EXT_LENGTH1 "\x0D\x00"
#define OPT_EXT_LENGTH2 "\x0E\x00\x00"
#define OPTS_SIZE (sizeof(OPT_EXT_LENGTH1 ZEROS_13 OPT_EXT_LENGTH2 ZEROS_269) - 1)

AVS_UNIT_TEST(coap_builder, option_ext_length) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA(OPT_EXT_LENGTH1 ZEROS_13 OPT_EXT_LENGTH2 ZEROS_269, OPTS_SIZE);
    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_opaque(&info, 0, ZEROS_13, sizeof(ZEROS_13) - 1));
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_opaque(&info, 0, ZEROS_269, sizeof(ZEROS_269) - 1));

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg_tpl) + OPTS_SIZE);

    _anjay_coap_msg_info_reset(&info);
    free(storage);
}

#undef OPT_EXT_LENGTH1
#undef OPT_EXT_LENGTH2
#undef OPTS_SIZE

#define STRING "SomeString"

AVS_UNIT_TEST(coap_builder, opt_string) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA("\x0A" STRING, 1 + sizeof(STRING) - 1);
    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);

    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_info_opt_string(&info, 0, STRING));

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    const anjay_coap_msg_t *msg = _anjay_coap_msg_build_without_payload(
            _anjay_coap_ensure_aligned_buffer(storage), storage_size,
            &info);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg_tpl) + sizeof(STRING));

    _anjay_coap_msg_info_reset(&info);
    free(storage);
}

#undef STRING

#define DATA_16 "0123456789abcdef"
#define DATA_256 DATA_16 DATA_16 DATA_16 DATA_16 DATA_16 DATA_16 DATA_16 DATA_16 \
                 DATA_16 DATA_16 DATA_16 DATA_16 DATA_16 DATA_16 DATA_16 DATA_16
#define DATA_8192 DATA_256 DATA_256 DATA_256 DATA_256 DATA_256 DATA_256 DATA_256 DATA_256 \
                  DATA_256 DATA_256 DATA_256 DATA_256 DATA_256 DATA_256 DATA_256 DATA_256
#define DATA_65536 DATA_8192 DATA_8192 DATA_8192 DATA_8192 DATA_8192 DATA_8192 DATA_8192 DATA_8192 \
                   DATA_8192 DATA_8192 DATA_8192 DATA_8192 DATA_8192 DATA_8192 DATA_8192 DATA_8192

AVS_UNIT_TEST(coap_builder, opt_string_too_long) {
    anjay_coap_msg_info_t info = INFO_WITH_DUMMY_HEADER;
    AVS_UNIT_ASSERT_FAILED(_anjay_coap_msg_info_opt_string(&info, 0, DATA_65536));
}

#undef DATA_16
#undef DATA_256
#undef DATA_8192
#undef DATA_65536

AVS_UNIT_TEST(coap_builder, payload_call_with_zero_size) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE(0);
    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info);
    void *storage = malloc(storage_size);

    anjay_coap_msg_builder_t builder;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_builder_init(
                &builder,
                _anjay_coap_ensure_aligned_buffer(storage),
                storage_size, &info));

    AVS_UNIT_ASSERT_EQUAL(0, _anjay_coap_msg_builder_payload(&builder, "", 0));
    const anjay_coap_msg_t *msg = _anjay_coap_msg_builder_get_msg(&builder);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg_tpl));
    free(storage);
}

#define PAYLOAD "And IiiiiiiiiiiiiiiIIIiiiii will alllwayyyyyys crash youuuuUUUUuuu"

AVS_UNIT_TEST(coap_builder, payload_call_with_zero_size_then_nonzero) {
    anjay_coap_msg_t *msg_tpl = MSG_TEMPLATE_WITH_DATA("\xFF" PAYLOAD, 1 + sizeof(PAYLOAD) - 1);

    anjay_coap_msg_info_t info = INFO_WITH_HEADER(&msg_tpl->header);

    size_t storage_size = _anjay_coap_msg_info_get_storage_size(&info)
                          + sizeof(ANJAY_COAP_PAYLOAD_MARKER)
                          + sizeof(PAYLOAD);
    void *storage = malloc(storage_size);

    anjay_coap_msg_builder_t builder;
    AVS_UNIT_ASSERT_SUCCESS(_anjay_coap_msg_builder_init(
                &builder,
                _anjay_coap_ensure_aligned_buffer(storage),
                storage_size, &info));

    AVS_UNIT_ASSERT_EQUAL(0, _anjay_coap_msg_builder_payload(&builder, "", 0));
    AVS_UNIT_ASSERT_EQUAL(sizeof(PAYLOAD) - 1, _anjay_coap_msg_builder_payload(&builder, PAYLOAD, sizeof(PAYLOAD) - 1));

    const anjay_coap_msg_t *msg = _anjay_coap_msg_builder_get_msg(&builder);

    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(msg, msg_tpl, sizeof(*msg_tpl));
    free(storage);
}

#undef PAYLOAD
