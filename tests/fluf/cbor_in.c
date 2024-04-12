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

#    include <fluf/fluf_defs.h>
#    include <fluf/fluf_io.h>
#    include <fluf/fluf_utils.h>

static void fluf_uri_path_t_compare(const fluf_uri_path_t *a,
                                    const fluf_uri_path_t *b) {
    AVS_UNIT_ASSERT_EQUAL(a->uri_len, b->uri_len);
    AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(a->ids, b->ids, sizeof(a->ids));
}

static const fluf_uri_path_t TEST_RESOURCE_PATH =
        FLUF_MAKE_RESOURCE_PATH(12, 34, 56);

AVS_UNIT_TEST(raw_cbor_in, invalid_paths) {
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_init(&ctx,
                                              FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                              NULL, FLUF_COAP_FORMAT_CBOR),
                          FLUF_IO_ERR_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &FLUF_MAKE_ROOT_PATH(), FLUF_COAP_FORMAT_CBOR),
            FLUF_IO_ERR_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_init(&ctx,
                                              FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                              &FLUF_MAKE_OBJECT_PATH(12),
                                              FLUF_COAP_FORMAT_CBOR),
                          FLUF_IO_ERR_FORMAT);
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_init(&ctx,
                                              FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                              &FLUF_MAKE_INSTANCE_PATH(12, 34),
                                              FLUF_COAP_FORMAT_CBOR),
                          FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(raw_cbor_in, invalid_type) {
    char RESOURCE[] = {
        "\xF6" // null
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(raw_cbor_in, single_integer) {
    char RESOURCE[] = {
        "\x18\x2A" // unsigned(42)
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_DOUBLE
                                        | FLUF_DATA_TYPE_UINT);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_INT;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_INT);
    AVS_UNIT_ASSERT_EQUAL(value->int_value, 42);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, single_negative_integer) {
    char RESOURCE[] = {
        "\x38\x29" // negative(41)
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_DOUBLE);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_INT;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_INT);
    AVS_UNIT_ASSERT_EQUAL(value->int_value, -42);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, single_half_float) {
    char RESOURCE[] = { "\xF9\x44\x80" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_DOUBLE
                                        | FLUF_DATA_TYPE_UINT);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_DOUBLE;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_DOUBLE);
    AVS_UNIT_ASSERT_EQUAL(value->double_value, 4.5);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, single_decimal_fraction) {
    char RESOURCE[] = {
        "\xC4"     // tag(4)
        "\x82"     // array(2)
        "\x20"     // negative(0)
        "\x18\x2D" // unsigned(45)
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_DOUBLE
                                        | FLUF_DATA_TYPE_UINT);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_DOUBLE;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_DOUBLE);
    AVS_UNIT_ASSERT_EQUAL(value->double_value, 4.5);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, single_boolean) {
    char RESOURCE[] = { "\xF5" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_BOOL);
    AVS_UNIT_ASSERT_EQUAL(value->bool_value, true);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

#    ifdef FLUF_WITH_CBOR_STRING_TIME
AVS_UNIT_TEST(raw_cbor_in, single_string_time) {
    char RESOURCE[] = "\xC0\x74"
                      "2003-12-13T18:30:02Z";
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_TIME);
    AVS_UNIT_ASSERT_EQUAL(value->time_value, 1071340202);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}
#    endif // FLUF_WITH_CBOR_STRING_TIME

