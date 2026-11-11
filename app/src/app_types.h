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

/** Max number of values (channels) in one measurement. */
#define APP_MAX_CHANNELS 4

/** Channel id 0 is the internal temperature sensor; Modbus registers follow. */
#define APP_CHANNEL_DIE_TEMP     0
#define APP_CHANNEL_MODBUS_FIRST 1

/** Quality of one value (mapped to Sparkplug quality in part 7). */
enum value_quality {
	QUALITY_GOOD = 0, /**< value read successfully                          */
	QUALITY_BAD,      /**< read failed (timeout, CRC, exception, driver)    */
	QUALITY_STALE,    /**< not read this cycle (Modbus backoff after errors) */
};

/** One value of one channel. 8 bytes. */
struct channel_value {
	int32_t value_milli; /**< value x 1000, e.g. m°C; 0 if quality != GOOD */
	uint8_t channel;     /**< channel id (APP_CHANNEL_*)                     */
	uint8_t quality;     /**< enum value_quality                             */
};

/**
 * One measurement = all channels read in one sampling cycle. 56 bytes,
 * copied by value through k_msgq.
 */
struct measurement {
	int64_t uptime_ms;    /**< k_uptime_get() at the start of the cycle         */
	int64_t timestamp_ms; /**< UTC ms since 1970; 0 until time sync (part 7)    */
	uint32_t seq;         /**< measurement counter since boot                   */
	uint8_t count;        /**< number of valid entries in values[]              */
	uint8_t quality;      /**< BAD if any value is BAD, else STALE if any is STALE */
	bool simulated;       /**< true if CONFIG_APP_SENSOR_SIMULATION generated it */
	struct channel_value values[APP_MAX_CHANNELS];
};

/** A batch of measurements sent as ONE Sparkplug B DDATA message. */
struct measurement_batch {
	uint8_t count;
	struct measurement items[CONFIG_APP_BATCH_MAX];
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

/** @return Short name of a quality value for logging. */
static inline const char *value_quality_str(uint8_t q)
{
	switch (q) {
	case QUALITY_GOOD:
		return "good";
	case QUALITY_BAD:
		return "BAD";
	case QUALITY_STALE:
		return "stale";
	default:
		return "?";
	}
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_APP_TYPES_H_ */
