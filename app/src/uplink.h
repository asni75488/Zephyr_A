/*
 * Zephyr_A - uplink: interval uplink scheduler (part 9).
 *
 * Only active with CONFIG_APP_UPLINK_INTERVAL. The uplink thread decides
 * when the cellular link is used:
 *
 *   WAIT --interval elapsed / batch queue 3/4 full--> network_request_up()
 *   UP   --sparkplug: BIRTH sent + batch queue empty--> linger
 *        --linger over--> sparkplug_end_session() (NDEATH + MQTT DISCONNECT)
 *        --MQTT closed--> network_request_down() (modem radio off) --> WAIT
 *   UP   --APP_UPLINK_TIMEOUT_S without success--> network_request_down()
 *
 * Owner thread: uplink (priority 9, lowest application thread).
 * Other modules only post events (k_event, thread-safe).
 *
 * In continuous mode uplink_init() returns -ENOTSUP and the notify
 * functions do nothing.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_UPLINK_H_
#define ZEPHYR_A_UPLINK_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct uplink_stats {
	uint32_t uplinks;        /**< uplinks started               */
	uint32_t failures;       /**< uplinks that timed out        */
	uint32_t last_online_s;  /**< modem-on time of the last uplink */
	uint32_t last_tx_bytes;  /**< MQTT payload bytes of the last uplink */
};

/** @brief Initialise; -ENOTSUP in continuous mode. */
int uplink_init(void);

/** @brief Start the uplink thread (first uplink right away: time + BIRTH). */
int uplink_start(void);

/** @brief app_logic queued a batch (may start an early uplink). */
void uplink_notify_batch(void);

/** @brief sparkplug: BIRTH sent and batch queue empty in this session. */
void uplink_notify_flushed(void);

/** @brief true while the uplink thread keeps the link up on purpose. */
bool uplink_is_active(void);

/** @brief Counters. */
void uplink_get_stats(struct uplink_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_UPLINK_H_ */
