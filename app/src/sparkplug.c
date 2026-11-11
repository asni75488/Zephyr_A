/*
 * Zephyr_A - sparkplug: Sparkplug B 3.0 edge node (part 7).
 *
 * Session (docs/07-sparkplug-tid.md):
 *   before CONNECT  mqtt calls sparkplug_encode_ndeath(): bdSeq++ -> Will
 *   after CONNACK   sparkplug_on_connected(): this thread publishes
 *                   NBIRTH (seq 0, same bdSeq) and DBIRTH (seq 1)
 *   then            DDATA per batch, NDATA with status every
 *                   APP_SPARKPLUG_STATUS_INTERVAL_S and after config changes
 *   NCMD            Node Control/Rebirth, Node Control/Reboot,
 *                   Config/Sample Interval, Config/Batch Size
 *   connection lost sparkplug_on_disconnected(): batches wait in batch_q
 *                   until the next BIRTH (a DATA message must never come
 *                   before the BIRTH of its session)
 *
 * Encoding: nanopb with a callback for the repeated "metrics" field, so a
 * Metric is built on the stack one at a time (no array of 48 metrics in RAM).
 * Names are only sent in BIRTH messages; DATA messages use aliases. Every
 * metric carries its datatype: Eclipse Tahu decoders need it to find the
 * value. Values are Float in engineering units (docs/05-cloud.md, section 8).
 *
 * Threading: the "sparkplug" thread owns seq, the alias table and the batch
 * and RX queues. sparkplug_encode_ndeath() runs in the mqtt thread and only
 * touches atomics. Other threads use the submit/notify functions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

#include <pb_decode.h>
#include <pb_encode.h>

#include "app_logic.h"
#include "device_info.h"
#include "mqtt_client.h"
#include "network.h"
#include "proto/sparkplug_b.pb.h"
#include "security.h"
#include "sensor.h"
#include "sparkplug.h"
#include "timesync.h"
#if defined(CONFIG_APP_MODBUS_RTU)
#include "modbus.h"
#endif

LOG_MODULE_REGISTER(sparkplug, CONFIG_APP_LOG_LEVEL);

typedef org_eclipse_tahu_protobuf_Payload spb_payload_t;
typedef org_eclipse_tahu_protobuf_Payload_Metric spb_metric_t;

#define SPB_PAYLOAD_FIELDS org_eclipse_tahu_protobuf_Payload_fields
#define SPB_METRIC_FIELDS  org_eclipse_tahu_protobuf_Payload_Metric_fields

#define DT_INT32   org_eclipse_tahu_protobuf_DataType_Int32
#define DT_INT64   org_eclipse_tahu_protobuf_DataType_Int64
#define DT_FLOAT   org_eclipse_tahu_protobuf_DataType_Float
#define DT_BOOLEAN org_eclipse_tahu_protobuf_DataType_Boolean
#define DT_STRING  org_eclipse_tahu_protobuf_DataType_String

#define TAG_INT    org_eclipse_tahu_protobuf_Payload_Metric_int_value_tag
#define TAG_LONG   org_eclipse_tahu_protobuf_Payload_Metric_long_value_tag
#define TAG_FLOAT  org_eclipse_tahu_protobuf_Payload_Metric_float_value_tag
#define TAG_DOUBLE org_eclipse_tahu_protobuf_Payload_Metric_double_value_tag
#define TAG_BOOL   org_eclipse_tahu_protobuf_Payload_Metric_boolean_value_tag
#define TAG_STRING org_eclipse_tahu_protobuf_Payload_Metric_string_value_tag

/** Max NCMD/DCMD payload that is decoded (commands are small). */
#define RX_PAYLOAD_MAX 256
/** Device metric alias = DEVICE_ALIAS_BASE + channel id. */
#define DEVICE_ALIAS_BASE 100U
/** Max values in one DDATA (batch items x channels). */
#define MAX_DATA_METRICS (CONFIG_APP_BATCH_MAX * APP_MAX_CHANNELS)

/* --- Node metrics (NBIRTH / NDATA / NCMD) ------------------------------- */
enum node_metric {
	NM_BDSEQ = 0,
	NM_REBIRTH,
	NM_REBOOT,
	NM_FW_VERSION,
	NM_RESET_CAUSE,
	NM_SAMPLE_INTERVAL,
	NM_BATCH_SIZE,
	NM_RSSI,
	NM_UPTIME,
	NM_MQTT_CONNECTS,
	NM_SECURITY,
	NM_COUNT
};

