/*
 * Zephyr_A - modbus: Modbus RTU client for data acquisition (part 3).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_MODBUS)
#include <zephyr/drivers/uart.h>
#include <zephyr/modbus/modbus.h>
#endif

#include "modbus.h"

LOG_MODULE_REGISTER(app_modbus, CONFIG_APP_LOG_LEVEL);

#if defined(CONFIG_APP_MODBUS_RTU)

/* ------------------------------------------------------------------------
 * Register table: one entry = one channel = one Modbus request.
 * value_milli = raw * scale_num / scale_den
 * Example: a slave that stores 23.4 °C as 234 (0.1 °C/bit) -> scale 100/1
 * gives 23400 m°C.
 * ------------------------------------------------------------------------
 */
enum mb_fc {
	MB_FC_HOLDING = 3, /* FC03 Read Holding Registers */
	MB_FC_INPUT = 4,   /* FC04 Read Input Registers   */
};

struct mb_reg_def {
	const char *name;  /* Sparkplug metric name        */
	const char *unit;
	uint8_t fc;        /* enum mb_fc                   */
	uint16_t addr;     /* 0-based register address     */
	bool is_signed;    /* interpret raw as int16_t     */
	int32_t scale_num;
	int32_t scale_den;
};

static const struct mb_reg_def reg_table[] = {
	{"Modbus/Temperature", "degC", MB_FC_HOLDING, 0, true, 100, 1}, /* 0.1 °C/bit */
	{"Modbus/Humidity", "%RH", MB_FC_HOLDING, 1, false, 100, 1},    /* 0.1 %/bit  */
};

BUILD_ASSERT(ARRAY_SIZE(reg_table) <= (APP_MAX_CHANNELS - APP_CHANNEL_MODBUS_FIRST),
	     "Modbus register table does not fit in struct measurement");

/* Consecutive failed cycles and cycles left to skip (backoff). Only used by
 * the sensor thread, so no locking is needed.
 */
static uint32_t fail_cycles;
static uint32_t skip_cycles_left;

#if defined(CONFIG_APP_SENSOR_SIMULATION)

static uint32_t sim_cycle;

/* Simulated slave: slowly changing values, optional injected timeouts. */
static int mb_transaction(const struct mb_reg_def *def, size_t idx, uint16_t *raw)
{
	ARG_UNUSED(def);

	if ((CONFIG_APP_SENSOR_SIM_FAULT_EVERY > 0) &&
	    ((sim_cycle % CONFIG_APP_SENSOR_SIM_FAULT_EVERY) ==
	     (CONFIG_APP_SENSOR_SIM_FAULT_EVERY - 1))) {
		return -ETIMEDOUT;
	}

	/* Temperature 21.0..23.9 °C, humidity 40.0..49.9 % (raw 0.1 units) */
	*raw = (idx == 0) ? (uint16_t)(210 + (sim_cycle % 30)) : (uint16_t)(400 + (sim_cycle % 100));
	return 0;
}

static int mb_iface_init(void)
{
	LOG_INF("Modbus RTU: SIMULATED slave %d", CONFIG_APP_MODBUS_SLAVE_ADDR);
	return 0;
}

static void mb_cycle_done(void)
{
	sim_cycle++;
}

#else /* real hardware */

#define MODBUS_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_modbus_serial)
BUILD_ASSERT(DT_NODE_EXISTS(MODBUS_NODE),
	     "CONFIG_APP_MODBUS_RTU needs a zephyr,modbus-serial node (app/modbus-rtu.overlay)");

static int mb_iface = -1;

static int mb_transaction(const struct mb_reg_def *def, size_t idx, uint16_t *raw)
{
	ARG_UNUSED(idx);

	if (def->fc == MB_FC_INPUT) {
		return modbus_read_input_regs(mb_iface, CONFIG_APP_MODBUS_SLAVE_ADDR, def->addr,
					      raw, 1);
	}
	return modbus_read_holding_regs(mb_iface, CONFIG_APP_MODBUS_SLAVE_ADDR, def->addr, raw,
					1);
}

static int mb_iface_init(void)
{
	const char iface_name[] = {DEVICE_DT_NAME(MODBUS_NODE)};
	const struct modbus_iface_param param = {
		.mode = MODBUS_MODE_RTU,
		.rx_timeout = CONFIG_APP_MODBUS_RESP_TIMEOUT_MS * USEC_PER_MSEC,
		.serial = {
			.baud = CONFIG_APP_MODBUS_BAUDRATE,
#if defined(CONFIG_APP_MODBUS_PARITY_EVEN)
			.parity = UART_CFG_PARITY_EVEN,
#elif defined(CONFIG_APP_MODBUS_PARITY_ODD)
			.parity = UART_CFG_PARITY_ODD,
#else
			.parity = UART_CFG_PARITY_NONE,
#endif
		},
	};

	mb_iface = modbus_iface_get_by_name(iface_name);
	if (mb_iface < 0) {
		LOG_ERR("Modbus interface %s not found (%d)", iface_name, mb_iface);
		return mb_iface;
	}

	int ret = modbus_init_client(mb_iface, param);

	if (ret != 0) {
		LOG_ERR("modbus_init_client failed (%d)", ret);
		return ret;
	}

	LOG_INF("Modbus RTU client on %s: slave %d, %d baud, parity %s, timeout %d ms",
		iface_name, CONFIG_APP_MODBUS_SLAVE_ADDR, CONFIG_APP_MODBUS_BAUDRATE,
		IS_ENABLED(CONFIG_APP_MODBUS_PARITY_EVEN)  ? "even (8E1)"
		: IS_ENABLED(CONFIG_APP_MODBUS_PARITY_ODD) ? "odd (8O1)"
							   : "none (8N2)",
		CONFIG_APP_MODBUS_RESP_TIMEOUT_MS);
	return 0;
}

