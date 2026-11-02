/*
 * Zephyr_A - sensor (skeleton, implemented in part 6).
 *
 * Planned thread (docs/02-arkitektur.md):
 *   K_THREAD_DEFINE(sensor_tid, CONFIG_APP_SENSOR_THREAD_STACK, sensor_thread,
 *                   NULL, NULL, NULL, CONFIG_APP_SENSOR_THREAD_PRIO, 0, K_TICKS_FOREVER);
 *   loop: read -> app_logic_submit_sample() -> k_sleep(K_TIMEOUT_ABS_MS(next))
 *   or k_sem_take(&trigger_sem, K_TIMEOUT_ABS_MS(next)) to allow app_sensor_trigger().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "sensor.h"

LOG_MODULE_REGISTER(sensor, CONFIG_APP_LOG_LEVEL);

int app_sensor_init(void)
{
	LOG_DBG("skeleton");
	return 0;
}

int app_sensor_start(void)
{
	return -ENOSYS;
}

int app_sensor_read_all(struct sample *out, size_t max)
{
	ARG_UNUSED(out);
	ARG_UNUSED(max);
	return -ENOSYS;
}

void app_sensor_trigger(void)
{
}
