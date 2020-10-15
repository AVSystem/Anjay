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

#ifndef AVS_TEST_UTILS_H
#define AVS_TEST_UTILS_H

#include <math.h>

#include <avsystem/coap/token.h>

#define SCOPED_PTR(Type, Deleter) __attribute__((__cleanup__(Deleter))) Type *

avs_coap_token_t nth_token(uint64_t k);
avs_coap_token_t current_token(void);
void reset_token_generator(void);

static inline avs_coap_token_t from_bytes(const void *bytes, size_t size) {
    avs_coap_token_t token;
    memcpy(token.bytes, bytes, size);
    token.size = (uint8_t) size;
    return token;
}

#define MAKE_TOKEN(Bytes)                                                    \
    from_bytes((Bytes),                                                      \
               (ASSERT_TRUE(sizeof(Bytes) - 1 <= AVS_COAP_MAX_TOKEN_LENGTH), \
                sizeof(Bytes) - 1))

/* Convenience macro for use in COAP_MSG, to allow skipping AVS_COAP_CODE_
 * prefix */
#define CODE__(x) AVS_COAP_CODE_##x

/* Convenience macro for use in COAP_MSG, to allow skipping AVS_COAP_FORMAT_
 * prefix */
#define FORMAT__(x) AVS_COAP_FORMAT_##x

// NOTE: The macros below is the common logic between tcp/utils.h and
// udp/utils.h. They refer to the COAP_MSG() macro and fields of test_msg_t,
// which differ between those two variants.

/* Used in COAP_MSG() to pass message token. */
#define TOKEN(Token) .token = (Token)

/* Used in COAP_MSG() to define a non-block message payload from external
 * variable (not only string literal). */
#define PAYLOAD_EXTERNAL(Payload, PayloadSize) \
    .payload = Payload,                        \
    .payload_size = PayloadSize

/* Used in COAP_MSG() to define a non-block message payload (string literal).
 * Terminating nullbyte is not considered part of the payload. */
#define PAYLOAD(Payload) PAYLOAD_EXTERNAL(Payload, sizeof(Payload) - 1)

/* Used in COAP_MSG() to specify a list of Uri-Path options. */
#define PATH(... /* Segments */) .uri_path = { __VA_ARGS__ }

/* Used in COAP_MSG() to specify an Uri-Host option. */
#define HOST(Host) .uri_host = Host

#ifdef WITH_AVS_COAP_OSCORE
/* Used in COAP_MSG() to specify an OSCORE option. */
#    define OSCORE(PartialIV, KidContext, KidPresent, Kid) \
        .oscore_opt_present = true,                        \
        .oscore_opt = (avs_coap_option_oscore_view_t) {    \
            .partial_iv = (uint8_t *) PartialIV,           \
            .partial_iv_size = sizeof(PartialIV) - 1,      \
            .kid_context = (uint8_t *) KidContext,         \
            .kid_context_size = sizeof(KidContext) - 1,    \
            .kid_present = KidPresent,                     \
            .kid = (uint8_t *) Kid,                        \
            .kid_size = sizeof(Kid) - 1                    \
        }

#    define OSCORE_EMPTY OSCORE("", "", false, "")
#endif // WITH_AVS_COAP_OSCORE

/* Used in COAP_MSG() to specify the Accept option. */
#define ACCEPT(Format)              \
    .accept = (const uint16_t[1]) { \
        (Format)                    \
    }

#define DUPLICATED_ACCEPT(Format)              \
    .duplicated_accept = (const uint16_t[1]) { \
        (Format)                               \
    }

/* Used in COAP_MSG() to specify the Observe option. */
#define OBSERVE(Value)               \
    .observe = (const uint32_t[1]) { \
        (Value)                      \
    }

/* Used in COAP_MSG() to define a message with no payload or BLOCK options. */
#define NO_PAYLOAD   \
    .payload = NULL, \
    .payload_size = 0

