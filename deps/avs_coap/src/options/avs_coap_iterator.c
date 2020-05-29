/*
 * Copyright 2017-2020 AVSystem <avsystem@avsystem.com>
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

#include <avs_coap_init.h>

#include <string.h>

#include <avsystem/commons/avs_utils.h>

#include "options/avs_coap_iterator.h"
#include "options/avs_coap_option.h"

#define MODULE_NAME coap
#include <avs_coap_x_log_config.h>

VISIBILITY_SOURCE_BEGIN

avs_coap_option_iterator_t _avs_coap_optit_begin(avs_coap_options_t *opts) {
    avs_coap_option_iterator_t optit = {
        .opts = opts,
        .curr_opt = opts->begin,
        .prev_opt_number = 0
    };

    return optit;
}

avs_coap_option_iterator_t *
_avs_coap_optit_next(avs_coap_option_iterator_t *optit) {
    assert(!_avs_coap_optit_end(optit));
    const avs_coap_option_t *opt = _avs_coap_optit_current(optit);
    optit->prev_opt_number += _avs_coap_option_delta(opt);
    optit->curr_opt =
            (uint8_t *) optit->curr_opt + _avs_coap_option_sizeof(opt);
    return optit;
}

static size_t optit_offset(const avs_coap_option_iterator_t *optit) {
    const uint8_t *curr = (const uint8_t *) optit->curr_opt;
    const uint8_t *begin = (const uint8_t *) optit->opts->begin;
    assert(curr >= begin);

    return (size_t) (curr - begin);
}

/*
 * Moves option pointed-to by @p src over @p dst . Both iterators must point
 * to options within the same @ref avs_coap_options_t object, additionally
 * @p src must immediately follow @p dst .
 *
 * @returns Number of bytes occupied by reserialized @p src .
 */
static size_t move_option_back(avs_coap_option_iterator_t *dst,
                               const avs_coap_option_iterator_t *src) {
    const avs_coap_option_t *src_opt = _avs_coap_optit_current(src);
    const size_t src_offset = optit_offset(src);
    const size_t src_sizeof = _avs_coap_option_sizeof(src_opt);
    const uint32_t src_number = _avs_coap_optit_number(src);
    const uint32_t src_length = _avs_coap_option_content_length(src_opt);
    const uint8_t *src_content = _avs_coap_option_value(src_opt);

    avs_coap_option_t *dst_opt = _avs_coap_optit_current(dst);
    const size_t dst_offset = optit_offset(dst);
    const size_t dst_sizeof = _avs_coap_option_sizeof(dst_opt);

    AVS_ASSERT(src->opts == dst->opts,
               "this function assumes both src and dst point to the same "
               "options object");
    AVS_ASSERT(dst_offset + dst_sizeof == src_offset,
               "this function assumes src immediately follows dst");

    const size_t new_delta = src_number - dst->prev_opt_number;
    const size_t new_sizeof =
            _avs_coap_get_opt_header_size(new_delta, src_length);

    AVS_ASSERT(new_sizeof <= src_sizeof + dst_sizeof,
               "moving the option makes its header grow too large to avoid "
               "overwriting its payload");
    (void) dst_offset;
    (void) new_sizeof;
    (void) src_offset;

    assert(new_delta < UINT16_MAX);
    assert(src_length < UINT16_MAX);

    return _avs_coap_option_serialize((uint8_t *) dst_opt,
                                      src_sizeof + dst_sizeof,
                                      (uint16_t) new_delta,
                                      src_content,
                                      (uint16_t) src_length);
}

