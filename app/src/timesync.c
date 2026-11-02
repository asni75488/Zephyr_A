/*
 * Zephyr_A - timesync (skeleton, implemented in part 5/6).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "timesync.h"

LOG_MODULE_REGISTER(timesync, CONFIG_APP_LOG_LEVEL);

int timesync_init(void)
{
	LOG_DBG("skeleton");
	return 0;
}

int timesync_sync(uint32_t timeout_ms)
{
	ARG_UNUSED(timeout_ms);
	/* TODO: SNTP query (CONFIG_SNTP) and clock_settime(CLOCK_REALTIME). */
	return -ENOSYS;
}

bool timesync_is_valid(void)
{
	return false;
}

int64_t timesync_now_ms(void)
{
	return 0;
}

int64_t timesync_uptime_to_utc_ms(int64_t uptime_ms)
{
	ARG_UNUSED(uptime_ms);
	return 0;
}
