/*
 * improv_handler.c - Improv state machine and RPC command dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "improv_handler.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <improv/improv.h>
#include "wifi_prov.h"

LOG_MODULE_REGISTER(improv_handler, LOG_LEVEL_INF);

#define MAX_TRANSPORTS 2

/* If DHCP has not produced an IP this long after association, send the Wi-Fi
 * settings response anyway (using the configured URL without IP substitution).
 */
#define IP_WAIT_TIMEOUT K_SECONDS(10)

static improv_send_fn transports[MAX_TRANSPORTS];
static size_t transport_count;

static enum improv_state current_state = IMPROV_STATE_AUTHORIZED;
static enum improv_error current_error = IMPROV_ERROR_NONE;

/* Set while a client-initiated WIFI_SETTINGS flow is awaiting its IP so we know
 * to emit the RPC response (vs. a background auto-connect on boot).
 */
static bool awaiting_provision_result;

static void ip_timeout_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(ip_timeout_work, ip_timeout_handler);

/* ---- low level send helpers ---------------------------------------------- */

static void broadcast(enum improv_serial_type type, const uint8_t *payload, size_t len)
{
	for (size_t i = 0; i < transport_count; i++) {
		transports[i](type, payload, len);
	}
}

static void send_current_state(void)
{
	uint8_t b = (uint8_t)current_state;

	broadcast(IMPROV_TYPE_CURRENT_STATE, &b, 1);
}

static void send_error(enum improv_error err)
{
	uint8_t b = (uint8_t)err;

	current_error = err;
	broadcast(IMPROV_TYPE_ERROR_STATE, &b, 1);
}

static void set_state(enum improv_state state)
{
	current_state = state;
	send_current_state();
}

/* ---- RPC responses ------------------------------------------------------- */

/* Build the redirect URL from CONFIG_IMPROV_REDIRECT_URL, substituting "{ip}"
 * or, when the configured URL is empty, defaulting to "http://<ip>/".
 */
static size_t build_redirect_url(char *buf, size_t buf_len)
{
	const char *tmpl = CONFIG_IMPROV_REDIRECT_URL;
	char ip[16];
	bool have_ip = wifi_prov_get_ip(ip, sizeof(ip));

	/* A single space means "explicitly no URL". */
	if (tmpl[0] == ' ' && tmpl[1] == '\0') {
		return 0;
	}

	if (tmpl[0] == '\0') {
		if (!have_ip) {
			return 0;
		}
		return snprintf(buf, buf_len, "http://%s/", ip);
	}

	const char *token = strstr(tmpl, "{ip}");

	if (token == NULL) {
		return snprintf(buf, buf_len, "%s", tmpl);
	}

	/* Substitute the first {ip} occurrence. */
	size_t prefix = (size_t)(token - tmpl);

	return snprintf(buf, buf_len, "%.*s%s%s", (int)prefix, tmpl,
			have_ip ? ip : "", token + strlen("{ip}"));
}

static void send_wifi_settings_response(void)
{
	char url[96];
	uint8_t payload[128];
	struct improv_str strings[1];
	size_t n = 0;

	size_t url_len = build_redirect_url(url, sizeof(url));

	if (url_len > 0 && url_len < sizeof(url)) {
		strings[0].data = (const uint8_t *)url;
		strings[0].len = (uint8_t)url_len;
		n = 1;
	}

	size_t len = improv_build_rpc(IMPROV_CMD_WIFI_SETTINGS, strings, n, false,
				      payload, sizeof(payload));

	if (len > 0) {
		broadcast(IMPROV_TYPE_RPC_RESPONSE, payload, len);
	}
}

static void send_device_info(void)
{
	const char *fw_name = CONFIG_IMPROV_FIRMWARE_NAME;
	const char *fw_version = CONFIG_IMPROV_FIRMWARE_VERSION;
	const char *chip = CONFIG_BOARD;
	const char *dev_name = CONFIG_IMPROV_DEVICE_NAME;

	struct improv_str strings[4] = {
		{ (const uint8_t *)fw_name, (uint8_t)strlen(fw_name) },
		{ (const uint8_t *)fw_version, (uint8_t)strlen(fw_version) },
		{ (const uint8_t *)chip, (uint8_t)strlen(chip) },
		{ (const uint8_t *)dev_name, (uint8_t)strlen(dev_name) },
	};
	uint8_t payload[160];

	size_t len = improv_build_rpc(IMPROV_CMD_GET_DEVICE_INFO, strings,
				      ARRAY_SIZE(strings), false, payload, sizeof(payload));

	if (len > 0) {
		broadcast(IMPROV_TYPE_RPC_RESPONSE, payload, len);
	}
}

/* ---- Wi-Fi scan -> RPC responses ----------------------------------------- */

