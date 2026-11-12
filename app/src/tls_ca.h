/*
 * Zephyr_A - tls_ca: trusted root CA certificates for TLS.
 *
 * The root certificates (app/ca/ *.der files, embedded at build time by
 * app/cmake/ca_certs.cmake) are registered in Zephyr's TLS credential store,
 * one security tag per certificate. A TLS socket that is given the list of
 * tags trusts exactly these roots - nothing else (no system trust store).
 *
 * Owner thread: none (library). tls_ca_init() is called once from
 * app_mqtt_init() before any thread starts; afterwards read-only.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_TLS_CA_H_
#define ZEPHYR_A_TLS_CA_H_

#include <stddef.h>

#include <zephyr/net/tls_credentials.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all embedded CA certificates.
 *
 * Tags are CONFIG_APP_MQTT_TLS_SEC_TAG_BASE, +1, +2, ...
 * @retval 0 on success, negative errno from tls_credential_add().
 */
int tls_ca_init(void);

/** @return The registered security tags (valid after tls_ca_init()). */
const sec_tag_t *tls_ca_tags(void);

/** @return Number of registered security tags. */
size_t tls_ca_count(void);

/**
 * @brief Common name (CN) of the embedded client certificate (mTLS).
 * @return 0, -ENOTSUP without mTLS, other negative errno on failure.
 */
int tls_client_cert_cn(char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_TLS_CA_H_ */
