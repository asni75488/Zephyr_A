/*
 * Zephyr_A - app_logic: application logic / batching / commands.
 *
 * Receives measurements from the sensor thread (k_msgq owned here), collects
 * them into batches of app_config.batch_size and hands each full batch to
 * sparkplug. Owns the runtime configuration (sample interval, batch size)
 * and applies commands from the cloud (NCMD, decoded by sparkplug).
 *
 * Owner thread: "app_logic" (event-driven: k_poll on the measurement queue
 * and the command queue).
 * Shared data: struct app_config, protected by a k_mutex (read by sensor).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_APP_LOGIC_H_
#define ZEPHYR_A_APP_LOGIC_H_

#include <zephyr/kernel.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Load the default configuration from Kconfig. */
int app_logic_init(void);

/** @brief Start the app_logic thread. */
int app_logic_start(void);

/**
 * @brief Queue one measurement (called by the sensor thread). Copied.
 * @retval 0 queued, -EAGAIN / -ENOMSG queue full (measurement dropped).
 */
int app_logic_submit_measurement(const struct measurement *m, k_timeout_t timeout);

/**
 * @brief Queue a decoded command (called by the sparkplug thread).
 * @retval 0 queued, -EAGAIN / -ENOMSG queue full.
 */
int app_logic_submit_command(const struct app_cmd *cmd, k_timeout_t timeout);

/** @brief Thread-safe copy of the current configuration (k_mutex). */
void app_logic_get_config(struct app_config *out);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_APP_LOGIC_H_ */
