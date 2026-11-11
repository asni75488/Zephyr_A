/*
 * Zephyr_A - network: cellular connectivity (part 4).
 *
 * Flow (one thread, event-driven):
 *   OFF --request up--> STARTING (CMUX recovery, pm RESUME, net_if_up)
 *   STARTING --registration callback--> REGISTERING
 *   REGISTERING --NET_EVENT_L4_CONNECTED--> PPP_UP
 *   PPP_UP --DNS lookup (+TCP connect) of own server OK--> CONNECTED (EVT_UP)
 *   CONNECTED --L4_DISCONNECTED--> REGISTERING (grace period, driver redials)
 *   any state --timeout/error--> BACKOFF (modem suspended) --delay--> STARTING
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/drivers/cellular.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/ppp.h>
#include <zephyr/net/socket.h>
#include <zephyr/pm/device.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>

#include "network.h"

LOG_MODULE_REGISTER(network, CONFIG_APP_LOG_LEVEL);

#define MODEM_NODE DT_ALIAS(modem)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(MODEM_NODE), "app.overlay must define an enabled modem node");

static const struct device *const modem = DEVICE_DT_GET(MODEM_NODE);
static const struct device *const modem_uart = DEVICE_DT_GET(DT_BUS(MODEM_NODE));
static struct net_if *ppp_iface;

/* Public events (waited on by other modules). */
static K_EVENT_DEFINE(net_events);

/* Internal events for the network thread. */
#define INT_REQ_UP      BIT(0)
#define INT_REQ_DOWN    BIT(1)
#define INT_L4_UP       BIT(2)
#define INT_L4_DOWN     BIT(3)
#define INT_DNS_ADDED   BIT(4)
#define INT_REG_CHANGED BIT(5)
#define INT_ALL         (INT_REQ_UP | INT_REQ_DOWN | INT_L4_UP | INT_L4_DOWN | INT_DNS_ADDED | \
			 INT_REG_CHANGED)

static K_EVENT_DEFINE(int_events);

static struct k_spinlock state_lock;
static enum network_state state = NETWORK_STATE_OFF;
static atomic_t reg_status = ATOMIC_INIT(CELLULAR_REGISTRATION_UNKNOWN);
static bool modem_running;
static bool modem_info_logged;
static bool started;

