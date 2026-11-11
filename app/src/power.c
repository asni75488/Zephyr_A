/*
 * Zephyr_A - power (skeleton, implemented in the low-power part).
 *
 * Known issue (Zephyr 4.4.0): with CONFIG_PM=y and logging enabled,
 * soc/espressif/common/power.c uses LOG_MODULE_DECLARE(soc, ...) but no file
 * registers the "soc" log module for ESP32-C6, so the link fails with
 * "undefined reference to log_const_soc". Workaround (verified in a
 * prototype build in part 2): register the module once in the application,
 * in a separate file, e.g.
 *   LOG_MODULE_REGISTER(soc, CONFIG_SOC_LOG_LEVEL);
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "power.h"

LOG_MODULE_REGISTER(app_power, CONFIG_APP_LOG_LEVEL);

int app_power_init(void)
{
	LOG_DBG("skeleton");
	return 0;
}

void app_power_sleep_lock(void)
{
}

void app_power_sleep_unlock(void)
{
}

int app_power_deep_sleep(uint32_t duration_ms)
{
	ARG_UNUSED(duration_ms);
	return -ENOSYS;
}