static const struct {
	const char *name;
	uint32_t datatype;
	uint8_t alias; /* 0 = no alias (bdSeq is never aliased) */
} node_metrics[NM_COUNT] = {
	[NM_BDSEQ] = {"bdSeq", DT_INT64, 0},
	[NM_REBIRTH] = {"Node Control/Rebirth", DT_BOOLEAN, 1},
	[NM_REBOOT] = {"Node Control/Reboot", DT_BOOLEAN, 2},
	[NM_FW_VERSION] = {"Properties/FW Version", DT_STRING, 3},
	[NM_RESET_CAUSE] = {"Properties/Reset Cause", DT_STRING, 4},
	[NM_SAMPLE_INTERVAL] = {"Config/Sample Interval", DT_INT64, 5},
	[NM_BATCH_SIZE] = {"Config/Batch Size", DT_INT64, 6},
	[NM_RSSI] = {"Status/RSSI", DT_INT32, 7},
	[NM_UPTIME] = {"Status/Uptime", DT_INT64, 8},
	[NM_MQTT_CONNECTS] = {"Status/MQTT Connects", DT_INT64, 9},
	[NM_SECURITY] = {"Properties/Security", DT_STRING, 10},
};

static const char *const msg_type_names[SPB_MSG_TYPE_COUNT] = {
	[SPB_NBIRTH] = "NBIRTH", [SPB_NDEATH] = "NDEATH", [SPB_NDATA] = "NDATA",
	[SPB_NCMD] = "NCMD",     [SPB_DBIRTH] = "DBIRTH", [SPB_DDEATH] = "DDEATH",
	[SPB_DDATA] = "DDATA",   [SPB_DCMD] = "DCMD",
};

/* --- Queues and signals (owned here, filled by other threads) ----------- */
struct rx_msg {
	char topic[APP_MQTT_TOPIC_MAX];
	uint16_t len;
	uint8_t payload[RX_PAYLOAD_MAX];
};

K_MSGQ_DEFINE(batch_q, sizeof(struct measurement_batch), CONFIG_APP_SPARKPLUG_BATCH_QUEUE_LEN, 8);
K_MSGQ_DEFINE(rx_q, sizeof(struct rx_msg), 2, 4);
static struct k_poll_signal wake_sig = K_POLL_SIGNAL_INITIALIZER(wake_sig);

static atomic_t session_gen;     /* ++ per MQTT session (on_connected) */
static atomic_t online;          /* 1 between on_connected and on_disconnected */
static atomic_t next_bdseq;      /* bdSeq for the next CONNECT */
static atomic_t current_bdseq;   /* bdSeq of the Will of the current session */
static atomic_t config_dirty;    /* app_logic changed the configuration */

static void sparkplug_thread(void *p1, void *p2, void *p3);
K_THREAD_DEFINE(sparkplug_tid, CONFIG_APP_SPARKPLUG_THREAD_STACK, sparkplug_thread, NULL, NULL,
		NULL, CONFIG_APP_SPARKPLUG_THREAD_PRIO, 0, K_TICKS_FOREVER);

/* --- Owned by the sparkplug thread --------------------------------------- */
static atomic_val_t born_gen;    /* session_gen of the last BIRTH, 0 = none */
static uint8_t seq;              /* 0..255, NBIRTH = 0 */
static struct measurement last_meas;
static bool have_last_meas;
static int64_t next_status_ms;
static struct rx_msg rx_buf;
static struct measurement_batch batch_buf;
static bool started;

/* ------------------------------------------------------------------------
 * Metric sources for the nanopb "metrics" callback
 * ------------------------------------------------------------------------
 */
struct metric_src {
	/** Fill metric @p idx; return false when there are no more metrics. */
	bool (*get)(const struct metric_src *src, size_t idx, spb_metric_t *m);
	const void *ctx;
	size_t count;
	bool names;      /* BIRTH: include names (and aliases) */
	uint64_t now_ms; /* metric timestamp for node/device metrics */
};

