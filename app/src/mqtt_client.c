/*
 * Zephyr_A - mqtt_client (skeleton, implemented in part 5).
 *
 * Planned thread (docs/02-arkitektur.md):
 *   K_THREAD_DEFINE(mqtt_tid, CONFIG_APP_MQTT_THREAD_STACK, mqtt_thread,
 *                   NULL, NULL, NULL, CONFIG_APP_MQTT_THREAD_PRIO, 0, K_TICKS_FOREVER);
 *   K_MSGQ_DEFINE(mqtt_tx_q, sizeof(struct app_mqtt_msg), 4, 4);
 *   K_MEM_SLAB_DEFINE(mqtt_payload_slab, APP_MQTT_PAYLOAD_MAX, 4, 4);
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "mqtt_client.h"

LOG_MODULE_REGISTER(app_mqtt, CONFIG_APP_LOG_LEVEL);

int app_mqtt_init(void)
{
	LOG_DBG("skeleton (broker %s:%d)", CONFIG_APP_MQTT_BROKER_HOST,
		CONFIG_APP_MQTT_BROKER_PORT);
	return 0;
}

int app_mqtt_start(void)
{
	return -ENOSYS;
}

bool app_mqtt_is_connected(void)
{
	return false;
}

enum app_mqtt_state app_mqtt_get_state(void)
{
	return APP_MQTT_STATE_IDLE;
}

uint8_t *app_mqtt_alloc(k_timeout_t timeout)
{
	ARG_UNUSED(timeout);
	return NULL;
}

void app_mqtt_free(uint8_t *buf)
{
	ARG_UNUSED(buf);
}

int app_mqtt_submit(const struct app_mqtt_msg *msg, k_timeout_t timeout)
{
	ARG_UNUSED(msg);
	ARG_UNUSED(timeout);
	return -ENOSYS;
}

int app_mqtt_set_will(const char *topic, const uint8_t *payload, size_t len)
{
	ARG_UNUSED(topic);
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
	return -ENOSYS;
}
