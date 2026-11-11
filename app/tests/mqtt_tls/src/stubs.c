/*
 * Zephyr_A - test stubs for modules that need the real hardware
 * (device_info: eFuse MAC; network: cellular modem + PPP).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>

#include "device_info.h"
#include "network.h"

K_EVENT_DEFINE(test_net_events);

const char *device_info_id(void)
{
	return "esp32c6-test0001";
}

const char *device_info_fw_version(void)
{
	return "test";
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
