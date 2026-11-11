/*
 * Zephyr_A - app_logic: batching and commands (part 3), batches go to
 * sparkplug and configuration commands come from NCMD (part 7).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "app_logic.h"
#include "sensor.h"
#include "sparkplug.h"

LOG_MODULE_REGISTER(app_logic, CONFIG_APP_LOG_LEVEL);

BUILD_ASSERT(CONFIG_APP_BATCH_SIZE <= CONFIG_APP_BATCH_MAX,
	     "APP_BATCH_SIZE must not exceed APP_BATCH_MAX");

#define MIN_INTERVAL_MS 5000U  /* requirement: 5-15 s (same as Kconfig range) */
#define MAX_INTERVAL_MS 15000U

/* Queues owned by this module (producers use the submit functions).
 * K_MSGQ_DEFINE is already static in Zephyr 4.4.0. */
K_MSGQ_DEFINE(meas_q, sizeof(struct measurement), CONFIG_APP_SAMPLE_QUEUE_LEN, 8);
K_MSGQ_DEFINE(cmd_q, sizeof(struct app_cmd), 4, 8);

/* Shared configuration: written here (commands), read by the sensor thread. */
static K_MUTEX_DEFINE(config_lock);
static struct app_config config;

static void app_logic_thread(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(app_logic_tid, CONFIG_APP_LOGIC_THREAD_STACK, app_logic_thread, NULL, NULL,
		NULL, CONFIG_APP_LOGIC_THREAD_PRIO, 0, SYS_FOREVER_MS);

/* Only touched by the app_logic thread. */
static struct measurement_batch batch;
static uint32_t batches_done;
static bool started;

/* "-12.345" style formatting of milli values without floating point. */
static int fmt_milli(char *buf, size_t size, int32_t milli)
{
	int64_t v = milli;
	const char *sign = (v < 0) ? "-" : "";

	v = (v < 0) ? -v : v;
	return snprintf(buf, size, "%s%lld.%03lld", sign, (long long)(v / 1000),
			(long long)(v % 1000));
}

static void log_measurement(const struct measurement *m)
{
	char line[192];
	int pos = 0;

	for (size_t i = 0; (i < m->count) && (pos < (int)sizeof(line)); i++) {
		const struct channel_value *cv = &m->values[i];
		char val[16];

		if (cv->quality == QUALITY_GOOD) {
			(void)fmt_milli(val, sizeof(val), cv->value_milli);
		} else {
			(void)snprintf(val, sizeof(val), "%s", value_quality_str(cv->quality));
		}

		const bool good = (cv->quality == QUALITY_GOOD);

		pos += snprintf(&line[pos], sizeof(line) - (size_t)pos, "%s%s=%s%s%s",
				(i > 0) ? ", " : "", app_sensor_channel_name(cv->channel), val,
				good ? " " : "", good ? app_sensor_channel_unit(cv->channel) : "");
	}

	if (m->quality == QUALITY_GOOD) {
		LOG_INF("meas #%u up=%lld ms%s: %s", m->seq, m->uptime_ms,
			m->simulated ? " (sim)" : "", line);
	} else {
		LOG_WRN("meas #%u up=%lld ms%s [%s]: %s", m->seq, m->uptime_ms,
			m->simulated ? " (sim)" : "", value_quality_str(m->quality), line);
	}
}

static void handle_measurement(const struct measurement *m, uint8_t batch_size)
{
	log_measurement(m);

	if (batch.count < CONFIG_APP_BATCH_MAX) {
		batch.items[batch.count++] = *m;
	}

	if (batch.count < batch_size) {
		return;
	}

	batches_done++;
	LOG_INF("Batch #%u ready: %u measurement(s), up %lld..%lld ms", batches_done,
		batch.count, batch.items[0].uptime_ms, batch.items[batch.count - 1].uptime_ms);

	int ret = sparkplug_submit_batch(&batch, K_NO_WAIT);

	if (ret != 0) {
		/* Queue full: MQTT has been down for longer than the queue covers */
		LOG_WRN("Batch #%u dropped - sparkplug queue full (MQTT down?)", batches_done);
	}
	batch.count = 0;
}

static void handle_command(const struct app_cmd *cmd)
{
	bool changed = false;

	k_mutex_lock(&config_lock, K_FOREVER);

	switch (cmd->type) {
	case APP_CMD_SET_SAMPLE_INTERVAL:
		if ((cmd->value < MIN_INTERVAL_MS) || (cmd->value > MAX_INTERVAL_MS)) {
			LOG_ERR("Rejected sample interval %lld ms (allowed %u-%u)", cmd->value,
				MIN_INTERVAL_MS, MAX_INTERVAL_MS);
		} else {
			config.sample_interval_ms = (uint32_t)cmd->value;
			LOG_INF("Sample interval set to %u ms", config.sample_interval_ms);
			app_sensor_trigger(); /* sensor re-reads config and restarts timer */
			changed = true;
		}
		break;
	case APP_CMD_SET_BATCH_SIZE:
		if ((cmd->value < 1) || (cmd->value > CONFIG_APP_BATCH_MAX)) {
			LOG_ERR("Rejected batch size %lld (allowed 1-%d)", cmd->value,
				CONFIG_APP_BATCH_MAX);
		} else {
			config.batch_size = (uint8_t)cmd->value;
			LOG_INF("Batch size set to %u", config.batch_size);
			changed = true;
		}
		break;
	case APP_CMD_REBIRTH:
	case APP_CMD_REBOOT:
		/* Session commands are executed by sparkplug itself */
		LOG_DBG("Command %u handled by sparkplug", cmd->type);
		break;
	default:
		LOG_ERR("Unknown command %u", cmd->type);
		break;
	}

	k_mutex_unlock(&config_lock);

	if (changed) {
		sparkplug_notify_config_changed(); /* report the new value (NDATA) */
	}
}

static void app_logic_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct k_poll_event events[] = {
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
						K_POLL_MODE_NOTIFY_ONLY, &meas_q, 0),
		K_POLL_EVENT_STATIC_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
						K_POLL_MODE_NOTIFY_ONLY, &cmd_q, 0),
	};

	while (true) {
		(void)k_poll(events, ARRAY_SIZE(events), K_FOREVER);

		struct app_cmd cmd;

		while (k_msgq_get(&cmd_q, &cmd, K_NO_WAIT) == 0) {
			handle_command(&cmd);
		}

		struct measurement m;
		struct app_config cfg;

		while (k_msgq_get(&meas_q, &m, K_NO_WAIT) == 0) {
			app_logic_get_config(&cfg);
			handle_measurement(&m, cfg.batch_size);
		}

		for (size_t i = 0; i < ARRAY_SIZE(events); i++) {
			events[i].state = K_POLL_STATE_NOT_READY;
		}
	}
}

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
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_name_set(app_logic_tid, "app_logic");
	k_thread_start(app_logic_tid);
	return 0;
}

int app_logic_submit_measurement(const struct measurement *m, k_timeout_t timeout)
{
	return k_msgq_put(&meas_q, m, timeout);
}

int app_logic_submit_command(const struct app_cmd *cmd, k_timeout_t timeout)
{
	return k_msgq_put(&cmd_q, cmd, timeout);
}

void app_logic_get_config(struct app_config *out)
{
	k_mutex_lock(&config_lock, K_FOREVER);
	*out = config;
	k_mutex_unlock(&config_lock);
}
