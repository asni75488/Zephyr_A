/*
 * Zephyr_A - sparkplug: Sparkplug B (spBv1.0) payloads and topics.
 *
 * Encodes/decodes Sparkplug B protobuf payloads with nanopb (Zephyr module)
 * from the Eclipse Tahu sparkplug_b.proto. Keeps the Sparkplug session
 * state: bdSeq (birth/death sequence) and seq (0-255 per message), and the
 * metric aliases sent in the BIRTH messages so DDATA can use aliases only.
 *
 * Topic namespace:
 *   spBv1.0/<group_id>/NBIRTH|NDATA|NDEATH|NCMD/<edge_node_id>
 *   spBv1.0/<group_id>/DBIRTH|DDATA|DDEATH|DCMD/<edge_node_id>/<device_id>
 *   group_id = CONFIG_APP_SPARKPLUG_GROUP_ID, edge_node_id = device_info_id(),
 *   device_id = CONFIG_APP_SPARKPLUG_DEVICE_ID
 *
 * Owner thread: "sparkplug" (event-driven: k_poll on the batch queue, the
 * RX queue and a signal for session/config events; timeout = next status
 * NDATA). Input: sparkplug_submit_batch() (app_logic), sparkplug_submit_rx(),
 * sparkplug_on_connected/disconnected() (mqtt), sparkplug_notify_config_changed()
 * (app_logic). Output: app_mqtt_submit(), app_logic_submit_command().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_SPARKPLUG_H_
#define ZEPHYR_A_SPARKPLUG_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "app_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPARKPLUG_NAMESPACE "spBv1.0"

enum sparkplug_msg_type {
	SPB_NBIRTH = 0,
	SPB_NDEATH,
	SPB_NDATA,
	SPB_NCMD,
	SPB_DBIRTH,
	SPB_DDEATH,
	SPB_DDATA,
	SPB_DCMD,
	SPB_MSG_TYPE_COUNT
};

/** @brief Prepare topics/aliases (needs device_info_init() first). */
int sparkplug_init(void);

/** @brief Start the sparkplug thread. */
int sparkplug_start(void);

/**
 * @brief Build the topic for a message type.
 * @return Length written (excluding NUL) or negative errno if too small.
 */
int sparkplug_topic(enum sparkplug_msg_type type, char *buf, size_t size);

/**
 * @brief Encode NDEATH (only bdSeq) for the MQTT Last Will.
 * Called by mqtt before every CONNECT; increments bdSeq.
 * @return Encoded length or negative errno.
 */
int sparkplug_encode_ndeath(uint8_t *buf, size_t size);

/**
 * @brief Notify that MQTT is (re)connected: queue NBIRTH + DBIRTH
 * (seq restarts at 0, NBIRTH carries the same bdSeq as the will).
 */
void sparkplug_on_connected(void);

/**
 * @brief Notify that the MQTT session ended. Batches are kept in the queue
 * (not encoded) until the BIRTH of the next session.
 */
void sparkplug_on_disconnected(void);

/** @brief Configuration changed (app_logic): publish the Config metrics as NDATA. */
void sparkplug_notify_config_changed(void);

/**
 * @brief End the session on purpose (interval uplink, part 9): NDEATH after
 * all queued messages, then a clean MQTT DISCONNECT (no Will).
 */
void sparkplug_end_session(void);

/** @brief Number of batches waiting for the next session. */
uint32_t sparkplug_batches_queued(void);

/**
 * @brief Queue a batch of samples to be sent as one DDATA (from app_logic).
 * The batch is copied.
 * @retval 0 queued, -EAGAIN queue full.
 */
int sparkplug_submit_batch(const struct measurement_batch *batch, k_timeout_t timeout);

/**
 * @brief Queue a received MQTT message (NCMD/DCMD/STATE) for decoding.
 * Called from the mqtt thread; the payload is copied.
 */
int sparkplug_submit_rx(const char *topic, const uint8_t *payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_SPARKPLUG_H_ */