static bool encode_metrics_cb(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	const struct metric_src *src = *arg;

	for (size_t i = 0; i < src->count; i++) {
		spb_metric_t m = org_eclipse_tahu_protobuf_Payload_Metric_init_zero;

		if (!src->get(src, i, &m)) {
			continue;
		}
		if (!pb_encode_tag_for_field(stream, field) ||
		    !pb_encode_submessage(stream, SPB_METRIC_FIELDS, &m)) {
			return false;
		}
	}
	return true;
}

static void set_name_alias(spb_metric_t *m, const char *name, uint64_t alias, bool names)
{
	if (names || (alias == 0U)) {
		m->has_name = true;
		strncpy(m->name, name, sizeof(m->name) - 1);
	}
	if (alias != 0U) {
		m->has_alias = true;
		m->alias = alias;
	}
}

static void set_long(spb_metric_t *m, int64_t v)
{
	m->which_value = TAG_LONG;
	m->value.long_value = (uint64_t)v;
}

static const char *reset_cause_text(uint32_t cause)
{
	/* hwinfo bits, most relevant first (same order as device_info.c) */
	static const struct {
		uint32_t bit;
		const char *text;
	} map[] = {
		{RESET_BROWNOUT, "brownout"}, {RESET_WATCHDOG, "watchdog"},
		{RESET_CPU_LOCKUP, "cpu lockup"}, {RESET_SOFTWARE, "software"},
		{RESET_LOW_POWER_WAKE, "low-power wake"}, {RESET_PIN, "reset pin"},
		{RESET_POR, "power-on"},
	};

	for (size_t i = 0; i < ARRAY_SIZE(map); i++) {
		if ((cause & map[i].bit) != 0U) {
			return map[i].text;
		}
	}
	return "unknown";
}

/** Node metrics; ctx = array of enum node_metric (uint8_t) or NULL = all. */
static bool get_node_metric(const struct metric_src *src, size_t idx, spb_metric_t *m)
{
	const uint8_t *list = src->ctx;
	enum node_metric nm = (list != NULL) ? (enum node_metric)list[idx] : (enum node_metric)idx;
	struct app_config cfg;
	struct app_mqtt_stats st;
	int16_t rssi;

	set_name_alias(m, node_metrics[nm].name, node_metrics[nm].alias, src->names);
	m->has_datatype = true;
	m->datatype = node_metrics[nm].datatype;
	if (src->now_ms != 0U) {
		m->has_timestamp = true;
		m->timestamp = src->now_ms;
	}

	switch (nm) {
	case NM_BDSEQ:
		set_long(m, (int64_t)atomic_get(&current_bdseq));
		break;
	case NM_REBIRTH:
	case NM_REBOOT:
		m->which_value = TAG_BOOL;
		m->value.boolean_value = false;
		break;
	case NM_FW_VERSION:
		m->which_value = TAG_STRING;
		strncpy(m->value.string_value, device_info_fw_version(),
			sizeof(m->value.string_value) - 1);
		break;
	case NM_RESET_CAUSE:
		m->which_value = TAG_STRING;
		strncpy(m->value.string_value, reset_cause_text(device_info_reset_cause()),
			sizeof(m->value.string_value) - 1);
		break;
	case NM_SAMPLE_INTERVAL:
		app_logic_get_config(&cfg);
		set_long(m, cfg.sample_interval_ms);
		break;
	case NM_BATCH_SIZE:
		app_logic_get_config(&cfg);
		set_long(m, cfg.batch_size);
		break;
	case NM_RSSI:
		if (network_get_rssi(&rssi) == 0) {
			/* Sparkplug Int32 travels as uint32 (two's complement) */
			m->which_value = TAG_INT;
			m->value.int_value = (uint32_t)(int32_t)rssi;
		} else {
			m->has_is_null = true;
			m->is_null = true;
		}
		break;
	case NM_SECURITY:
		m->which_value = TAG_STRING;
		strncpy(m->value.string_value, app_security_posture(),
			sizeof(m->value.string_value) - 1);
		break;
	case NM_UPTIME:
		set_long(m, k_uptime_get() / 1000);
		break;
	case NM_MQTT_CONNECTS:
		app_mqtt_get_stats(&st);
		set_long(m, st.connects);
		break;
	default:
		return false;
	}
	return true;
}

