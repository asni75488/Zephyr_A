/*
 * Zephyr_A - workaround for Zephyr 4.4.0 with CONFIG_PM=y on ESP32-C6
 * (docs/02-arkitektur.md, finding F3).
 *
 * soc/espressif/common/power.c uses LOG_MODULE_DECLARE(soc, ...), but no file
 * of the ESP32-C6 SoC registers the "soc" log module, so the link fails with
 * "undefined reference to log_const_soc". It is registered here, in a file
 * of its own (one LOG_MODULE_REGISTER per file).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#if defined(CONFIG_PM) && defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
LOG_MODULE_REGISTER(soc, CONFIG_SOC_LOG_LEVEL);
#endif
