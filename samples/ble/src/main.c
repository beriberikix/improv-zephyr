/*
 * Improv-WiFi BLE provisioning sample.
 *
 * Brings up improv over a BLE GATT service. Provision with the Web Bluetooth
 * installer at https://www.improv-wifi.com/ble/.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

#include <improv/improv_wifi.h>

int main(void)
{
	improv_wifi_init();
	return 0;
}