/** Channels of the device "sensors" in DBIRTH order. */
static size_t device_channels(uint8_t *out, size_t max)
{
	size_t n = 0;

#if defined(CONFIG_APP_SOURCE_DIE_TEMP)
	if (n < max) {
		out[n++] = APP_CHANNEL_DIE_TEMP;
	}
#endif
#if defined(CONFIG_APP_MODBUS_RTU)
	for (size_t i = 0; (i < app_modbus_channel_count()) && (n < max); i++) {
		out[n++] = (uint8_t)(APP_CHANNEL_MODBUS_FIRST + i);
	}
#endif
	return n;
}

static void set_value(spb_metric_t *m, const struct channel_value *cv)
{
	m->has_datatype = true;
	m->datatype = DT_FLOAT;
	if (cv->quality == QUALITY_GOOD) {
		m->which_value = TAG_FLOAT;
		m->value.float_value = (float)cv->value_milli / 1000.0f;
	} else {
		/* BAD/STALE: explicit null instead of a fake number */
		m->has_is_null = true;
		m->is_null = true;
	}
}

static const struct channel_value *find_value(const struct measurement *meas, uint8_t channel)
{
	for (size_t i = 0; i < meas->count; i++) {
		if (meas->values[i].channel == channel) {
			return &meas->values[i];
		}
	}
	return NULL;
}

/** DBIRTH: ctx = channel list; value = last measured value or null. */
static bool get_device_birth_metric(const struct metric_src *src, size_t idx, spb_metric_t *m)
{
	const uint8_t *channels = src->ctx;
	const struct channel_value *cv = have_last_meas ? find_value(&last_meas, channels[idx]) : NULL;
	static const struct channel_value none = {.quality = QUALITY_STALE};

	set_name_alias(m, app_sensor_channel_name(channels[idx]),
		       DEVICE_ALIAS_BASE + channels[idx], true);
	set_value(m, (cv != NULL) ? cv : &none);
	m->has_timestamp = true;
	m->timestamp = src->now_ms;
	return true;
}

/** One value of one measurement in a DDATA. */
struct data_ref {
	uint8_t item;
	uint8_t value;
};

struct data_ctx {
	const struct measurement_batch *batch;
	const struct data_ref *refs;
};

static int64_t measurement_utc_ms(const struct measurement *meas)
{
	return (meas->timestamp_ms != 0) ? meas->timestamp_ms
					 : timesync_uptime_to_utc_ms(meas->uptime_ms);
}

static bool get_data_metric(const struct metric_src *src, size_t idx, spb_metric_t *m)
{
	const struct data_ctx *dc = src->ctx;
	const struct data_ref *r = &dc->refs[idx];
	const struct measurement *meas = &dc->batch->items[r->item];
	const struct channel_value *cv = &meas->values[r->value];
	int64_t utc = measurement_utc_ms(meas);

	m->has_alias = true;
	m->alias = DEVICE_ALIAS_BASE + cv->channel;
	if (utc > 0) {
		m->has_timestamp = true;
		m->timestamp = (uint64_t)utc;
	}
	set_value(m, cv);
	return true;
}

static bool get_ndeath_metric(const struct metric_src *src, size_t idx, spb_metric_t *m)
{
	ARG_UNUSED(idx);
	m->has_name = true;
	strcpy(m->name, node_metrics[NM_BDSEQ].name);
	m->has_datatype = true;
	m->datatype = DT_INT64;
	set_long(m, (int64_t)(uintptr_t)src->ctx);
	return true;
}

/* ------------------------------------------------------------------------
 * Payload encoding and publishing
 * ------------------------------------------------------------------------
 */
/** Encode a payload; buf == NULL only computes the size. */
static int encode_payload(uint8_t *buf, size_t size, const struct metric_src *src, bool with_seq,
			  uint8_t seq_value, int64_t ts_ms, size_t *len)
{
	spb_payload_t p = org_eclipse_tahu_protobuf_Payload_init_zero;
	pb_ostream_t os = PB_OSTREAM_SIZING;

	if (buf != NULL) {
		os = pb_ostream_from_buffer(buf, size);
	}

	if (ts_ms > 0) {
		p.has_timestamp = true;
		p.timestamp = (uint64_t)ts_ms;
	}
	if (with_seq) {
		p.has_seq = true;
		p.seq = seq_value;
	}
	p.metrics.funcs.encode = encode_metrics_cb;
	p.metrics.arg = (void *)src;

	if (!pb_encode(&os, SPB_PAYLOAD_FIELDS, &p)) {
		LOG_ERR("protobuf encode failed: %s", PB_GET_ERROR(&os));
		return -ENOMEM;
	}
	*len = os.bytes_written;
	return 0;
}