#define _BLOCK_WITH_PAYLOAD(N, Seq, Size, Payload)                            \
    .block1 = {                                                               \
        .type = AVS_COAP_BLOCK1,                                              \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)),             \
        .size = (uint16_t) (((N) == 1) ? (assert((Size) < (1 << 15)), (Size)) \
                                       : 0),                                  \
        .has_more = ((Seq + 1) * (Size) + 1 < sizeof(Payload))                \
    },                                                                        \
    .block2 = {                                                               \
        .type = AVS_COAP_BLOCK2,                                              \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)),             \
        .size = (uint16_t) (((N) == 2) ? (assert((Size) < (1 << 15)), (Size)) \
                                       : 0),                                  \
        .has_more = ((Seq + 1) * (Size) + 1 < sizeof(Payload))                \
    },                                                                        \
    .payload = ((const uint8_t *) (Payload)) + (Seq) * (Size),                \
    .payload_size =                                                           \
            sizeof(Payload) == sizeof("")                                     \
                    ? 0                                                       \
                    : ((((Seq) + 1) * (Size) + 1 < sizeof(Payload))           \
                               ? (Size)                                       \
                               : (sizeof(Payload) - 1 - (Seq) * (Size)))

#define _BLOCK_WITH_UNSPECIFIED_PAYLOAD(N, Seq, Size, HasMore)                \
    .block1 = {                                                               \
        .type = AVS_COAP_BLOCK1,                                              \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)),             \
        .size = (uint16_t) (((N) == 1) ? (assert((Size) < (1 << 15)), (Size)) \
                                       : 0),                                  \
        .has_more = (HasMore)                                                 \
    },                                                                        \
    .block2 = {                                                               \
        .type = AVS_COAP_BLOCK2,                                              \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)),             \
        .size = (uint16_t) (((N) == 2) ? (assert((Size) < (1 << 15)), (Size)) \
                                       : 0),                                  \
        .has_more = (HasMore)                                                 \
    }

#define _BLOCK_WITHOUT_PAYLOAD(N, Seq, Size, HasMore)               \
    _BLOCK_WITH_UNSPECIFIED_PAYLOAD((N), (Seq), (Size), (HasMore)), \
            .payload = NULL,                                        \
            .payload_size = 0

#define _BLOCK_12_WITH_PAYLOAD(Seq1, Size1, Size2, Payload)         \
    .block1 = {                                                     \
        .type = AVS_COAP_BLOCK1,                                    \
        .seq_num = (assert((Seq1) < (1 << 23)), (uint32_t) (Seq1)), \
        .size = (uint16_t) (assert((Size1) < (1 << 15)), (Size1)),  \
        .has_more = false                                           \
    },                                                              \
    .block2 = {                                                     \
        .type = AVS_COAP_BLOCK2,                                    \
        .seq_num = 0,                                               \
        .size = (uint16_t) (assert((Size2) < (1 << 15)), (Size2)),  \
        .has_more = ((Size2) + 1 < sizeof(Payload))                 \
    },                                                              \
    .payload = ((const uint8_t *) (Payload)),                       \
    .payload_size = sizeof(Payload) == sizeof("")                   \
                            ? 0                                     \
                            : ((Size2) + 1 < sizeof(Payload))       \
                                      ? (Size2)                     \
                                      : (sizeof(Payload) - 1)

/**
 * Used in COAP_MSG to define BLOCK1 option and define request payload.
 * @p Seq     - the block sequence number.
 * @p Size    - block size.
 * @p Payload - if specified, FULL PAYLOAD OF WHOLE BLOCK-WISE TRANSFER (!).
 *              The macro will extract the portion of it based on Seq and Size.
 *              Terminating nullbyte is not considered part of the payload.
 */
#define BLOCK1_REQ(Seq, Size, ... /* Payload */) \
    _BLOCK_WITH_PAYLOAD(1, (Seq), (Size), __VA_ARGS__)

/**
 * Used in COAP_MSG to define BLOCK1 option for use in responses to BLOCK
 * requests.
 *
 * @p Seq     - the block sequence number.
 * @p Size    - block size.
 * @p HasMore - false if the packet is a response to last request block,
 *              true otherwise.
 */
#define BLOCK1_RES(Seq, Size, HasMore) \
    _BLOCK_WITH_UNSPECIFIED_PAYLOAD(1, (Seq), (Size), (HasMore))

/**
 * Used in COAP_MSG to define BLOCK2 option for use in requests for BLOCK
 * payloads.
 *
 * @p Seq     - the block sequence number.
 * @p Size    - block size.
 */
#define BLOCK2_REQ(Seq, Size) _BLOCK_WITHOUT_PAYLOAD(2, (Seq), (Size), false)

/**
 * Used in COAP_MSG to define BLOCK2 option for use in requests for BLOCK
 * payloads. Also, it allows to set some unrelated payload in the message.
 *
 * @p Seq     - the block sequence number.
 * @p Size    - block size.
 * @p Payload - message payload.
 */
