/*
 * Zephyr_A - sparkplug (skeleton, implemented in part 7).
 *
 * Planned thread (docs/02-arkitektur.md):
 *   K_THREAD_DEFINE(sparkplug_tid, CONFIG_APP_SPARKPLUG_THREAD_STACK, sparkplug_thread,
 *                   NULL, NULL, NULL, CONFIG_APP_SPARKPLUG_THREAD_PRIO, 0, K_TICKS_FOREVER);
 *   K_MSGQ_DEFINE(batch_q, sizeof(struct sample_batch), 2, 8);
 *   K_MSGQ_DEFINE(rx_q, ...);  k_poll() on both queues.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "device_info.h"
#include "sparkplug.h"

LOG_MODULE_REGISTER(sparkplug, CONFIG_APP_LOG_LEVEL);

static const char *const msg_type_names[SPB_MSG_TYPE_COUNT] = {
	[SPB_NBIRTH] = "NBIRTH", [SPB_NDEATH] = "NDEATH", [SPB_NDATA] = "NDATA",
	[SPB_NCMD] = "NCMD",     [SPB_DBIRTH] = "DBIRTH", [SPB_DDEATH] = "DDEATH",
	[SPB_DDATA] = "DDATA",   [SPB_DCMD] = "DCMD",
};

int sparkplug_init(void)
{
	char topic[96];

	if (sparkplug_topic(SPB_DDATA, topic, sizeof(topic)) > 0) {
		LOG_DBG("skeleton, data topic %s", topic);
	}
	return 0;
}

int sparkplug_start(void)
{
	return -ENOSYS;
}

int sparkplug_topic(enum sparkplug_msg_type type, char *buf, size_t size)
{
	int len;

	if ((unsigned int)type >= SPB_MSG_TYPE_COUNT) {
		return -EINVAL;
	}

	if (type >= SPB_DBIRTH) {
		len = snprintf(buf, size, "%s/%s/%s/%s/%s", SPARKPLUG_NAMESPACE,
			       CONFIG_APP_SPARKPLUG_GROUP_ID, msg_type_names[type],
			       device_info_id(), CONFIG_APP_SPARKPLUG_DEVICE_ID);
	} else {
		len = snprintf(buf, size, "%s/%s/%s/%s", SPARKPLUG_NAMESPACE,
			       CONFIG_APP_SPARKPLUG_GROUP_ID, msg_type_names[type],
			       device_info_id());
	}

	if ((len < 0) || ((size_t)len >= size)) {
		return -ENOMEM;
	}
	return len;
}

int sparkplug_encode_ndeath(uint8_t *buf, size_t size)
{
	ARG_UNUSED(buf);
	ARG_UNUSED(size);
	return -ENOSYS;
}

void sparkplug_on_connected(void)
{
}

int sparkplug_submit_batch(const struct sample_batch *batch, k_timeout_t timeout)
{
	ARG_UNUSED(batch);
	ARG_UNUSED(timeout);
	return -ENOSYS;
}

int sparkplug_submit_rx(const char *topic, const uint8_t *payload, size_t len)
{
	ARG_UNUSED(topic);
	ARG_UNUSED(payload);
	ARG_UNUSED(len);
	return -ENOSYS;
}
