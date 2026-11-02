/*
 * Zephyr_A - timesync: UTC time for timestamps.
 *
 * Every MQTT message must carry a timestamp. Sparkplug B uses UTC
 * milliseconds since 1970. The ESP32-C6 has no battery-backed RTC on the
 * DevKitC, so UTC is obtained with SNTP over the cellular link after PPP is
 * up, and kept with the kernel clock (CLOCK_REALTIME) afterwards.
 *
 * Owner thread: network (calls timesync_sync() after "PPP up").
 * Readers: sensor (timesync_now_ms()), sparkplug. Reads are lock-free
 * (64-bit offset updated with an atomic-safe spinlock).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_TIMESYNC_H_
#define ZEPHYR_A_TIMESYNC_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Prepare the module. No network access. */
int timesync_init(void);

/**
 * @brief Synchronise UTC with SNTP (blocking, network must be up).
 *
 * @param timeout_ms Maximum time to wait for the SNTP reply.
 * @retval 0 on success, negative errno on failure (time stays unsynced
 *         or keeps the previous value).
 */
int timesync_sync(uint32_t timeout_ms);

/** @return true once UTC has been synchronised at least once since boot. */
bool timesync_is_valid(void);

/**
 * @brief Current UTC time in ms since 1970-01-01.
 * @return UTC ms, or 0 if time has never been synchronised.
 */
int64_t timesync_now_ms(void);

/**
 * @brief Convert an uptime stamp (k_uptime_get()) to UTC ms.
 *
 * Used to back-date samples that were taken before the first SNTP sync.
 * @return UTC ms, or 0 if time is not valid yet.
 */
int64_t timesync_uptime_to_utc_ms(int64_t uptime_ms);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_TIMESYNC_H_ */
