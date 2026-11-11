/*
 * Zephyr_A - tls_ca: register the embedded root CA certificates.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/tls_credentials.h>

#include "tls_ca.h"

/* Generated from the DER files in app/ca by app/cmake/ca_certs.cmake */
#include "zephyr_a_ca/ca_list.h"

LOG_MODULE_REGISTER(tls_ca, CONFIG_APP_LOG_LEVEL);

#define CA_COUNT ARRAY_SIZE(zephyr_a_ca_list)

BUILD_ASSERT(CA_COUNT <= CONFIG_TLS_MAX_CREDENTIALS_NUMBER,
	     "Increase CONFIG_TLS_MAX_CREDENTIALS_NUMBER");
BUILD_ASSERT(CA_COUNT <= CONFIG_NET_SOCKETS_TLS_MAX_CREDENTIALS,
	     "Increase CONFIG_NET_SOCKETS_TLS_MAX_CREDENTIALS (tags per socket)");

static sec_tag_t tags[CA_COUNT];
static size_t registered;

int tls_ca_init(void)
{
	if (registered == CA_COUNT) {
		return 0; /* already done */
	}

	for (size_t i = 0; i < CA_COUNT; i++) {
		const struct zephyr_a_ca *ca = &zephyr_a_ca_list[i];
		sec_tag_t tag = CONFIG_APP_MQTT_TLS_SEC_TAG_BASE + (sec_tag_t)i;
		int ret;

		/* The DER data stays in flash: the socket uses TLS_CERT_NOCOPY. */
		ret = tls_credential_add(tag, TLS_CREDENTIAL_CA_CERTIFICATE, ca->der, ca->len);
		if ((ret != 0) && (ret != -EEXIST)) {
			LOG_ERR("CA %s: tls_credential_add(tag %d) failed (%d)", ca->name,
				(int)tag, ret);
			return ret;
		}
		tags[i] = tag;
		LOG_INF("Trusted CA %s (%u B, tag %d)", ca->name, (unsigned int)ca->len,
			(int)tag);
	}

	registered = CA_COUNT;
	return 0;
}

const sec_tag_t *tls_ca_tags(void)
{
	return tags;
}

size_t tls_ca_count(void)
{
	return registered;
}
