/*
 * Zephyr_A - modbus: Modbus RTU client for data acquisition.
 *
 * Uses Zephyr's Modbus subsystem (<zephyr/modbus/modbus.h>, client role)
 * on the interface defined by the devicetree node with compatible
 * "zephyr,modbus-serial" (app/modbus-rtu.overlay: UART0 on GPIO20/21,
 * DE/RE on GPIO23). Slave address, baud rate, parity, timeout and retries
 * are Kconfig options (CONFIG_APP_MODBUS_*); the register table with
 * scaling is in modbus.c.
 *
 * Error handling: every failed request (timeout -ETIMEDOUT, CRC error -EIO,
 * exception > 0) is logged with LOG_ERR and retried with exponential
 * backoff. If all retries fail the value is marked QUALITY_BAD and the
 * slave is skipped for an increasing number of cycles (QUALITY_STALE).
 * The program never stops because of a Modbus error.
 *
 * Owner thread: none of its own - called synchronously by the sensor
 * thread, so all acquisition happens with the same fixed interval.
 *
 * Functions are prefixed app_modbus_ to avoid clashing with Zephyr's
 * modbus_*() API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_MODBUS_H_
#define ZEPHYR_A_MODBUS_H_

#include <stddef.h>
#include <stdint.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialise the Modbus client interface.
 * @retval 0 ok, -ENOTSUP if Modbus is not enabled in this build,
 *         other negative errno if the interface could not be initialised.
 */
int app_modbus_init(void);

/** @return Number of channels (registers) in the register table, 0 if disabled. */
size_t app_modbus_channel_count(void);

/** @return Metric name of Modbus channel @p idx (0-based), e.g. "Modbus/Temperature". */
const char *app_modbus_channel_name(size_t idx);

/** @return Unit of Modbus channel @p idx, e.g. "degC". */
const char *app_modbus_channel_unit(size_t idx);

/**
 * @brief Read all registers of the table once (blocking, with retries).
 *
 * Writes exactly app_modbus_channel_count() values to @p out, also when
 * reads fail (then with QUALITY_BAD or QUALITY_STALE).
 *
 * @param out  Array to fill; channel ids are APP_CHANNEL_MODBUS_FIRST + idx.
 * @param max  Capacity of @p out.
 * @return Number of values written, or negative errno if @p max is too small.
 */
int app_modbus_read(struct channel_value *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_MODBUS_H_ */