static void scan_result(const char *ssid, int8_t rssi, bool secured)
{
	char rssi_str[8];
	uint8_t payload[64];

	snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);

	struct improv_str strings[3] = {
		{ (const uint8_t *)ssid, (uint8_t)strlen(ssid) },
		{ (const uint8_t *)rssi_str, (uint8_t)strlen(rssi_str) },
		{ (const uint8_t *)(secured ? "YES" : "NO"), (uint8_t)(secured ? 3 : 2) },
	};

	size_t len = improv_build_rpc(IMPROV_CMD_GET_WIFI_NETWORKS, strings,
				      ARRAY_SIZE(strings), false, payload, sizeof(payload));

	if (len > 0) {
		broadcast(IMPROV_TYPE_RPC_RESPONSE, payload, len);
	}
}

static void scan_done(void)
{
	/* An empty RPC response terminates the network list. */
	uint8_t payload[8];
	size_t len = improv_build_rpc(IMPROV_CMD_GET_WIFI_NETWORKS, NULL, 0, false,
				      payload, sizeof(payload));

	if (len > 0) {
		broadcast(IMPROV_TYPE_RPC_RESPONSE, payload, len);
	}
}

/* ---- command dispatch ---------------------------------------------------- */

static void handle_wifi_settings(const struct improv_rpc *cmd)
{
	LOG_INF("WIFI_SETTINGS for SSID '%s'", (const char *)cmd->ssid);

	set_state(IMPROV_STATE_PROVISIONING);
	current_error = IMPROV_ERROR_NONE;
	awaiting_provision_result = true;

	int ret = wifi_prov_connect(cmd->ssid, cmd->ssid_len, cmd->password,
				    cmd->password_len);

	if (ret != 0) {
		LOG_ERR("connect request failed: %d", ret);
		awaiting_provision_result = false;
		send_error(IMPROV_ERROR_UNABLE_TO_CONNECT);
		set_state(IMPROV_STATE_AUTHORIZED);
	}
}

void improv_handler_on_command(const struct improv_rpc *cmd)
{
	switch (cmd->command) {
	case IMPROV_CMD_WIFI_SETTINGS:
		handle_wifi_settings(cmd);
		break;

	case IMPROV_CMD_GET_CURRENT_STATE:
		send_current_state();
		/* If already provisioned, (re)send the redirect URL result. */
		if (current_state == IMPROV_STATE_PROVISIONED) {
			send_wifi_settings_response();
		}
		break;

	case IMPROV_CMD_GET_DEVICE_INFO:
		send_device_info();
		break;

	case IMPROV_CMD_GET_WIFI_NETWORKS:
		if (wifi_prov_scan(scan_result, scan_done) != 0) {
			scan_done();
		}
		break;

	case IMPROV_CMD_BAD_CHECKSUM:
		send_error(IMPROV_ERROR_INVALID_RPC);
		break;

	default:
		LOG_WRN("unknown command 0x%02x", cmd->command);
		send_error(IMPROV_ERROR_UNKNOWN_RPC);
		break;
	}
}

void improv_handler_on_rpc_bytes(const uint8_t *data, size_t len)
{
	struct improv_rpc cmd;

	/* BLE carries the checksum-suffixed RPC body. */
	improv_parse_rpc(data, len, true, &cmd);
	improv_handler_on_command(&cmd);
}

/* ---- Wi-Fi result hooks -------------------------------------------------- */

void improv_handler_on_connect_result(bool success)
{
	if (success) {
		LOG_INF("Wi-Fi connected");
		set_state(IMPROV_STATE_PROVISIONED);

		if (awaiting_provision_result) {
			/* Wait for DHCP to provide an IP for the redirect URL,
			 * but don't wait forever.
			 */
			k_work_reschedule(&ip_timeout_work, IP_WAIT_TIMEOUT);
		}
	} else {
		LOG_WRN("Wi-Fi connect failed");
		if (awaiting_provision_result) {
			awaiting_provision_result = false;
			send_error(IMPROV_ERROR_UNABLE_TO_CONNECT);
		}
		set_state(IMPROV_STATE_AUTHORIZED);
	}
}

void improv_handler_on_got_ip(const char *ip_str)
{
	LOG_INF("Got IP %s", ip_str);

	if (current_state != IMPROV_STATE_PROVISIONED) {
		set_state(IMPROV_STATE_PROVISIONED);
	}

	if (awaiting_provision_result) {
		awaiting_provision_result = false;
		k_work_cancel_delayable(&ip_timeout_work);
		send_wifi_settings_response();
	}
}

static void ip_timeout_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (awaiting_provision_result) {
		LOG_WRN("DHCP timed out; sending response without IP");
		awaiting_provision_result = false;
		send_wifi_settings_response();
	}
}

void improv_handler_report_error(enum improv_error err)
{
	send_error(err);
}

void improv_handler_register_transport(improv_send_fn fn)
{
	if (transport_count < MAX_TRANSPORTS) {
		transports[transport_count++] = fn;
	}
}

enum improv_state improv_handler_get_state(void)
{
	return current_state;
}

uint8_t improv_handler_get_error(void)
{
	return (uint8_t)current_error;
}

void improv_handler_init(void)
{
	if (wifi_prov_is_connected()) {
		current_state = IMPROV_STATE_PROVISIONED;
	} else {
		current_state = IMPROV_STATE_AUTHORIZED;
	}
	current_error = IMPROV_ERROR_NONE;
}
