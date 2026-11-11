/*
 * Zephyr_A - mqtt_client: MQTT 3.1.1 over TLS to the own broker.
 *
 * Uses Zephyr's MQTT library (<zephyr/net/mqtt.h>) with a TLS socket
 * (CONFIG_MQTT_LIB_TLS, Mbed TLS 4.1/PSA). The broker certificate is
 * verified against the embedded root CAs (tls_ca) and the host name
 * CONFIG_APP_MQTT_BROKER_HOST. Registers the Sparkplug NDEATH as MQTT
 * "Last Will", subscribes to NCMD/DCMD and publishes the messages handed
 * to it by the sparkplug thread.
 *
 * Owner thread: "mqtt" (state machine; zsock_poll() on the TLS socket and an
 * eventfd that is signalled when a message is queued or the link is lost;
 * timeout = time to the next keep-alive).
 * Input:  app_mqtt_submit() -> k_msgq (owned here) + buffer from k_mem_slab.
 * Output: received payloads -> sparkplug_submit_rx().
 *
 * Functions are prefixed app_mqtt_ because Zephyr's MQTT library already
 * defines mqtt_client_init().
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_MQTT_CLIENT_H_
#define ZEPHYR_A_MQTT_CLIENT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Max size of one encoded Sparkplug payload (one slab block). */
#define APP_MQTT_PAYLOAD_MAX 512
/** Max topic length, e.g. "spBv1.0/ProjektA/DDATA/esp32c6-14c19fe0ab24/sensors". */
#define APP_MQTT_TOPIC_MAX   96

/** One message to publish. The payload buffer is owned by the queue until sent. */
struct app_mqtt_msg {
	char topic[APP_MQTT_TOPIC_MAX];
	uint8_t *payload;   /**< block from app_mqtt_alloc(); freed by the mqtt thread */
	uint16_t len;
	uint8_t qos;        /**< 0 for NDATA/DDATA (Sparkplug rule), 1 where required */
	bool retain;
};

/** States, see the state machine in docs/02-arkitektur.md. */
enum app_mqtt_state {
	APP_MQTT_STATE_IDLE = 0,    /**< waiting for NETWORK_EVT_UP           */
	APP_MQTT_STATE_RESOLVING,   /**< DNS lookup of the broker             */
	APP_MQTT_STATE_CONNECTING,  /**< TCP + TLS handshake + MQTT CONNECT   */
	APP_MQTT_STATE_CONNECTED,   /**< CONNACK ok, subscriptions done       */
	APP_MQTT_STATE_BACKOFF,     /**< error, waiting before retry          */
};

/** Counters for the log and the data-use measurements (part 9). */
struct app_mqtt_stats {
	uint32_t connects;          /**< successful CONNACKs                   */
	uint32_t connect_failures;  /**< DNS/TCP/TLS/CONNACK failures          */
	uint32_t tx_msgs;           /**< PUBLISH packets sent                  */
	uint32_t tx_payload_bytes;  /**< sum of PUBLISH payload sizes          */
	uint32_t tx_dropped;        /**< messages lost on a failed publish     */
	uint32_t rx_msgs;           /**< PUBLISH packets received (NCMD/DCMD)  */
	uint32_t last_connect_ms;   /**< TCP + TLS handshake + CONNECT, last   */
	uint16_t last_ciphersuite;  /**< IANA id, e.g. 0x1301 = TLS1.3 AES-128 */
};

/** @brief Register TLS credentials and prepare the client. */
int app_mqtt_init(void);

/** @brief Start the MQTT thread. */
int app_mqtt_start(void);

/** @return true when connected to the broker (CONNACK received). */
bool app_mqtt_is_connected(void);

/** @return Current state. */
enum app_mqtt_state app_mqtt_get_state(void);

/** @return Name of a state for logging. */
const char *app_mqtt_state_str(enum app_mqtt_state state);

/** @brief Copy the counters (any thread). */
void app_mqtt_get_stats(struct app_mqtt_stats *stats);

/**
 * @brief Get a payload buffer of APP_MQTT_PAYLOAD_MAX bytes from the slab.
 * @return Buffer or NULL on timeout.
 */
uint8_t *app_mqtt_alloc(k_timeout_t timeout);

/** @brief Return a buffer that was not submitted. */
void app_mqtt_free(uint8_t *buf);

/**
 * @brief Queue a message for publishing (called by the sparkplug thread).
 *
 * On success the mqtt thread takes ownership of msg->payload.
 * @retval 0 queued, -EAGAIN queue full (caller must free the payload).
 */
int app_mqtt_submit(const struct app_mqtt_msg *msg, k_timeout_t timeout);

/**
 * @brief Set the Last Will used on the next CONNECT (QoS 1, no retain).
 *
 * The payload is copied. Normally not needed: before every CONNECT the mqtt
 * thread asks sparkplug_encode_ndeath() for a fresh NDEATH (new bdSeq); this
 * stored will is only used when that returns an error.
 * @retval 0 ok, -EINVAL topic/payload too long.
 */
int app_mqtt_set_will(const char *topic, const uint8_t *payload, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_MQTT_CLIENT_H_ */