AVS_UNIT_TEST(raw_cbor_in, single_objlnk) {
    char RESOURCE[] = {
        "\x69"
        "1234:5678" // text(9)
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_OBJLNK;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_EQUAL(value->objlnk.oid, 1234);
    AVS_UNIT_ASSERT_EQUAL(value->objlnk.iid, 5678);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, single_objlnk_split) {
    const char RESOURCE[] = {
        "\x6B"
        "12345:65432" // text(11)
    };
    for (size_t split = 0; split < 9; ++split) {
        char tmp[sizeof(RESOURCE)];
        memcpy(tmp, RESOURCE, sizeof(tmp));

        fluf_io_in_ctx_t ctx;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_init(
                &ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, &TEST_RESOURCE_PATH,
                FLUF_COAP_FORMAT_CBOR));
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_in_ctx_feed_payload(&ctx, tmp, split, false));

        size_t count;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
        AVS_UNIT_ASSERT_EQUAL(count, 1);

        fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
        const fluf_res_value_t *value;
        const fluf_uri_path_t *path;
        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_WANT_NEXT_PAYLOAD);

        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
                &ctx, tmp + split, sizeof(tmp) - split - 1, true));

        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_WANT_TYPE_DISAMBIGUATION);
        AVS_UNIT_ASSERT_EQUAL(type,
                              FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
        AVS_UNIT_ASSERT_NULL(value);
        fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

        type = FLUF_DATA_TYPE_OBJLNK;
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
        AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_OBJLNK);
        AVS_UNIT_ASSERT_EQUAL(value->objlnk.oid, 12345);
        AVS_UNIT_ASSERT_EQUAL(value->objlnk.iid, 65432);
        fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

        type = FLUF_DATA_TYPE_ANY;
        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_EOF);
    }
    for (size_t split = 9; split < sizeof(RESOURCE) - 1; ++split) {
        char tmp[sizeof(RESOURCE)];
        memcpy(tmp, RESOURCE, sizeof(tmp));

        fluf_io_in_ctx_t ctx;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_init(
                &ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, &TEST_RESOURCE_PATH,
                FLUF_COAP_FORMAT_CBOR));
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_in_ctx_feed_payload(&ctx, tmp, split, false));

        size_t count;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
        AVS_UNIT_ASSERT_EQUAL(count, 1);

        fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
        const fluf_res_value_t *value;
        const fluf_uri_path_t *path;

        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_WANT_TYPE_DISAMBIGUATION);
        AVS_UNIT_ASSERT_EQUAL(type,
                              FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
        AVS_UNIT_ASSERT_NULL(value);
        fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

        type = FLUF_DATA_TYPE_OBJLNK;
        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_WANT_NEXT_PAYLOAD);

        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
                &ctx, tmp + split, sizeof(tmp) - split - 1, true));

        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
        AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_OBJLNK);
        AVS_UNIT_ASSERT_EQUAL(value->objlnk.oid, 12345);
        AVS_UNIT_ASSERT_EQUAL(value->objlnk.iid, 65432);
        fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

        type = FLUF_DATA_TYPE_ANY;
        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_EOF);
    }
    {
        char tmp[sizeof(RESOURCE)];
        memcpy(tmp, RESOURCE, sizeof(tmp));

        fluf_io_in_ctx_t ctx;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_init(
                &ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, &TEST_RESOURCE_PATH,
                FLUF_COAP_FORMAT_CBOR));
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_in_ctx_feed_payload(&ctx, tmp, sizeof(tmp) - 1, false));

        size_t count;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
        AVS_UNIT_ASSERT_EQUAL(count, 1);

        fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
        const fluf_res_value_t *value;
        const fluf_uri_path_t *path;

        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_WANT_TYPE_DISAMBIGUATION);
        AVS_UNIT_ASSERT_EQUAL(type,
                              FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
        AVS_UNIT_ASSERT_NULL(value);
        fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

        type = FLUF_DATA_TYPE_OBJLNK;
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
        AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_OBJLNK);
        AVS_UNIT_ASSERT_EQUAL(value->objlnk.oid, 12345);
        AVS_UNIT_ASSERT_EQUAL(value->objlnk.iid, 65432);
        fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

        type = FLUF_DATA_TYPE_ANY;
        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_WANT_NEXT_PAYLOAD);

        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_in_ctx_feed_payload(&ctx, NULL, 0, true));

        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_EOF);
    }
}

