/*
 * Zephyr_A - health: task watchdog for the application threads (part 10).
 *
 * Built on Zephyr's task watchdog (subsys/task_wdt) with the ESP32-C6 timer
 * group watchdog (wdt0, TIMG0) as hardware fallback:
 *
 *  - Every application thread has one channel with its own timeout.
 *  - A thread calls health_feed() at every turn of its loop and around long
 *    blocking calls that have a bounded duration.
 *  - Before a wait that may legitimately last forever (waiting for an event
 *    that only another module produces, a long backoff), the thread calls
 *    health_pause(): its channel is removed, so only the time a thread is
 *    BUSY is supervised. The next health_feed() adds the channel again.
 *  - If a channel times out (thread stuck in a loop, deadlock, waiting on a
 *    lock forever), the thread is recorded in the LP SRAM (survives the
 *    reset, reported at the next boot and in NBIRTH "Reset Cause") and the
 *    chip is reset.
 *  - The hardware watchdog catches what the kernel timer cannot: interrupts
 *    disabled or the kernel itself stuck (no task watchdog timer callback
 *    for CONFIG_TASK_WDT_MIN_TIMEOUT + CONFIG_TASK_WDT_HW_FALLBACK_DELAY).
 *
 * The LP-core supervisor (part 9) stays: it is independent of the HP core
 * and watches the measurement chain; the task watchdog covers all threads.
 *
 * Thread safety: each slot is used by exactly one thread (its owner);
 * task_wdt itself is thread-safe.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_HEALTH_H_
#define ZEPHYR_A_HEALTH_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Supervised threads (one task watchdog channel each). */
enum health_task {
	HEALTH_SENSOR = 0,
	HEALTH_APP_LOGIC,
	HEALTH_NETWORK,
	HEALTH_MQTT,
	HEALTH_SPARKPLUG,
	HEALTH_UPLINK,
	HEALTH_TASK_COUNT,
};

/** @brief Start the task watchdog (and the hardware fallback). */
int health_init(void);

/** @brief The calling thread is alive (adds its channel if paused). */
void health_feed(enum health_task task);

/** @brief The calling thread starts a wait that may last forever. */
void health_pause(enum health_task task);

/** @brief Name of a task (for logs). */
const char *health_task_name(enum health_task task);

/**
 * @brief Thread whose watchdog caused the last reset.
 * @return Task name, or NULL if the last reset was not a task watchdog reset.
 */
const char *health_last_reset_task(void);

/** @brief Timeouts of a task since boot (only > 0 with CONFIG_APP_HEALTH_REBOOT=n, tests). */
unsigned int health_expirations(enum health_task task);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_HEALTH_H_ */
