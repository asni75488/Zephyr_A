/*
 * Zephyr_A - mqtt_client: MQTT 3.1.1 over TLS to the own broker.
 *
 * State machine (docs/02-arkitektur.md 5.4, docs/06-mqtt-tls.md):
 *
 *   IDLE --NETWORK_EVT_UP--> RESOLVING --DNS ok--> CONNECTING
 *   CONNECTING --TCP + TLS + CONNACK ok--> CONNECTED
 *   CONNECTED --socket error / PINGRESP timeout / DISCONNECT--> BACKOFF
 *   CONNECTED --L4 link lost--> IDLE (no backoff, the network thread
 *                                     re-establishes the link)
 *   RESOLVING/CONNECTING --error--> BACKOFF --delay--> IDLE
 *
 * Backoff doubles from APP_MQTT_BACKOFF_MIN_S to APP_MQTT_BACKOFF_MAX_S
 * (+ up to 25 % random jitter, so many devices do not reconnect in step).
 * Errors that do not go away by themselves (certificate verification,
 * wrong password, not authorised) jump directly to the maximum, so a
 * misconfigured device does not spend its data plan on TLS handshakes.
 *
 * Threading: everything that touches the MQTT client runs in the mqtt
 * thread (the MQTT library is not thread-safe across calls that use the
 * socket). Other threads only use app_mqtt_alloc/submit/free (k_mem_slab +
 * k_msgq, thread-safe) and wake the thread through an eventfd that is
 * polled together with the socket. No busy-waiting: the thread sleeps in
 * zsock_poll() until data arrives, a message is queued, the link is lost or
 * the keep-alive is due.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/zvfs/eventfd.h>

#include "device_info.h"
#include "mqtt_client.h"
#include "network.h"
#include "sparkplug.h"
#include "tls_ca.h"
#if defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
#include <mbedtls/memory_buffer_alloc.h>
#endif

LOG_MODULE_REGISTER(app_mqtt, CONFIG_APP_LOG_LEVEL);

/* MQTT packet buffers. TX holds the fixed header + topic of a PUBLISH (the
 * payload is sent from the slab block with a second iovec) and the whole
 * CONNECT (client id, user, password, will topic + NDEATH payload, ~25 B).
 * RX holds incoming packet headers; PUBLISH payloads are read separately.
 */
#define MQTT_TX_BUF_SIZE 384
#define MQTT_RX_BUF_SIZE 256
/** Largest NCMD/DCMD payload that is passed on; larger ones are discarded. */
#define RX_PAYLOAD_MAX   256
#define WILL_PAYLOAD_MAX 64
#define SUB_TOPIC_COUNT  2

/* Internal error codes (positive range of errno is not used by Zephyr). */
#define ERR_LINK_LOST    (-ENETDOWN)

K_MSGQ_DEFINE(mqtt_tx_q, sizeof(struct app_mqtt_msg), CONFIG_APP_MQTT_TX_QUEUE_LEN, 4);
/* One block more than the queue: one payload is being encoded meanwhile. */
K_MEM_SLAB_DEFINE_STATIC(payload_slab, APP_MQTT_PAYLOAD_MAX, CONFIG_APP_MQTT_TX_QUEUE_LEN + 1, 4);

static void mqtt_thread(void *p1, void *p2, void *p3);
K_THREAD_DEFINE(mqtt_tid, CONFIG_APP_MQTT_THREAD_STACK, mqtt_thread, NULL, NULL, NULL,
		CONFIG_APP_MQTT_THREAD_PRIO, 0, K_TICKS_FOREVER);

static const char *const state_names[] = {
	[APP_MQTT_STATE_IDLE] = "IDLE",
	[APP_MQTT_STATE_RESOLVING] = "RESOLVING",
	[APP_MQTT_STATE_CONNECTING] = "CONNECTING",
	[APP_MQTT_STATE_CONNECTED] = "CONNECTED",
	[APP_MQTT_STATE_BACKOFF] = "BACKOFF",
};

/* --- Shared with other threads (atomic / spinlock / mutex) ---------------- */
static atomic_t state = ATOMIC_INIT(APP_MQTT_STATE_IDLE);
static atomic_t link_lost;
static int wake_fd = -1;

