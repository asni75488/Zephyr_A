/*
 * Zephyr_A - network: cellular connectivity (SIM7080G over PPP).
 *
 * Uses Zephyr's modem_cellular driver (compatible "simcom,sim7080") which
 * runs CMUX + PPP over UART1, the PPP L2 and the connection manager
 * (conn_mgr / NET_EVENT_L4_CONNECTED). The module turns driver/net_mgmt
 * callbacks into a simple state machine and a set of k_event flags that
 * other threads can wait on.
 *
 * Owner thread: "network" (event-driven: waits on its own k_event for
 * net_mgmt callbacks and up/down requests).
 * Shared state: network_events (k_event) - set here, waited on by mqtt.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_NETWORK_H_
#define ZEPHYR_A_NETWORK_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/** States, see the state machine in docs/02-arkitektur.md. */
enum network_state {
	NETWORK_STATE_OFF = 0,     /**< modem suspended (pm_device), radio off   */
	NETWORK_STATE_STARTING,    /**< modem resume, init chat script, CMUX     */
	NETWORK_STATE_REGISTERING, /**< waiting for +CEREG: 1/5 (home/roaming)   */
	NETWORK_STATE_PPP_UP,      /**< PPP link up, IP + DNS from network       */
	NETWORK_STATE_CONNECTED,   /**< L4 connected and UTC time synced         */
	NETWORK_STATE_BACKOFF,     /**< error, waiting before retry              */
};

/** Event bits in the k_event returned by network_events(). */
#define NETWORK_EVT_UP          BIT(0) /**< L4 connected (IP usable)      */
#define NETWORK_EVT_DOWN        BIT(1) /**< link lost / modem suspended   */
#define NETWORK_EVT_TIME_SYNCED BIT(2) /**< UTC valid (timesync)          */

/** @brief Check the modem device and register net_mgmt callbacks. */
int network_init(void);

/** @brief Start the network thread. Does not bring the link up by itself. */
int network_start(void);

/** @brief Request the link up (resume modem, dial PPP). Non-blocking. */
int network_request_up(void);

/** @brief Request the link down (suspend modem -> radio off). Non-blocking. */
int network_request_down(void);

/** @return Current state (read with a spinlock, any thread). */
enum network_state network_get_state(void);

/** @return Name of a state for logging. */
const char *network_state_str(enum network_state state);

/**
 * @brief Wait until the link is up.
 * @retval true link is up, false on timeout.
 */
bool network_wait_up(k_timeout_t timeout);

/** @return The k_event with NETWORK_EVT_* bits (for k_event_wait / k_poll). */
struct k_event *network_events(void);

/**
 * @brief Last known signal quality.
 * @param rssi_dbm Out: RSSI in dBm (from the driver's periodic +CSQ/+CESQ).
 * @retval 0 on success, -ENODATA if not known yet.
 */
int network_get_rssi(int16_t *rssi_dbm);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_NETWORK_H_ */
