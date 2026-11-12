/*
 * Zephyr_A - unit test of health.c (task watchdog, part 10).
 *
 * native_sim runs in simulated time, so the 30 s timeouts take milliseconds.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "health.h"
#include "power.h"

/* power.c is not linked: the reset note is kept here. */
static uint32_t note_store;

void app_power_set_reset_note(uint32_t note)
{
	note_store = note;
}

uint32_t app_power_take_reset_note(void)
{
	uint32_t n = note_store;

	note_store = 0U;
	return n;
}

static void *setup(void)
{
	/* Pretend the previous boot was reset because "mqtt" was stuck */
	note_store = 0x57440000U | HEALTH_MQTT;
	zassert_ok(health_init());
	return NULL;
}

ZTEST(health, test_0_last_reset_reported)
{
	zassert_not_null(health_last_reset_task());
	zassert_str_equal(health_last_reset_task(), "mqtt");
	zassert_equal(note_store, 0U, "note consumed");
}

ZTEST(health, test_1_fed_thread_never_expires)
{
	for (int i = 0; i < 20; i++) {
		health_feed(HEALTH_SENSOR);
		k_sleep(K_SECONDS(10)); /* like one measurement every 10 s */
	}
	zassert_equal(health_expirations(HEALTH_SENSOR), 0U);
	health_pause(HEALTH_SENSOR);
}

ZTEST(health, test_2_stuck_thread_expires)
{
	health_feed(HEALTH_NETWORK);
	k_sleep(K_SECONDS(29));
	zassert_equal(health_expirations(HEALTH_NETWORK), 0U, "not yet");
	k_sleep(K_SECONDS(2));
	zassert_equal(health_expirations(HEALTH_NETWORK), 1U, "30 s busy without feed");
	/* Only the stuck thread is reported */
	zassert_equal(health_expirations(HEALTH_MQTT), 0U);
}

ZTEST(health, test_3_paused_thread_may_wait_forever)
{
	health_feed(HEALTH_APP_LOGIC);
	health_pause(HEALTH_APP_LOGIC);
	k_sleep(K_SECONDS(600)); /* e.g. waiting for the network for 10 min */
	zassert_equal(health_expirations(HEALTH_APP_LOGIC), 0U);
	/* Busy again: supervised again */
	health_feed(HEALTH_APP_LOGIC);
	k_sleep(K_SECONDS(31));
	zassert_equal(health_expirations(HEALTH_APP_LOGIC), 1U);
}

ZTEST(health, test_4_all_tasks_get_a_channel)
{
	for (int t = 0; t < HEALTH_TASK_COUNT; t++) {
		health_feed((enum health_task)t);
	}
	k_sleep(K_SECONDS(5));
	for (int t = 0; t < HEALTH_TASK_COUNT; t++) {
		health_feed((enum health_task)t);
		health_pause((enum health_task)t);
	}
	k_sleep(K_SECONDS(100));
	for (int t = 0; t < HEALTH_TASK_COUNT; t++) {
		zassert_true(health_expirations((enum health_task)t) <= 1U, "%s",
			     health_task_name((enum health_task)t));
	}
	zassert_equal(health_expirations(HEALTH_UPLINK), 0U);
}

ZTEST_SUITE(health, NULL, setup, NULL, NULL, NULL);