static struct k_spinlock stats_lock;
static struct app_mqtt_stats stats;

K_MUTEX_DEFINE(will_lock);
static char will_topic_buf[APP_MQTT_TOPIC_MAX];
static uint8_t will_payload_buf[WILL_PAYLOAD_MAX];
static size_t will_payload_len;
static bool will_set;

/* --- Owned by the mqtt thread --------------------------------------------- */
static struct mqtt_client client;
static struct net_sockaddr_storage broker_addr;
static uint8_t tx_buf[MQTT_TX_BUF_SIZE];
static uint8_t rx_buf[MQTT_RX_BUF_SIZE];
static uint8_t rx_payload[RX_PAYLOAD_MAX];
static char rx_topic[APP_MQTT_TOPIC_MAX];

static struct mqtt_utf8 user_name;
static struct mqtt_utf8 password;
static struct mqtt_topic will_topic;
static struct mqtt_utf8 will_message;
/* Copy used for the CONNECT in progress (the stored will may change). */
static char conn_will_topic[APP_MQTT_TOPIC_MAX];
static uint8_t conn_will_payload[WILL_PAYLOAD_MAX];

static char sub_topics[SUB_TOPIC_COUNT][APP_MQTT_TOPIC_MAX];

static bool connack_received;
static int connack_rc;
static bool disconnected;
static uint16_t next_msg_id = 1;
static int64_t ping_sent_ms;   /* 0 = no PINGREQ outstanding */
static bool stack_logged;

/* ------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------
 */
static void set_state(enum app_mqtt_state new_state)
{
	enum app_mqtt_state old = (enum app_mqtt_state)atomic_set(&state, new_state);

	if (old != new_state) {
		LOG_INF("State %s -> %s", app_mqtt_state_str(old), app_mqtt_state_str(new_state));
	}
}

static void wake_thread(void)
{
	if (wake_fd >= 0) {
		(void)zvfs_eventfd_write(wake_fd, 1);
	}
}

static uint16_t new_msg_id(void)
{
	uint16_t id = next_msg_id++;

	if (next_msg_id == 0U) {
		next_msg_id = 1U; /* 0 is not a valid packet identifier */
	}
	return id;
}

static const char *ciphersuite_str(int id)
{
	switch (id) {
	case 0x1301:
		return "TLS 1.3 TLS_AES_128_GCM_SHA256";
	case 0x1302:
		return "TLS 1.3 TLS_AES_256_GCM_SHA384";
	case 0xC02B:
		return "TLS 1.2 ECDHE-ECDSA-AES128-GCM-SHA256";
	case 0xC02C:
		return "TLS 1.2 ECDHE-ECDSA-AES256-GCM-SHA384";
	case 0xC02F:
		return "TLS 1.2 ECDHE-RSA-AES128-GCM-SHA256";
	case 0xC030:
		return "TLS 1.2 ECDHE-RSA-AES256-GCM-SHA384";
	default:
		return "?";
	}
}

static const char *connack_str(int rc)
{
	switch (rc) {
	case MQTT_CONNECTION_ACCEPTED:
		return "accepted";
	case MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
		return "unacceptable protocol version";
	case MQTT_IDENTIFIER_REJECTED:
		return "client id rejected";
	case MQTT_SERVER_UNAVAILABLE:
		return "server unavailable";
	case MQTT_BAD_USER_NAME_OR_PASSWORD:
		return "bad user name or password";
	case MQTT_NOT_AUTHORIZED:
		return "not authorised";
	default:
		return "?";
	}
}

static const char *mqtt_user(void)
{
	return (CONFIG_APP_MQTT_USERNAME[0] != '\0') ? CONFIG_APP_MQTT_USERNAME : device_info_id();
}

static void log_tls_heap(void)
{
#if defined(CONFIG_MBEDTLS_MEMORY_DEBUG)
	size_t max_used = 0;
	size_t max_blocks = 0;

	mbedtls_memory_buffer_alloc_max_get(&max_used, &max_blocks);
	LOG_INF("Mbed TLS heap peak: %u of %u B", (unsigned int)max_used,
		(unsigned int)CONFIG_MBEDTLS_HEAP_SIZE);
#endif
}

