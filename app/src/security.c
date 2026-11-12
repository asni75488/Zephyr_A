/*
 * Zephyr_A - security: entropy source and eFuse security posture (part 8).
 *
 * Only reads eFuses. Burning them (secure boot, flash encryption, disabling
 * JTAG) is irreversible and is described in docs/08-sikkerhed.md section 7,
 * not done by the firmware.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "security.h"

#if defined(CONFIG_SOC_SERIES_ESP32C6)
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <hal/efuse_hal.h>
#include <soc_random.h>
#endif

LOG_MODULE_REGISTER(security, CONFIG_APP_LOG_LEVEL);

static char posture[48] = "n/a";
static bool hardened;

#if defined(CONFIG_SOC_SERIES_ESP32C6)
static bool efuse_bit(const esp_efuse_desc_t *field[])
{
	return esp_efuse_read_field_bit(field);
}

/* SOFT_DIS_JTAG is a 3-bit field: JTAG is disabled when an odd number of
 * bits is set (it can be re-enabled with an HMAC key, ESP32-C6 TRM 21).
 */
static bool soft_jtag_disabled(void)
{
	uint8_t v = 0;

	(void)esp_efuse_read_field_blob(ESP_EFUSE_SOFT_DIS_JTAG, &v, 3);
	return (__builtin_popcount(v) % 2) == 1;
}
#endif

int app_security_init(void)
{
#if defined(CONFIG_SOC_SERIES_ESP32C6)
	const char *rng = "rc";

	if (IS_ENABLED(CONFIG_APP_SECURITY_SAR_ENTROPY)) {
		/* Safe here: the application uses neither the ADC nor the radio.
		 * The temperature sensor registers are saved/restored inside.
		 */
		soc_random_enable();
		rng = "sar";
	}

	bool secure_boot = efuse_bit(ESP_EFUSE_SECURE_BOOT_EN);
	bool flash_enc = efuse_hal_flash_encryption_enabled();
	bool pad_jtag = !efuse_bit(ESP_EFUSE_DIS_PAD_JTAG) && !soft_jtag_disabled();
	bool usb_jtag = !efuse_bit(ESP_EFUSE_DIS_USB_JTAG);
	bool download = !efuse_bit(ESP_EFUSE_DIS_DOWNLOAD_MODE);
	bool secure_dl = efuse_bit(ESP_EFUSE_ENABLE_SECURITY_DOWNLOAD);

	(void)snprintf(posture, sizeof(posture), "sb=%d fe=%d jtag=%d dl=%d rng=%s", secure_boot,
		       flash_enc, pad_jtag || usb_jtag, download && !secure_dl, rng);
	hardened = secure_boot && flash_enc && !pad_jtag && !usb_jtag;

	if (hardened) {
		LOG_INF("SECURITY: hardened device (%s)", posture);
	} else {
		/* Not an error on a development board, but must be visible. */
		LOG_WRN("SECURITY: development device - secure boot %s, flash encryption %s, "
			"JTAG %s, download mode %s (docs/08-sikkerhed.md section 7)",
			secure_boot ? "on" : "OFF", flash_enc ? "on" : "OFF",
			(pad_jtag || usb_jtag) ? "OPEN" : "off",
			(download && !secure_dl) ? "OPEN" : "restricted");
	}
	LOG_INF("RNG entropy source: %s", IS_ENABLED(CONFIG_APP_SECURITY_SAR_ENTROPY)
						  ? "SAR ADC noise + RC_FAST clock (true random)"
						  : "RC_FAST clock only (secondary source)");
#else
	(void)snprintf(posture, sizeof(posture), "n/a (%s)", CONFIG_BOARD);
	LOG_INF("SECURITY: no eFuses on this target (%s)", CONFIG_BOARD);
#endif
	return 0;
}

const char *app_security_posture(void)
{
	return posture;
}

bool app_security_is_hardened(void)
{
	return hardened;
}