#define BLOCK2_REQ_WITH_REGULAR_PAYLOAD(Seq, Size, Payload)       \
    .block2 = {                                                   \
        .type = AVS_COAP_BLOCK2,                                  \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)), \
        .size = (uint16_t) (assert((Size) < (1 << 15)), (Size)),  \
        .has_more = false                                         \
    },                                                            \
    .payload = ((const uint8_t *) (Payload)),                     \
    .payload_size = sizeof(Payload) == sizeof("") ? 0 : (sizeof(Payload) - 1)

/**
 * Used in COAP_MSG to define BLOCK2 option and define response payload.
 * @p Seq     - the block sequence number.
 * @p Size    - block size.
 * @p Payload - if specified, FULL PAYLOAD OF WHOLE BLOCK-WISE TRANSFER (!).
 *              The macro will extract the portion of it based on Seq and Size.
 *              Terminating nullbyte is not considered part of the payload.
 */
#define BLOCK2_RES(Seq, Size, ... /* Payload */) \
    _BLOCK_WITH_PAYLOAD(2, (Seq), (Size), __VA_ARGS__)

/**
 * Used in COAP_MSG to define final BLOCK1, initial BLOCK2 and payload.
 *
 * Implies:
 * - BLOCK1.has_more == false
 * - BLOCK2.seq_num == 0
 *
 * @p Seq1    - the BLOCK1 sequence number.
 * @p Size1   - request block size.
 * @p Size2   - response block size.
 * @p Payload - if specified, FULL PAYLOAD OF WHOLE BLOCK-WISE RESPONSE (!),
 *              given as a string literal. The macro will extract the
 *              first portion based on Size. Terminating nullbyte is not
 *              considered part of the payload.
 */
#define BLOCK1_AND_2_RES(Seq1, Size1, Size2, ... /* Payload */) \
    _BLOCK_12_WITH_PAYLOAD((Seq1), (Size1), (Size2), "" __VA_ARGS__)

/**
 * Used in COAP_MSG to define not-necessairly final BLOCK1 with payload, and
 * some BLOCK2.
 *
 * Implies:
 * - BLOCK2.has_more == false
 *
 * @p Seq1     - the BLOCK1 sequence number.
 * @p Size1    - request block size.
 * @p Size2    - response block size.
 * @p Payload1 - if specified, FULL PAYLOAD OF WHOLE BLOCK-WISE RESPONSE (!),
 *               given as a string literal. The macro will extract the
 *               first portion based on Size. Terminating nullbyte is not
 *               considered part of the payload.
 */
#define BLOCK1_REQ_AND_2_RES(Seq1, Size1, Size2, Payload1)             \
    .block1 = {                                                        \
        .type = AVS_COAP_BLOCK1,                                       \
        .seq_num = (assert((Seq1) < (1 << 23)), (uint32_t) (Seq1)),    \
        .size = (uint16_t) (assert((Size1) < (1 << 15)), (Size1)),     \
        .has_more = ((Seq1 + 1) * (Size1) + 1 < sizeof(Payload1))      \
    },                                                                 \
    .block2 = {                                                        \
        .type = AVS_COAP_BLOCK2,                                       \
        .seq_num = 0,                                                  \
        .size = (uint16_t) (assert((Size2) < (1 << 15)), (Size2)),     \
        .has_more = false                                              \
    },                                                                 \
    .payload = ((const uint8_t *) (Payload1)) + (Seq1) * (Size1),      \
    .payload_size =                                                    \
            sizeof(Payload1) == sizeof("")                             \
                    ? 0                                                \
                    : ((((Seq1) + 1) * (Size1) + 1 < sizeof(Payload1)) \
                               ? (Size1)                               \
                               : (sizeof(Payload1) - 1 - (Seq1) * (Size1)))

// As defined in RFC8323, BERT option indicates multiple blocks of size 1024
#define BERT_BLOCK_SIZE 1024

