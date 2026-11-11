/*
 * Zephyr_A - test stubs for modules that need the real hardware
 * (device_info: eFuse MAC + hwinfo; network: cellular modem + PPP;
 * sensor: coretemp/Modbus).
 *
 * network_request_up/down emulate the network thread for the interval
 * uplink (part 9): up -> NETWORK_EVT_UP after 200 ms ("modem start"),
 * down -> NET_EVENT_L4_DISCONNECTED + NETWORK_EVT_DOWN + state OFF.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/sys/atomic.h>

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
	return "0.9.0-test";
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

static atomic_t test_net_state = ATOMIC_INIT(NETWORK_STATE_OFF);

static void net_up_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	atomic_set(&test_net_state, NETWORK_STATE_CONNECTED);
	k_event_clear(&test_net_events, NETWORK_EVT_DOWN);
	k_event_post(&test_net_events, NETWORK_EVT_UP);
	LOG_INF("TEST: network up");
}

static K_WORK_DELAYABLE_DEFINE(net_up_work, net_up_handler);

int network_request_up(void)
{
	if (atomic_get(&test_net_state) == NETWORK_STATE_OFF) {
		atomic_set(&test_net_state, NETWORK_STATE_STARTING);
		(void)k_work_schedule(&net_up_work, K_MSEC(200));
	}
	return 0;
}

int network_request_down(void)
{
	(void)k_work_cancel_delayable(&net_up_work);
	if (atomic_get(&test_net_state) != NETWORK_STATE_OFF) {
		k_event_clear(&test_net_events, NETWORK_EVT_UP);
		net_mgmt_event_notify(NET_EVENT_L4_DISCONNECTED, net_if_get_default());
		k_event_post(&test_net_events, NETWORK_EVT_DOWN);
		atomic_set(&test_net_state, NETWORK_STATE_OFF);
		LOG_INF("TEST: network down (modem off)");
	}
	return 0;
}

enum network_state network_get_state(void)
{
	return (enum network_state)atomic_get(&test_net_state);
}
