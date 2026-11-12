/*
 * Zephyr_A - sensor: data acquisition with a fixed interval.
 *
 * A periodic k_timer (period = sample interval, Kconfig default 10 s, valid
 * range 5-15 s) gives a semaphore from its expiry function; the sensor
 * thread blocks on that semaphore (no busy-wait, CPU can sleep). Each cycle
 * reads all channels into one struct measurement:
 *   - internal temperature sensor ("coretemp", Zephyr sensor API,
 *     SENSOR_CHAN_DIE_TEMP), if CONFIG_APP_SOURCE_DIE_TEMP
 *   - Modbus RTU registers (modbus module), if CONFIG_APP_MODBUS_RTU
 *   - or simulated values, if CONFIG_APP_SENSOR_SIMULATION
 * and hands it to app_logic (k_msgq owned by app_logic).
 *
 * Failed reads are logged (LOG_ERR), retried, and the value is marked
 * QUALITY_BAD - the thread never stops because of a read error.
 *
 * Owner thread: "sensor" (prio CONFIG_APP_SENSOR_THREAD_PRIO).
 * Input:  sample interval from app_logic_get_config() (k_mutex).
 * Output: app_logic_submit_measurement().
 *
 * Functions are prefixed app_sensor_ to avoid clashing with Zephyr's
 * sensor_*() driver API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_SENSOR_H_
#define ZEPHYR_A_SENSOR_H_

#include <stdint.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check that the sensor devices are ready and init Modbus.
 * @retval 0 if at least one source is usable, negative errno otherwise.
 */
int app_sensor_init(void);

/** @brief Start the sampling timer and the sensor thread (first sample immediately). */
int app_sensor_start(void);

/**
 * @brief Read all channels once (blocking). Called by the sensor thread.
 *
 * Always fills @p m (failed channels get QUALITY_BAD), sets uptime_ms,
 * count and the overall quality.
 * @return 0, or negative errno if no channel is configured.
 */
int app_sensor_read_all(struct measurement *m);

/** @brief Ask the sensor thread to sample now (e.g. after an NCMD). */
void app_sensor_trigger(void);

/** @return Metric name of a channel, e.g. "Temperature/Die". */
const char *app_sensor_channel_name(uint8_t channel);

/** @return Unit of a channel, e.g. "degC". */
const char *app_sensor_channel_unit(uint8_t channel);

/** Statistics since boot (for logging / NBIRTH properties later). */
struct app_sensor_stats {
	uint32_t cycles;        /**< measurements taken                 */
	uint32_t bad_values;    /**< values with QUALITY_BAD            */
	uint32_t dropped;       /**< measurements lost (queue full)     */
	uint32_t overruns;      /**< cycles that took longer than the interval */
};

/** @brief Thread-safe copy of the statistics. */
void app_sensor_get_stats(struct app_sensor_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_SENSOR_H_ */
