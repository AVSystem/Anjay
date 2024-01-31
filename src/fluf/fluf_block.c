/*
 * Copyright 2023-2024 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#include "fluf_block.h"
#include "fluf_options.h"

#define _FLUF_BLOCK_OPTION_MAX_SIZE 3

#define _FLUF_BLOCK_OPTION_M_MASK 0x08
#define _FLUF_BLOCK_OPTION_M_SHIFT 3
#define _FLUF_BLOCK_OPTION_SZX_MASK 0x07
#define _FLUF_BLOCK_OPTION_SZX_CALC_CONST 3
#define _FLUF_BLOCK_OPTION_NUM_MASK 0xF0
#define _FLUF_BLOCK_OPTION_NUM_SHIFT 4
#define _FLUF_BLOCK_OPTION_BYTE_SHIFT 8

#define _FLUF_BLOCK_1_BYTE_NUM_MAX_VALUE 15
#define _FLUF_BLOCK_2_BYTE_NUM_MAX_VALUE 4095
#define _FLUF_BLOCK_NUM_MAX_VALUE 0x000FFFFF

int _fluf_block_decode(fluf_coap_options_t *opts, fluf_block_t *block) {
    bool check_block2_opt = false;
    uint8_t block_buff[_FLUF_BLOCK_OPTION_MAX_SIZE];
    size_t block_option_size = 0;

    memset(block, 0, sizeof(fluf_block_t));

    int res = _fluf_coap_options_get_data_iterate(
            opts, _FLUF_COAP_OPTION_BLOCK1, NULL, &block_option_size,
            block_buff, _FLUF_BLOCK_OPTION_MAX_SIZE);
    if (res == _FLUF_COAP_OPTION_MISSING) {
        res = _fluf_coap_options_get_data_iterate(
                opts, _FLUF_COAP_OPTION_BLOCK2, NULL, &block_option_size,
                block_buff, _FLUF_BLOCK_OPTION_MAX_SIZE);
        check_block2_opt = true;
    }
    if (res == _FLUF_COAP_OPTION_MISSING) {
        return 0;
    } else if (res) {
        return res;
    } else if (!block_option_size) {
        // dont't allow empty block option
        return FLUF_ERR_MALFORMED_MESSAGE;
    } else if (!res && block_option_size <= _FLUF_BLOCK_OPTION_MAX_SIZE) {
        block->block_type =
                check_block2_opt ? FLUF_OPTION_BLOCK_2 : FLUF_OPTION_BLOCK_1;
        block->more_flag = !!(block_buff[block_option_size - 1]
                              & _FLUF_BLOCK_OPTION_M_MASK);

        size_t SZX = (block_buff[block_option_size - 1]
                      & _FLUF_BLOCK_OPTION_SZX_MASK);
        block->size =
                2U << (SZX + _FLUF_BLOCK_OPTION_SZX_CALC_CONST); // block size =
                                                                 // 2**(SZX + 4)

        if (block_option_size == 1) {
            block->number = (block_buff[0] & _FLUF_BLOCK_OPTION_NUM_MASK)
                            >> _FLUF_BLOCK_OPTION_NUM_SHIFT;
        } else if (block_option_size == 2) {
            // network big-endian order
            block->number =
                    (uint32_t) ((block_buff[0] << _FLUF_BLOCK_OPTION_NUM_SHIFT)
                                + ((block_buff[1] & _FLUF_BLOCK_OPTION_NUM_MASK)
                                   >> _FLUF_BLOCK_OPTION_NUM_SHIFT));
        } else {
            block->number =
                    (uint32_t) ((block_buff[0]
                                 << (_FLUF_BLOCK_OPTION_BYTE_SHIFT
                                     + _FLUF_BLOCK_OPTION_NUM_SHIFT))
                                + (block_buff[1]
                                   << _FLUF_BLOCK_OPTION_NUM_SHIFT)
                                + ((block_buff[2] & _FLUF_BLOCK_OPTION_NUM_MASK)
                                   >> _FLUF_BLOCK_OPTION_NUM_SHIFT));
        }

        return 0;
    } else {
        return FLUF_ERR_MALFORMED_MESSAGE;
    }
}

int _fluf_block_prepare(fluf_coap_options_t *opts, fluf_block_t *block) {
    uint16_t opt_number;

    if (block->block_type == FLUF_OPTION_BLOCK_1) {
        opt_number = _FLUF_COAP_OPTION_BLOCK1;
    } else if (block->block_type == FLUF_OPTION_BLOCK_2) {
        opt_number = _FLUF_COAP_OPTION_BLOCK2;
    } else {
        return FLUF_ERR_INPUT_ARG;
    }

    // prepare SZX parameter
    uint8_t SZX = 0xFF;
    const unsigned int allowed_block_sized_values[] = { 16,  32,  64,  128,
                                                        256, 512, 1024 };
    for (uint8_t i = 0; i < sizeof(allowed_block_sized_values); i++) {
        if (block->size == allowed_block_sized_values[i]) {
            SZX = i;
            break;
        }
    }
    if (SZX == 0xFF) {
        // incorrect block_size_option
        return FLUF_ERR_INPUT_ARG;
    }

    // determine block option size
    size_t block_opt_size = 1;
    if (block->number > _FLUF_BLOCK_1_BYTE_NUM_MAX_VALUE) {
        block_opt_size++;
    }
    if (block->number > _FLUF_BLOCK_2_BYTE_NUM_MAX_VALUE) {
        block_opt_size++;
    }
    if (block->number > _FLUF_BLOCK_NUM_MAX_VALUE) {
        // block number out of the range
        return FLUF_ERR_INPUT_ARG;
    }

    uint8_t buff[_FLUF_BLOCK_OPTION_MAX_SIZE];

    buff[block_opt_size - 1] =
            (uint8_t) ((((uint8_t) block->more_flag
                         << _FLUF_BLOCK_OPTION_M_SHIFT)
                        & _FLUF_BLOCK_OPTION_M_MASK)
                       + SZX
                       + ((block->number << _FLUF_BLOCK_OPTION_NUM_SHIFT)
                          & _FLUF_BLOCK_OPTION_NUM_MASK));
    if (block_opt_size == 2) {
        buff[0] = (uint8_t) (block->number >> _FLUF_BLOCK_OPTION_NUM_SHIFT);
    } else if (block_opt_size == 3) {
        buff[1] = (uint8_t) (block->number >> _FLUF_BLOCK_OPTION_NUM_SHIFT);
        buff[0] =
                (uint8_t) (block->number >> (_FLUF_BLOCK_OPTION_NUM_SHIFT
                                             + _FLUF_BLOCK_OPTION_BYTE_SHIFT));
    }

    return _fluf_coap_options_add_data(opts, opt_number, buff, block_opt_size);
}