avs_coap_option_iterator_t *
_avs_coap_optit_erase(avs_coap_option_iterator_t *optit) {
    assert(optit);
    assert(!_avs_coap_optit_end(optit));

    /*
     *                                                 rest_begin
     *                                                      |
     *                                |<--- next_sizeof --->|<- rest_sizeof ...
     *                                |                     v
     * -----+------------+------------+---------------------+------------------
     *      |  prev_opt  | erased_opt |       next_opt      |
     *  ... |- - - - - - |- - - - - - | - - - - - - - - - - | rest ...
     *      | hdr | data | hdr | data | hdr |      data     |
     * -----+------------+------------+---------------------+------------------
     *                                |                     |
     * [1]               .------------'             .-------'
     *                   v                          v
     * -----+------------+--------------------------+-------+------------------
     *      |  prev_opt  |         moved_opt        |       |
     *  ... |- - - - - - | - - - - - - - - - - - - -|       | rest ...
     *      | hdr | data |  hdr'  |       data      |       |
     * -----+------------+--------------------------+-------+-----------------
     *                                                      |
     * [2]                                          .-------'
     *                                              v
     * -----+------------+--------------------------+-----------------
     *      |  prev_opt  |         moved_opt        |
     *  ... |- - - - - - | - - - - - - - - - - - - -| rest ...
     *      | hdr | data |  hdr'  |      data       |
     * -----+------------+--------------------------+-----------------
     *                   |                          |
     *                   |<----- moved_sizeof ----->|
     *                   |     (>= next_sizeof)     |
     *
     * erased_opt needs to be removed. Unfortunately, we can't achieve that
     * with a simple memmove(), because option number delta in next_opt may
     * need to be updated.
     *
     * To achieve that, we serialize next_opt again //over// erased_opt first
     * [1] and only then do memmove() on all options that follow [2].
     *
     * Why doesn't writing over erased_opt overwrite next_opt data?
     *
     * - If the erased_opt does not affect next_opt option delta (i.e.
     *   erased_opt option delta == 0), next_opt header does not get modified
     *   so everything degrades to plain memmove().
     *
     * - The problematic case is when erased_opt option delta > 0. That means
     *   next_opt' may be larger than next_opt. The worst possible scenario is
     *   when the option is empty - because otherwise we have more room for
     *   next_opt. So let's assume the option consist only of a 1-byte header
     *   and possibly extended option delta field, which, according to the RFC
     *   can have at most 2 bytes.
     *
     *   - If erased_opt option delta < 13, there is no extended delta field
     *     in the option header, so erased_opt has exactly 1 byte. Increasing
     *     next_opt option delta by 13 can only grow its header by 1 byte, so
     *     we're fine because we get that byte from erased_opt.
     *
     *   - If erased_opt option delta is in [13, 13+255] range, extended delta
     *     field has 1 byte, and the entire size of erased_opt header is 2
     *     bytes. Increasing next_opt option delta in this case can grow its
     *     header by at most 2 bytes (when erased_opt.delta == 13+255,
     *     next_opt.delta == 1, next_opt'.delta becomes 13+255+1) - so again,
     *     we're fine.
     *
     *   - If erased_opt option delta is larger than 13+255, extended delta
     *     field has 2 bytes and sizeof(erased_opt) == 3. next_opt may only
     *     grow by at most 2 bytes (if there was no extended delta field, and
     *     it grows to max possible size of 2 bytes). We have more than enough
     *     room to spare.
     */

    avs_coap_option_t *erased_opt = _avs_coap_optit_current(optit);
    const size_t erased_sizeof = _avs_coap_option_sizeof(erased_opt);
    const size_t erased_offset = optit_offset(optit);

    avs_coap_option_iterator_t next_optit = *optit;
    _avs_coap_optit_next(&next_optit);
    if (_avs_coap_optit_end(&next_optit)) {
        // no next option - just move the end pointer
        optit->opts->size = erased_offset;
        return optit;
    }

    const avs_coap_option_t *next_opt = _avs_coap_optit_current(&next_optit);
    const size_t next_sizeof = _avs_coap_option_sizeof(next_opt);

    avs_coap_option_iterator_t rest_optit = next_optit;
    _avs_coap_optit_next(&rest_optit);

    const uint8_t *rest_begin = (const uint8_t *) rest_optit.curr_opt;
    const size_t rest_offset = optit_offset(&rest_optit);
    const size_t rest_sizeof = optit->opts->size - rest_offset;

    // [1] Reserialize next_opt over erased_opt
    const size_t moved_sizeof = move_option_back(optit, &next_optit);
    assert(moved_sizeof > 0);
    uint8_t *moved_end = (uint8_t *) erased_opt + moved_sizeof;

    // [2] memmove() all options past next_opt
    memmove(moved_end, rest_begin, rest_sizeof);

    optit->opts->size -= (erased_sizeof + next_sizeof - moved_sizeof);
    return optit;
}

bool _avs_coap_optit_end(const avs_coap_option_iterator_t *optit) {
    return optit_offset(optit) >= optit->opts->size
           || *(const uint8_t *) optit->curr_opt == AVS_COAP_PAYLOAD_MARKER;
}

uint32_t _avs_coap_optit_number(const avs_coap_option_iterator_t *optit) {
    return optit->prev_opt_number
           + _avs_coap_option_delta(_avs_coap_optit_current(optit));
}
