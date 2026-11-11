/*
 * Zephyr_A - Mbed TLS user configuration (included after Zephyr's
 * config-mbedtls.h, see CONFIG_MBEDTLS_USER_CONFIG_FILE in prj.conf).
 *
 * Zephyr sets the input and output record buffers to the same size
 * (CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN). Only the input side needs the
 * large buffer: the broker sends its whole certificate chain (4 certificates
 * with Let's Encrypt "Generation Y", about 3-4.5 kB) in one handshake record.
 * The client only sends the ClientHello and MQTT packets of at most
 * APP_MQTT_PAYLOAD_MAX + header, so a 2 kB output buffer is enough and saves
 * 6 kB of Mbed TLS heap per connection.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_MBEDTLS_USER_H_
#define ZEPHYR_A_MBEDTLS_USER_H_

#undef MBEDTLS_SSL_OUT_CONTENT_LEN
#define MBEDTLS_SSL_OUT_CONTENT_LEN 2048

#endif /* ZEPHYR_A_MBEDTLS_USER_H_ */
