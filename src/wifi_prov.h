/*
 * wifi_prov.h - Zephyr Wi-Fi management glue for improv.
 *
 * Wraps the net_mgmt Wi-Fi API: connecting with SSID/PSK, reporting connection
 * and DHCP results, scanning, and persisting credentials via the wifi_credentials
 * library. Results are delivered asynchronously to the improv handler.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WIFI_PROV_H_
#define WIFI_PROV_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Per-network result for a scan, and a terminator callback. */
typedef void (*wifi_prov_scan_result_cb)(const char *ssid, int8_t rssi, bool secured);
typedef void (*wifi_prov_scan_done_cb)(void);

/* Initialise networking event handling. Must be called once at boot. */
void wifi_prov_init(void);

/* True if the Wi-Fi interface currently has an association. */
bool wifi_prov_is_connected(void);

/* Copy the current IPv4 address as a dotted string. Returns false if none. */
bool wifi_prov_get_ip(char *buf, size_t buf_len);

/*
 * Start an asynchronous connection attempt. The result arrives via
 * improv_handler_on_connect_result(); on success the obtained IP arrives via
 * improv_handler_on_got_ip(). On success the credentials are persisted.
 * Returns 0 if the request was accepted.
 */
int wifi_prov_connect(const uint8_t *ssid, uint8_t ssid_len, const uint8_t *psk,
		      uint8_t psk_len);

/*
 * Start an asynchronous scan. @result_cb is invoked once per network and
 * @done_cb once when the scan completes. Returns 0 if the request was accepted.
 */
int wifi_prov_scan(wifi_prov_scan_result_cb result_cb, wifi_prov_scan_done_cb done_cb);

#endif /* WIFI_PROV_H_ */
