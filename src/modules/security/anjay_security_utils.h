/*
 * Copyright 2017-2023 AVSystem <avsystem@avsystem.com>
 * AVSystem Anjay LwM2M SDK
 * All rights reserved.
 *
 * Licensed under the AVSystem-5-clause License.
 * See the attached LICENSE file for details.
 */

#ifndef SECURITY_UTILS_H
#define SECURITY_UTILS_H
#include <anjay_init.h>

#include <assert.h>

#include "anjay_mod_security.h"

VISIBILITY_PRIVATE_HEADER_BEGIN

sec_repr_t *_anjay_sec_get(const anjay_dm_installed_object_t obj_ptr);

/**
 * Fetches UDP Security Mode from @p ctx, performs validation and in case of
 * success sets @p *out to one of @p anjay_security_mode_t enum value.
 */
int _anjay_sec_fetch_security_mode(anjay_unlocked_input_ctx_t *ctx,
                                   anjay_security_mode_t *out);

int _anjay_sec_validate_security_mode(int32_t security_mode);

/**
 * Fetches SSID from @p ctx, performs validation and in case of success sets
 * @p *out .
 */
int _anjay_sec_fetch_short_server_id(anjay_unlocked_input_ctx_t *ctx,
                                     anjay_ssid_t *out);

#if defined(ANJAY_WITH_SECURITY_STRUCTURED)
int _anjay_sec_init_certificate_chain_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_certificate_chain_info_t *in_value);

int _anjay_sec_init_private_key_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_private_key_info_t *in_value);
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) ||             \
          (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) && \
          defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE)) */

#if defined(ANJAY_WITH_SECURITY_STRUCTURED)
int _anjay_sec_init_psk_identity_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_psk_identity_info_t *in_value);

int _anjay_sec_init_psk_key_resource(sec_key_or_data_t *out_resource,
                                     sec_key_or_data_type_t type,
                                     const avs_crypto_psk_key_info_t *in_value);
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) ||             \
          (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) && \
          defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE)) */

void _anjay_sec_key_or_data_cleanup(sec_key_or_data_t *value,
                                    bool remove_from_engine);

/**
 * Frees all resources held in the @p instance.
 */
void _anjay_sec_destroy_instance_fields(sec_instance_t *instance,
                                        bool remove_from_engine);

/**
 * Frees all resources held in instances from the @p instances_ptr list,
 * and the list itself.
 */
void _anjay_sec_destroy_instances(AVS_LIST(sec_instance_t) *instances_ptr,
                                  bool remove_from_engine);

/**
 * Clones all instances of the given Security Object @p repr . Return NULL
 * if either there was nothing to clone or an error has occurred.
 */
AVS_LIST(sec_instance_t) _anjay_sec_clone_instances(const sec_repr_t *repr);

VISIBILITY_PRIVATE_HEADER_END

#endif /* SECURITY_UTILS_H */