/** Encode and queue one message with the next seq (all types except NDEATH). */
static int publish(enum sparkplug_msg_type type, const struct metric_src *src, uint8_t qos)
{
	struct app_mqtt_msg msg = {.qos = qos, .retain = false};
	size_t len = 0;
	int ret;

	msg.payload = app_mqtt_alloc(K_MSEC(500));
	if (msg.payload == NULL) {
		LOG_WRN("%s: no payload buffer free", msg_type_names[type]);
		return -ENOMEM;
	}

	ret = sparkplug_topic(type, msg.topic, sizeof(msg.topic));
	if (ret >= 0) {
		ret = encode_payload(msg.payload, APP_MQTT_PAYLOAD_MAX, src, true, seq,
				     timesync_now_ms(), &len);
	}
	if (ret >= 0) {
		msg.len = (uint16_t)len;
		ret = app_mqtt_submit(&msg, K_MSEC(500));
	}
	if (ret < 0) {
		app_mqtt_free(msg.payload);
		LOG_ERR("%s not sent (%d)", msg_type_names[type], ret);
		return ret;
	}

	LOG_INF("%s seq %u: %u metric(s), %u B", msg_type_names[type], seq, (unsigned int)src->count,
		(unsigned int)len);
	seq++; /* uint8_t: wraps 255 -> 0 as required */
	return 0;
}

static int publish_births(void)
{
	uint8_t channels[APP_MAX_CHANNELS];
	int64_t now = timesync_now_ms();
	struct metric_src node = {
		.get = get_node_metric, .count = NM_COUNT, .names = true, .now_ms = (uint64_t)now};
	struct metric_src dev = {.get = get_device_birth_metric, .ctx = channels, .names = true,
				 .now_ms = (uint64_t)now};
	int ret;

	dev.count = device_channels(channels, ARRAY_SIZE(channels));
	seq = 0U;

	ret = publish(SPB_NBIRTH, &node, MQTT_QOS_0_AT_MOST_ONCE);
	if (ret == 0) {
		ret = publish(SPB_DBIRTH, &dev, MQTT_QOS_0_AT_MOST_ONCE);
	}
	return ret;
}

static void publish_node_data(const uint8_t *list, size_t count, const char *why)
{
	struct metric_src src = {.get = get_node_metric, .ctx = list, .count = count,
				 .now_ms = (uint64_t)timesync_now_ms()};

	LOG_DBG("NDATA (%s)", why);
	(void)publish(SPB_NDATA, &src, MQTT_QOS_0_AT_MOST_ONCE);
}

/**
 * One batch -> one or more DDATA. Normally one message; it is split only if
 * the encoded size exceeds APP_MQTT_PAYLOAD_MAX (e.g. 12 items x 4 channels).
 */
static void publish_batch(const struct measurement_batch *b)
{
	struct data_ref refs[MAX_DATA_METRICS];
	struct data_ctx dc = {.batch = b, .refs = refs};
	size_t n = 0;
	size_t start = 0;

	for (uint8_t i = 0; i < b->count; i++) {
		for (uint8_t v = 0; (v < b->items[i].count) && (n < ARRAY_SIZE(refs)); v++) {
			refs[n++] = (struct data_ref){.item = i, .value = v};
		}
	}

	while (start < n) {
		size_t end = start + 1U;
		size_t len = 0;

		/* Grow the chunk while it still fits. */
		while (end < n) {
			struct metric_src probe = {.get = get_data_metric, .ctx = &dc,
						   .count = end + 1U - start};

			dc.refs = &refs[start];
			if ((encode_payload(NULL, 0, &probe, true, seq, timesync_now_ms(), &len) != 0) ||
			    (len > APP_MQTT_PAYLOAD_MAX)) {
				break;
			}
			end++;
		}

		struct metric_src src = {.get = get_data_metric, .ctx = &dc, .count = end - start};

		dc.refs = &refs[start];
		if (publish(SPB_DDATA, &src, MQTT_QOS_0_AT_MOST_ONCE) != 0) {
			LOG_WRN("DDATA with %u value(s) lost", (unsigned int)(end - start));
		}
		start = end;
	}

	if (b->count > 0U) {
		last_meas = b->items[b->count - 1U];
		have_last_meas = true;
	}
}

