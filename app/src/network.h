/*
 * Zephyr_A - network: cellular connectivity (SIM7080G over PPP).
 *
 * Uses Zephyr's modem_cellular driver (compatible "simcom,sim7080", CMUX +
 * PPP over UART1), the cellular driver API (<zephyr/drivers/cellular.h>)
 * for registration status, signal and modem info, net_mgmt L4 events from
 * the connection manager, and pm_device to start/stop the modem.
 *
 * The network thread runs the state machine in docs/02-arkitektur.md
 * section 5.3, logs every state change and the network status, verifies the
 * link with a DNS lookup (+ optional TCP connect) of the own server, and
 * restarts the modem with exponential backoff when the link cannot be
 * established.
 *
 * Owner thread: "network" (event-driven: waits on an internal k_event that
 * is posted from net_mgmt and cellular callbacks and up/down requests).
 * Shared state: network_events() (k_event) - set here, waited on by mqtt.
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

/** States, see the state machine in docs/02-arkitektur.md / 04-netvaerk.md. */
enum network_state {
	NETWORK_STATE_OFF = 0,     /**< modem suspended (pm_device)                      */
	NETWORK_STATE_STARTING,    /**< modem resume: power-on wait, init script, CMUX    */
	NETWORK_STATE_REGISTERING, /**< modem searching / registered, waiting for PPP     */
	NETWORK_STATE_PPP_UP,      /**< PPP up (L4 connected), DNS + connectivity check   */
	NETWORK_STATE_CONNECTED,   /**< link verified -> NETWORK_EVT_UP                   */
	NETWORK_STATE_BACKOFF,     /**< error, modem stopped, waiting before restart      */
};

/** Event bits in the k_event returned by network_events(). */
#define NETWORK_EVT_UP          BIT(0) /**< link verified (IP + DNS usable)  */
#define NETWORK_EVT_DOWN        BIT(1) /**< link lost / modem stopped        */
#define NETWORK_EVT_TIME_SYNCED BIT(2) /**< UTC valid (timesync, part 7)     */

/** @brief Check the modem device and the PPP interface. */
int network_init(void);

/** @brief Start the network thread (brings the link up if APP_NET_AUTOSTART). */
int network_start(void);

/** @brief Request the link up (resume modem, dial PPP). Non-blocking. */
int network_request_up(void);

/** @brief Request the link down (suspend modem). Non-blocking. */
int network_request_down(void);

/** @return Current state (any thread). */
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
 * @brief Current signal strength from the modem.
 * @param rssi_dbm Out: RSSI in dBm.
 * @retval 0 on success, -ENODATA if the modem is not registered.
 */
int network_get_rssi(int16_t *rssi_dbm);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_NETWORK_H_ */
