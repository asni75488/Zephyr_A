/*
 * Zephyr_A - app_logic (skeleton, implemented in part 6/7).
 *
 * Planned thread (docs/02-arkitektur.md):
 *   K_THREAD_DEFINE(app_logic_tid, CONFIG_APP_LOGIC_THREAD_STACK, app_logic_thread,
 *                   NULL, NULL, NULL, CONFIG_APP_LOGIC_THREAD_PRIO, 0, K_TICKS_FOREVER);
 *   K_MSGQ_DEFINE(sample_q, sizeof(struct sample), CONFIG_APP_SAMPLE_QUEUE_LEN, 8);
 *   K_MSGQ_DEFINE(cmd_q, sizeof(struct app_cmd), 4, 8);
 *   k_poll() on sample_q and cmd_q.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_logic.h"

LOG_MODULE_REGISTER(app_logic, CONFIG_APP_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_BATCH_SIZE <= CONFIG_APP_BATCH_MAX,
	     "APP_BATCH_SIZE must not exceed APP_BATCH_MAX");

/* Shared configuration: written by the app_logic thread (NCMD), read by the
 * sensor thread. Protected by config_lock.
 */
static K_MUTEX_DEFINE(config_lock);
static struct app_config config;

int app_logic_init(void)
{
	k_mutex_lock(&config_lock, K_FOREVER);
	config.sample_interval_ms = CONFIG_APP_SAMPLE_INTERVAL_MS;
	config.batch_size = CONFIG_APP_BATCH_SIZE;
	k_mutex_unlock(&config_lock);

	LOG_INF("Config: sample interval %u ms, batch size %u", config.sample_interval_ms,
		config.batch_size);
	return 0;
}

int app_logic_start(void)
{
	return -ENOSYS;
}

int app_logic_submit_sample(const struct sample *s, k_timeout_t timeout)
{
	ARG_UNUSED(s);
	ARG_UNUSED(timeout);
	return -ENOSYS;
}

int app_logic_submit_command(const struct app_cmd *cmd, k_timeout_t timeout)
{
	ARG_UNUSED(cmd);
	ARG_UNUSED(timeout);
	return -ENOSYS;
}

void app_logic_get_config(struct app_config *out)
{
	k_mutex_lock(&config_lock, K_FOREVER);
	*out = config;
	k_mutex_unlock(&config_lock);
}
