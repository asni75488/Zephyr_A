/*
 * Zephyr_A - power: low-power policy (sleep modes).
 *
 * Wraps Zephyr's power management: the HP core enters light sleep
 * automatically in the idle thread (CONFIG_PM, power state "standby" in the
 * ESP32-C6 devicetree) whenever all threads are blocked. This module lets
 * modules temporarily forbid sleep while timing matters, and prepares deep
 * sleep with LP-core wake-up (remote/) in the low-power part.
 *
 * Owner thread: none (library, called by app_logic and network).
 * Shared data: lock counter, protected by Zephyr's pm_policy_state_lock_*
 * (atomic inside the PM subsystem).
 *
 * Functions are prefixed app_power_ to avoid clashing with Zephyr names.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_POWER_H_
#define ZEPHYR_A_POWER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Initialise power policy. */
int app_power_init(void);

/** @brief Forbid light sleep (e.g. while the modem UART is active). Nestable. */
void app_power_sleep_lock(void);

/** @brief Allow light sleep again. */
void app_power_sleep_unlock(void);

/**
 * @brief Enter deep sleep; the LP core or a timer wakes the HP core.
 * Does not return on success (the chip resets on wake-up).
 * @return Negative errno if deep sleep is not possible.
 */
int app_power_deep_sleep(uint32_t duration_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_POWER_H_ */
