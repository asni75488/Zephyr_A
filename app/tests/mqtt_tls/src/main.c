/*
 * Zephyr_A - native_sim MQTT/TLS test driver.
 *
 * Starts the real mqtt_client module (with mqtt_selftest), marks the
 * "network" as up, optionally simulates a link loss, and prints the
 * counters, the Mbed TLS heap peak and the mqtt thread stack use. The
 * output is checked by run.sh.
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

#include "mqtt_client.h"
#include "network.h"

#include "cmdline.h"          /* native_sim command line options */
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

int main(void)
{
	struct app_mqtt_stats st;
	size_t heap_max = 0;
	size_t blocks = 0;
	int ret;

	ret = app_mqtt_init();
	if (ret == 0) {
		ret = app_mqtt_start();
	}
	if (ret != 0) {
		LOG_ERR("TEST: start failed (%d)", ret);
		exit(2);
	}

	k_event_post(network_events(), NETWORK_EVT_UP);

	for (uint32_t s = 1; s <= run_s; s++) {
		k_sleep(K_SECONDS(1));
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
