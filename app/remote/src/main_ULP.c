/*
 * Zephyr_A - LP-core program (part 9, docs/09-strom-data.md).
 *
 * The ESP32-C6 LP core (20 MHz RISC-V, always-on power domain) runs this
 * program once per LP timer period (CONFIG_ESP32_ULP_LP_CORE_LP_TIMER_
 * SLEEP_DURATION_US in the HP image, 1 s). Each run starts at the reset
 * vector, executes one supervisor step and halts again; between runs the
 * LP core is powered down. Two jobs:
 *
 *  1. Deep-sleep wake-up: while the HP core is in deep sleep
 *     (NCMD "Node Control/Sleep"), count the time and wake the HP core when
 *     the requested duration has passed.
 *  2. Supervisor: while the HP core runs, check that its heartbeat (one per
 *     processed measurement) keeps changing. If it stops for watchdog_s
 *     (sampling pipeline hung), reset the HP system. The LP core runs on its
 *     own clock and power domain, so this also works when the HP core hangs
 *     with interrupts disabled.
 *
 * All decisions are made by lp_supervisor_step() in lp_shared.h (unit tested
 * on the host). This file only carries out the actions.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>

#include "soc/lp_aon_reg.h"
#include "soc/soc.h"
#include "ulp_lp_core_lp_timer_shared.h"
#include "ulp_lp_core_memory_shared.h"
#include "ulp_lp_core_utils.h"

#include "lp_shared.h"

#define LP_SHM_NODE DT_NODELABEL(app_lp_shm)
BUILD_ASSERT(DT_REG_SIZE(LP_SHM_NODE) == sizeof(struct lp_shared),
	     "app_lp_shm must match struct lp_shared");

static volatile struct lp_shared *const shm =
	(volatile struct lp_shared *)DT_REG_ADDR(LP_SHM_NODE);

/*
 * Initialised data (.data) is written once when the HP core loads the LP
 * image and is NOT reset between LP timer runs (only .bss is cleared by the
 * start code). So this flag is true exactly on the first run after a load.
 */
static volatile bool first_run_after_load = true;

int main(void)
{
	if (first_run_after_load) {
		first_run_after_load = false;
		lp_shared_on_load(shm);
	}

	switch (lp_supervisor_step(shm)) {
	case LP_ACTION_WAKE_HP:
		ulp_lp_core_wakeup_main_processor();
		break;

	case LP_ACTION_RESET_HP:
		/* Software reset of the HP system (digital core). The LP domain,
		 * the LP SRAM and therefore lp_resets survive it; the HP core
		 * reports the reset after boot (reset reason RESET_REASON_CORE_SW).
		 */
		REG_SET_BIT(LP_AON_SYS_CFG_REG, LP_AON_HPSYS_SW_RESET);
		break;

	case LP_ACTION_NONE:
	default:
		break;
	}

	/* Next run after one period (configured by the HP core when it started
	 * the LP core), then power down until then.
	 */
	ulp_lp_core_memory_shared_cfg_t *cfg = ulp_lp_core_memory_shared_cfg_get();

	if (cfg->sleep_duration_ticks != 0U) {
		ulp_lp_core_lp_timer_set_wakeup_ticks(cfg->sleep_duration_ticks);
	}
	ulp_lp_core_halt();

	return 0;
}