/* ------------------------------------------------------------------------
 * Commands (NCMD / DCMD)
 * ------------------------------------------------------------------------
 */
struct cmd_result {
	bool rebirth;
	bool reboot;
	bool has_interval;
	int64_t interval_ms;
	bool has_batch;
	int64_t batch_size;
	uint8_t unknown;
};

static bool metric_as_int64(const spb_metric_t *m, int64_t *out)
{
	switch (m->which_value) {
	case TAG_INT:
		/* Int8/16/32 are sign-extended, UInt8/16 are not (Sparkplug B 3.0) */
		*out = (m->has_datatype && (m->datatype <= DT_INT32)) ? (int64_t)(int32_t)m->value.int_value
								      : (int64_t)m->value.int_value;
		return true;
	case TAG_LONG:
		*out = (int64_t)m->value.long_value;
		return true;
	case TAG_FLOAT:
		*out = (int64_t)m->value.float_value;
		return true;
	case TAG_DOUBLE:
		*out = (int64_t)m->value.double_value;
		return true;
	case TAG_BOOL:
		*out = m->value.boolean_value ? 1 : 0;
		return true;
	default:
		return false;
	}
}

static int match_node_metric(const spb_metric_t *m)
{
	for (int i = 0; i < NM_COUNT; i++) {
		if (m->has_name && (strcmp(m->name, node_metrics[i].name) == 0)) {
			return i;
		}
		if (!m->has_name && m->has_alias && (node_metrics[i].alias != 0U) &&
		    (m->alias == node_metrics[i].alias)) {
			return i;
		}
	}
	return -1;
}

static bool decode_metrics_cb(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	struct cmd_result *res = *arg;
	spb_metric_t m = org_eclipse_tahu_protobuf_Payload_Metric_init_zero;
	int64_t v = 0;
	int nm;

	ARG_UNUSED(field);
	if (!pb_decode(stream, SPB_METRIC_FIELDS, &m)) {
		return false;
	}

	nm = match_node_metric(&m);
	if ((nm < 0) || !metric_as_int64(&m, &v)) {
		res->unknown++;
		LOG_WRN("SECURITY: NCMD with unsupported metric %s (alias %llu) ignored",
			m.has_name ? m.name : "-",
			(unsigned long long)m.alias);
		return true;
	}

	switch (nm) {
	case NM_REBIRTH:
		res->rebirth = res->rebirth || (v != 0);
		break;
	case NM_REBOOT:
		res->reboot = res->reboot || (v != 0);
		break;
	case NM_SAMPLE_INTERVAL:
		res->has_interval = true;
		res->interval_ms = v;
		break;
	case NM_BATCH_SIZE:
		res->has_batch = true;
		res->batch_size = v;
		break;
	default:
		res->unknown++;
		LOG_WRN("SECURITY: NCMD tried to write read-only metric %s",
			node_metrics[nm].name);
		break;
	}
	return true;
}

static void send_ndeath_and_reboot(void)
{
	struct app_mqtt_msg msg = {.qos = MQTT_QOS_1_AT_LEAST_ONCE, .retain = false};
	int ret;

	/* Announce the planned death ourselves: a reboot drops the TCP
	 * connection silently, and the broker would only send the Will after
	 * 1.5 x keep-alive (30 min). Same bdSeq as the Will of this session.
	 */
	msg.payload = app_mqtt_alloc(K_MSEC(500));
	if (msg.payload != NULL) {
		struct metric_src src = {
			.get = get_ndeath_metric,
			.ctx = (const void *)(uintptr_t)atomic_get(&current_bdseq),
			.count = 1};
		size_t n = 0;

		ret = encode_payload(msg.payload, APP_MQTT_PAYLOAD_MAX, &src, false, 0,
				     timesync_now_ms(), &n);
		if ((ret == 0) && (sparkplug_topic(SPB_NDEATH, msg.topic, sizeof(msg.topic)) > 0)) {
			msg.len = (uint16_t)n;
			if (app_mqtt_submit(&msg, K_MSEC(500)) != 0) {
				app_mqtt_free(msg.payload);
			}
		} else {
			app_mqtt_free(msg.payload);
		}
	}

	LOG_WRN("Rebooting in 2 s (NCMD Node Control/Reboot)");
	k_sleep(K_SECONDS(2));
	sys_reboot(SYS_REBOOT_COLD);
}

