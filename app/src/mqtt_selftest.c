/*
 * Zephyr_A - mqtt_selftest: hand-encoded Sparkplug B NDATA (part 6 only).
 *
 * Protobuf wire format of the fields used (Eclipse Tahu sparkplug_b.proto):
 *   Payload: 2 metrics (repeated Metric, LEN), 3 seq (uint64, varint)
 *   Metric:  1 name (string, LEN), 4 datatype (uint32, varint),
 *            11 long_value (uint64, varint), 12 float_value (float, I32)
 *   DataType: UInt64 = 8, Float = 9
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/sys/byteorder.h>

#include "mqtt_client.h"
#include "mqtt_selftest.h"
#include "network.h"
#include "sparkplug.h"

LOG_MODULE_REGISTER(mqtt_selftest, CONFIG_APP_LOG_LEVEL);

#define PB_WT_VARINT 0U
#define PB_WT_I32    5U
#define PB_WT_LEN    2U

#define SPB_TYPE_UINT64 8U
#define SPB_TYPE_FLOAT  9U

struct pb_buf {
	uint8_t *p;
	size_t size;
	size_t len;
	bool overflow;
};

static void pb_put(struct pb_buf *b, uint8_t byte)
{
	if (b->len < b->size) {
		b->p[b->len++] = byte;
	} else {
		b->overflow = true;
	}
}

static void pb_varint(struct pb_buf *b, uint64_t v)
{
	do {
		uint8_t byte = (uint8_t)(v & 0x7FU);

		v >>= 7;
		pb_put(b, (v != 0U) ? (byte | 0x80U) : byte);
	} while (v != 0U);
}

static void pb_key(struct pb_buf *b, uint32_t field, uint32_t wire_type)
{
	pb_varint(b, ((uint64_t)field << 3) | wire_type);
}

/** Size of a varint, needed for the LEN prefix of a nested Metric. */
static size_t pb_varint_size(uint64_t v)
{
	size_t n = 1;

	while (v >= 0x80U) {
		v >>= 7;
		n++;
	}
	return n;
}

static void put_metric_u64(struct pb_buf *b, const char *name, uint64_t value)
{
	size_t name_len = strlen(name);
	size_t body = 1 + pb_varint_size(name_len) + name_len /* 1 name     */
		      + 1 + 1                                /* 4 datatype */
		      + 1 + pb_varint_size(value);           /* 11 long    */

	pb_key(b, 2, PB_WT_LEN);
	pb_varint(b, body);
	pb_key(b, 1, PB_WT_LEN);
	pb_varint(b, name_len);
	for (size_t i = 0; i < name_len; i++) {
		pb_put(b, (uint8_t)name[i]);
	}
	pb_key(b, 4, PB_WT_VARINT);
	pb_varint(b, SPB_TYPE_UINT64);
	pb_key(b, 11, PB_WT_VARINT);
	pb_varint(b, value);
}

static void put_metric_float(struct pb_buf *b, const char *name, float value)
{
	size_t name_len = strlen(name);
	size_t body = 1 + pb_varint_size(name_len) + name_len + 1 + 1 + 1 + 4;
	uint32_t bits;
	uint8_t le[4];

	memcpy(&bits, &value, sizeof(bits));
	sys_put_le32(bits, le);

	pb_key(b, 2, PB_WT_LEN);
	pb_varint(b, body);
	pb_key(b, 1, PB_WT_LEN);
	pb_varint(b, name_len);
	for (size_t i = 0; i < name_len; i++) {
		pb_put(b, (uint8_t)name[i]);
	}
	pb_key(b, 4, PB_WT_VARINT);
	pb_varint(b, SPB_TYPE_FLOAT);
	pb_key(b, 12, PB_WT_I32);
	for (size_t i = 0; i < sizeof(le); i++) {
		pb_put(b, le[i]);
	}
}

int mqtt_selftest_encode(uint8_t *buf, size_t size, uint8_t seq, uint64_t uptime_s,
			 const int16_t *rssi_dbm, uint32_t connects)
{
	struct pb_buf b = {.p = buf, .size = size};

	put_metric_u64(&b, "Test/Uptime", uptime_s);
	if (rssi_dbm != NULL) {
		put_metric_float(&b, "Test/RSSI", (float)*rssi_dbm);
	}
	put_metric_u64(&b, "Test/MQTT Connects", connects);
	pb_key(&b, 3, PB_WT_VARINT);
	pb_varint(&b, seq);

	return b.overflow ? -ENOMEM : (int)b.len;
}

static void selftest_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(selftest_work, selftest_work_handler);
static uint8_t seq;

static void selftest_work_handler(struct k_work *work)
{
	struct app_mqtt_msg msg = {
		.qos = MQTT_QOS_0_AT_MOST_ONCE, /* Sparkplug: NDATA/DDATA with QoS 0 */
		.retain = false,
	};
	struct app_mqtt_stats st;
	int16_t rssi;
	bool have_rssi;
	int len;
	int ret;

	ARG_UNUSED(work);

	if (!app_mqtt_is_connected()) {
		return; /* restarted by mqtt_selftest_on_connected() */
	}

	have_rssi = (network_get_rssi(&rssi) == 0);
	app_mqtt_get_stats(&st);

	msg.payload = app_mqtt_alloc(K_NO_WAIT);
	if (msg.payload == NULL) {
		LOG_WRN("No payload buffer free");
		goto again;
	}

	len = mqtt_selftest_encode(msg.payload, APP_MQTT_PAYLOAD_MAX, seq,
				   (uint64_t)(k_uptime_get() / 1000), have_rssi ? &rssi : NULL,
				   st.connects);
	ret = sparkplug_topic(SPB_NDATA, msg.topic, sizeof(msg.topic));
	if ((len < 0) || (ret < 0)) {
		app_mqtt_free(msg.payload);
		LOG_ERR("Encode failed (%d/%d)", len, ret);
		goto again;
	}
	msg.len = (uint16_t)len;

	ret = app_mqtt_submit(&msg, K_NO_WAIT);
	if (ret != 0) {
		app_mqtt_free(msg.payload);
		LOG_WRN("Queue full, self-test message dropped");
		goto again;
	}

	LOG_INF("Self-test NDATA seq %u queued (%d B%s)", seq, len, have_rssi ? "" : ", no RSSI");
	seq++;

again:
	(void)k_work_reschedule(&selftest_work, K_SECONDS(CONFIG_APP_MQTT_SELFTEST_INTERVAL_S));
}

void mqtt_selftest_start(void)
{
	LOG_INF("Self-test NDATA every %d s after connect", CONFIG_APP_MQTT_SELFTEST_INTERVAL_S);
}

void mqtt_selftest_on_connected(void)
{
	(void)k_work_reschedule(&selftest_work, K_NO_WAIT);
}
