/*
 * Zephyr_A - test stubs for modules that need the real hardware
 * (device_info: eFuse MAC + hwinfo; network: cellular modem + PPP;
 * sensor: coretemp/Modbus).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_types.h"
#include "device_info.h"
#include "network.h"
#include "sensor.h"

LOG_MODULE_REGISTER(test_stubs, LOG_LEVEL_INF);

K_EVENT_DEFINE(test_net_events);

const char *device_info_id(void)
{
	return "esp32c6-test0001";
}

const char *device_info_fw_version(void)
{
	return "0.7.0-test";
}

uint32_t device_info_reset_cause(void)
{
	return RESET_POR;
}

bool network_wait_up(k_timeout_t timeout)
{
	return k_event_wait(&test_net_events, NETWORK_EVT_UP, false, timeout) != 0U;
}

struct k_event *network_events(void)
{
	return &test_net_events;
}

int network_get_rssi(int16_t *rssi_dbm)
{
	*rssi_dbm = -71;
	return 0;
}

void app_sensor_trigger(void)
{
	LOG_INF("TEST: sensor trigger (config changed)");
}

const char *app_sensor_channel_name(uint8_t channel)
{
	return (channel == APP_CHANNEL_DIE_TEMP) ? "Temperature/Die" : "?";
}

const char *app_sensor_channel_unit(uint8_t channel)
{
	return (channel == APP_CHANNEL_DIE_TEMP) ? "degC" : "";
}
