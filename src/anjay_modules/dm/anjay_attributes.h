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

#ifndef ANJAY_INCLUDE_ANJAY_MODULES_DM_ATTRIBUTES_H
#define ANJAY_INCLUDE_ANJAY_MODULES_DM_ATTRIBUTES_H

#include <anjay/dm.h>

VISIBILITY_PRIVATE_HEADER_BEGIN

#if defined(ANJAY_WITH_CON_ATTR) // || defined(...)
#    define WITH_CUSTOM_ATTRIBUTES
#endif

typedef enum {
    ANJAY_DM_CON_ATTR_DEFAULT = -1,
    ANJAY_DM_CON_ATTR_NON = 0,
    ANJAY_DM_CON_ATTR_CON = 1
} anjay_dm_con_attr_t;

#ifdef WITH_CUSTOM_ATTRIBUTES
typedef struct {
#    ifdef ANJAY_WITH_CON_ATTR
    anjay_dm_con_attr_t con;
#    endif
} anjay_dm_custom_attrs_t;

typedef struct {
#    ifdef ANJAY_WITH_CON_ATTR
    bool has_con;
#    endif
} anjay_dm_custom_request_attribute_flags_t;

/*
 * This structure does a lot of magic to ensure proper padding and alignment
 * of the custom attributes relative to standard ones. To rationalize this
 * problem, let's consider what structures do we have:
 *
 *     anjay_dm_oi_       anjay_dm_r_     anjay_dm_internal_ anjay_dm_internal_
 *     attributes_t       attributes_t        oi_attrs_t         r_attrs_t
 *                                        +----------------+ +----------------+
 *                                        |  custom attrs: | |  custom attrs: |
 *                                        |    anjay_dm_   | |    anjay_dm_   |
 *                                        | custom_attrs_t | | custom_attrs_t |
 *                                        +----------------+ +----------------+
 *                                        |    PADDING A   | |    PADDING B   |
 *  +----------------+ +----------------+ +----------------+ +-+--------------+
 *  |                | |  common attrs: | |   std attrs:   | |s| common attrs:|
 *  |     common     | |                | |                | |t|              |
 *  |   attributes   | |  anjay_dm_oi_  | |  anjay_dm_r_   | |d| anjay_dm_oi_ |
 *  |                | |  attributes_t  | |  attributes_t  | | | attributes_t |
 *  +----------------+ +----------------+ +----------------+ |a+--------------+
 *                     |    PADDING C   |                    |t|  PADDING C'  |
 *                     +----------------+                    |t+--------------+
 *                     |    resource-   |                    |r|   resource-  |
 *                     |    specific    |                    |s|   specific   |
 *                     |   attributes   |                    |*|  attributes  |
 *                     +----------------+                    +-+--------------+
 *
 * (*) note that "std attrs" is of type anjay_dm_r_attributes_t
 *
 * We need to be able to extract a pointer to anjay_dm_internal_oi_attrs_t from
 * any pointer to anjay_dm_internal_r_attrs_t; for example, one place where
 * we need this is _anjay_dm_effective_attrs().
 *
 * for this to be possible, we need to ensure that PADDING A and PADDING B have
 * the same size. This is not exactly trivial, because anjay_dm_oi_attributes_t
 * and anjay_dm_r_attributes_t might have different alignment requirements.
 *
 * If we had C11, we could just do something like:
 *
 * typedef struct {
 *     anjay_dm_custom_attrs_storage_t custom;
 *     alignas(anjay_dm_r_attributes_t) anjay_dm_oi_attributes_t standard;
 * } anjay_dm_internal_oi_attrs_t;
 *
 * typedef struct {
 *     anjay_dm_custom_attrs_storage_t custom;
 *     anjay_dm_r_attributes_t standard;
 * } anjay_dm_internal_r_attrs_t;
 *
 * But we're using C99 for better compiler compatibility, and C99 does not have
 * a standard alignas attribute. Thus, we calculate the size of the padding
 * through this anjay_dm_custom_attrs_storage_t wrapper explicitly.
 *
 * ----------------------------------------------------------------------------
 *
 * NOTE: One might think that we could work around this problem by introducing
 * a dedicated structure for the resource-specific part of
 * anjay_dm_r_attributes_t and writing declaractions like:
 *
 * typedef struct {
 *     anjay_dm_oi_attributes_t common;
 *     // PADDING C
 *     anjay_dm_resource_specific_attributes_t resource;
 * } anjay_dm_r_attributes_t;
 *
 * typedef struct {
 *     anjay_dm_custom_attrs_storage_t custom;
 *     anjay_dm_oi_attributes_t standard;
 * } anjay_dm_internal_oi_attrs_t;
 *
 * typedef struct {
 *     anjay_dm_custom_attrs_storage_t custom;
 *     anjay_dm_oi_attributes_t standard_common;
 *     // PADDING C'
 *     anjay_dm_resource_specific_attributes_t standard_resource;
 * } anjay_dm_internal_r_attrs_t;
 *
 * But then we would have no guarantees on whether PADDING C and PADDING C'
 * are the same. For similar reasons, any method involving moving the custom
 * attributes to the end would not solve anything. The root cause is that we
 * need to implement partially overlapping structures with compatible layouts,
 * which is not directly supported by C - hence the need for such dark magic
 * hackery.
 */