static void mb_cycle_done(void)
{
}

#endif /* CONFIG_APP_SENSOR_SIMULATION */

static const char *mb_err_str(int err)
{
	if (err == -ETIMEDOUT) {
		return "timeout (no response)";
	}
	if (err == -EIO) {
		return "CRC/frame error";
	}
	if (err > 0) {
		return "exception from slave";
	}
	return "error";
}

/* One request with retries and exponential backoff between attempts. */
static int read_with_retry(const struct mb_reg_def *def, size_t idx, uint16_t *raw)
{
	uint32_t backoff_ms = CONFIG_APP_MODBUS_RETRY_BACKOFF_MS;
	int err = 0;

	for (int attempt = 0; attempt <= CONFIG_APP_MODBUS_RETRIES; attempt++) {
		err = mb_transaction(def, idx, raw);
		if (err == 0) {
			return 0;
		}

		LOG_ERR("%s: FC%02u addr %u slave %d attempt %d/%d: %s (%d)", def->name, def->fc,
			def->addr, CONFIG_APP_MODBUS_SLAVE_ADDR, attempt + 1,
			CONFIG_APP_MODBUS_RETRIES + 1, mb_err_str(err), err);

		if (attempt < CONFIG_APP_MODBUS_RETRIES) {
			k_sleep(K_MSEC(backoff_ms));
			backoff_ms *= 2U;
		}
	}
	return err;
}

static int32_t scale(const struct mb_reg_def *def, uint16_t raw)
{
	int64_t v = def->is_signed ? (int64_t)(int16_t)raw : (int64_t)raw;

	return (int32_t)((v * def->scale_num) / def->scale_den);
}

int app_modbus_init(void)
{
	fail_cycles = 0U;
	skip_cycles_left = 0U;
	return mb_iface_init();
}

size_t app_modbus_channel_count(void)
{
	return ARRAY_SIZE(reg_table);
}

const char *app_modbus_channel_name(size_t idx)
{
	return (idx < ARRAY_SIZE(reg_table)) ? reg_table[idx].name : "?";
}

const char *app_modbus_channel_unit(size_t idx)
{
	return (idx < ARRAY_SIZE(reg_table)) ? reg_table[idx].unit : "";
}

int app_modbus_read(struct channel_value *out, size_t max)
{
	const size_t n = ARRAY_SIZE(reg_table);
	bool any_ok = false;
	bool all_ok = true;

	if (max < n) {
		return -ENOMEM;
	}

	/* Cycle-level backoff: slave was offline, do not poll this cycle. */
	if (skip_cycles_left > 0U) {
		skip_cycles_left--;
		for (size_t i = 0; i < n; i++) {
			out[i] = (struct channel_value){
				.value_milli = 0,
				.channel = (uint8_t)(APP_CHANNEL_MODBUS_FIRST + i),
				.quality = QUALITY_STALE,
			};
		}
		LOG_WRN("Modbus slave %d skipped (backoff), %u cycle(s) left",
			CONFIG_APP_MODBUS_SLAVE_ADDR, skip_cycles_left);
		mb_cycle_done();
		return (int)n;
	}

	for (size_t i = 0; i < n; i++) {
		uint16_t raw = 0U;
		int err = read_with_retry(&reg_table[i], i, &raw);

		out[i].channel = (uint8_t)(APP_CHANNEL_MODBUS_FIRST + i);
		if (err == 0) {
			out[i].value_milli = scale(&reg_table[i], raw);
			out[i].quality = QUALITY_GOOD;
			any_ok = true;
		} else {
			out[i].value_milli = 0;
			out[i].quality = QUALITY_BAD;
			all_ok = false;
		}
	}

	if (any_ok) {
		if (fail_cycles > 0U) {
			LOG_INF("Modbus slave %d responding again after %u failed cycle(s)",
				CONFIG_APP_MODBUS_SLAVE_ADDR, fail_cycles);
		}
		fail_cycles = 0U;
	} else {
		/* Whole cycle failed: skip min(2^(n-1) - 1, MAX) following cycles. */
		fail_cycles++;
		uint32_t skip = (fail_cycles >= 31U) ? UINT32_MAX : ((1U << (fail_cycles - 1U)) - 1U);

		skip_cycles_left = MIN(skip, (uint32_t)CONFIG_APP_MODBUS_MAX_SKIP_CYCLES);
		LOG_ERR("Modbus slave %d: no valid response (%u failed cycle(s) in a row), "
			"next attempt in %u cycle(s)",
			CONFIG_APP_MODBUS_SLAVE_ADDR, fail_cycles, skip_cycles_left + 1U);
	}

	if (!all_ok && any_ok) {
		LOG_WRN("Modbus slave %d: some registers failed", CONFIG_APP_MODBUS_SLAVE_ADDR);
	}

	mb_cycle_done();
	return (int)n;
}

#else /* !CONFIG_APP_MODBUS_RTU */

int app_modbus_init(void)
{
	return -ENOTSUP;
}

size_t app_modbus_channel_count(void)
{
	return 0;
}

const char *app_modbus_channel_name(size_t idx)
{
	ARG_UNUSED(idx);
	return "?";
}

const char *app_modbus_channel_unit(size_t idx)
{
	ARG_UNUSED(idx);
	return "";
}

int app_modbus_read(struct channel_value *out, size_t max)
{
	ARG_UNUSED(out);
	ARG_UNUSED(max);
	return 0;
}

#endif /* CONFIG_APP_MODBUS_RTU */
