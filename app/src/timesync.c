/*
 * Zephyr_A - timesync: UTC from SNTP, kept as an offset to the uptime clock.
 *
 *   utc_ms = k_uptime_get() + offset_ms
 *
 * The offset is measured with SNTP (server transmit time + the one-way delay
 * estimated from the round trip) and stored under a spinlock. SYS_CLOCK_REALTIME is set as well, because
 * Mbed TLS uses time() for the certificate date check
 * (CONFIG_MBEDTLS_HAVE_TIME_DATE, enabled in part 7).
 *
 * Plausibility: an answer before 2026-01-01 is rejected (broken server or a
 * spoofed reply), so a bad SNTP reply cannot make expired certificates look
 * valid or put 1970 timestamps into the data.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdio.h>
#include <time.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/sntp.h>
#include <zephyr/sys/clock.h>

#include "timesync.h"

LOG_MODULE_REGISTER(timesync, CONFIG_APP_LOG_LEVEL);

/** 2026-01-01T00:00:00Z: no valid answer can be older than this firmware. */
#define MIN_VALID_UTC_S 1767225600ULL

static struct k_spinlock lock;
static bool valid;
static int64_t offset_ms;       /* utc_ms - uptime_ms */
static int64_t last_sync_uptime_ms;
static uint32_t sync_count;

/* UTC ms -> "YYYY-MM-DDThh:mm:ss.mmmZ" without libc (days-to-civil algorithm
 * by H. Hinnant; gmtime_r is not available in every C library).
 */
static void format_utc(int64_t utc_ms, char *buf, size_t size)
{
	int64_t secs = utc_ms / 1000;
	int64_t days = secs / 86400;
	int64_t rem = secs % 86400;
	int64_t z = days + 719468;
	int64_t era = z / 146097;
	int64_t doe = z - era * 146097;
	int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int64_t mp = (5 * doy + 2) / 153;
	int day = (int)(doy - (153 * mp + 2) / 5 + 1);
	int month = (int)((mp < 10) ? mp + 3 : mp - 9);
	int year = (int)(yoe + era * 400 + ((month <= 2) ? 1 : 0));

	(void)snprintf(buf, size, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", year, month, day,
		       (int)(rem / 3600), (int)((rem / 60) % 60), (int)(rem % 60),
		       (int)(utc_ms % 1000));
}

static int query(const char *server, uint32_t timeout_ms, int64_t *utc_ms, int64_t *at_uptime_ms,
		 uint32_t *uncertainty_ms)
{
	struct sntp_time ts;
	int64_t t0 = k_uptime_get();
	int ret;

	ret = sntp_simple(server, timeout_ms, &ts);
	if (ret != 0) {
		LOG_WRN("SNTP %s failed (%d)", server, ret);
		return ret;
	}
	if (ts.seconds < MIN_VALID_UTC_S) {
		LOG_ERR("SNTP %s: implausible time %llu s - rejected", server,
			(unsigned long long)ts.seconds);
		return -EBADMSG;
	}

	/* Server transmit time (fraction = 1/2^32 s) + estimated one-way delay
	 * = UTC at the moment the reply arrived, i.e. now.
	 * (CONFIG_SNTP_UNCERTAINTY is not used: in Zephyr 4.4 it measures
	 * against SYS_CLOCK_REALTIME and returns the fraction in microseconds.)
	 */
	int32_t delay_us = (int32_t)ts.rsp_delay_us;

	if ((delay_us < 0) || (delay_us > 10000000)) {
		delay_us = 0; /* implausible estimate (e.g. clock jump): ignore */
	}
	*utc_ms = (int64_t)(ts.seconds * 1000ULL) +
		  (int64_t)(((uint64_t)ts.fraction * 1000ULL) >> 32) + (delay_us / 1000);
	*at_uptime_ms = k_uptime_get();
	*uncertainty_ms = (uint32_t)(delay_us / 1000) + 1U;
	LOG_DBG("SNTP %s: %lld ms after %lld ms", server, *utc_ms, k_uptime_get() - t0);
	return 0;
}

int timesync_init(void)
{
	LOG_INF("SNTP servers: %s%s%s, resync every %d h", CONFIG_APP_TIMESYNC_SERVER1,
		(CONFIG_APP_TIMESYNC_SERVER2[0] != '\0') ? ", " : "", CONFIG_APP_TIMESYNC_SERVER2,
		CONFIG_APP_TIMESYNC_RESYNC_H);
	return 0;
}

int timesync_sync(uint32_t timeout_ms)
{
	static const char *const servers[] = {CONFIG_APP_TIMESYNC_SERVER1,
					      CONFIG_APP_TIMESYNC_SERVER2};
	int64_t utc_ms = 0;
	int64_t at_uptime_ms = 0;
	uint32_t uncertainty_ms = 0;
	const char *used = NULL;
	int ret = -ENOENT;

	for (size_t i = 0; i < ARRAY_SIZE(servers); i++) {
		if (servers[i][0] == '\0') {
			continue;
		}
		ret = query(servers[i], timeout_ms, &utc_ms, &at_uptime_ms, &uncertainty_ms);
		if (ret == 0) {
			used = servers[i];
			break;
		}
	}
	if (ret != 0) {
		return ret;
	}

	int64_t new_offset = utc_ms - at_uptime_ms;
	int64_t old_offset;
	bool was_valid;
	int64_t now_utc_ms = new_offset + k_uptime_get();
	struct timespec rt = {
		.tv_sec = (time_t)(now_utc_ms / 1000),
		.tv_nsec = (long)((now_utc_ms % 1000) * 1000000L),
	};

	K_SPINLOCK(&lock) {
		was_valid = valid;
		old_offset = offset_ms;
		offset_ms = new_offset;
		valid = true;
		last_sync_uptime_ms = k_uptime_get();
		sync_count++;
	}

	ret = sys_clock_settime(SYS_CLOCK_REALTIME, &rt);
	if (ret != 0) {
		LOG_WRN("sys_clock_settime failed (%d): TLS date check may fail", ret);
	}

	char when[48];

	format_utc(utc_ms, when, sizeof(when));
	if (was_valid) {
		LOG_INF("Time resynced via %s: %s (drift %+lld ms, +/- %u ms)", used, when,
			new_offset - old_offset, uncertainty_ms);
	} else {
		LOG_INF("Time synced via %s: %s (+/- %u ms)", used, when, uncertainty_ms);
	}
	return 0;
}

bool timesync_is_valid(void)
{
	bool v;

	K_SPINLOCK(&lock) {
		v = valid;
	}
	return v;
}

bool timesync_needs_resync(void)
{
	bool need;

	K_SPINLOCK(&lock) {
		need = !valid || ((CONFIG_APP_TIMESYNC_RESYNC_H > 0) &&
				  ((k_uptime_get() - last_sync_uptime_ms) >=
				   (CONFIG_APP_TIMESYNC_RESYNC_H * 3600LL * 1000LL)));
	}
	return need;
}

int64_t timesync_now_ms(void)
{
	return timesync_uptime_to_utc_ms(k_uptime_get());
}

int64_t timesync_uptime_to_utc_ms(int64_t uptime_ms)
{
	int64_t utc = 0;

	K_SPINLOCK(&lock) {
		if (valid) {
			utc = uptime_ms + offset_ms;
		}
	}
	return utc;
}