AVS_UNIT_TEST(raw_cbor_in, single_objlnk_invalid) {
    char RESOURCE[] = {
        "\x69#StayHome" // text(9)
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_OBJLNK;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(raw_cbor_in, single_string) {
    char RESOURCE[] = {
        "\x6C#ZostanWDomu" // text(12)
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_STRING;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, "#ZostanWDomu");
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 12);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 12);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, empty_string) {
    char RESOURCE[] = {
        "\x60" // text(0)
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_STRING;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, "");
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

#    define CHUNK1 "test"
#    define CHUNK2 "string"
#    define TEST_STRING (CHUNK1 CHUNK2)

AVS_UNIT_TEST(raw_cbor_in, string_indefinite) {
    // (_ "test", "string")
    char RESOURCE[] = { "\x7F\x64" CHUNK1 "\x66" CHUNK2 "\xFF" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_STRING;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, CHUNK1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length,
                          sizeof(CHUNK1) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, CHUNK2);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, sizeof(CHUNK1) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length,
                          sizeof(CHUNK2) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset,
                          sizeof(TEST_STRING) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint,
                          sizeof(TEST_STRING) - 1);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite_with_empty_strings) {
    // (_ "", "test", "", "string", "")
    char RESOURCE[] = { "\x7F\x60\x64" CHUNK1 "\x60\x66" CHUNK2 "\x60\xFF" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_STRING;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, CHUNK1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length,
                          sizeof(CHUNK1) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, CHUNK2);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, sizeof(CHUNK1) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length,
                          sizeof(CHUNK2) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset,
                          sizeof(TEST_STRING) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint,
                          sizeof(TEST_STRING) - 1);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite_with_empty_strings_split) {
    // (_ "", "test", "", "string", "")
    const char RESOURCE[] = { "\x7F\x60\x64" CHUNK1 "\x60\x66" CHUNK2
                              "\x60\xFF" };
    for (size_t split = 0; split < sizeof(RESOURCE); ++split) {
        char tmp[sizeof(RESOURCE)];
        memcpy(tmp, RESOURCE, sizeof(tmp));

        fluf_io_in_ctx_t ctx;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_init(
                &ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE, &TEST_RESOURCE_PATH,
                FLUF_COAP_FORMAT_CBOR));
        AVS_UNIT_ASSERT_SUCCESS(
                fluf_io_in_ctx_feed_payload(&ctx, tmp, split, false));

        size_t count;
        AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
        AVS_UNIT_ASSERT_EQUAL(count, 1);

        fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
        const fluf_res_value_t *value;
        const fluf_uri_path_t *path;
        size_t expected_offset = 0;
        bool second_chunk_provided = false;
        int result = fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path);
        if (result == FLUF_IO_WANT_NEXT_PAYLOAD) {
            AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
                    &ctx, tmp + split, sizeof(tmp) - split - 1, true));
            second_chunk_provided = true;
            result = fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path);
        }
        AVS_UNIT_ASSERT_EQUAL(result, FLUF_IO_WANT_TYPE_DISAMBIGUATION);
        AVS_UNIT_ASSERT_EQUAL(type,
                              FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
        AVS_UNIT_ASSERT_NULL(value);
        fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

        type = FLUF_DATA_TYPE_STRING;
        do {
            if ((result = fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path))
                    == FLUF_IO_WANT_NEXT_PAYLOAD) {
                AVS_UNIT_ASSERT_FALSE(second_chunk_provided);
                AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
                        &ctx, tmp + split, sizeof(tmp) - split - 1, true));
                second_chunk_provided = true;
                result = fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path);
            }
            AVS_UNIT_ASSERT_SUCCESS(result);
            AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
            fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);
            AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset,
                                  expected_offset);
            if (expected_offset < sizeof(TEST_STRING) - 1) {
                AVS_UNIT_ASSERT_TRUE(value->bytes_or_string.chunk_length > 0);
                AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint,
                                      0);
                AVS_UNIT_ASSERT_EQUAL_BYTES_SIZED(
                        value->bytes_or_string.data,
                        &TEST_STRING[expected_offset],
                        value->bytes_or_string.chunk_length);
                expected_offset += value->bytes_or_string.chunk_length;
            } else {
                AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 0);
                AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint,
                                      sizeof(TEST_STRING) - 1);
            }
        } while (value->bytes_or_string.offset
                         + value->bytes_or_string.chunk_length
                 != value->bytes_or_string.full_length_hint);

        type = FLUF_DATA_TYPE_ANY;
        AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value,
                                                       &path),
                              FLUF_IO_EOF);
    }
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite_empty_string) {
    // (_ "")
    char RESOURCE[] = { "\x7F\x60\xFF" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_STRING;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, string_indefinite_empty_struct) {
    // (_ )
    char RESOURCE[] = { "\x7F\xFF" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING | FLUF_DATA_TYPE_OBJLNK);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_STRING;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_STRING);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

#    undef TEST_STRING
#    undef CHUNK1
#    undef CHUNK2

#    define CHUNK1 "\x00\x11\x22\x33\x44\x55"
#    define CHUNK2 "\x66\x77\x88\x99"
#    define TEST_BYTES (CHUNK1 CHUNK2)

AVS_UNIT_TEST(raw_cbor_in, bytes_indefinite) {
    // (_ h'001122334455', h'66778899')
    char RESOURCE[] = { "\x5F\x46" CHUNK1 "\x44" CHUNK2 "\xFF" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_BYTES);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, CHUNK1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length,
                          sizeof(CHUNK1) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_BYTES);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, CHUNK2);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, sizeof(CHUNK1) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length,
                          sizeof(CHUNK2) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_BYTES);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset,
                          sizeof(TEST_BYTES) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint,
                          sizeof(TEST_BYTES) - 1);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, bytes_indefinite_with_empty_strings) {
    // (_ h'', h'001122334455', h'', h'66778899', h'')
    char RESOURCE[] = { "\x5F\x40\x46" CHUNK1 "\x40\x44" CHUNK2 "\x40\xFF" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_init(&ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
                                &TEST_RESOURCE_PATH, FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_BYTES);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, CHUNK1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length,
                          sizeof(CHUNK1) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_BYTES);
    AVS_UNIT_ASSERT_EQUAL_BYTES(value->bytes_or_string.data, CHUNK2);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset, sizeof(CHUNK1) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length,
                          sizeof(CHUNK2) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint, 0);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_BYTES);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.offset,
                          sizeof(TEST_BYTES) - 1);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.chunk_length, 0);
    AVS_UNIT_ASSERT_EQUAL(value->bytes_or_string.full_length_hint,
                          sizeof(TEST_BYTES) - 1);
    fluf_uri_path_t_compare(path, &TEST_RESOURCE_PATH);

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_EOF);
}

