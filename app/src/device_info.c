/*
 * Zephyr_A - device_info: device identity and firmware version.
 *
 * Moved from main.c (part 1). Functional: this code was tested on hardware
 * in part 1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/hwinfo.h>
#include <app_version.h>

#include "device_info.h"

LOG_MODULE_REGISTER(device_info, CONFIG_APP_LOG_LEVEL);

#define DEVICE_ID_PREFIX "esp32c6-"

static char device_id[DEVICE_INFO_ID_MAX_LEN];
static uint32_t reset_cause;

static const char *reset_cause_str(uint32_t cause)
{
	/* Several bits can be set; report the most relevant one. */
	if (cause & RESET_BROWNOUT) {
		return "brownout";
	}
	if (cause & RESET_WATCHDOG) {
		return "watchdog";
	}
	if (cause & RESET_CPU_LOCKUP) {
		return "cpu lockup";
	}
	if (cause & RESET_SOFTWARE) {
		return "software";
	}
	if (cause & RESET_LOW_POWER_WAKE) {
		return "low-power wake";
	}
	if (cause & RESET_PIN) {
		return "reset pin";
	}
	if (cause & RESET_POR) {
		return "power-on";
	}
	return "unknown";
}

static void read_reset_cause(void)
{
	int ret = hwinfo_get_reset_cause(&reset_cause);

	if (ret != 0) {
		LOG_WRN("Reset cause not available (%d)", ret);
		return;
	}

	if (reset_cause & RESET_BROWNOUT) {
		/* Typical symptom of the cellular module drawing current peaks
		 * from the DevKitC 5 V rail.
		 */
		LOG_WRN("Reset cause: %s (0x%08x) - check power supply",
			reset_cause_str(reset_cause), reset_cause);
	} else {
		LOG_INF("Reset cause: %s (0x%08x)", reset_cause_str(reset_cause), reset_cause);
	}

	(void)hwinfo_clear_reset_cause();
}

int device_info_init(void)
{
	uint8_t hwid[6];
	ssize_t len = hwinfo_get_device_id(hwid, sizeof(hwid));

	read_reset_cause();

	if (len <= 0) {
		LOG_ERR("hwinfo_get_device_id failed (%d)", (int)len);
		(void)snprintf(device_id, sizeof(device_id), "%sunknown", DEVICE_ID_PREFIX);
		return (len < 0) ? (int)len : -EIO;
	}

	int pos = snprintf(device_id, sizeof(device_id), "%s", DEVICE_ID_PREFIX);

	for (ssize_t i = 0; (i < len) && (pos < (int)sizeof(device_id) - 2); i++) {
		pos += snprintf(&device_id[pos], sizeof(device_id) - (size_t)pos, "%02x", hwid[i]);
	}

	LOG_INF("Device id: %s, firmware %s", device_id, APP_VERSION_STRING);
	return 0;
}

const char *device_info_id(void)
{
	return device_id;
}

const char *device_info_fw_version(void)
{
	return APP_VERSION_STRING;
}

uint32_t device_info_reset_cause(void)
{
	return reset_cause;
}
