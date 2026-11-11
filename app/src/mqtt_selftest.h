/*
 * Zephyr_A - mqtt_selftest: end-to-end test message for part 6.
 *
 * Publishes a small, hand-encoded Sparkplug B NDATA
 *   spBv1.0/<group>/NDATA/<edge_node_id>
 * with the metrics Test/Uptime (UInt64, s), Test/RSSI (Float, dBm) and
 * Test/MQTT Connects (UInt64) right after each connect and then every
 * CONFIG_APP_MQTT_SELFTEST_INTERVAL_S. The message goes through the same
 * path as real data (app_mqtt_alloc -> app_mqtt_submit -> mqtt thread), so
 * the queue, TLS, broker ACL and the Node-RED decoder are all exercised.
 *
 * Temporary: replaced by the nanopb-based sparkplug module in part 7
 * (CONFIG_APP_MQTT_SELFTEST=n). No UTC timestamp is sent yet (no time sync
 * before part 7); Node-RED then uses the receive time.
 *
 * Owner: system workqueue (k_work_delayable). Only uses the public mqtt API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_MQTT_SELFTEST_H_
#define ZEPHYR_A_MQTT_SELFTEST_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Prepare the periodic work item (called by app_mqtt_start()). */
void mqtt_selftest_start(void);

/** @brief Publish now and restart the period (called by the mqtt thread after CONNACK). */
void mqtt_selftest_on_connected(void);

/**
 * @brief Encode the self-test payload (exposed for the unit test).
 * @return Encoded length or -ENOMEM if @p size is too small.
 */
int mqtt_selftest_encode(uint8_t *buf, size_t size, uint8_t seq, uint64_t uptime_s,
			 const int16_t *rssi_dbm, uint32_t connects);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_MQTT_SELFTEST_H_ */