AVS_UNIT_TEST(raw_cbor_in, empty_input) {
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_init(
            &ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
            &FLUF_MAKE_RESOURCE_INSTANCE_PATH(12, 34, 56, 78),
            FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(&ctx, NULL, 0, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(raw_cbor_in, invalid_input) {
    char RESOURCE[] = { "\xFF" };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_init(
            &ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
            &FLUF_MAKE_RESOURCE_INSTANCE_PATH(12, 34, 56, 78),
            FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_ERR_FORMAT);
}

AVS_UNIT_TEST(raw_cbor_in, overlong_input) {
    char RESOURCE[] = {
        "\x15"     // unsigned(21)
        "\x18\x25" // unsigned(37)
    };
    fluf_io_in_ctx_t ctx;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_init(
            &ctx, FLUF_OP_DM_WRITE_PARTIAL_UPDATE,
            &FLUF_MAKE_RESOURCE_INSTANCE_PATH(12, 34, 56, 78),
            FLUF_COAP_FORMAT_CBOR));
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_feed_payload(
            &ctx, RESOURCE, sizeof(RESOURCE) - 1, true));

    size_t count;
    AVS_UNIT_ASSERT_SUCCESS(fluf_io_in_ctx_get_entry_count(&ctx, &count));
    AVS_UNIT_ASSERT_EQUAL(count, 1);

    fluf_data_type_t type = FLUF_DATA_TYPE_ANY;
    const fluf_res_value_t *value;
    const fluf_uri_path_t *path;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_WANT_TYPE_DISAMBIGUATION);
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_INT | FLUF_DATA_TYPE_DOUBLE
                                        | FLUF_DATA_TYPE_UINT);
    AVS_UNIT_ASSERT_NULL(value);
    fluf_uri_path_t_compare(path,
                            &FLUF_MAKE_RESOURCE_INSTANCE_PATH(12, 34, 56, 78));

    type = FLUF_DATA_TYPE_UINT;
    AVS_UNIT_ASSERT_SUCCESS(
            fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path));
    AVS_UNIT_ASSERT_EQUAL(type, FLUF_DATA_TYPE_UINT);
    AVS_UNIT_ASSERT_EQUAL(value->uint_value, 21);
    fluf_uri_path_t_compare(path,
                            &FLUF_MAKE_RESOURCE_INSTANCE_PATH(12, 34, 56, 78));

    type = FLUF_DATA_TYPE_ANY;
    AVS_UNIT_ASSERT_EQUAL(fluf_io_in_ctx_get_entry(&ctx, &type, &value, &path),
                          FLUF_IO_ERR_FORMAT);
}

#    undef TEST_BYTES
#    undef CHUNK1
#    undef CHUNK2

#endif // FLUF_WITH_CBOR
