/*
 * Zephyr_A - power: sleep policy and LP core (part 9).
 *
 * - Light sleep: the HP core enters light sleep automatically in the idle
 *   thread (CONFIG_PM, power state "standby", woken by the RTC timer)
 *   whenever all threads wait. Modules forbid it while a UART transfer can
 *   happen (modem link, Modbus/sensor read): bytes that arrive during light
 *   sleep are lost because the UARTs have no flow control.
 * - LP core: sets up the memory shared with the LP-core program
 *   (app/remote, app/common/lp_shared.h), feeds its supervisor with a
 *   heartbeat and uses it to wake the HP core from deep sleep.
 * - Deep sleep: sys_poweroff() with the LP core as wake-up source.
 *
 * Owner thread: none (library). The heartbeat is given by app_logic only;
 * sleep locks are counted by Zephyr's PM policy (thread-safe).
 *
 * Functions are prefixed app_power_ to avoid clashing with Zephyr names.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_POWER_H_
#define ZEPHYR_A_POWER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** LP-core counters for logs and NBIRTH (all 0 without LP core). */
struct app_power_lp_stats {
	bool active;          /**< LP core runs and its image was verified */
	uint32_t steps;       /**< LP steps since the LP image was loaded   */
	uint32_t wakes;       /**< HP wake-ups from deep sleep (total)      */
	uint32_t resets;      /**< HP resets by the supervisor (total)      */
};

/** @brief Initialise sleep policy and the LP core interface. */
int app_power_init(void);

/** @brief Forbid light sleep (UART transfer possible). Nestable. */
void app_power_sleep_lock(void);

/** @brief Allow light sleep again (one call per app_power_sleep_lock()). */
void app_power_sleep_unlock(void);

/** @brief Heartbeat for the LP-core supervisor (once per processed measurement). */
void app_power_heartbeat(void);

/** @brief true if this boot was caused by the LP-core supervisor. */
bool app_power_reset_by_lp(void);

/** @brief true if deep sleep with LP-core wake-up is available. */
bool app_power_deep_sleep_supported(void);

/**
 * @brief Enter deep sleep; the LP core wakes the HP core after @p seconds.
 * The caller must have stopped the modem and announced NDEATH before.
 * Does not return on success (the chip boots again on wake-up).
 * @return -ENOTSUP if deep sleep with LP-core wake-up is not available.
 */
int app_power_deep_sleep(uint32_t seconds);

/** @brief LP-core counters. */
void app_power_get_lp_stats(struct app_power_lp_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_POWER_H_ */
