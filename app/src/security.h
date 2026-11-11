/*
 * Zephyr_A - security: device security posture and entropy (part 8).
 *
 * - Enables the SAR ADC noise source of the ESP32-C6 hardware RNG for the
 *   whole run time (Espressif: only then are the RNG outputs true random
 *   numbers when Wi-Fi/BT/802.15.4 are off). Zephyr's start-up code enables it
 *   only while the application is being loaded and disables it again before
 *   main. TLS key exchange (ECDHE) and stack canaries depend on it.
 * - Reads the security-relevant eFuses once (secure boot, flash encryption,
 *   JTAG, UART download mode) and reports them in the log and in the
 *   Sparkplug NBIRTH ("Properties/Security"), so a development device can be
 *   told apart from a hardened one (IEC 62443-4-2 CR 3.x / CRA Annex I 1.b).
 *
 * Owner: none (called once from main before the other modules start).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_A_SECURITY_H_
#define ZEPHYR_A_SECURITY_H_

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Enable the entropy source and read/log the security posture. */
int app_security_init(void);

/**
 * @return Short posture string for NBIRTH, e.g.
 *         "sb=0 fe=0 jtag=1 dl=1 rng=sar" (max 47 characters).
 *         sb = secure boot (ROM), fe = flash encryption, jtag = JTAG usable,
 *         dl = UART/USB download mode usable, rng = entropy source.
 */
const char *app_security_posture(void);

/** @return true if secure boot and flash encryption are on and JTAG is off. */
bool app_security_is_hardened(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_A_SECURITY_H_ */
