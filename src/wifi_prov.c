/*
 * wifi_prov.c - Zephyr Wi-Fi management glue for improv.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_prov.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/net/wifi_mgmt.h>

#if defined(CONFIG_WIFI_CREDENTIALS)
#include <zephyr/net/wifi_credentials.h>
#endif

#include "improv_handler.h"

LOG_MODULE_REGISTER(wifi_prov, LOG_LEVEL_INF);

#define WIFI_EVENTS                                                                                \
	(NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT |                         \
	 NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE)
#define IPV4_EVENTS (NET_EVENT_IPV4_ADDR_ADD)

static struct net_mgmt_event_callback wifi_cb;
static struct net_mgmt_event_callback ipv4_cb;

static wifi_prov_scan_result_cb scan_cb;
static wifi_prov_scan_done_cb scan_done_cb;

/* Credentials of the in-flight client request, persisted once it connects. */
static struct {
	uint8_t ssid[IMPROV_MAX_SSID_LEN + 1];
	uint8_t ssid_len;
	uint8_t psk[IMPROV_MAX_PASSWORD_LEN + 1];
	uint8_t psk_len;
	bool pending;
} attempt;

static struct net_if *wifi_iface(void)
{
	struct net_if *iface = net_if_get_wifi_sta();

	return iface ? iface : net_if_get_first_wifi();
}

#if defined(CONFIG_WIFI_CREDENTIALS)
static void save_credentials(void)
{
	enum wifi_security_type type =
		attempt.psk_len ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE;

	/* Single-slot provisioning: replace any previous network. */
	(void)wifi_credentials_delete_all();

	int err = wifi_credentials_set_personal((const char *)attempt.ssid, attempt.ssid_len,
						type, NULL, 0, (const char *)attempt.psk,
						attempt.psk_len, 0, 0, 0);
	if (err) {
		LOG_WRN("failed to persist credentials: %d", err);
	} else {
		LOG_INF("credentials stored for '%s'", attempt.ssid);
	}
}
#endif

static void wifi_event(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
		       struct net_if *iface)
{
	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *st = (const struct wifi_status *)cb->info;
		bool ok = (st != NULL && st->status == 0);

		if (ok && attempt.pending) {
#if defined(CONFIG_WIFI_CREDENTIALS)
			save_credentials();
#endif
		}
		if (!ok) {
			attempt.pending = false;
		}
		improv_handler_on_connect_result(ok);
		break;
	}
	case NET_EVENT_WIFI_DISCONNECT_RESULT:
		LOG_INF("Wi-Fi disconnected");
		break;
	case NET_EVENT_WIFI_SCAN_RESULT: {
		const struct wifi_scan_result *entry =
			(const struct wifi_scan_result *)cb->info;

		if (scan_cb && entry->ssid_length > 0) {
			scan_cb((const char *)entry->ssid, entry->rssi,
				entry->security != WIFI_SECURITY_TYPE_NONE);
		}
		break;
	}
	case NET_EVENT_WIFI_SCAN_DONE:
		if (scan_done_cb) {
			scan_done_cb();
		}
		scan_cb = NULL;
		scan_done_cb = NULL;
		break;
	default:
		break;
	}
}

static void ipv4_event(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
		       struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	if (mgmt_event != NET_EVENT_IPV4_ADDR_ADD) {
		return;
	}

	char ip[NET_IPV4_ADDR_LEN];

	if (wifi_prov_get_ip(ip, sizeof(ip))) {
		attempt.pending = false;
		improv_handler_on_got_ip(ip);
	}
}

void wifi_prov_init(void)
{
	net_mgmt_init_event_callback(&wifi_cb, wifi_event, WIFI_EVENTS);
	net_mgmt_add_event_callback(&wifi_cb);

	net_mgmt_init_event_callback(&ipv4_cb, ipv4_event, IPV4_EVENTS);
	net_mgmt_add_event_callback(&ipv4_cb);

#if defined(CONFIG_WIFI_CREDENTIALS)
	/* Auto-connect to a stored network on boot, if any. */
	struct net_if *iface = wifi_iface();

	if (iface && !wifi_credentials_is_empty()) {
		LOG_INF("connecting using stored credentials");
		int err = net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0);

		if (err) {
			LOG_WRN("connect-stored request failed: %d", err);
		}
	}
#endif
}

bool wifi_prov_is_connected(void)
{
	struct net_if *iface = wifi_iface();
	struct wifi_iface_status status = { 0 };

	if (!iface) {
		return false;
	}

	if (net_mgmt(NET_REQUEST_WIFI_IFACE_STATUS, iface, &status, sizeof(status))) {
		return false;
	}

	return status.state >= WIFI_STATE_ASSOCIATED;
}

bool wifi_prov_get_ip(char *buf, size_t buf_len)
{
	struct net_if *iface = wifi_iface();

	if (!iface) {
		return false;
	}

	struct net_in_addr *addr = net_if_ipv4_get_global_addr(iface, NET_ADDR_PREFERRED);

	if (!addr) {
		return false;
	}

	return net_addr_ntop(NET_AF_INET, addr, buf, buf_len) != NULL;
}

int wifi_prov_connect(const uint8_t *ssid, uint8_t ssid_len, const uint8_t *psk, uint8_t psk_len)
{
	struct net_if *iface = wifi_iface();

	if (!iface) {
		LOG_ERR("no Wi-Fi interface");
		return -ENODEV;
	}

	if (ssid_len == 0 || ssid_len > IMPROV_MAX_SSID_LEN) {
		return -EINVAL;
	}

	/* Remember the attempt so we can persist it once connected. */
	memcpy(attempt.ssid, ssid, ssid_len);
	attempt.ssid[ssid_len] = '\0';
	attempt.ssid_len = ssid_len;
	memcpy(attempt.psk, psk, psk_len);
	attempt.psk[psk_len] = '\0';
	attempt.psk_len = psk_len;
	attempt.pending = true;

	struct wifi_connect_req_params params = {
		.ssid = attempt.ssid,
		.ssid_length = ssid_len,
		.psk = psk_len ? attempt.psk : NULL,
		.psk_length = psk_len,
		.security = psk_len ? WIFI_SECURITY_TYPE_PSK : WIFI_SECURITY_TYPE_NONE,
		.channel = WIFI_CHANNEL_ANY,
		.band = WIFI_FREQ_BAND_UNKNOWN,
		.mfp = WIFI_MFP_OPTIONAL,
		.timeout = SYS_FOREVER_MS,
	};

	LOG_INF("connecting to '%s'", attempt.ssid);

	int err = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params, sizeof(params));

	if (err) {
		attempt.pending = false;
		LOG_ERR("connect request failed: %d", err);
	}

	return err;
}

int wifi_prov_scan(wifi_prov_scan_result_cb result_cb, wifi_prov_scan_done_cb done_cb)
{
	struct net_if *iface = wifi_iface();

	if (!iface) {
		return -ENODEV;
	}

	scan_cb = result_cb;
	scan_done_cb = done_cb;

	int err = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);

	if (err) {
		scan_cb = NULL;
		scan_done_cb = NULL;
		LOG_ERR("scan request failed: %d", err);
	}

	return err;
}