static void network_thread(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(network_tid, CONFIG_APP_NETWORK_THREAD_STACK, network_thread, NULL, NULL, NULL,
		CONFIG_APP_NETWORK_THREAD_PRIO, 0, SYS_FOREVER_MS);

static const char *const state_names[] = {
	[NETWORK_STATE_OFF] = "off",
	[NETWORK_STATE_STARTING] = "starting",
	[NETWORK_STATE_REGISTERING] = "registering",
	[NETWORK_STATE_PPP_UP] = "ppp-up",
	[NETWORK_STATE_CONNECTED] = "connected",
	[NETWORK_STATE_BACKOFF] = "backoff",
};

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------
 */
static const char *reg_status_str(int st)
{
	switch (st) {
	case CELLULAR_REGISTRATION_NOT_REGISTERED:
		return "not registered";
	case CELLULAR_REGISTRATION_REGISTERED_HOME:
		return "registered (home)";
	case CELLULAR_REGISTRATION_SEARCHING:
		return "searching";
	case CELLULAR_REGISTRATION_DENIED:
		return "DENIED";
	case CELLULAR_REGISTRATION_REGISTERED_ROAMING:
		return "registered (roaming)";
	default:
		return "unknown";
	}
}

static bool reg_is_registered(int st)
{
	return (st == CELLULAR_REGISTRATION_REGISTERED_HOME) ||
	       (st == CELLULAR_REGISTRATION_REGISTERED_ROAMING);
}

static void set_state(enum network_state new_state)
{
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	enum network_state old = state;

	state = new_state;
	k_spin_unlock(&state_lock, key);

	if (old != new_state) {
		LOG_INF("Network: %s -> %s", network_state_str(old), network_state_str(new_state));
	}
}

static void log_signal(const char *prefix)
{
	int16_t rssi;
	int ret = cellular_get_signal(modem, CELLULAR_SIGNAL_RSSI, &rssi);

	if (ret == 0) {
		LOG_INF("%s: %s, RSSI %d dBm, registration %s", prefix,
			network_state_str(network_get_state()), rssi,
			reg_status_str((int)atomic_get(&reg_status)));
	} else {
		LOG_INF("%s: %s, RSSI n/a (%d), registration %s", prefix,
			network_state_str(network_get_state()), ret,
			reg_status_str((int)atomic_get(&reg_status)));
	}
}

static void log_modem_info_once(void)
{
	char buf[32];

	if (modem_info_logged) {
		return;
	}
	modem_info_logged = true;

	if (cellular_get_modem_info(modem, CELLULAR_MODEM_INFO_MODEL_ID, buf, sizeof(buf)) == 0) {
		LOG_INF("Modem model: %s", buf);
	}
	if (cellular_get_modem_info(modem, CELLULAR_MODEM_INFO_FW_VERSION, buf, sizeof(buf)) == 0) {
		LOG_INF("Modem firmware: %s", buf);
	}
	if (cellular_get_modem_info(modem, CELLULAR_MODEM_INFO_IMEI, buf, sizeof(buf)) == 0) {
		LOG_INF("Modem IMEI: %s", buf);
	}
	if (cellular_get_modem_info(modem, CELLULAR_MODEM_INFO_SIM_ICCID, buf, sizeof(buf)) == 0) {
		LOG_INF("SIM ICCID: %s", buf);
	}
}

static void log_ip_address(void)
{
	char addr_str[NET_IPV4_ADDR_LEN];
	struct net_in_addr *addr = net_if_ipv4_get_global_addr(ppp_iface, NET_ADDR_PREFERRED);

	if ((addr != NULL) &&
	    (net_addr_ntop(NET_AF_INET, addr, addr_str, sizeof(addr_str)) != NULL)) {
		LOG_INF("PPP up, IPv4 address %s", addr_str);
	} else {
		LOG_INF("PPP up (no IPv4 address yet)");
	}
}

/*
 * The modem keeps power when the ESP32-C6 resets (flashing, reset button)
 * and may still be in CMUX mode from the previous run. It then ignores plain
 * AT commands and, without a PWRKEY/RESET pin, the driver could never
 * recover. Send a 3GPP TS 27.010 multiplexer close-down command (CLD, UIH
 * frame on DLCI 0, same encoding as Zephyr's modem_cmux disconnect).
 * In AT command mode the bytes are just ignored by the modem.
 *   F9 | 03 (DLCI0, C/R=1, EA=1) | EF (UIH) | 05 (len 2) | C3 01 (CLD) | FCS | F9
 * FCS 0xF2 = 0xFF - CRC8 over 03 EF 05 (verified against reference frames).
 */
static void cmux_recovery(void)
{
	static const uint8_t cld_frame[] = {0xF9, 0x03, 0xEF, 0x05, 0xC3, 0x01, 0xF2, 0xF9};

	if (!IS_ENABLED(CONFIG_APP_NET_CMUX_RECOVERY) || !device_is_ready(modem_uart)) {
		return;
	}

	/* Two frames: the first one may be swallowed while the modem re-syncs
	 * on the 0xF9 flags.
	 */
	for (int n = 0; n < 2; n++) {
		for (size_t i = 0; i < sizeof(cld_frame); i++) {
			uart_poll_out(modem_uart, cld_frame[i]);
		}
	}
	k_msleep(300); /* let the modem leave CMUX mode */
	LOG_DBG("CMUX close-down sent to modem");
}

static void modem_start(void)
{
	int ret;

	cmux_recovery();

	LOG_INF("Starting modem (APN \"%s\")", CONFIG_MODEM_CELLULAR_APN);
	ret = pm_device_action_run(modem, PM_DEVICE_ACTION_RESUME);
	if ((ret != 0) && (ret != -EALREADY)) {
		LOG_ERR("Modem resume failed (%d)", ret);
	}

	ret = net_if_up(ppp_iface);
	if ((ret != 0) && (ret != -EALREADY)) {
		LOG_ERR("net_if_up(PPP) failed (%d)", ret);
	}

	modem_running = true;
}

static void modem_stop(void)
{
	int ret;

	if (!modem_running) {
		return;
	}

	LOG_INF("Stopping modem");
	ret = net_if_down(ppp_iface);
	if ((ret != 0) && (ret != -EALREADY)) {
		LOG_WRN("net_if_down(PPP) failed (%d)", ret);
	}

	ret = pm_device_action_run(modem, PM_DEVICE_ACTION_SUSPEND);
	if ((ret != 0) && (ret != -EALREADY)) {
		LOG_ERR("Modem suspend failed (%d)", ret);
	}

	modem_running = false;
	atomic_set(&reg_status, CELLULAR_REGISTRATION_UNKNOWN);
}

static void publish_up(bool up)
{
	if (up) {
		k_event_clear(&net_events, NETWORK_EVT_DOWN);
		k_event_post(&net_events, NETWORK_EVT_UP);
	} else {
		k_event_clear(&net_events, NETWORK_EVT_UP);
		k_event_post(&net_events, NETWORK_EVT_DOWN);
	}
}

/* DNS lookup (+ optional TCP connect) of the own server. */
static int connectivity_check(void)
{
	struct zsock_addrinfo hints = {
		.ai_family = NET_AF_INET,
		.ai_socktype = NET_SOCK_STREAM,
	};
	struct zsock_addrinfo *res = NULL;
	char port_str[8];
	char addr_str[NET_IPV4_ADDR_LEN] = "?";
	int64_t t0 = k_uptime_get();
	int ret;

	(void)snprintf(port_str, sizeof(port_str), "%d",
		       (CONFIG_APP_NET_CHECK_PORT > 0) ? CONFIG_APP_NET_CHECK_PORT : 80);

	ret = zsock_getaddrinfo(CONFIG_APP_NET_CHECK_HOST, port_str, &hints, &res);
	if ((ret != 0) || (res == NULL)) {
		LOG_ERR("DNS lookup of %s failed (%d)", CONFIG_APP_NET_CHECK_HOST, ret);
		return -EHOSTUNREACH;
	}

	(void)net_addr_ntop(NET_AF_INET, &net_sin(res->ai_addr)->sin_addr, addr_str,
			    sizeof(addr_str));
	LOG_INF("DNS: %s -> %s (%lld ms)", CONFIG_APP_NET_CHECK_HOST, addr_str,
		k_uptime_get() - t0);

	if (CONFIG_APP_NET_CHECK_PORT > 0) {
		int sock = zsock_socket(NET_AF_INET, NET_SOCK_STREAM, NET_IPPROTO_TCP);

		if (sock < 0) {
			LOG_WRN("socket() failed (%d)", errno);
		} else {
			t0 = k_uptime_get();
			if (zsock_connect(sock, res->ai_addr, res->ai_addrlen) == 0) {
				LOG_INF("TCP connect to %s:%d OK (%lld ms)", addr_str,
					CONFIG_APP_NET_CHECK_PORT, k_uptime_get() - t0);
			} else {
				/* Not fatal: the server/firewall may simply not listen. */
				LOG_WRN("TCP connect to %s:%d failed (errno %d)", addr_str,
					CONFIG_APP_NET_CHECK_PORT, errno);
			}
			(void)zsock_close(sock);
		}
	}

	zsock_freeaddrinfo(res);
	return 0;
}

/* ------------------------------------------------------------------------
 * Callbacks (driver / net_mgmt context: only post events)
 * ------------------------------------------------------------------------
 */
static void l4_event_handler(uint64_t mgmt_event, struct net_if *iface, void *info,
			     size_t info_length, void *user_data)
{
	ARG_UNUSED(info);
	ARG_UNUSED(info_length);
	ARG_UNUSED(user_data);

	if ((ppp_iface != NULL) && (iface != ppp_iface)) {
		return;
	}

	switch (mgmt_event) {
	case NET_EVENT_L4_CONNECTED:
		k_event_post(&int_events, INT_L4_UP);
		break;
	case NET_EVENT_L4_DISCONNECTED:
		k_event_post(&int_events, INT_L4_DOWN);
		break;
	case NET_EVENT_DNS_SERVER_ADD:
		k_event_post(&int_events, INT_DNS_ADDED);
		break;
	default:
		break;
	}
}

NET_MGMT_REGISTER_EVENT_HANDLER(app_network_l4, NET_EVENT_L4_CONNECTED |
			       NET_EVENT_L4_DISCONNECTED | NET_EVENT_DNS_SERVER_ADD,
			       l4_event_handler, NULL);

static void cellular_event_handler(const struct device *dev, enum cellular_event evt,
				   const void *payload, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	if (evt == CELLULAR_EVENT_REGISTRATION_STATUS_CHANGED) {
		const struct cellular_evt_registration_status *rs = payload;

		atomic_set(&reg_status, rs->status);
		k_event_post(&int_events, INT_REG_CHANGED);
	}
}

/* ------------------------------------------------------------------------
 * Thread
 * ------------------------------------------------------------------------
 */
static uint32_t take_events(uint32_t mask, k_timeout_t timeout)
{
	uint32_t ev = k_event_wait(&int_events, mask, false, timeout);

	if (ev != 0U) {
		k_event_clear(&int_events, ev);
	}
	return ev;
}

static k_timeout_t until(int64_t deadline_ms)
{
	int64_t left = deadline_ms - k_uptime_get();

	return (left > 0) ? K_MSEC(left) : K_NO_WAIT;
}

static void network_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t backoff_s = CONFIG_APP_NET_BACKOFF_MIN_S;
	uint32_t failures = 0U;
	int64_t deadline = 0;
	bool dns_ready = false;

	while (true) {
		enum network_state st = network_get_state();
		uint32_t ev;

		switch (st) {
		case NETWORK_STATE_OFF:
			(void)take_events(INT_REQ_UP, K_FOREVER);
			k_event_clear(&int_events, INT_ALL); /* drop stale events */
			dns_ready = false;
			modem_start();
			deadline = k_uptime_get() + (CONFIG_APP_NET_CONNECT_TIMEOUT_S * 1000LL);
			set_state(NETWORK_STATE_STARTING);
			break;

		case NETWORK_STATE_STARTING:
		case NETWORK_STATE_REGISTERING:
			ev = take_events(INT_REQ_DOWN | INT_L4_UP | INT_DNS_ADDED | INT_REG_CHANGED,
					 until(deadline));
			if (ev & INT_REQ_DOWN) {
				modem_stop();
				publish_up(false);
				set_state(NETWORK_STATE_OFF);
				break;
			}
			if (ev & INT_DNS_ADDED) {
				dns_ready = true;
			}
			if (ev & INT_REG_CHANGED) {
				int rs = (int)atomic_get(&reg_status);

				LOG_INF("Cellular registration: %s", reg_status_str(rs));
				if (rs == CELLULAR_REGISTRATION_DENIED) {
					LOG_ERR("Registration denied by the network (SIM/APN/coverage?)");
				}
				if (st == NETWORK_STATE_STARTING) {
					set_state(NETWORK_STATE_REGISTERING);
				}
			}
			if (ev & INT_L4_UP) {
				set_state(NETWORK_STATE_PPP_UP);
				log_ip_address();
				break;
			}
			if ((ev == 0U) && (k_uptime_get() >= deadline)) {
				LOG_ERR("No IP link before the deadline (state %s, registration: %s)",
					network_state_str(st),
					reg_status_str((int)atomic_get(&reg_status)));
				set_state(NETWORK_STATE_BACKOFF);
			}
			break;

		case NETWORK_STATE_PPP_UP:
			/* DNS servers are learned via PPP IPCP; give them a moment. */
			if (!dns_ready) {
				ev = take_events(INT_DNS_ADDED | INT_L4_DOWN | INT_REQ_DOWN,
						 K_SECONDS(10));
				if (ev & INT_REQ_DOWN) {
					modem_stop();
					set_state(NETWORK_STATE_OFF);
					break;
				}
				if (ev & INT_L4_DOWN) {
					LOG_WRN("PPP went down before the link was verified");
					set_state(NETWORK_STATE_REGISTERING);
					break;
				}
				if (!(ev & INT_DNS_ADDED)) {
					LOG_WRN("No DNS server announced by PPP, trying anyway");
				}
				dns_ready = true;
			}

			log_modem_info_once();
			if (connectivity_check() == 0) {
				failures = 0U;
				backoff_s = CONFIG_APP_NET_BACKOFF_MIN_S;
				set_state(NETWORK_STATE_CONNECTED);
				log_signal("Network status");
				publish_up(true);
			} else {
				set_state(NETWORK_STATE_BACKOFF);
			}
			break;

		case NETWORK_STATE_CONNECTED:
			ev = take_events(INT_L4_DOWN | INT_REQ_DOWN | INT_REG_CHANGED,
					 K_SECONDS(CONFIG_APP_NET_STATUS_INTERVAL_S));
			if (ev & INT_REQ_DOWN) {
				publish_up(false);
				modem_stop();
				set_state(NETWORK_STATE_OFF);
				break;
			}
			if (ev & INT_REG_CHANGED) {
				int rs = (int)atomic_get(&reg_status);

				if (reg_is_registered(rs)) {
					LOG_INF("Cellular registration: %s", reg_status_str(rs));
				} else {
					LOG_WRN("Cellular registration: %s", reg_status_str(rs));
				}
			}
			if (ev & INT_L4_DOWN) {
				LOG_WRN("Network link lost - waiting %d s for the modem to reconnect",
					CONFIG_APP_NET_RECONNECT_GRACE_S);
				publish_up(false);
				dns_ready = false;
				deadline = k_uptime_get() + (CONFIG_APP_NET_RECONNECT_GRACE_S * 1000LL);
				set_state(NETWORK_STATE_REGISTERING);
				break;
			}
			if (ev == 0U) {
				log_signal("Network status"); /* periodic status log */
			}
			break;

		case NETWORK_STATE_BACKOFF:
		default:
			publish_up(false);
			modem_stop();
			failures++;
			LOG_WRN("Network failure #%u - restarting modem in %u s", failures, backoff_s);

			ev = take_events(INT_REQ_DOWN, K_SECONDS(backoff_s));
			backoff_s = MIN(backoff_s * 2U, (uint32_t)CONFIG_APP_NET_BACKOFF_MAX_S);
			if (ev & INT_REQ_DOWN) {
				set_state(NETWORK_STATE_OFF);
				break;
			}

			k_event_clear(&int_events, INT_ALL);
			dns_ready = false;
			modem_start();
			deadline = k_uptime_get() + (CONFIG_APP_NET_CONNECT_TIMEOUT_S * 1000LL);
			set_state(NETWORK_STATE_STARTING);
			break;
		}
	}
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------
 */
int network_init(void)
{
	if (!device_is_ready(modem)) {
		LOG_ERR("Modem device %s not ready", modem->name);
		return -ENODEV;
	}

	ppp_iface = net_if_get_first_by_type(&NET_L2_GET_NAME(PPP));
	if (ppp_iface == NULL) {
		LOG_ERR("No PPP network interface");
		return -ENODEV;
	}

	int ret = cellular_set_callback(modem, CELLULAR_EVENT_REGISTRATION_STATUS_CHANGED,
					cellular_event_handler, NULL);

	if (ret != 0) {
		LOG_WRN("cellular_set_callback failed (%d) - no registration logs", ret);
	}

	k_event_post(&net_events, NETWORK_EVT_DOWN);
	LOG_INF("Modem %s on %s, APN \"%s\"", modem->name, modem_uart->name,
		CONFIG_MODEM_CELLULAR_APN);
	return 0;
}

int network_start(void)
{
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_name_set(network_tid, "network");
	k_thread_start(network_tid);

	if (IS_ENABLED(CONFIG_APP_NET_AUTOSTART)) {
		(void)network_request_up();
	}
	return 0;
}

int network_request_up(void)
{
	k_event_post(&int_events, INT_REQ_UP);
	return 0;
}

int network_request_down(void)
{
	k_event_post(&int_events, INT_REQ_DOWN);
	return 0;
}

enum network_state network_get_state(void)
{
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	enum network_state st = state;

	k_spin_unlock(&state_lock, key);
	return st;
}

const char *network_state_str(enum network_state st)
{
	if ((unsigned int)st >= ARRAY_SIZE(state_names)) {
		return "?";
	}
	return state_names[st];
}

bool network_wait_up(k_timeout_t timeout)
{
	return k_event_wait(&net_events, NETWORK_EVT_UP, false, timeout) != 0U;
}

struct k_event *network_events(void)
{
	return &net_events;
}

int network_get_rssi(int16_t *rssi_dbm)
{
	return cellular_get_signal(modem, CELLULAR_SIGNAL_RSSI, rssi_dbm);
}
