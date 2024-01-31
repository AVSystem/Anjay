/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include <fluf/fluf_defs.h>
#include <fluf/fluf_io.h>
#include <fluf/fluf_utils.h>

#include <anj/anj_time.h>
#include <anj/sdm.h>
#include <anj/sdm_io.h>
#include <anj/sdm_send.h>

static int get_item_count(sdm_data_model_t *dm,
                          const fluf_uri_path_t *paths,
                          const size_t path_cnt,
                          size_t *item_cnt) {
    assert(item_cnt);

    *item_cnt = 0;

    for (size_t i = 0; i < path_cnt; i++) {
        size_t res_count;
        int ret =
                sdm_get_composite_readable_res_count(dm, &paths[i], &res_count);
        if (!ret) {
            *item_cnt += res_count;
        } else {
            return ret;
        }
    }

    return 0;
}

int sdm_send_create_msg_from_dm(sdm_data_model_t *dm,
                                uint16_t format,
                                uint8_t *out_buff,
                                size_t *inout_size,
                                const fluf_uri_path_t *paths,
                                const size_t path_cnt) {
    assert(dm && out_buff && paths && inout_size && path_cnt);
    assert(format == FLUF_COAP_FORMAT_SENML_CBOR);
    for (size_t i = 0; i < path_cnt; i++) {
        assert(fluf_uri_path_has(&paths[i], FLUF_ID_RID));
    }

    int ret;
    size_t in_size = *inout_size;
    fluf_io_out_ctx_t out_ctx;
    double timestamp = (double) anj_time_now() / 1000;

    *inout_size = 0;

    if ((ret = sdm_operation_begin(dm, FLUF_OP_DM_READ_COMP, false, NULL))) {
        return ret;
    }

    size_t item_cnt;
    if (get_item_count(dm, paths, path_cnt, &item_cnt)) {
        sdm_operation_end(dm);
        return SDM_ERR_INPUT_ARG;
    }

    if (fluf_io_out_ctx_init(&out_ctx, FLUF_OP_INF_SEND, NULL, item_cnt,
                             format)) {
        sdm_operation_end(dm);
        return SDM_ERR_INPUT_ARG;
    }

    for (size_t i = 0; i < path_cnt; i++) {
        int read_entry_ret;

        do {
            fluf_io_out_entry_t record = { 0 };
            size_t record_len;

            read_entry_ret =
                    sdm_get_composite_read_entry(dm, &paths[i], &record);
            if (read_entry_ret && read_entry_ret != SDM_LAST_RECORD) {
                sdm_operation_end(dm);
                return read_entry_ret;
            }
            record.timestamp = timestamp;

            if ((ret = fluf_io_out_ctx_new_entry(&out_ctx, &record))
                    || (ret = fluf_io_out_ctx_get_payload(
                                &out_ctx, out_buff + *inout_size,
                                in_size - *inout_size, &record_len))) {
                sdm_operation_end(dm);
                if (ret == FLUF_IO_NEED_NEXT_CALL) {
                    return SDM_ERR_MEMORY;
                } else {
                    return SDM_ERR_INPUT_ARG;
                }
            }

            *inout_size += record_len;
        } while (read_entry_ret != SDM_LAST_RECORD);
    }
    sdm_operation_end(dm);

    return 0;
}

int sdm_send_create_msg_from_list_of_records(uint16_t format,
                                             uint8_t *out_buff,
                                             size_t *inout_size,
                                             const fluf_io_out_entry_t *records,
                                             const size_t record_cnt) {
    assert(out_buff && inout_size && records && record_cnt);
    assert(format == FLUF_COAP_FORMAT_SENML_CBOR);
    for (size_t i = 0; i < record_cnt; i++) {
        assert(fluf_uri_path_has(&records[i].path, FLUF_ID_RID));
    }

    size_t in_size = *inout_size;
    *inout_size = 0;

    fluf_io_out_ctx_t out_ctx;

    if (fluf_io_out_ctx_init(&out_ctx, FLUF_OP_INF_SEND, NULL, record_cnt,
                             format)) {
        return SDM_ERR_INPUT_ARG;
    }

    for (size_t i = 0; i < record_cnt; i++) {
        size_t record_len;
        int ret;

        if ((ret = fluf_io_out_ctx_new_entry(&out_ctx, &records[i]))
                || (ret = fluf_io_out_ctx_get_payload(
                            &out_ctx, out_buff + *inout_size,
                            in_size - *inout_size, &record_len))) {
            if (ret == FLUF_IO_NEED_NEXT_CALL) {
                return SDM_ERR_MEMORY;
            } else {
                return SDM_ERR_INPUT_ARG;
            }
        }
        *inout_size += record_len;
    }

    return 0;
}
