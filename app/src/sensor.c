/*
 * Zephyr_A - sensor: data acquisition with a fixed interval (part 3).
 *
 * Timing: a periodic k_timer gives sample_sem every interval; the thread
 * blocks in k_sem_take(). The kernel timer keeps the period without drift,
 * independent of how long a cycle takes (as long as it is shorter than the
 * interval - otherwise the cycle is counted as an overrun and the next tick
 * is taken immediately; the semaphore limit of 1 means ticks never pile up).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>

#include "app_logic.h"
#include "modbus.h"
#include "sensor.h"

LOG_MODULE_REGISTER(sensor, CONFIG_APP_LOG_LEVEL);

#if defined(CONFIG_APP_SOURCE_DIE_TEMP) && !defined(CONFIG_APP_SENSOR_SIMULATION)
#define USE_CORETEMP 1
#define TEMP_NODE DT_NODELABEL(coretemp)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(TEMP_NODE),
	     "CONFIG_APP_SOURCE_DIE_TEMP needs &coretemp { status = \"okay\"; } in app.overlay");
static const struct device *const temp_dev = DEVICE_DT_GET(TEMP_NODE);
#else
#define USE_CORETEMP 0
#endif

static void sensor_thread(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(sensor_tid, CONFIG_APP_SENSOR_THREAD_STACK, sensor_thread, NULL, NULL, NULL,
		CONFIG_APP_SENSOR_THREAD_PRIO, 0, SYS_FOREVER_MS);

/* Max count 1: if the thread is late, ticks do not accumulate. */
static K_SEM_DEFINE(sample_sem, 0, 1);

static void sample_timer_expiry(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	k_sem_give(&sample_sem); /* ISR context: only signal the thread */
}

static K_TIMER_DEFINE(sample_timer, sample_timer_expiry, NULL);

static struct k_spinlock stats_lock;
static struct app_sensor_stats stats;
static uint32_t meas_seq;
static bool started;

#if defined(CONFIG_APP_SENSOR_SIMULATION)
static uint32_t sim_cycle;
#endif

/* ------------------------------------------------------------------------
 * Internal temperature sensor
 * ------------------------------------------------------------------------
 */
#if defined(CONFIG_APP_SOURCE_DIE_TEMP)

#if defined(CONFIG_APP_SENSOR_SIMULATION)

/* Triangle 22.00..25.00 °C over 60 cycles + small pseudo-random noise. */
static int read_die_temp(int32_t *milli)
{
	if ((CONFIG_APP_SENSOR_SIM_FAULT_EVERY > 0) &&
	    ((sim_cycle % CONFIG_APP_SENSOR_SIM_FAULT_EVERY) ==
	     (CONFIG_APP_SENSOR_SIM_FAULT_EVERY - 1))) {
		return -EIO;
	}

	uint32_t phase = sim_cycle % 60U;
	int32_t tri = (phase < 30U) ? (int32_t)phase * 100 : (int32_t)(60U - phase) * 100;
	int32_t noise = (int32_t)((sim_cycle * 2654435761U) >> 27) - 16; /* -16..+15 */

	*milli = 22000 + tri + noise;
	return 0;
}

#else

static int read_die_temp(int32_t *milli)
{
	struct sensor_value val;
	int ret = sensor_sample_fetch(temp_dev);

	if (ret == 0) {
		ret = sensor_channel_get(temp_dev, SENSOR_CHAN_DIE_TEMP, &val);
	}
	if (ret == 0) {
		*milli = (int32_t)sensor_value_to_milli(&val);
	}
	return ret;
}

#endif

static void read_die_temp_with_retry(struct channel_value *cv)
{
	int32_t milli = 0;
	int ret;

	cv->channel = APP_CHANNEL_DIE_TEMP;

	ret = read_die_temp(&milli);
	if (ret != 0) {
		LOG_ERR("Temperature/Die: read failed (%d), retrying", ret);
		k_sleep(K_MSEC(CONFIG_APP_SENSOR_RETRY_DELAY_MS));
		ret = read_die_temp(&milli);
	}

	if (ret == 0) {
		cv->value_milli = milli;
		cv->quality = QUALITY_GOOD;
	} else {
		LOG_ERR("Temperature/Die: read failed again (%d), value marked BAD", ret);
		cv->value_milli = 0;
		cv->quality = QUALITY_BAD;
	}
}

#endif /* CONFIG_APP_SOURCE_DIE_TEMP */

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------
 */
const char *app_sensor_channel_name(uint8_t channel)
{
	if (channel == APP_CHANNEL_DIE_TEMP) {
		return "Temperature/Die";
	}
	return app_modbus_channel_name((size_t)channel - APP_CHANNEL_MODBUS_FIRST);
}

const char *app_sensor_channel_unit(uint8_t channel)
{
	if (channel == APP_CHANNEL_DIE_TEMP) {
		return "degC";
	}
	return app_modbus_channel_unit((size_t)channel - APP_CHANNEL_MODBUS_FIRST);
}

