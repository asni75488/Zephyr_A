/*
 * Zephyr_A - health: task watchdog for the application threads (part 10).
 *
 * See health.h and docs/10-nice-to-have.md, section 1.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include "health.h"
#include "power.h"

#if defined(CONFIG_APP_HEALTH)
#include <zephyr/task_wdt/task_wdt.h>
#endif

LOG_MODULE_REGISTER(health, CONFIG_APP_LOG_LEVEL);

/* Note in the LP SRAM: magic in the upper half, task index in the lower. */
#define NOTE_MAGIC 0x57440000U /* "WD" */
#define NOTE_MASK  0xFFFF0000U

static const char *const names[HEALTH_TASK_COUNT] = {
	[HEALTH_SENSOR] = "sensor",
	[HEALTH_APP_LOGIC] = "app_logic",
	[HEALTH_NETWORK] = "network",
	[HEALTH_MQTT] = "mqtt",
	[HEALTH_SPARKPLUG] = "sparkplug",
	[HEALTH_UPLINK] = "uplink",
};

static const char *last_reset_task;

const char *health_task_name(enum health_task task)
{
	return ((unsigned int)task < HEALTH_TASK_COUNT) ? names[task] : "?";
}

const char *health_last_reset_task(void)
{
	return last_reset_task;
}

#if defined(CONFIG_APP_HEALTH)

BUILD_ASSERT(CONFIG_TASK_WDT_CHANNELS >= HEALTH_TASK_COUNT, "one channel per task");
BUILD_ASSERT(CONFIG_APP_HEALTH_TIMEOUT_S * 1000 > 2 * CONFIG_APP_SAMPLE_INTERVAL_MS,
	     "APP_HEALTH_TIMEOUT_S must be much longer than the sample interval");

#define HW_WDT_NODE DT_ALIAS(watchdog0)

static int channel[HEALTH_TASK_COUNT] = {-1, -1, -1, -1, -1, -1};
static atomic_t expirations[HEALTH_TASK_COUNT];
static bool started;

/* Runs in the task watchdog's timer callback (ISR context). */
static void expired(int channel_id, void *user_data)
{
	const uintptr_t task = (uintptr_t)user_data;

	ARG_UNUSED(channel_id);
	if (task >= HEALTH_TASK_COUNT) {
		return;
	}
	(void)atomic_inc(&expirations[task]);

	if (IS_ENABLED(CONFIG_APP_HEALTH_REBOOT)) {
		app_power_set_reset_note(NOTE_MAGIC | (uint32_t)task);
		/* printk: synchronous, safe here, nothing is lost by the reset */
		printk("\nSECURITY: task watchdog - thread '%s' stuck for %d s, resetting\n",
		       names[task], CONFIG_APP_HEALTH_TIMEOUT_S);
		sys_reboot(SYS_REBOOT_COLD);
	} else {
		/* Test builds: report and keep running; the channel stays expired
		 * until the thread feeds again.
		 */
		printk("TEST: task watchdog expired for '%s'\n", names[task]);
		(void)task_wdt_delete(channel[task]);
		channel[task] = -1;
	}
}

int health_init(void)
{
	const struct device *hw = NULL;
	uint32_t note = app_power_take_reset_note();
	int ret;

	if ((note & NOTE_MASK) == NOTE_MAGIC && (note & ~NOTE_MASK) < HEALTH_TASK_COUNT) {
		last_reset_task = names[note & ~NOTE_MASK];
		LOG_ERR("SECURITY: last reset by the task watchdog - thread '%s' was stuck",
			last_reset_task);
	}

#if DT_NODE_HAS_STATUS_OKAY(HW_WDT_NODE) && defined(CONFIG_TASK_WDT_HW_FALLBACK)
	hw = DEVICE_DT_GET(HW_WDT_NODE);
	if (!device_is_ready(hw)) {
		LOG_WRN("Hardware watchdog not ready - task watchdog without fallback");
		hw = NULL;
	}
#endif
	ret = task_wdt_init(hw);
	if (ret != 0) {
		LOG_ERR("task_wdt_init failed (%d)", ret);
		return ret;
	}
	started = true;
	LOG_INF("Task watchdog: %d s per thread, hardware fallback %s", CONFIG_APP_HEALTH_TIMEOUT_S,
		(hw != NULL) ? "wdt0" : "none");
	return 0;
}

void health_feed(enum health_task task)
{
	if (!started || ((unsigned int)task >= HEALTH_TASK_COUNT)) {
		return;
	}
	if (channel[task] < 0) {
		channel[task] = task_wdt_add(CONFIG_APP_HEALTH_TIMEOUT_S * 1000U, expired,
					     (void *)(uintptr_t)task);
		if (channel[task] < 0) {
			LOG_ERR("No task watchdog channel for %s (%d)", names[task],
				channel[task]);
		}
		return; /* task_wdt_add() feeds the new channel */
	}
	(void)task_wdt_feed(channel[task]);
}

void health_pause(enum health_task task)
{
	if (!started || ((unsigned int)task >= HEALTH_TASK_COUNT) || (channel[task] < 0)) {
		return;
	}
	(void)task_wdt_delete(channel[task]);
	channel[task] = -1;
}

unsigned int health_expirations(enum health_task task)
{
	return ((unsigned int)task < HEALTH_TASK_COUNT) ? (unsigned int)atomic_get(&expirations[task])
						       : 0U;
}

#else /* !CONFIG_APP_HEALTH */

int health_init(void)
{
	return -ENOTSUP;
}

void health_feed(enum health_task task)
{
	ARG_UNUSED(task);
}

void health_pause(enum health_task task)
{
	ARG_UNUSED(task);
}

unsigned int health_expirations(enum health_task task)
{
	ARG_UNUSED(task);
	return 0U;
}

#endif /* CONFIG_APP_HEALTH */
