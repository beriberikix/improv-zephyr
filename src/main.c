/*
 * main.c - Improv-WiFi provisioning for Zephyr.
 *
 * Wires the Wi-Fi management layer, the improv state machine, and the enabled
 * transports together. JS-over-Serial is the default and primary path; BLE is
 * built in when CONFIG_IMPROV_BLE is set.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if defined(CONFIG_SETTINGS)
#include <zephyr/settings/settings.h>
#endif

#include "improv_handler.h"
#include "improv_transport.h"
#include "wifi_prov.h"

LOG_MODULE_REGISTER(improv_main, LOG_LEVEL_INF);

int main(void)
{
	LOG_INF("Improv-WiFi for Zephyr starting (board: %s)", CONFIG_BOARD);

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

	int err = improv_serial_init();

	if (err) {
		LOG_ERR("serial transport init failed: %d", err);
	}

	err = improv_ble_init();
	if (err) {
		LOG_ERR("BLE transport init failed: %d", err);
	}

	LOG_INF("Improv-WiFi ready");
	return 0;
}