#define _BERT_WITH_PAYLOAD(N, Seq, Size, Payload)                              \
    .block1 = {                                                                \
        .type = AVS_COAP_BLOCK1,                                               \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)),              \
        .size = (uint16_t) (((N) == 1) ? (assert((Size) >= BERT_BLOCK_SIZE),   \
                                          BERT_BLOCK_SIZE)                     \
                                       : 0),                                   \
        .has_more = ((Seq) *BERT_BLOCK_SIZE + (Size) + 1 < sizeof(Payload)),   \
        .is_bert = ((N) == 1)                                                  \
    },                                                                         \
    .block2 = {                                                                \
        .type = AVS_COAP_BLOCK2,                                               \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)),              \
        .size = (uint16_t) (((N) == 2) ? (assert((Size) >= BERT_BLOCK_SIZE),   \
                                          BERT_BLOCK_SIZE)                     \
                                       : 0),                                   \
        .has_more = ((Seq) *BERT_BLOCK_SIZE + (Size) + 1 < sizeof(Payload)),   \
        .is_bert = ((N) == 2)                                                  \
    },                                                                         \
    .payload = ((const uint8_t *) (Payload)) + (Seq) *BERT_BLOCK_SIZE,         \
    .payload_size =                                                            \
            sizeof(Payload) == sizeof("")                                      \
                    ? 0                                                        \
                    : (((Seq) *BERT_BLOCK_SIZE + (Size) + 1 < sizeof(Payload)) \
                               ? (Size)                                        \
                               : (sizeof(Payload) - 1                          \
                                  - (Seq) *BERT_BLOCK_SIZE))

#define _BERT_WITHOUT_PAYLOAD(N, Seq, HasMore)                    \
    .block1 = {                                                   \
        .type = AVS_COAP_BLOCK1,                                  \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)), \
        .size = ((N) == 1) ? BERT_BLOCK_SIZE : 0,                 \
        .has_more = (HasMore),                                    \
        .is_bert = ((N) == 1)                                     \
    },                                                            \
    .block2 = {                                                   \
        .type = AVS_COAP_BLOCK2,                                  \
        .seq_num = (assert((Seq) < (1 << 23)), (uint32_t) (Seq)), \
        .size = ((N) == 2) ? BERT_BLOCK_SIZE : 0,                 \
        .has_more = (HasMore),                                    \
        .is_bert = ((N) == 2)                                     \
    },                                                            \
    .payload = NULL,                                              \
    .payload_size = 0

#define _BERT1_BLOCK2_WITH_PAYLOAD(Seq1, Size2, Payload)            \
    .block1 = {                                                     \
        .type = AVS_COAP_BLOCK1,                                    \
        .seq_num = (assert((Seq1) < (1 << 23)), (uint32_t) (Seq1)), \
        .size = BERT_BLOCK_SIZE,                                    \
        .has_more = false,                                          \
        .is_bert = true                                             \
    },                                                              \
    .block2 = {                                                     \
        .type = AVS_COAP_BLOCK2,                                    \
        .seq_num = 0,                                               \
        .size = (uint16_t) (assert((Size2) < (1 << 15)), (Size2)),  \
        .has_more = ((Size2) + 1 < sizeof(Payload))                 \
    },                                                              \
    .payload = ((const uint8_t *) (Payload)),                       \
    .payload_size = sizeof(Payload) == sizeof("")                   \
                            ? 0                                     \
                            : ((Size2) + 1 < sizeof(Payload))       \
                                      ? (Size2)                     \
                                      : (sizeof(Payload) - 1)

// Don't use macros below with Size smaller than 1024 bytes. Use BLOCK macros
// instead.
#define BERT1_REQ(Seq, Size, ... /* Payload */) \
    _BERT_WITH_PAYLOAD(1, (Seq), (Size), __VA_ARGS__)

#define BERT1_RES(Seq, HasMore) _BERT_WITHOUT_PAYLOAD(1, (Seq), (HasMore))

#define BERT2_REQ(Seq) _BERT_WITHOUT_PAYLOAD(2, (Seq), false)

#define BERT2_RES(Seq, Size, ... /* Payload */) \
    _BERT_WITH_PAYLOAD(2, (Seq), (Size), __VA_ARGS__)

#define BERT2_REQ(Seq) _BERT_WITHOUT_PAYLOAD(2, (Seq), false)

#define BERT2_RES(Seq, Size, ... /* Payload */) \
    _BERT_WITH_PAYLOAD(2, (Seq), (Size), __VA_ARGS__)

#define BERT1_AND_BLOCK2_RES(Seq1, Size2, ... /* Payload */) \
    _BERT1_BLOCK2_WITH_PAYLOAD((Seq1), (Size2), "" __VA_ARGS__)

#define DATA_16B "123456789abcdef "
#define DATA_32B DATA_16B DATA_16B
#define DATA_64B DATA_32B DATA_32B
#define DATA_256B DATA_64B DATA_64B DATA_64B DATA_64B
#define DATA_1KB DATA_256B DATA_256B DATA_256B DATA_256B
#define DATA_2KB DATA_1KB DATA_1KB

#endif /* AVS_TEST_UTILS_H */
