/*
 * Improv-WiFi serial provisioning sample.
 *
 * Brings up improv over the console UART (the default transport). Provision with
 * the Web Serial installer at https://www.improv-wifi.com/serial/.
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