static void log_stack_use_once(void)
{
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO) && !defined(CONFIG_ARCH_POSIX)
	/* (native_sim runs threads on host stacks: no meaningful number there) */
	size_t unused = 0;

	if (stack_logged || (k_thread_stack_space_get(mqtt_tid, &unused) != 0)) {
		return;
	}
	stack_logged = true;
	LOG_INF("mqtt thread stack: %u of %u B used after TLS handshake",
		(unsigned int)(CONFIG_APP_MQTT_THREAD_STACK - unused),
		(unsigned int)CONFIG_APP_MQTT_THREAD_STACK);
#else
	ARG_UNUSED(stack_logged);
#endif
}

/* ------------------------------------------------------------------------
 * Link-lost notification (net_mgmt context: only set a flag and wake up)
 * ------------------------------------------------------------------------
 */
static void l4_event_handler(uint64_t mgmt_event, struct net_if *iface, void *info,
			     size_t info_length, void *user_data)
{
	ARG_UNUSED(iface);
	ARG_UNUSED(info);
	ARG_UNUSED(info_length);
	ARG_UNUSED(user_data);

	if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		atomic_set(&link_lost, 1);
		wake_thread();
	}
}

NET_MGMT_REGISTER_EVENT_HANDLER(app_mqtt_l4, NET_EVENT_L4_DISCONNECTED, l4_event_handler, NULL);

/* ------------------------------------------------------------------------
 * MQTT event callback (called from mqtt_input()/mqtt_live() in this thread)
 * ------------------------------------------------------------------------
 */
static void handle_publish(struct mqtt_client *c, const struct mqtt_publish_param *pub)
{
	const struct mqtt_utf8 *t = &pub->message.topic.topic;
	size_t tlen = MIN(t->size, sizeof(rx_topic) - 1U);
	uint32_t len = pub->message.payload.len;
	int ret;

	memcpy(rx_topic, t->utf8, tlen);
	rx_topic[tlen] = '\0';

	if (len <= sizeof(rx_payload)) {
		ret = mqtt_readall_publish_payload(c, rx_payload, len);
	} else {
		/* Too large for a command: read and discard to keep the stream in sync. */
		uint32_t left = len;

		ret = 0;
		while ((left > 0U) && (ret == 0)) {
			uint32_t chunk = MIN(left, sizeof(rx_payload));

			ret = mqtt_readall_publish_payload(c, rx_payload, chunk);
			left -= chunk;
		}
	}
	if (ret != 0) {
		LOG_ERR("RX %s: payload read failed (%d)", rx_topic, ret);
		return;
	}

	if (pub->message.topic.qos == MQTT_QOS_1_AT_LEAST_ONCE) {
		struct mqtt_puback_param ack = {.message_id = pub->message_id};

		(void)mqtt_publish_qos1_ack(c, &ack);
	}

	K_SPINLOCK(&stats_lock) {
		stats.rx_msgs++;
	}

	if (len > sizeof(rx_payload)) {
		LOG_WRN("RX %s: %u B discarded (max %u)", rx_topic, len,
			(unsigned int)sizeof(rx_payload));
		return;
	}

	LOG_INF("RX %s (%u B)", rx_topic, len);
	ret = sparkplug_submit_rx(rx_topic, rx_payload, len);
	if ((ret != 0) && (ret != -ENOSYS)) {
		LOG_WRN("RX %s: not queued (%d)", rx_topic, ret);
	}
}

static void mqtt_evt_handler(struct mqtt_client *c, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		connack_received = true;
		connack_rc = (evt->result == 0) ? evt->param.connack.return_code : evt->result;
		break;

	case MQTT_EVT_DISCONNECT:
		disconnected = true;
		break;

	case MQTT_EVT_PUBLISH:
		handle_publish(c, &evt->param.publish);
		break;

	case MQTT_EVT_PUBACK:
		LOG_DBG("PUBACK id %u", evt->param.puback.message_id);
		break;

	case MQTT_EVT_SUBACK:
		for (size_t i = 0; i < evt->param.suback.return_codes.len; i++) {
			uint8_t rc = evt->param.suback.return_codes.data[i];

			if (rc == MQTT_SUBACK_FAILURE) {
				LOG_WRN("Subscription %s refused by broker", sub_topics[i]);
			} else {
				LOG_INF("Subscribed %s (QoS %u)", sub_topics[i], rc);
			}
		}
		break;

	case MQTT_EVT_PINGRESP:
		ping_sent_ms = 0;
		LOG_DBG("PINGRESP");
		break;

	default:
		break;
	}
}

