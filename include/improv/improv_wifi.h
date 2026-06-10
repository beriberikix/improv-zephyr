/*
 * improv_wifi.h - Public entry point for the improv-zephyr module.
 *
 * Most applications only need this one call. It loads any stored Wi-Fi
 * credentials, starts Wi-Fi management, and brings up whichever transports are
 * selected in Kconfig (CONFIG_IMPROV_SERIAL / CONFIG_IMPROV_BLE).
 *
 * For finer-grained control, the protocol core (<improv/improv.h>) and the
 * handler/transport internals are also available.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IMPROV_WIFI_H_
#define IMPROV_WIFI_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialise improv-wifi provisioning. Call once, typically from main().
 * Returns 0 on success or a negative errno if a transport failed to start
 * (provisioning over any transport that did start still works).
 */
int improv_wifi_init(void);

#ifdef __cplusplus
}
#endif

#endif /* IMPROV_WIFI_H_ */