typedef union {
    anjay_dm_custom_attrs_t data;
    char padding[offsetof(
            struct {
                anjay_dm_custom_attrs_t custom;
                union {
                    anjay_dm_oi_attributes_t common;
                    anjay_dm_r_attributes_t res;
                } standard;
            },
            standard)];
} anjay_dm_custom_attrs_storage_t;
#endif // WITH_CUSTOM_ATTRIBUTES

AVS_STATIC_ASSERT(offsetof(anjay_dm_r_attributes_t, common) == 0,
                  common_attributes_at_start_of_resource_attributes);

typedef struct {
#ifdef WITH_CUSTOM_ATTRIBUTES
    anjay_dm_custom_attrs_storage_t custom;
#endif
    anjay_dm_oi_attributes_t standard;
} anjay_dm_internal_oi_attrs_t;

typedef struct {
#ifdef WITH_CUSTOM_ATTRIBUTES
    anjay_dm_custom_attrs_storage_t custom;
#endif
    anjay_dm_r_attributes_t standard;
} anjay_dm_internal_r_attrs_t;

AVS_STATIC_ASSERT(offsetof(anjay_dm_internal_oi_attrs_t, standard)
                          == offsetof(anjay_dm_internal_r_attrs_t, standard),
                  standard_attrs_alignment_constant);

static inline anjay_dm_internal_oi_attrs_t *
_anjay_dm_get_internal_oi_attrs(anjay_dm_oi_attributes_t *attrs) {
    return AVS_CONTAINER_OF(attrs, anjay_dm_internal_oi_attrs_t, standard);
}

static inline const anjay_dm_internal_oi_attrs_t *
_anjay_dm_get_internal_oi_attrs_const(const anjay_dm_oi_attributes_t *attrs) {
    return AVS_CONTAINER_OF(attrs, anjay_dm_internal_oi_attrs_t, standard);
}

static inline anjay_dm_internal_r_attrs_t *
_anjay_dm_get_internal_r_attrs(anjay_dm_r_attributes_t *attrs) {
    return AVS_CONTAINER_OF(attrs, anjay_dm_internal_r_attrs_t, standard);
}

static inline const anjay_dm_internal_r_attrs_t *
_anjay_dm_get_internal_r_attrs_const(const anjay_dm_r_attributes_t *attrs) {
    return AVS_CONTAINER_OF(attrs, const anjay_dm_internal_r_attrs_t, standard);
}

#ifdef WITH_CUSTOM_ATTRIBUTES

#    ifdef ANJAY_WITH_CON_ATTR
#        define _ANJAY_DM_CUSTOM_CON_ATTR_INITIALIZER \
            .con = ANJAY_DM_CON_ATTR_DEFAULT
#    else // ANJAY_WITH_CON_ATTR
#        define _ANJAY_DM_CUSTOM_CON_ATTR_INITIALIZER
#    endif // ANJAY_WITH_CON_ATTR

#    define _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER                \
        .custom = {                                           \
            .data = { _ANJAY_DM_CUSTOM_CON_ATTR_INITIALIZER } \
        }

#else // WITH_CUSTOM_ATTRIBUTES
#    define _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER
#endif // WITH_CUSTOM_ATTRIBUTES

#define _ANJAY_DM_OI_ATTRIBUTES_EMPTY                \
    {                                                \
        .min_period = ANJAY_ATTRIB_PERIOD_NONE,      \
        .max_period = ANJAY_ATTRIB_PERIOD_NONE,      \
        .min_eval_period = ANJAY_ATTRIB_PERIOD_NONE, \
        .max_eval_period = ANJAY_ATTRIB_PERIOD_NONE  \
    }

#define _ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY          \
    {                                              \
        .standard = _ANJAY_DM_OI_ATTRIBUTES_EMPTY, \
        _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER         \
    }

// clang-format off
#define _ANJAY_DM_R_ATTRIBUTES_EMPTY             \
    {                                            \
        .common = _ANJAY_DM_OI_ATTRIBUTES_EMPTY, \
        .greater_than = ANJAY_ATTRIB_VALUE_NONE, \
        .less_than = ANJAY_ATTRIB_VALUE_NONE,    \
        .step = ANJAY_ATTRIB_VALUE_NONE          \
    }
// clang-format on

#define _ANJAY_DM_INTERNAL_R_ATTRS_EMPTY          \
    {                                             \
        .standard = _ANJAY_DM_R_ATTRIBUTES_EMPTY, \
        _ANJAY_DM_CUSTOM_ATTRS_INITIALIZER        \
    }

extern const anjay_dm_internal_oi_attrs_t ANJAY_DM_INTERNAL_OI_ATTRS_EMPTY;
extern const anjay_dm_internal_r_attrs_t ANJAY_DM_INTERNAL_R_ATTRS_EMPTY;

VISIBILITY_PRIVATE_HEADER_END

#endif /* ANJAY_INCLUDE_ANJAY_MODULES_DM_ATTRIBUTES_H */