/* ------------------------------------------------------------------------
 * Connection steps
 * ------------------------------------------------------------------------
 */
static int resolve_broker(void)
{
	struct zsock_addrinfo hints = {
		.ai_family = NET_AF_INET,
		.ai_socktype = NET_SOCK_STREAM,
	};
	struct zsock_addrinfo *res = NULL;
	struct net_sockaddr_in *sin = (struct net_sockaddr_in *)&broker_addr;
	char ip[NET_INET_ADDRSTRLEN];
	int ret;

	ret = zsock_getaddrinfo(CONFIG_APP_MQTT_BROKER_HOST, NULL, &hints, &res);
	if ((ret != 0) || (res == NULL)) {
		LOG_ERR("DNS %s failed (%d)", CONFIG_APP_MQTT_BROKER_HOST, ret);
		return -EHOSTUNREACH;
	}

	memset(&broker_addr, 0, sizeof(broker_addr));
	memcpy(sin, res->ai_addr, sizeof(*sin));
	sin->sin_port = net_htons(CONFIG_APP_MQTT_BROKER_PORT);
	zsock_freeaddrinfo(res);

	(void)net_addr_ntop(NET_AF_INET, &sin->sin_addr, ip, sizeof(ip));
	LOG_INF("Broker %s -> %s:%d", CONFIG_APP_MQTT_BROKER_HOST, ip,
		CONFIG_APP_MQTT_BROKER_PORT);
	return 0;
}

/** Fill in the Last Will: fresh NDEATH from sparkplug, else the stored will. */
static void prepare_will(void)
{
	int len = sparkplug_encode_ndeath(conn_will_payload, sizeof(conn_will_payload));

	if (len > 0) {
		(void)sparkplug_topic(SPB_NDEATH, conn_will_topic, sizeof(conn_will_topic));
	} else {
		len = 0;
		k_mutex_lock(&will_lock, K_FOREVER);
		if (will_set) {
			strcpy(conn_will_topic, will_topic_buf);
			memcpy(conn_will_payload, will_payload_buf, will_payload_len);
			len = (int)will_payload_len;
		}
		k_mutex_unlock(&will_lock);
		if (len == 0) {
			client.will_topic = NULL;
			client.will_message = NULL;
			return;
		}
	}

	will_topic.topic.utf8 = (const uint8_t *)conn_will_topic;
	will_topic.topic.size = strlen(conn_will_topic);
	will_topic.qos = MQTT_QOS_1_AT_LEAST_ONCE;
	will_message.utf8 = conn_will_payload;
	will_message.size = (uint32_t)len;
	client.will_topic = &will_topic;
	client.will_message = &will_message;
	client.will_retain = 0U;
	LOG_INF("Last Will: %s (%d B)", conn_will_topic, len);
}

static void client_setup(void)
{
	struct mqtt_sec_config *tls = &client.transport.tls.config;

	mqtt_client_init(&client);

	client.broker = &broker_addr;
	client.evt_cb = mqtt_evt_handler;
	client.client_id.utf8 = (const uint8_t *)device_info_id();
	client.client_id.size = strlen(device_info_id());
	user_name.utf8 = (const uint8_t *)mqtt_user();
	user_name.size = strlen(mqtt_user());
	password.utf8 = (const uint8_t *)CONFIG_APP_MQTT_PASSWORD;
	password.size = strlen(CONFIG_APP_MQTT_PASSWORD);
	client.user_name = &user_name;
	client.password = &password;
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.keepalive = CONFIG_APP_MQTT_KEEPALIVE_S;
	client.clean_session = 1U;
	client.rx_buf = rx_buf;
	client.rx_buf_size = sizeof(rx_buf);
	client.tx_buf = tx_buf;
	client.tx_buf_size = sizeof(tx_buf);

	client.transport.type = MQTT_TRANSPORT_SECURE;
	tls->peer_verify = ZSOCK_TLS_PEER_VERIFY_REQUIRED;
	tls->cipher_list = NULL;            /* all suites enabled in prj.conf */
	tls->cipher_count = 0;
	tls->sec_tag_list = tls_ca_tags();
	tls->sec_tag_count = tls_ca_count();
	tls->hostname = CONFIG_APP_MQTT_BROKER_HOST; /* certificate name check */
	tls->cert_nocopy = ZSOCK_TLS_CERT_NOCOPY_OPTIONAL; /* CA DER stays in flash */

	prepare_will();
}

