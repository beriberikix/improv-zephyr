/*
 * improv_wifi.c - Module entry point: wire Wi-Fi, the state machine, and the
 * enabled transports together behind a single improv_wifi_init() call.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <improv/improv_wifi.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include "improv_handler.h"
#include "improv_transport.h"
#include "wifi_prov.h"

LOG_MODULE_REGISTER(improv_wifi, LOG_LEVEL_INF);

int improv_wifi_init(void)
{
	int err;
	int ret = 0;

#if defined(CONFIG_SETTINGS)
	/* Load persisted Wi-Fi credentials before attempting connect-stored. */
	settings_subsys_init();
	settings_load();
#endif

	/* Registers net_mgmt callbacks and starts a connect attempt with any
	 * stored credentials.
	 */
	wifi_prov_init();

	/* Initial improv state reflects whether Wi-Fi is already up. */
	improv_handler_init();

	err = improv_serial_init();
	if (err) {
		LOG_ERR("serial transport init failed: %d", err);
		ret = err;
	}

	err = improv_ble_init();
	if (err) {
		LOG_ERR("BLE transport init failed: %d", err);
		ret = err;
	}

	LOG_INF("improv-zephyr ready (board: %s)", CONFIG_BOARD);
	return ret;
}
