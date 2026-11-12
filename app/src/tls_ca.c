/*
 * Zephyr_A - tls_ca: register the embedded root CA certificates and, with
 * mTLS (part 10), the device client certificate and private key.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/tls_credentials.h>

#include "tls_ca.h"

/* Generated from the DER files in app/ca by app/cmake/ca_certs.cmake */
#include "zephyr_a_ca/ca_list.h"

#if defined(CONFIG_APP_MQTT_MTLS)
#include <mbedtls/x509_crt.h>
/* Generated from certs/device-pki/devices/<id>.{crt,key}.der */
#include "zephyr_a_client/client_cred.h"
#define CLIENT_TAGS 1
#else
#define CLIENT_TAGS 0
#endif

LOG_MODULE_REGISTER(tls_ca, CONFIG_APP_LOG_LEVEL);

#define CA_COUNT ARRAY_SIZE(zephyr_a_ca_list)

BUILD_ASSERT(CA_COUNT + 2 * CLIENT_TAGS <= CONFIG_TLS_MAX_CREDENTIALS_NUMBER,
	     "Increase CONFIG_TLS_MAX_CREDENTIALS_NUMBER");
BUILD_ASSERT(CA_COUNT + CLIENT_TAGS <= CONFIG_NET_SOCKETS_TLS_MAX_CREDENTIALS,
	     "Increase CONFIG_NET_SOCKETS_TLS_MAX_CREDENTIALS (tags per socket)");

static sec_tag_t tags[CA_COUNT + CLIENT_TAGS];
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

#if defined(CONFIG_APP_MQTT_MTLS)
	const sec_tag_t ctag = CONFIG_APP_MQTT_CLIENT_SEC_TAG;
	int ret = tls_credential_add(ctag, TLS_CREDENTIAL_PUBLIC_CERTIFICATE, client_cert_der,
				     sizeof(client_cert_der));

	if ((ret == 0) || (ret == -EEXIST)) {
		ret = tls_credential_add(ctag, TLS_CREDENTIAL_PRIVATE_KEY, client_key_der,
					 sizeof(client_key_der));
	}
	if ((ret != 0) && (ret != -EEXIST)) {
		LOG_ERR("Client certificate: tls_credential_add(tag %d) failed (%d)", (int)ctag,
			ret);
		return ret;
	}
	tags[CA_COUNT] = ctag;
	LOG_INF("Client certificate %u B + key (tag %d) for mTLS",
		(unsigned int)sizeof(client_cert_der), (int)ctag);
#endif

	registered = CA_COUNT + CLIENT_TAGS;
	return 0;
}

int tls_client_cert_cn(char *buf, size_t size)
{
#if defined(CONFIG_APP_MQTT_MTLS)
	mbedtls_x509_crt crt;
	char dn[128];
	const char *cn;
	size_t n = 0;
	int ret;

	mbedtls_x509_crt_init(&crt);
	ret = mbedtls_x509_crt_parse_der(&crt, client_cert_der, sizeof(client_cert_der));
	if (ret == 0) {
		ret = mbedtls_x509_dn_gets(dn, sizeof(dn), &crt.subject);
	}
	mbedtls_x509_crt_free(&crt);
	if (ret < 0) {
		return -EINVAL;
	}

	/* "O=Zephyr_A, CN=esp32c6-..." -> value of CN up to the next ", " */
	cn = strstr(dn, "CN=");
	if (cn == NULL) {
		return -ENOENT;
	}
	cn += 3;
	while ((cn[n] != '\0') && (cn[n] != ',')) {
		n++;
	}
	if (n >= size) {
		return -ENOMEM;
	}
	memcpy(buf, cn, n);
	buf[n] = '\0';
	return 0;
#else
	ARG_UNUSED(buf);
	ARG_UNUSED(size);
	return -ENOTSUP;
#endif
}

const sec_tag_t *tls_ca_tags(void)
{
	return tags;
}

size_t tls_ca_count(void)
{
	return registered;
}
