/*
 * Zephyr_A - power: sleep policy, LP core and deep sleep (part 9).
 *
 * See power.h and docs/09-strom-data.md.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/policy.h>
#include <zephyr/sys/poweroff.h>

#include "power.h"

#if defined(CONFIG_APP_LPCORE)
#include <zephyr/devicetree.h>
#include <zephyr/storage/flash_map.h>
#include <psa/crypto.h>
#include <esp_sleep.h>
#include <ulp_lp_core.h>

#include "lp_shared.h"
#endif

LOG_MODULE_REGISTER(app_power, CONFIG_APP_LOG_LEVEL);

#if defined(CONFIG_APP_LPCORE)

#define LP_SHM_NODE DT_NODELABEL(app_lp_shm)
BUILD_ASSERT(DT_NODE_EXISTS(LP_SHM_NODE), "app.overlay must define app_lp_shm");
BUILD_ASSERT(DT_REG_SIZE(LP_SHM_NODE) == sizeof(struct lp_shared),
	     "app_lp_shm must match struct lp_shared");
BUILD_ASSERT(CONFIG_APP_LPCORE_WATCHDOG_S == 0 ||
	     CONFIG_APP_LPCORE_WATCHDOG_S * 1000 > 2 * CONFIG_APP_SAMPLE_INTERVAL_MS,
	     "APP_LPCORE_WATCHDOG_S must be much longer than the sample interval");

static volatile struct lp_shared *const shm =
	(volatile struct lp_shared *)DT_REG_ADDR(LP_SHM_NODE);

/* Generated at build time from the LP zephyr.bin (cmake/lp_image.cmake). */
extern const uint8_t zephyr_a_lp_image_sha256[32];
extern const size_t zephyr_a_lp_image_size;

#define LP_PERIOD_MS (CONFIG_ESP32_ULP_LP_CORE_LP_TIMER_SLEEP_DURATION_US / 1000)

static bool lp_active;
static bool reset_by_lp;

/*
 * The LP image is outside MCUboot's signature check: Zephyr copies the LP
 * partition into the LP SRAM at boot. Compare it with the hash the signed HP
 * image was built with; on a mismatch stop the LP core.
 */
static int verify_lp_image(void)
{
	const struct flash_area *fa;
	psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
	uint8_t chunk[256];
	uint8_t digest[32];
	size_t digest_len = 0;
	size_t done = 0;
	int ret;

	if (zephyr_a_lp_image_size > PARTITION_SIZE(slot0_lpcore_partition)) {
		return -EFBIG;
	}
	ret = flash_area_open(PARTITION_ID(slot0_lpcore_partition), &fa);
	if (ret != 0) {
		return ret;
	}
	if ((psa_crypto_init() != PSA_SUCCESS) || (psa_hash_setup(&op, PSA_ALG_SHA_256) != PSA_SUCCESS)) {
		flash_area_close(fa);
		return -EIO;
	}

	while ((ret == 0) && (done < zephyr_a_lp_image_size)) {
		size_t n = MIN(sizeof(chunk), zephyr_a_lp_image_size - done);

		ret = flash_area_read(fa, (off_t)done, chunk, n);
		if ((ret == 0) && (psa_hash_update(&op, chunk, n) != PSA_SUCCESS)) {
			ret = -EIO;
		}
		done += n;
	}
	flash_area_close(fa);

	if ((ret != 0) ||
	    (psa_hash_finish(&op, digest, sizeof(digest), &digest_len) != PSA_SUCCESS)) {
		(void)psa_hash_abort(&op);
		return (ret != 0) ? ret : -EIO;
	}
	return (memcmp(digest, zephyr_a_lp_image_sha256, sizeof(digest)) == 0) ? 0 : -EBADMSG;
}

