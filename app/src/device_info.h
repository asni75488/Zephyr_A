/*
 * Zephyr_A - device_info: device identity and firmware version.
 *
 * Owner thread: none (library). Initialised once by main before any thread
 * starts; afterwards read-only, so no locking is needed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_DEVICE_INFO_H_
#define ZEPHYR_A_DEVICE_INFO_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** "esp32c6-" + 12 hex characters + NUL */
#define DEVICE_INFO_ID_MAX_LEN 21

/**
 * @brief Read the hardware id (factory MAC in eFuse) and build the device id.
 *
 * Must be called once from main before the other modules are initialised.
 *
 * @retval 0 on success, negative errno if the hardware id could not be read
 *         (the id is then "esp32c6-unknown").
 */
int device_info_init(void);

/** @return Device id, e.g. "esp32c6-14c19fe0ab24". Also the Sparkplug edge node id. */
const char *device_info_id(void);

/** @return Firmware version string from app/VERSION, e.g. "0.2.0-del02". */
const char *device_info_fw_version(void);

/** @return Last reset cause bits (enum in <zephyr/drivers/hwinfo.h>), read at init. */
uint32_t device_info_reset_cause(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_DEVICE_INFO_H_ */
