/*
 * Zephyr_A - memory shared between the HP core (app/src/power.c) and the
 * LP core (app/remote/src/main_ULP.c), part 9.
 *
 * Location: the devicetree node app_lp_shm (64 B at the end of the LP SRAM
 * area that is otherwise reserved for the LP image, see app/app.overlay and
 * app/remote/boards/esp32c6_devkitc_lpcore.overlay - both images must use
 * the same layout). The area lies outside both images, so it keeps its
 * content when the HP core resets or sleeps and when the LP image is
 * reloaded. It is NOT initialised at power-on: the magic tells whether the
 * HP core has written it.
 *
 * Rules (no locks are needed):
 *  - Every field has exactly one writer (HP: or LP: in the comment). The
 *    HP fields are grouped at the top, except hp_reset_note (part 10),
 *    which took the place of a reserved word at the end.
 *  - All fields are naturally aligned 32-bit words: single loads/stores are
 *    atomic on both RISC-V cores.
 *
 * lp_supervisor_step() is the complete decision logic of the LP core. It is
 * a pure function of this struct, so the same code runs on the LP core and
 * in the host unit test (app/tests/lp_supervisor).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_LP_SHARED_H_
#define ZEPHYR_A_LP_SHARED_H_

#include <stdint.h>

#define LP_SHARED_MAGIC   0x5A41504CU /* "ZAPL" */
#define LP_SHARED_VERSION 1U

/** HP core state, written by the HP core. */
enum lp_hp_state {
	LP_HP_RUN = 1,   /**< HP core runs (or is in light sleep): supervise it */
	LP_HP_SLEEP = 2, /**< HP core is in deep sleep: wake it after wake_after_s */
};

/** What the LP core must do after one step. */
enum lp_action {
	LP_ACTION_NONE = 0,
	LP_ACTION_WAKE_HP,  /**< ulp_lp_core_wakeup_main_processor() */
	LP_ACTION_RESET_HP, /**< reset the HP system (supervisor timeout) */
};

struct lp_shared {
	/* --- written by the HP core ------------------------------------- */
	uint32_t magic;          /**< HP: LP_SHARED_MAGIC when the fields are valid */
	uint32_t version;        /**< HP: LP_SHARED_VERSION                         */
	uint32_t period_ms;      /**< HP: LP timer period (time per LP step)        */
	uint32_t hp_state;       /**< HP: enum lp_hp_state                          */
	uint32_t hp_heartbeat;   /**< HP: +1 per processed measurement              */
	uint32_t watchdog_s;     /**< HP: reset HP if no heartbeat for this long, 0 = off */
	uint32_t wake_after_s;   /**< HP: deep sleep duration (valid in LP_HP_SLEEP) */
	uint32_t hp_resets_seen; /**< HP: last lp_resets value the HP core reported */

	/* --- written by the LP core ------------------------------------- */
	uint32_t lp_steps;       /**< LP: steps since the LP image was loaded       */
	uint32_t lp_heartbeat;   /**< LP: last heartbeat value seen                 */
	uint32_t lp_stale_ms;    /**< LP: time without a heartbeat change           */
	uint32_t lp_sleep_ms;    /**< LP: time the HP core has been in deep sleep   */
	uint32_t lp_wakes;       /**< LP: HP wake-ups requested (total)             */
	uint32_t lp_resets;      /**< LP: HP resets by the supervisor (total)       */
	uint32_t lp_wake_sent;   /**< LP: 1 = wake-up for the current sleep sent    */
	uint32_t hp_reset_note;  /**< HP: why the HP core reset itself (task watchdog, part 10) */
};

/* 16 words = 64 B = size of the app_lp_shm devicetree node. */
_Static_assert(sizeof(struct lp_shared) == 64, "lp_shared must be 64 bytes");

/**
 * @brief Called by the LP core on the first step after its image was
 * (re)loaded, i.e. after every HP boot that was not a wake-up from deep
 * sleep. Forgets supervision state from before the reset, so a hang that
 * was almost detected before a manual reset cannot reset the new boot.
 * The totals (lp_wakes, lp_resets) are kept.
 */
static inline void lp_shared_on_load(volatile struct lp_shared *s)
{
	s->lp_steps = 0U;
	s->lp_heartbeat = s->hp_heartbeat;
	s->lp_stale_ms = 0U;
	s->lp_sleep_ms = 0U;
	s->lp_wake_sent = 0U;
}

/**
 * @brief One LP-core step (called once per LP timer period).
 * @return Action the LP core must carry out.
 */
static inline enum lp_action lp_supervisor_step(volatile struct lp_shared *s)
{
	if ((s->magic != LP_SHARED_MAGIC) || (s->version != LP_SHARED_VERSION)) {
		return LP_ACTION_NONE; /* HP core has not set up the area yet */
	}

	const uint32_t dt = s->period_ms;

	s->lp_steps++;

	if (s->hp_state == LP_HP_SLEEP) {
		s->lp_stale_ms = 0U;
		if (s->lp_sleep_ms < UINT32_MAX - dt) {
			s->lp_sleep_ms += dt;
		}
		if (s->lp_sleep_ms >= (s->wake_after_s * 1000U)) {
			if (s->lp_wake_sent == 0U) {
				s->lp_wake_sent = 1U;
				s->lp_wakes++;
			}
			/* Repeated every step until the HP core reports LP_HP_RUN:
			 * a trigger that is lost while the HP core enters sleep
			 * is simply sent again one period later.
			 */
			return LP_ACTION_WAKE_HP;
		}
		return LP_ACTION_NONE;
	}

	/* LP_HP_RUN (or unknown): supervise the heartbeat */
	s->lp_sleep_ms = 0U;
	s->lp_wake_sent = 0U;

	if (s->hp_heartbeat != s->lp_heartbeat) {
		s->lp_heartbeat = s->hp_heartbeat;
		s->lp_stale_ms = 0U;
		return LP_ACTION_NONE;
	}
	if (s->watchdog_s == 0U) {
		s->lp_stale_ms = 0U;
		return LP_ACTION_NONE;
	}

	s->lp_stale_ms += dt;
	if (s->lp_stale_ms >= (s->watchdog_s * 1000U)) {
		s->lp_stale_ms = 0U;
		s->lp_resets++;
		return LP_ACTION_RESET_HP;
	}
	return LP_ACTION_NONE;
}

#endif /* ZEPHYR_A_LP_SHARED_H_ */