static int wait_connack(void)
{
	int64_t deadline = k_uptime_get() + (CONFIG_APP_MQTT_CONNACK_TIMEOUT_S * 1000LL);
	struct zsock_pollfd fd = {.fd = client.transport.tls.sock, .events = ZSOCK_POLLIN};

	while (!connack_received) {
		int64_t left = deadline - k_uptime_get();
		int ret;

		if (left <= 0) {
			LOG_ERR("No CONNACK within %d s", CONFIG_APP_MQTT_CONNACK_TIMEOUT_S);
			return -ETIMEDOUT;
		}
		ret = zsock_poll(&fd, 1, (int)left);
		if (ret < 0) {
			return -errno;
		}
		if (ret == 0) {
			continue;
		}
		ret = mqtt_input(&client);
		if (connack_received) {
			break; /* a refused CONNACK also closes the socket (ret < 0) */
		}
		if ((ret != 0) || disconnected) {
			LOG_ERR("Connection closed before CONNACK (%d)", ret);
			return (ret != 0) ? ret : -ECONNRESET;
		}
	}

	if (connack_rc != MQTT_CONNECTION_ACCEPTED) {
		LOG_ERR("CONNACK: %s (%d)", connack_str(connack_rc), connack_rc);
		return ((connack_rc == MQTT_BAD_USER_NAME_OR_PASSWORD) ||
			(connack_rc == MQTT_NOT_AUTHORIZED)) ? -EACCES : -ECONNREFUSED;
	}
	return 0;
}

static int subscribe_commands(void)
{
	struct mqtt_topic topics[SUB_TOPIC_COUNT];
	struct mqtt_subscription_list list = {
		.list = topics,
		.list_count = SUB_TOPIC_COUNT,
		.message_id = new_msg_id(),
	};

	/* spBv1.0/<group>/NCMD/<node> and spBv1.0/<group>/DCMD/<node>/+ (all devices) */
	(void)snprintf(sub_topics[0], sizeof(sub_topics[0]), "%s/%s/NCMD/%s",
		       SPARKPLUG_NAMESPACE, CONFIG_APP_SPARKPLUG_GROUP_ID, device_info_id());
	(void)snprintf(sub_topics[1], sizeof(sub_topics[1]), "%s/%s/DCMD/%s/+",
		       SPARKPLUG_NAMESPACE, CONFIG_APP_SPARKPLUG_GROUP_ID, device_info_id());

	for (size_t i = 0; i < SUB_TOPIC_COUNT; i++) {
		topics[i].topic.utf8 = (const uint8_t *)sub_topics[i];
		topics[i].topic.size = strlen(sub_topics[i]);
		topics[i].qos = MQTT_QOS_1_AT_LEAST_ONCE;
	}

	return mqtt_subscribe(&client, &list);
}

