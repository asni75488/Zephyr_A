/*
 * Zephyr_A - modbus: Modbus RTU/TCP client for data acquisition.
 *
 * Uses Zephyr's Modbus subsystem (<zephyr/modbus/modbus.h>, client role).
 * The physical interface (RTU over RS485 or TCP over the cellular link) is
 * not decided yet - see docs/02-arkitektur.md, "Pin- og UART-plan".
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

/** @brief Initialise the Modbus client interface. -ENOTSUP if not configured. */
int app_modbus_init(void);

/**
 * @brief Read the configured registers of one slave.
 *
 * @param out  Array to fill with samples (source SAMPLE_SRC_MODBUS_x).
 * @param max  Capacity of @p out.
 * @return Number of samples written or negative errno.
 */
int app_modbus_read(struct sample *out, size_t max);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_MODBUS_H_ */