static void lp_setup(void)
{
	const bool woke_by_lp = (esp_sleep_get_wakeup_causes() & BIT(ESP_SLEEP_WAKEUP_ULP)) != 0U;
	const bool valid = (shm->magic == LP_SHARED_MAGIC) && (shm->version == LP_SHARED_VERSION);
	int ret = verify_lp_image();

	if (ret != 0) {
		ulp_lp_core_stop();
		shm->magic = 0U;
		LOG_ERR("SECURITY: LP-core image check failed (%d) - LP core stopped "
			"(no supervisor, no deep sleep)", ret);
		return;
	}

	if (valid && (shm->lp_resets != shm->hp_resets_seen) && !woke_by_lp) {
		reset_by_lp = true;
		LOG_WRN("SECURITY: HP core was reset by the LP-core supervisor (no measurement "
			"for %u s, %u reset(s) in total)", shm->watchdog_s, shm->lp_resets);
	}

	/* Field order: state first (stops sleep counting), magic last. A cold
	 * power-on leaves random data, so the totals are cleared then.
	 */
	shm->hp_state = LP_HP_RUN;
	if (!valid) {
		shm->lp_wakes = 0U;
		shm->lp_resets = 0U;
	}
	shm->hp_resets_seen = shm->lp_resets;
	shm->period_ms = LP_PERIOD_MS;
	shm->watchdog_s = CONFIG_APP_LPCORE_WATCHDOG_S;
	shm->wake_after_s = 0U;
	shm->version = LP_SHARED_VERSION;
	shm->magic = LP_SHARED_MAGIC;
	lp_active = true;

	if (woke_by_lp) {
		LOG_INF("Woken from deep sleep by the LP core after %u s (wake-up #%u)",
			shm->lp_sleep_ms / 1000U, shm->lp_wakes);
	}
	LOG_INF("LP core: image verified (%u B), step %u ms, supervisor %s%u s, "
		"wakes %u, resets %u", (unsigned int)zephyr_a_lp_image_size, LP_PERIOD_MS,
		(CONFIG_APP_LPCORE_WATCHDOG_S == 0) ? "off " : "", CONFIG_APP_LPCORE_WATCHDOG_S,
		shm->lp_wakes, shm->lp_resets);
}

#endif /* CONFIG_APP_LPCORE */

int app_power_init(void)
{
#if defined(CONFIG_PM)
	if (!IS_ENABLED(CONFIG_APP_POWER_LIGHT_SLEEP)) {
		/* Light sleep disabled by configuration: lock it permanently */
		pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
	}
	LOG_INF("Light sleep %s, uplink mode %s",
		IS_ENABLED(CONFIG_APP_POWER_LIGHT_SLEEP) ? "allowed when idle" : "disabled",
		IS_ENABLED(CONFIG_APP_UPLINK_INTERVAL) ? "interval" : "continuous");
#endif
#if defined(CONFIG_APP_LPCORE)
	lp_setup();
#endif
	return 0;
}

void app_power_sleep_lock(void)
{
#if defined(CONFIG_PM)
	pm_policy_state_lock_get(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
#endif
}

void app_power_sleep_unlock(void)
{
#if defined(CONFIG_PM)
	pm_policy_state_lock_put(PM_STATE_STANDBY, PM_ALL_SUBSTATES);
#endif
}

void app_power_heartbeat(void)
{
#if defined(CONFIG_APP_LPCORE)
	if (lp_active) {
		shm->hp_heartbeat = shm->hp_heartbeat + 1U; /* only writer */
	}
#endif
}

bool app_power_reset_by_lp(void)
{
#if defined(CONFIG_APP_LPCORE)
	return reset_by_lp;
#else
	return false;
#endif
}

bool app_power_deep_sleep_supported(void)
{
#if defined(CONFIG_APP_LPCORE) && defined(CONFIG_POWEROFF)
	return lp_active;
#else
	return false;
#endif
}

int app_power_deep_sleep(uint32_t seconds)
{
#if defined(CONFIG_APP_LPCORE) && defined(CONFIG_POWEROFF)
	if (!lp_active) {
		return -ENOTSUP;
	}

	LOG_WRN("Deep sleep for %u s - the LP core wakes the HP core", seconds);
	shm->wake_after_s = seconds;
	shm->hp_state = LP_HP_SLEEP; /* LP core counts from its next step */
	k_sleep(K_MSEC(200));        /* let the log thread print */
	sys_poweroff();
	CODE_UNREACHABLE;
#else
	ARG_UNUSED(seconds);
	return -ENOTSUP;
#endif
}

void app_power_get_lp_stats(struct app_power_lp_stats *out)
{
	memset(out, 0, sizeof(*out));
#if defined(CONFIG_APP_LPCORE)
	out->active = lp_active;
	if (lp_active) {
		out->steps = shm->lp_steps;
		out->wakes = shm->lp_wakes;
		out->resets = shm->lp_resets;
	}
#endif
}
