/*
 * Zephyr_A - native_sim end-to-end test driver.
 *
 * Plays the role of the network and sensor threads around the real
 * app_logic, sparkplug, mqtt_client, tls_ca and timesync modules:
 *   1. SNTP sync (like network.c before NETWORK_EVT_UP), exit 3 if it fails
 *   2. NETWORK_EVT_UP -> mqtt connects -> sparkplug sends NBIRTH + DBIRTH
 *   3. one measurement per second into app_logic (batch size 3 -> DDATA),
 *      measurement 4 is BAD (-> is_null), optional link loss
 *   4. prints the counters and the Mbed TLS heap peak; run.sh checks the
 *      broker side with spb.py
 *
 * Options (zephyr.exe): -run_s=<s> -link_loss_at=<s>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <mbedtls/memory_buffer_alloc.h>

#include "app_logic.h"
#include "mqtt_client.h"
#include "network.h"
#include "sparkplug.h"
#include "timesync.h"

#include "cmdline.h" /* native_sim command line options */
#include "posix_native_task.h"

LOG_MODULE_REGISTER(test_main, LOG_LEVEL_INF);

static uint32_t run_s = 20;
static uint32_t link_loss_at_s;

static void add_test_options(void)
{
	static struct args_struct_t opts[] = {
		{.option = "run_s", .name = "s", .type = 'u', .dest = (void *)&run_s,
		 .descript = "Print the result and exit after s seconds (default 20)"},
		{.option = "link_loss_at", .name = "s", .type = 'u', .dest = (void *)&link_loss_at_s,
		 .descript = "Simulate NET_EVENT_L4_DISCONNECTED after s seconds (0 = never)"},
		ARG_TABLE_ENDMARKER};

	native_add_command_line_opts(opts);
}
NATIVE_TASK(add_test_options, PRE_BOOT_1, 10);

static void submit_measurement(uint32_t n)
{
	struct measurement m = {
		.uptime_ms = k_uptime_get(),
		.timestamp_ms = timesync_now_ms(),
		.seq = n,
		.count = 1,
	};

	m.values[0].channel = APP_CHANNEL_DIE_TEMP;
	if (n == 4U) {
		m.values[0].quality = QUALITY_BAD; /* -> Sparkplug is_null */
		m.quality = QUALITY_BAD;
	} else {
		m.values[0].value_milli = 21000 + (int32_t)n * 125; /* 21.000, 21.125 ... */
		m.values[0].quality = QUALITY_GOOD;
	}
	(void)app_logic_submit_measurement(&m, K_NO_WAIT);
}

int main(void)
{
	struct app_mqtt_stats st;
	size_t heap_max = 0;
	size_t blocks = 0;
	int ret;

	(void)timesync_init();
	(void)app_logic_init();
	(void)app_logic_start();
	(void)sparkplug_init();
	(void)sparkplug_start();
	ret = app_mqtt_init();
	if (ret == 0) {
		ret = app_mqtt_start();
	}
	if (ret != 0) {
		LOG_ERR("TEST: start failed (%d)", ret);
		exit(2);
	}

	/* Like network.c: no NETWORK_EVT_UP without valid UTC time */
	ret = timesync_sync(CONFIG_APP_TIMESYNC_TIMEOUT_MS);
	if (ret != 0) {
		LOG_ERR("TEST: no time (%d) - network stays down", ret);
		k_sleep(K_SECONDS(1));
		exit(3);
	}
	k_event_post(network_events(), NETWORK_EVT_UP);

	for (uint32_t s = 1; s <= run_s; s++) {
		k_sleep(K_SECONDS(1));
		submit_measurement(s);
		if (s == link_loss_at_s) {
			LOG_INF("TEST: simulating link loss");
			k_event_clear(network_events(), NETWORK_EVT_UP);
			net_mgmt_event_notify(NET_EVENT_L4_DISCONNECTED, net_if_get_default());
			k_sleep(K_SECONDS(2));
			k_event_post(network_events(), NETWORK_EVT_UP);
		}
	}

	app_mqtt_get_stats(&st);
	mbedtls_memory_buffer_alloc_max_get(&heap_max, &blocks);
	LOG_INF("TEST RESULT state=%s connects=%u failures=%u tx=%u tx_bytes=%u dropped=%u "
		"rx=%u connect_ms=%u suite=0x%04x mbedtls_heap_peak=%u",
		app_mqtt_state_str(app_mqtt_get_state()), st.connects, st.connect_failures,
		st.tx_msgs, st.tx_payload_bytes, st.tx_dropped, st.rx_msgs, st.last_connect_ms,
		st.last_ciphersuite, (unsigned int)heap_max);
	exit(0);
	return 0;
}
