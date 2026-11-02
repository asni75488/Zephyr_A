/*
 * Zephyr_A - modbus (skeleton, implemented when the Modbus source is known).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "modbus.h"

LOG_MODULE_REGISTER(app_modbus, CONFIG_APP_LOG_LEVEL);

int app_modbus_init(void)
{
	LOG_DBG("skeleton - Modbus source not decided yet");
	return -ENOTSUP;
}

int app_modbus_read(struct sample *out, size_t max)
{
	ARG_UNUSED(out);
	ARG_UNUSED(max);
	return -ENOTSUP;
}
