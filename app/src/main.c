/*
 * Zephyr_A - Project A (Industrial IoT, Micro Technic)
 *
 * Part 1: proves that the toolchain, flashing and logging work.
 * Logs a boot message, the device id (derived from the factory MAC address
 * via the hwinfo API), the reset cause and the Zephyr / application version,
 * then repeats a heartbeat line every 5 seconds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/version.h>
#include <app_version.h>

LOG_MODULE_REGISTER(app_main, LOG_LEVEL_INF);

#define HEARTBEAT_PERIOD K_SECONDS(5)

/* "esp32c6-" + 12 hex chars + NUL */
#define DEVICE_ID_PREFIX "esp32c6-"
#define DEVICE_ID_MAX_LEN (sizeof(DEVICE_ID_PREFIX) + 12)

static char device_id[DEVICE_ID_MAX_LEN];

/*
 * Build a stable, human-readable device id from the hardware id.
 * On ESP32-C6 hwinfo returns the 6-byte factory MAC stored in eFuse, so the
 * id is unique per chip and survives re-flashing. It will be used as the
 * Sparkplug B edge node id and in every MQTT message.
 */
static int device_id_init(void)
{
	uint8_t hwid[6];
	ssize_t len = hwinfo_get_device_id(hwid, sizeof(hwid));

	if (len <= 0) {
		LOG_ERR("hwinfo_get_device_id failed (%d)", (int)len);
		(void)snprintf(device_id, sizeof(device_id), "%sunknown", DEVICE_ID_PREFIX);
		return (len < 0) ? (int)len : -EIO;
	}

	int pos = snprintf(device_id, sizeof(device_id), "%s", DEVICE_ID_PREFIX);

	for (ssize_t i = 0; (i < len) && (pos < (int)sizeof(device_id) - 2); i++) {
		pos += snprintf(&device_id[pos], sizeof(device_id) - (size_t)pos, "%02x", hwid[i]);
	}

	return 0;
}

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

static void log_reset_cause(void)
{
	uint32_t cause = 0;
	int ret = hwinfo_get_reset_cause(&cause);

	if (ret != 0) {
		LOG_WRN("Reset cause not available (%d)", ret);
		return;
	}

	if (cause & RESET_BROWNOUT) {
		/* Typical symptom of the cellular module drawing current peaks
		 * from the DevKitC 5 V rail. Logged as a warning on purpose.
		 */
		LOG_WRN("Reset cause: %s (0x%08x) - check power supply", reset_cause_str(cause),
			cause);
	} else {
		LOG_INF("Reset cause: %s (0x%08x)", reset_cause_str(cause), cause);
	}

	(void)hwinfo_clear_reset_cause();
}

int main(void)
{
	LOG_INF("boot");

	(void)device_id_init();
	LOG_INF("Device id: %s", device_id);
	LOG_INF("Zephyr version: %s", KERNEL_VERSION_STRING);
	LOG_INF("App version: %s (board %s)", APP_VERSION_STRING, CONFIG_BOARD_TARGET);
	log_reset_cause();

	uint32_t count = 0U;

	while (true) {
		count++;
		LOG_INF("heartbeat #%u device=%s zephyr=%s uptime=%lld ms", count, device_id,
			KERNEL_VERSION_STRING, k_uptime_get());
		k_sleep(HEARTBEAT_PERIOD);
	}

	return 0;
}