static int session_connect(void)
{
	int64_t t0;
	int64_t t_tls;
	int ret;
	int cs = 0;
	net_socklen_t cs_len = sizeof(cs);

	connack_received = false;
	connack_rc = 0;
	disconnected = false;
	ping_sent_ms = 0;
	client_setup();

	LOG_INF("Connecting to %s:%d as %s (TLS, CA tags %u)", CONFIG_APP_MQTT_BROKER_HOST,
		CONFIG_APP_MQTT_BROKER_PORT, mqtt_user(), (unsigned int)tls_ca_count());

	t0 = k_uptime_get();
	ret = mqtt_connect(&client); /* TCP connect + TLS handshake + CONNECT */
	if (ret != 0) {
		if (ret == -ECONNABORTED) {
			LOG_ERR("TLS handshake failed - see 'TLS handshake error' above "
				"(-0x2700 = certificate not trusted, wrong host name or "
				"Mbed TLS heap too small)");
		} else {
			LOG_ERR("mqtt_connect failed (%d)", ret);
		}
		return ret;
	}

	if (zsock_getsockopt(client.transport.tls.sock, ZSOCK_SOL_TLS,
			     ZSOCK_TLS_CIPHERSUITE_USED, &cs, &cs_len) != 0) {
		cs = 0;
	}
	t_tls = k_uptime_get() - t0;
	LOG_INF("TLS established in %u ms: %s (0x%04x), server certificate verified for %s",
		(unsigned int)t_tls, ciphersuite_str(cs), cs, CONFIG_APP_MQTT_BROKER_HOST);

	ret = wait_connack();
	if (ret != 0) {
		(void)mqtt_abort(&client);
		return ret;
	}

	K_SPINLOCK(&stats_lock) {
		stats.connects++;
		stats.last_connect_ms = (uint32_t)(k_uptime_get() - t0);
		stats.last_ciphersuite = (uint16_t)cs;
	}
	LOG_INF("MQTT connected (CONNACK after %u ms), keep-alive %d s",
		(unsigned int)(k_uptime_get() - t0 - t_tls), CONFIG_APP_MQTT_KEEPALIVE_S);
	log_stack_use_once();
	log_tls_heap();

	ret = subscribe_commands();
	if (ret != 0) {
		LOG_ERR("SUBSCRIBE failed (%d)", ret);
		(void)mqtt_abort(&client);
		return ret;
	}
	return 0;
}

static int publish_one(const struct app_mqtt_msg *msg)
{
	struct mqtt_publish_param p = {0};
	int ret;

	p.message.topic.topic.utf8 = (const uint8_t *)msg->topic;
	p.message.topic.topic.size = strlen(msg->topic);
	p.message.topic.qos = msg->qos;
	p.message.payload.data = msg->payload;
	p.message.payload.len = msg->len;
	p.message_id = (msg->qos == MQTT_QOS_0_AT_MOST_ONCE) ? 0U : new_msg_id();
	p.dup_flag = 0U;
	p.retain_flag = msg->retain ? 1U : 0U;

	ret = mqtt_publish(&client, &p);

	K_SPINLOCK(&stats_lock) {
		if (ret == 0) {
			stats.tx_msgs++;
			stats.tx_payload_bytes += msg->len;
		} else {
			stats.tx_dropped++;
		}
	}

	if (ret == 0) {
		LOG_DBG("TX %s (%u B, QoS %u)", msg->topic, msg->len, msg->qos);
	} else {
		LOG_ERR("PUBLISH %s failed (%d), message dropped", msg->topic, ret);
	}
	return ret;
}

