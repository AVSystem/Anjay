#ifndef ANJAY_STANDALONE_SECURITY_UTILS_H
#define ANJAY_STANDALONE_SECURITY_UTILS_H

#include <assert.h>

#include "standalone_mod_security.h"

sec_repr_t *_standalone_sec_get(const anjay_dm_object_def_t *const *obj_ptr);

/**
 * Fetches UDP Security Mode from @p ctx, performs validation and in case of
 * success sets @p *out to one of @p anjay_security_mode_t enum value.
 */
int _standalone_sec_fetch_security_mode(anjay_input_ctx_t *ctx,
                                        anjay_security_mode_t *out);

int _standalone_sec_validate_security_mode(int32_t security_mode);

#ifdef ANJAY_WITH_SMS
/**
 * Fetches SMS Security Mode from @p ctx, performs validation and in case of
 * success sets @p *out to one of @p anjay_sms_security_mode_t enum value.
 */
int _standalone_sec_fetch_sms_security_mode(anjay_input_ctx_t *ctx,
                                            anjay_sms_security_mode_t *out);

int _standalone_sec_validate_sms_security_mode(int32_t security_mode);
#endif // ANJAY_WITH_SMS

/**
 * Fetches SSID from @p ctx, performs validation and in case of success sets
 * @p *out .
 */
int _standalone_sec_fetch_short_server_id(anjay_input_ctx_t *ctx,
                                          anjay_ssid_t *out);

#if defined(ANJAY_WITH_SECURITY_STRUCTURED)                    \
        || (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) \
            && defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE))
int _standalone_sec_init_certificate_chain_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_certificate_chain_info_t *in_value);

int _standalone_sec_init_private_key_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_private_key_info_t *in_value);
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) ||             \
          (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) && \
          defined(AVS_COMMONS_WITH_AVS_CRYPTO_PKI_ENGINE)) */

#if defined(ANJAY_WITH_SECURITY_STRUCTURED)                    \
        || (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) \
            && defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE))
int _standalone_sec_init_psk_identity_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_psk_identity_info_t *in_value);

int _standalone_sec_init_psk_key_resource(
        sec_key_or_data_t *out_resource,
        sec_key_or_data_type_t type,
        const avs_crypto_psk_key_info_t *in_value);
#endif /* defined(ANJAY_WITH_SECURITY_STRUCTURED) ||             \
          (defined(ANJAY_WITH_MODULE_SECURITY_ENGINE_SUPPORT) && \
          defined(AVS_COMMONS_WITH_AVS_CRYPTO_PSK_ENGINE)) */

void _standalone_sec_key_or_data_cleanup(sec_key_or_data_t *value,
                                         bool remove_from_engine);

/**
 * Frees all resources held in the @p instance.
 */
void _standalone_sec_destroy_instance_fields(sec_instance_t *instance,
                                             bool remove_from_engine);

/**
 * Frees all resources held in instances from the @p instances_ptr list,
 * and the list itself.
 */
void _standalone_sec_destroy_instances(AVS_LIST(sec_instance_t) *instances_ptr,
                                       bool remove_from_engine);

/**
 * Clones all instances of the given Security Object @p repr . Return NULL
 * if either there was nothing to clone or an error has occurred.
 */
AVS_LIST(sec_instance_t)
_standalone_sec_clone_instances(const sec_repr_t *repr);

void _standalone_raw_buffer_clear(standalone_raw_buffer_t *buffer);

int _standalone_raw_buffer_clone(standalone_raw_buffer_t *dst,
                                 const standalone_raw_buffer_t *src);

/**
 * Fetches bytes from @p ctx. On success it frees underlying @p buffer storage
 * via @p _anjay_sec_raw_buffer_clear and reinitializes @p buffer properly with
 * obtained data.
 */
int _standalone_io_fetch_bytes(anjay_input_ctx_t *ctx,
                               standalone_raw_buffer_t *buffer);

/**
 * Fetches string from @p ctx. It calls avs_free() on @p *out and, on success,
 * reinitializes @p *out properly with a pointer to (heap allocated) obtained
 * data.
 */
int _standalone_io_fetch_string(anjay_input_ctx_t *ctx, char **out);

#endif /* ANJAY_STANDALONE_SECURITY_UTILS_H */