int app_sensor_read_all(struct measurement *m)
{
	size_t n = 0;

	*m = (struct measurement){0};
	m->uptime_ms = k_uptime_get();
	m->timestamp_ms = 0; /* real UTC time from timesync in part 7 */
	m->seq = meas_seq++;
	m->simulated = IS_ENABLED(CONFIG_APP_SENSOR_SIMULATION);

#if defined(CONFIG_APP_SOURCE_DIE_TEMP)
	read_die_temp_with_retry(&m->values[n]);
	n++;
#endif

	if (IS_ENABLED(CONFIG_APP_MODBUS_RTU)) {
		int ret = app_modbus_read(&m->values[n], APP_MAX_CHANNELS - n);

		if (ret > 0) {
			n += (size_t)ret;
		}
	}

#if defined(CONFIG_APP_SENSOR_SIMULATION)
	sim_cycle++;
#endif

	m->count = (uint8_t)n;
	/* Overall quality: BAD if any value is BAD, else STALE if any is STALE. */
	m->quality = QUALITY_GOOD;
	for (size_t i = 0; i < n; i++) {
		if (m->values[i].quality == QUALITY_BAD) {
			m->quality = QUALITY_BAD;
			break;
		}
		if (m->values[i].quality == QUALITY_STALE) {
			m->quality = QUALITY_STALE;
		}
	}

	return (n > 0U) ? 0 : -ENODEV;
}

void app_sensor_trigger(void)
{
	k_sem_give(&sample_sem);
}

void app_sensor_get_stats(struct app_sensor_stats *out)
{
	k_spinlock_key_t key = k_spin_lock(&stats_lock);

	*out = stats;
	k_spin_unlock(&stats_lock, key);
}

int app_sensor_init(void)
{
	bool any_source = false;

	if (IS_ENABLED(CONFIG_APP_SENSOR_SIMULATION)) {
		LOG_WRN("SIMULATION enabled - values are generated, no hardware is read");
	}

#if USE_CORETEMP
	if (!device_is_ready(temp_dev)) {
		LOG_ERR("Temperature sensor %s not ready", temp_dev->name);
	} else {
		LOG_INF("Source: internal temperature sensor (%s)", temp_dev->name);
		any_source = true;
	}
#else
	if (IS_ENABLED(CONFIG_APP_SOURCE_DIE_TEMP)) {
		LOG_INF("Source: internal temperature sensor (simulated)");
		any_source = true;
	}
#endif

	if (IS_ENABLED(CONFIG_APP_MODBUS_RTU)) {
		int ret = app_modbus_init();

		if (ret == 0) {
			LOG_INF("Source: Modbus RTU, %u register(s)",
				(unsigned int)app_modbus_channel_count());
			any_source = true;
		} else {
			LOG_ERR("Modbus init failed (%d) - Modbus values will be BAD", ret);
		}
	}

	return any_source ? 0 : -ENODEV;
}

int app_sensor_start(void)
{
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_name_set(sensor_tid, "sensor");
	k_thread_start(sensor_tid);
	return 0;
}

/* ------------------------------------------------------------------------
 * Thread
 * ------------------------------------------------------------------------
 */
static void sensor_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	struct app_config cfg;
	uint32_t interval_ms;

	app_logic_get_config(&cfg);
	interval_ms = cfg.sample_interval_ms;

	/* First tick immediately, then every interval_ms. */
	k_timer_start(&sample_timer, K_NO_WAIT, K_MSEC(interval_ms));
	LOG_INF("Sampling every %u ms (k_timer)", interval_ms);

	while (true) {
		struct measurement m;

		(void)k_sem_take(&sample_sem, K_FOREVER);

		/* Interval may be changed at runtime (NCMD, part 7). */
		app_logic_get_config(&cfg);
		if (cfg.sample_interval_ms != interval_ms) {
			interval_ms = cfg.sample_interval_ms;
			k_timer_start(&sample_timer, K_MSEC(interval_ms), K_MSEC(interval_ms));
			LOG_INF("Sample interval changed to %u ms", interval_ms);
		}

		int64_t start = k_uptime_get();

		(void)app_sensor_read_all(&m);

		int64_t duration = k_uptime_get() - start;
		uint32_t bad = 0U;

		for (size_t i = 0; i < m.count; i++) {
			if (m.values[i].quality == QUALITY_BAD) {
				bad++;
			}
		}

		int ret = app_logic_submit_measurement(&m, K_NO_WAIT);

		k_spinlock_key_t key = k_spin_lock(&stats_lock);

		stats.cycles++;
		stats.bad_values += bad;
		if (ret != 0) {
			stats.dropped++;
		}
		if (duration >= (int64_t)interval_ms) {
			stats.overruns++;
		}
		k_spin_unlock(&stats_lock, key);

		if (ret != 0) {
			LOG_WRN("Measurement #%u dropped: queue full (%d)", m.seq, ret);
		}
		if (duration >= (int64_t)interval_ms) {
			LOG_WRN("Measurement #%u took %lld ms (> interval %u ms)", m.seq, duration,
				interval_ms);
		}
	}
}
