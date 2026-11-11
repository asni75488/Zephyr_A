/*
 * Zephyr_A - uplink: interval uplink scheduler (part 9).
 *
 * See uplink.h and docs/09-strom-data.md, section 3.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "mqtt_client.h"
#include "network.h"
#include "sparkplug.h"
#include "uplink.h"

LOG_MODULE_REGISTER(uplink, CONFIG_APP_LOG_LEVEL);

#if defined(CONFIG_APP_UPLINK_INTERVAL)

#define EV_BATCH   BIT(0)
#define EV_FLUSHED BIT(1)

/* Early uplink when the batch queue is 3/4 full (no data is lost). */
#define EARLY_FILL ((CONFIG_APP_SPARKPLUG_BATCH_QUEUE_LEN * 3 + 3) / 4)

#define MQTT_CLOSE_TIMEOUT_MS 10000
#define NET_OFF_TIMEOUT_MS    45000 /* modem suspend alone takes ~12 s */

static K_EVENT_DEFINE(uplink_events);
static struct k_spinlock stats_lock;
static struct uplink_stats stats;
static atomic_t active;
static bool started;

static void uplink_thread(void *p1, void *p2, void *p3);

K_THREAD_DEFINE(uplink_tid, CONFIG_APP_UPLINK_THREAD_STACK, uplink_thread, NULL, NULL, NULL,
		CONFIG_APP_UPLINK_THREAD_PRIO, 0, SYS_FOREVER_MS);

/** Wait until cond() is true, checking every period_ms. */
static bool wait_for(bool (*cond)(void), int timeout_ms, int period_ms)
{
	int64_t end = k_uptime_get() + timeout_ms;

	while (!cond()) {
		if (k_uptime_get() >= end) {
			return false;
		}
		k_msleep(period_ms);
	}
	return true;
}

static bool mqtt_closed(void)
{
	return !app_mqtt_is_connected();
}

static bool network_off(void)
{
	return network_get_state() == NETWORK_STATE_OFF;
}

static const char *run_uplink(uint32_t n)
{
	struct app_mqtt_stats before;
	struct app_mqtt_stats after;
	int64_t t0 = k_uptime_get();
	const char *result = "ok";
	uint32_t ev;

	app_mqtt_get_stats(&before);
	k_event_clear(&uplink_events, EV_FLUSHED);
	atomic_set(&active, 1);

	LOG_INF("Uplink #%u: starting the modem (%u batch(es) queued)", n,
		sparkplug_batches_queued());
	(void)network_request_up();

	ev = k_event_wait(&uplink_events, EV_FLUSHED, false,
			  K_SECONDS(CONFIG_APP_UPLINK_TIMEOUT_S));
	if (ev != 0U) {
		LOG_INF("Uplink #%u: data sent after %lld s, staying %d s for commands", n,
			(k_uptime_get() - t0) / 1000, CONFIG_APP_UPLINK_LINGER_S);
		k_sleep(K_SECONDS(CONFIG_APP_UPLINK_LINGER_S));

		/* Batches that arrived during the linger time were sent at once
		 * (session is up); end the session cleanly: NDEATH + DISCONNECT.
		 */
		sparkplug_end_session();
		if (!wait_for(mqtt_closed, MQTT_CLOSE_TIMEOUT_MS, 100)) {
			LOG_WRN("Uplink #%u: MQTT session did not close in time", n);
		}
	} else {
		result = "timeout";
		LOG_WRN("Uplink #%u: no MQTT session within %d s - data stays queued", n,
			CONFIG_APP_UPLINK_TIMEOUT_S);
		K_SPINLOCK(&stats_lock) {
			stats.failures++;
		}
	}

	atomic_set(&active, 0);
	(void)network_request_down();
	if (!wait_for(network_off, NET_OFF_TIMEOUT_MS, 250)) {
		LOG_WRN("Uplink #%u: modem did not stop within %d s", n, NET_OFF_TIMEOUT_MS / 1000);
	}

	app_mqtt_get_stats(&after);
	K_SPINLOCK(&stats_lock) {
		stats.last_online_s = (uint32_t)((k_uptime_get() - t0) / 1000);
		stats.last_tx_bytes = after.tx_payload_bytes - before.tx_payload_bytes;
	}
	LOG_INF("Uplink #%u %s: modem on %u s, %u message(s), %u B MQTT payload", n, result,
		stats.last_online_s, after.tx_msgs - before.tx_msgs, stats.last_tx_bytes);
	return result;
}

static void uplink_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* First uplink right after boot: time sync, BIRTH (device is visible)
	 * and the data measured while the modem started.
	 */
	int64_t next_due = k_uptime_get();

	for (;;) {
		int64_t left = next_due - k_uptime_get();
		bool early = false;

		while (left > 0) {
			uint32_t ev = k_event_wait(&uplink_events, EV_BATCH, false, K_MSEC(left));

			k_event_clear(&uplink_events, EV_BATCH);
			if ((ev != 0U) && (sparkplug_batches_queued() >= EARLY_FILL)) {
				early = true;
				break;
			}
			left = next_due - k_uptime_get();
		}

		uint32_t n;

		K_SPINLOCK(&stats_lock) {
			n = ++stats.uplinks;
		}
		if (early) {
			LOG_INF("Uplink #%u early: batch queue %u/%d", n, sparkplug_batches_queued(),
				CONFIG_APP_SPARKPLUG_BATCH_QUEUE_LEN);
		}

		int64_t started_at = k_uptime_get();

		(void)run_uplink(n);
		next_due = started_at + (CONFIG_APP_UPLINK_INTERVAL_S * 1000LL);
	}
}

int uplink_init(void)
{
	LOG_INF("Interval uplink every %d s (linger %d s, timeout %d s, queue %d batches, "
		"early at %d)", CONFIG_APP_UPLINK_INTERVAL_S, CONFIG_APP_UPLINK_LINGER_S,
		CONFIG_APP_UPLINK_TIMEOUT_S, CONFIG_APP_SPARKPLUG_BATCH_QUEUE_LEN, EARLY_FILL);
	return 0;
}

int uplink_start(void)
{
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_name_set(uplink_tid, "uplink");
	k_thread_start(uplink_tid);
	return 0;
}

void uplink_notify_batch(void)
{
	k_event_post(&uplink_events, EV_BATCH);
}

void uplink_notify_flushed(void)
{
	k_event_post(&uplink_events, EV_FLUSHED);
}

bool uplink_is_active(void)
{
	return atomic_get(&active) != 0;
}

void uplink_get_stats(struct uplink_stats *out)
{
	K_SPINLOCK(&stats_lock) {
		*out = stats;
	}
}

#else /* continuous mode */

int uplink_init(void)
{
	return -ENOTSUP;
}

int uplink_start(void)
{
	return -ENOTSUP;
}

void uplink_notify_batch(void)
{
}

void uplink_notify_flushed(void)
{
}

bool uplink_is_active(void)
{
	return false;
}

void uplink_get_stats(struct uplink_stats *out)
{
	memset(out, 0, sizeof(*out));
}

#endif /* CONFIG_APP_UPLINK_INTERVAL */
