/*
 * Zephyr_A - sensor: data acquisition with a fixed interval.
 *
 * Reads the ESP32-C6 internal temperature sensor (devicetree node
 * "coretemp", Zephyr sensor API, SENSOR_CHAN_DIE_TEMP) and - in a later
 * part - Modbus registers via the modbus module. Every sample gets an
 * uptime stamp and (if synced) a UTC timestamp and is handed to app_logic.
 *
 * Owner thread: "sensor" (periodic, absolute deadlines -> no drift).
 * Input:  sample interval from app_logic (read via app_logic_get_config()).
 * Output: app_logic_submit_sample() -> k_msgq owned by app_logic.
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

/** @brief Check that the sensor devices are ready. */
int app_sensor_init(void);

/** @brief Start the sensor thread (first sample immediately). */
int app_sensor_start(void);

/**
 * @brief Read all sources once (blocking, called by the sensor thread).
 *
 * @param out   Array to fill.
 * @param max   Capacity of @p out.
 * @return Number of samples written (>= 0) or negative errno.
 */
int app_sensor_read_all(struct sample *out, size_t max);

/** @brief Ask the sensor thread to sample now (e.g. after an NCMD). */
void app_sensor_trigger(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_SENSOR_H_ */