static int drain_tx_queue(void)
{
	struct app_mqtt_msg msg;

	while (k_msgq_get(&mqtt_tx_q, &msg, K_NO_WAIT) == 0) {
		int ret = publish_one(&msg);

		app_mqtt_free(msg.payload);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

/*
 * Messages still queued from the previous session belong to it (Sparkplug
 * seq numbers, DATA before the new BIRTH): drop them before the new BIRTH.
 * sparkplug only encodes while a session is up, so this is at most what was
 * queued in the moment the connection broke.
 */
static void discard_stale_tx(void)
{
	struct app_mqtt_msg msg;
	uint32_t n = 0;

	while (k_msgq_get(&mqtt_tx_q, &msg, K_NO_WAIT) == 0) {
		app_mqtt_free(msg.payload);
		n++;
	}
	if (n > 0U) {
		K_SPINLOCK(&stats_lock) {
			stats.tx_dropped += n;
		}
		LOG_WRN("Discarded %u message(s) from the previous session", n);
	}
}

/** Poll timeout [ms]: until keep-alive is due or the PINGRESP deadline. */
static int poll_timeout_ms(void)
{
	int timeout = mqtt_keepalive_time_left(&client);

	if (ping_sent_ms != 0) {
		int64_t left = ping_sent_ms + (CONFIG_APP_MQTT_PINGRESP_TIMEOUT_S * 1000LL) -
			       k_uptime_get();

		timeout = (timeout < 0) ? (int)MAX(left, 0) : MIN(timeout, (int)MAX(left, 0));
	}
	return timeout; /* -1 = forever (keep-alive disabled, not used here) */
}

static int session_run(void)
{
	struct zsock_pollfd fds[2] = {
		{.fd = client.transport.tls.sock, .events = ZSOCK_POLLIN},
		{.fd = wake_fd, .events = ZSOCK_POLLIN},
	};

	for (;;) {
		int ret;
		int8_t pings_before;

		if (atomic_get(&link_lost) != 0) {
			LOG_WRN("Network link lost - closing the MQTT session");
			(void)mqtt_abort(&client);
			return ERR_LINK_LOST;
		}

		ret = drain_tx_queue();
		if (ret != 0) {
			(void)mqtt_abort(&client);
			return ret;
		}

		ret = zsock_poll(fds, ARRAY_SIZE(fds), poll_timeout_ms());
		if (ret < 0) {
			ret = -errno;
			LOG_ERR("poll failed (%d)", ret);
			(void)mqtt_abort(&client);
			return ret;
		}

		if ((fds[1].revents & ZSOCK_POLLIN) != 0) {
			zvfs_eventfd_t val;

			(void)zvfs_eventfd_read(wake_fd, &val);
		}

		if ((fds[0].revents & ZSOCK_POLLIN) != 0) {
			ret = mqtt_input(&client);
			if (ret != 0) {
				LOG_WRN("Connection closed by broker or network (%d)", ret);
				(void)mqtt_abort(&client);
				return ret;
			}
		}
		if ((fds[0].revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) != 0) {
			LOG_WRN("Socket error (revents 0x%x)", fds[0].revents);
			(void)mqtt_abort(&client);
			return -ECONNRESET;
		}
		if (disconnected) {
			return -ECONNRESET;
		}

		/* Keep-alive: sends PINGREQ when no packet was sent for keepalive s. */
		pings_before = client.unacked_ping;
		ret = mqtt_live(&client);
		if ((ret != 0) && (ret != -EAGAIN)) {
			LOG_WRN("PINGREQ failed (%d)", ret);
			(void)mqtt_abort(&client);
			return ret;
		}
		if ((ret == 0) && (client.unacked_ping > pings_before) && (ping_sent_ms == 0)) {
			ping_sent_ms = k_uptime_get();
			LOG_DBG("PINGREQ");
		}
		if ((ping_sent_ms != 0) &&
		    ((k_uptime_get() - ping_sent_ms) >= (CONFIG_APP_MQTT_PINGRESP_TIMEOUT_S * 1000LL))) {
			LOG_WRN("No PINGRESP within %d s - connection is dead",
				CONFIG_APP_MQTT_PINGRESP_TIMEOUT_S);
			(void)mqtt_abort(&client);
			return -ETIMEDOUT;
		}
	}
}

/* ------------------------------------------------------------------------
 * Thread
 * ------------------------------------------------------------------------
 */
static bool is_permanent_error(int err)
{
	/* Will not be fixed by retrying soon: certificate, password, ACL. */
	return (err == -ECONNABORTED) || (err == -EACCES);
}

static void mqtt_thread(void *p1, void *p2, void *p3)
{
	uint32_t backoff_s = CONFIG_APP_MQTT_BACKOFF_MIN_S;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		int64_t connected_at = 0;
		int ret;

		set_state(APP_MQTT_STATE_IDLE);
		(void)network_wait_up(K_FOREVER);
		atomic_set(&link_lost, 0);

		set_state(APP_MQTT_STATE_RESOLVING);
		ret = resolve_broker();

		if (ret == 0) {
			set_state(APP_MQTT_STATE_CONNECTING);
			ret = session_connect();
		}

		if (ret == 0) {
			set_state(APP_MQTT_STATE_CONNECTED);
			connected_at = k_uptime_get();
			discard_stale_tx();
			sparkplug_on_connected();
			ret = session_run();
			sparkplug_on_disconnected();
			LOG_INF("Session ended after %u s (%d)",
				(unsigned int)((k_uptime_get() - connected_at) / 1000), ret);
		}

		if (ret == ERR_LINK_LOST) {
			/* Not the broker's fault: wait for the network thread. */
			continue;
		}

		if (connected_at == 0) {
			K_SPINLOCK(&stats_lock) {
				stats.connect_failures++;
			}
		} else if ((k_uptime_get() - connected_at) >= (CONFIG_APP_MQTT_STABLE_S * 1000LL)) {
			backoff_s = CONFIG_APP_MQTT_BACKOFF_MIN_S;
		}

		if (is_permanent_error(ret)) {
			backoff_s = CONFIG_APP_MQTT_BACKOFF_MAX_S;
		}

		{
			uint32_t delay_s = backoff_s + (sys_rand32_get() % ((backoff_s / 4U) + 1U));

			set_state(APP_MQTT_STATE_BACKOFF);
			LOG_WRN("Reconnect in %u s", delay_s);
			k_sleep(K_SECONDS(delay_s));
		}
		backoff_s = MIN(backoff_s * 2U, (uint32_t)CONFIG_APP_MQTT_BACKOFF_MAX_S);
	}
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------
 */
int app_mqtt_init(void)
{
	int ret;

	ret = tls_ca_init();
	if (ret != 0) {
		return ret;
	}

	wake_fd = zvfs_eventfd(0, ZVFS_EFD_NONBLOCK);
	if (wake_fd < 0) {
		LOG_ERR("eventfd failed (%d)", -errno);
		return -ENOMEM;
	}

	if ((CONFIG_APP_MQTT_USERNAME[0] != '\0') &&
	    (strcmp(CONFIG_APP_MQTT_USERNAME, device_info_id()) != 0)) {
		LOG_WRN("MQTT user %s != device id %s: the broker ACL only allows "
			"topics of the user name", CONFIG_APP_MQTT_USERNAME, device_info_id());
	}
	return 0;
}

int app_mqtt_start(void)
{
	if (CONFIG_APP_MQTT_PASSWORD[0] == '\0') {
		LOG_ERR("No MQTT password: create app/secrets.conf (see secrets.conf.example) "
			"with CONFIG_APP_MQTT_PASSWORD for user %s", mqtt_user());
		return -EINVAL;
	}
	k_thread_name_set(mqtt_tid, "mqtt");
	k_thread_start(mqtt_tid);
	return 0;
}

bool app_mqtt_is_connected(void)
{
	return atomic_get(&state) == APP_MQTT_STATE_CONNECTED;
}

enum app_mqtt_state app_mqtt_get_state(void)
{
	return (enum app_mqtt_state)atomic_get(&state);
}

const char *app_mqtt_state_str(enum app_mqtt_state st)
{
	if ((unsigned int)st >= ARRAY_SIZE(state_names)) {
		return "?";
	}
	return state_names[st];
}

void app_mqtt_get_stats(struct app_mqtt_stats *out)
{
	K_SPINLOCK(&stats_lock) {
		*out = stats;
	}
}

uint8_t *app_mqtt_alloc(k_timeout_t timeout)
{
	void *block = NULL;

	if (k_mem_slab_alloc(&payload_slab, &block, timeout) != 0) {
		return NULL;
	}
	return block;
}

void app_mqtt_free(uint8_t *buf)
{
	if (buf != NULL) {
		k_mem_slab_free(&payload_slab, buf);
	}
}

int app_mqtt_submit(const struct app_mqtt_msg *msg, k_timeout_t timeout)
{
	if ((msg == NULL) || (msg->payload == NULL) || (msg->len > APP_MQTT_PAYLOAD_MAX) ||
	    (msg->qos > MQTT_QOS_1_AT_LEAST_ONCE) ||
	    (memchr(msg->topic, '\0', sizeof(msg->topic)) == NULL)) {
		return -EINVAL;
	}

	if (k_msgq_put(&mqtt_tx_q, msg, timeout) != 0) {
		return -EAGAIN;
	}
	wake_thread();
	return 0;
}

int app_mqtt_set_will(const char *topic, const uint8_t *payload, size_t len)
{
	if ((topic == NULL) || (strlen(topic) >= sizeof(will_topic_buf)) ||
	    (len > sizeof(will_payload_buf)) || ((payload == NULL) && (len > 0U))) {
		return -EINVAL;
	}

	k_mutex_lock(&will_lock, K_FOREVER);
	strcpy(will_topic_buf, topic);
	if (len > 0U) {
		memcpy(will_payload_buf, payload, len);
	}
	will_payload_len = len;
	will_set = true;
	k_mutex_unlock(&will_lock);
	return 0;
}
