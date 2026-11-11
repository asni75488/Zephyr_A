/*
 * Zephyr_A - unit test of lp_supervisor_step() (part 9).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/ztest.h>

#include "lp_shared.h"

static struct lp_shared s;

/* Like power.c lp_setup() after a cold boot. */
static void hp_setup(uint32_t watchdog_s)
{
	memset(&s, 0xA5, sizeof(s)); /* LP SRAM is random after power-on */
	s.hp_state = LP_HP_RUN;
	s.lp_wakes = 0;
	s.lp_resets = 0;
	s.hp_resets_seen = 0;
	s.period_ms = 1000;
	s.watchdog_s = watchdog_s;
	s.wake_after_s = 0;
	s.version = LP_SHARED_VERSION;
	s.magic = LP_SHARED_MAGIC;
	lp_shared_on_load(&s);
}

static enum lp_action steps(uint32_t n)
{
	enum lp_action a = LP_ACTION_NONE;

	for (uint32_t i = 0; i < n; i++) {
		a = lp_supervisor_step(&s);
		if (a != LP_ACTION_NONE) {
			break;
		}
	}
	return a;
}

ZTEST(lp_supervisor, test_no_action_before_hp_setup)
{
	memset(&s, 0xA5, sizeof(s));
	zassert_equal(steps(1000), LP_ACTION_NONE, "random memory must not trigger anything");
	s.magic = LP_SHARED_MAGIC;
	s.version = LP_SHARED_VERSION + 1U;
	zassert_equal(steps(1000), LP_ACTION_NONE, "other layout version must be ignored");
}

ZTEST(lp_supervisor, test_heartbeat_keeps_hp_alive)
{
	hp_setup(120);
	for (int i = 0; i < 1000; i++) {
		if ((i % 10) == 0) {
			s.hp_heartbeat++; /* one measurement every 10 s */
		}
		zassert_equal(lp_supervisor_step(&s), LP_ACTION_NONE, "step %d", i);
	}
	zassert_equal(s.lp_resets, 0);
}

ZTEST(lp_supervisor, test_reset_after_watchdog_timeout)
{
	hp_setup(120);
	s.hp_heartbeat++;
	zassert_equal(steps(1), LP_ACTION_NONE);         /* heartbeat seen */
	zassert_equal(steps(119), LP_ACTION_NONE);       /* 119 s stale */
	zassert_equal(lp_supervisor_step(&s), LP_ACTION_RESET_HP, "120 s stale -> reset");
	zassert_equal(s.lp_resets, 1);
	/* counter restarts: the next reset needs another full timeout */
	zassert_equal(steps(119), LP_ACTION_NONE);
	zassert_equal(lp_supervisor_step(&s), LP_ACTION_RESET_HP);
	zassert_equal(s.lp_resets, 2);
}

ZTEST(lp_supervisor, test_watchdog_off)
{
	hp_setup(0);
	zassert_equal(steps(100000), LP_ACTION_NONE);
}

ZTEST(lp_supervisor, test_reload_forgets_stale_time)
{
	hp_setup(120);
	zassert_equal(steps(110), LP_ACTION_NONE);
	/* HP reset by the user: LP image reloaded, HP needs time to boot */
	lp_shared_on_load(&s);
	zassert_equal(steps(119), LP_ACTION_NONE, "no reset during the new boot");
	zassert_equal(s.lp_resets, 0);
}

ZTEST(lp_supervisor, test_deep_sleep_wakeup)
{
	hp_setup(120);
	s.wake_after_s = 300;
	s.hp_state = LP_HP_SLEEP;
	/* No heartbeat during sleep, but no reset either */
	zassert_equal(steps(299), LP_ACTION_NONE);
	zassert_equal(lp_supervisor_step(&s), LP_ACTION_WAKE_HP, "wake after 300 s");
	zassert_equal(s.lp_wakes, 1);
	/* Repeated until the HP core runs, counted once */
	zassert_equal(lp_supervisor_step(&s), LP_ACTION_WAKE_HP);
	zassert_equal(s.lp_wakes, 1);
	/* HP boots and reports RUN */
	s.hp_state = LP_HP_RUN;
	zassert_equal(lp_supervisor_step(&s), LP_ACTION_NONE);
	zassert_equal(s.lp_sleep_ms, 0);
	zassert_equal(s.lp_wake_sent, 0);
}

ZTEST(lp_supervisor, test_long_sleep_no_overflow)
{
	hp_setup(120);
	s.wake_after_s = 86400; /* APP_SLEEP_MAX_S */
	s.hp_state = LP_HP_SLEEP;
	zassert_equal(steps(86399), LP_ACTION_NONE);
	zassert_equal(lp_supervisor_step(&s), LP_ACTION_WAKE_HP);
}

ZTEST_SUITE(lp_supervisor, NULL, NULL, NULL, NULL, NULL);
