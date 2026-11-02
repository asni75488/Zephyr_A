/*
 * Zephyr_A - Project A (Industrial IoT, Micro Technic)
 *
 * main: initialises all modules in dependency order and starts their
 * threads. main itself owns no data and does no periodic work once the
 * module threads run (see docs/02-arkitektur.md).
 *
 * Part 2: module skeletons only. The start functions return -ENOSYS until
 * the module is implemented; until then main keeps the part-1 heartbeat so
 * the board still shows signs of life.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/version.h>

#include "app_logic.h"
#include "device_info.h"
#include "modbus.h"
#include "mqtt_client.h"
#include "network.h"
#include "power.h"
#include "sensor.h"
#include "sparkplug.h"
#include "timesync.h"

LOG_MODULE_REGISTER(app_main, CONFIG_APP_LOG_LEVEL);

#define HEARTBEAT_PERIOD K_SECONDS(5)

struct module {
	const char *name;
	int (*init)(void);
	int (*start)(void);
};

/* Order matters: device id before sparkplug topics, config before sensor,
 * consumers (app_logic, sparkplug, mqtt) before producers (sensor).
 */
static const struct module modules[] = {
	{"power", app_power_init, NULL},
	{"timesync", timesync_init, NULL},
	{"app_logic", app_logic_init, app_logic_start},
	{"sparkplug", sparkplug_init, sparkplug_start},
	{"mqtt", app_mqtt_init, app_mqtt_start},
	{"network", network_init, network_start},
	{"modbus", app_modbus_init, NULL},
	{"sensor", app_sensor_init, app_sensor_start},
};

static void init_and_start_modules(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(modules); i++) {
		int ret = modules[i].init();

		if ((ret != 0) && (ret != -ENOTSUP)) {
			LOG_ERR("%s: init failed (%d)", modules[i].name, ret);
			continue;
		}
		if (ret == -ENOTSUP) {
			LOG_INF("%s: not used in this build", modules[i].name);
			continue;
		}
		if (modules[i].start == NULL) {
			continue;
		}

		ret = modules[i].start();
		if (ret == -ENOSYS) {
			LOG_INF("%s: skeleton (not implemented yet)", modules[i].name);
		} else if (ret != 0) {
			LOG_ERR("%s: start failed (%d)", modules[i].name, ret);
		}
	}
}

int main(void)
{
	LOG_INF("boot");
	LOG_INF("Zephyr version: %s, board %s", KERNEL_VERSION_STRING, CONFIG_BOARD_TARGET);

	(void)device_info_init();
	init_and_start_modules();

	uint32_t count = 0U;

	while (true) {
		count++;
		LOG_INF("heartbeat #%u device=%s uptime=%lld ms", count, device_info_id(),
			k_uptime_get());
		k_sleep(HEARTBEAT_PERIOD);
	}

	return 0;
}
