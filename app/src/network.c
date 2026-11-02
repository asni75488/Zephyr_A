/*
 * Zephyr_A - network (skeleton, implemented in part 4).
 *
 * Planned thread (docs/02-arkitektur.md):
 *   K_THREAD_DEFINE(network_tid, CONFIG_APP_NETWORK_THREAD_STACK, network_thread,
 *                   NULL, NULL, NULL, CONFIG_APP_NETWORK_THREAD_PRIO, 0, K_TICKS_FOREVER);
 *   net_mgmt callback (L4_CONNECTED/DISCONNECTED) -> k_event -> state machine,
 *   pm_device_action_run(modem, RESUME/SUSPEND), timesync_sync() after PPP up.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "network.h"

LOG_MODULE_REGISTER(network, CONFIG_APP_LOG_LEVEL);

static K_EVENT_DEFINE(net_events);

static const char *const state_names[] = {
	[NETWORK_STATE_OFF] = "off",
	[NETWORK_STATE_STARTING] = "starting",
	[NETWORK_STATE_REGISTERING] = "registering",
	[NETWORK_STATE_PPP_UP] = "ppp-up",
	[NETWORK_STATE_CONNECTED] = "connected",
	[NETWORK_STATE_BACKOFF] = "backoff",
};

int network_init(void)
{
	LOG_DBG("skeleton");
	return 0;
}

int network_start(void)
{
	return -ENOSYS;
}

int network_request_up(void)
{
	return -ENOSYS;
}

int network_request_down(void)
{
	return -ENOSYS;
}

enum network_state network_get_state(void)
{
	return NETWORK_STATE_OFF;
}

const char *network_state_str(enum network_state state)
{
	if ((unsigned int)state >= ARRAY_SIZE(state_names)) {
		return "?";
	}
	return state_names[state];
}

bool network_wait_up(k_timeout_t timeout)
{
	return k_event_wait(&net_events, NETWORK_EVT_UP, false, timeout) != 0U;
}

struct k_event *network_events(void)
{
	return &net_events;
}

int network_get_rssi(int16_t *rssi_dbm)
{
	ARG_UNUSED(rssi_dbm);
	return -ENODATA;
}