static void handle_rx(const struct rx_msg *rx)
{
	char ncmd[APP_MQTT_TOPIC_MAX];
	struct cmd_result res = {0};
	spb_payload_t p = org_eclipse_tahu_protobuf_Payload_init_zero;
	pb_istream_t is = pb_istream_from_buffer(rx->payload, rx->len);

	(void)sparkplug_topic(SPB_NCMD, ncmd, sizeof(ncmd));
	if (strcmp(rx->topic, ncmd) != 0) {
		/* DCMD: the device "sensors" has no writable metrics */
		LOG_WRN("SECURITY: command on %s ignored (no writable metrics)", rx->topic);
		return;
	}

	p.metrics.funcs.decode = decode_metrics_cb;
	p.metrics.arg = &res;
	if (!pb_decode(&is, SPB_PAYLOAD_FIELDS, &p)) {
		LOG_ERR("NCMD: protobuf decode failed: %s", PB_GET_ERROR(&is));
		return;
	}

	if (res.has_interval) {
		struct app_cmd cmd = {.type = APP_CMD_SET_SAMPLE_INTERVAL, .value = res.interval_ms};

		LOG_INF("NCMD: Config/Sample Interval = %lld ms", res.interval_ms);
		(void)app_logic_submit_command(&cmd, K_MSEC(100));
	}
	if (res.has_batch) {
		struct app_cmd cmd = {.type = APP_CMD_SET_BATCH_SIZE, .value = res.batch_size};

		LOG_INF("NCMD: Config/Batch Size = %lld", res.batch_size);
		(void)app_logic_submit_command(&cmd, K_MSEC(100));
	}
	if (res.rebirth) {
		LOG_INF("NCMD: Node Control/Rebirth");
		born_gen = 0; /* the thread loop publishes NBIRTH + DBIRTH again */
	}
	if (res.reboot) {
		int64_t up_s = k_uptime_get() / 1000;

		if (up_s < CONFIG_APP_SPARKPLUG_REBOOT_MIN_UPTIME_S) {
			LOG_WRN("SECURITY: NCMD Reboot rejected - uptime %lld s < %d s "
				"(reboot-loop protection)", up_s,
				CONFIG_APP_SPARKPLUG_REBOOT_MIN_UPTIME_S);
		} else {
			LOG_WRN("SECURITY: NCMD Reboot accepted");
			send_ndeath_and_reboot();
		}
	}
}

/* ------------------------------------------------------------------------
 * Thread
 * ------------------------------------------------------------------------
 */
