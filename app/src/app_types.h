/*
 * Zephyr_A - shared data types used between modules/threads.
 *
 * Only plain data types live here (no kernel objects). Each queue is owned
 * by the module whose thread consumes it; producers use that module's
 * *_submit_*() function. See docs/02-arkitektur.md, "Inter-task communication".
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_APP_TYPES_H_
#define ZEPHYR_A_APP_TYPES_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where a sample came from. Maps to a Sparkplug metric (name + alias). */
enum sample_source {
	SAMPLE_SRC_DIE_TEMP = 0, /**< ESP32-C6 internal temperature sensor */
	SAMPLE_SRC_MODBUS_0,     /**< First Modbus register (later part)   */
	SAMPLE_SRC_COUNT
};

/** Quality flag, sent as a Sparkplug metric property later. */
enum sample_quality {
	SAMPLE_QUALITY_GOOD = 0,
	SAMPLE_QUALITY_BAD,      /**< read failed, value invalid            */
	SAMPLE_QUALITY_NO_TIME,  /**< value ok, but UTC time not synced yet */
};

/**
 * One measurement. 24 bytes; copied by value through k_msgq.
 * Values are integers (milli-units) to avoid float formatting/encoding cost.
 */
struct sample {
	int64_t timestamp_ms; /**< UTC ms since 1970 (Sparkplug), 0 if unknown */
	int64_t uptime_ms;    /**< k_uptime_get() when sampled                 */
	int32_t value_milli;  /**< e.g. temperature in m°C                     */
	uint8_t source;       /**< enum sample_source                          */
	uint8_t quality;      /**< enum sample_quality                         */
};

/** A batch of samples sent as ONE Sparkplug B DDATA message. */
struct sample_batch {
	uint8_t count;
	struct sample samples[CONFIG_APP_BATCH_MAX];
};

/** Commands received from the cloud (Sparkplug NCMD/DCMD), decoded. */
enum app_cmd_type {
	APP_CMD_REBIRTH = 0,         /**< "Node Control/Rebirth"            */
	APP_CMD_SET_SAMPLE_INTERVAL, /**< "Config/Sample Interval" [ms]     */
	APP_CMD_SET_BATCH_SIZE,      /**< "Config/Batch Size"               */
	APP_CMD_REBOOT,              /**< "Node Control/Reboot"             */
};

struct app_cmd {
	uint8_t type;   /**< enum app_cmd_type */
	int64_t value;  /**< argument, if any  */
};

/** Runtime configuration, owned by app_logic and protected by a k_mutex. */
struct app_config {
	uint32_t sample_interval_ms;
	uint8_t batch_size;
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_APP_TYPES_H_ */