static void sparkplug_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		atomic_val_t gen = atomic_get(&session_gen);
		bool is_online = atomic_get(&online) != 0;
		int64_t now = k_uptime_get();

		if (is_online && (born_gen != gen)) {
			if (publish_births() == 0) {
				born_gen = gen;
				next_status_ms = now + (CONFIG_APP_SPARKPLUG_STATUS_INTERVAL_S * 1000LL);
				(void)atomic_set(&config_dirty, 0); /* BIRTH has the current config */
			} else {
				k_sleep(K_SECONDS(1)); /* MQTT queue full: try again */
				continue;
			}
		}

		bool born = is_online && (born_gen == gen);

		if (born && atomic_cas(&config_dirty, 1, 0)) {
			static const uint8_t cfg[] = {NM_SAMPLE_INTERVAL, NM_BATCH_SIZE};

			publish_node_data(cfg, ARRAY_SIZE(cfg), "config");
		}
		if (born && (CONFIG_APP_SPARKPLUG_STATUS_INTERVAL_S > 0) && (now >= next_status_ms)) {
			static const uint8_t status[] = {NM_RSSI, NM_UPTIME, NM_MQTT_CONNECTS};

			publish_node_data(status, ARRAY_SIZE(status), "status");
			next_status_ms = now + (CONFIG_APP_SPARKPLUG_STATUS_INTERVAL_S * 1000LL);
		}

		/* Wait: signal (session/config), commands, and batches only when born
		 * (otherwise they stay queued until the next BIRTH).
		 */
		struct k_poll_event events[3] = {
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY,
						 &wake_sig),
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
						 K_POLL_MODE_NOTIFY_ONLY, &rx_q),
			K_POLL_EVENT_INITIALIZER(K_POLL_TYPE_MSGQ_DATA_AVAILABLE,
						 K_POLL_MODE_NOTIFY_ONLY, &batch_q),
		};
		k_timeout_t timeout = K_FOREVER;

		if (born && (CONFIG_APP_SPARKPLUG_STATUS_INTERVAL_S > 0)) {
			timeout = K_MSEC(MAX(next_status_ms - k_uptime_get(), 0));
		}
		(void)k_poll(events, born ? 3 : 2, timeout);
		k_poll_signal_reset(&wake_sig);

		while (k_msgq_get(&rx_q, &rx_buf, K_NO_WAIT) == 0) {
			if (is_online) {
				handle_rx(&rx_buf);
			}
		}

		if (born && (atomic_get(&online) != 0) && (atomic_get(&session_gen) == gen)) {
			while (k_msgq_get(&batch_q, &batch_buf, K_NO_WAIT) == 0) {
				publish_batch(&batch_buf);
			}
		}
	}
}

/* ------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------
 */
int sparkplug_init(void)
{
	char topic[APP_MQTT_TOPIC_MAX];
	uint8_t channels[APP_MAX_CHANNELS];
	size_t n = device_channels(channels, ARRAY_SIZE(channels));

	if (sparkplug_topic(SPB_DDATA, topic, sizeof(topic)) < 0) {
		LOG_ERR("Topic too long");
		return -ENAMETOOLONG;
	}
	LOG_INF("Edge node %s/%s, device %s: %u metric(s), data topic %s",
		CONFIG_APP_SPARKPLUG_GROUP_ID, device_info_id(), CONFIG_APP_SPARKPLUG_DEVICE_ID,
		(unsigned int)n, topic);
	return 0;
}

int sparkplug_start(void)
{
	if (started) {
		return -EALREADY;
	}
	started = true;
	k_thread_name_set(sparkplug_tid, "sparkplug");
	k_thread_start(sparkplug_tid);
	return 0;
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
	/* bdSeq 0..255, one per MQTT session; NBIRTH of the session repeats it */
	atomic_val_t bd = atomic_inc(&next_bdseq) & 0xFF;
	struct metric_src src = {.get = get_ndeath_metric, .ctx = (const void *)(uintptr_t)bd,
				 .count = 1};
	size_t len = 0;
	int ret;

	(void)atomic_set(&current_bdseq, bd);
	ret = encode_payload(buf, size, &src, false, 0, timesync_now_ms(), &len);
	return (ret == 0) ? (int)len : ret;
}

void sparkplug_on_connected(void)
{
	(void)atomic_inc(&session_gen);
	(void)atomic_set(&online, 1);
	k_poll_signal_raise(&wake_sig, 1);
}

void sparkplug_on_disconnected(void)
{
	(void)atomic_set(&online, 0);
	k_poll_signal_raise(&wake_sig, 0);
}

void sparkplug_notify_config_changed(void)
{
	(void)atomic_set(&config_dirty, 1);
	k_poll_signal_raise(&wake_sig, 2);
}

int sparkplug_submit_batch(const struct measurement_batch *batch, k_timeout_t timeout)
{
	return (k_msgq_put(&batch_q, batch, timeout) == 0) ? 0 : -EAGAIN;
}

int sparkplug_submit_rx(const char *topic, const uint8_t *payload, size_t len)
{
	/* Runs in the mqtt thread (8 kB stack); the copy is ~360 B. */
	struct rx_msg m;

	if ((len > RX_PAYLOAD_MAX) || (strlen(topic) >= sizeof(m.topic))) {
		return -EMSGSIZE;
	}
	strcpy(m.topic, topic);
	memcpy(m.payload, payload, len);
	m.len = (uint16_t)len;
	return (k_msgq_put(&rx_q, &m, K_NO_WAIT) == 0) ? 0 : -EAGAIN;
}
