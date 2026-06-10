/*
 * improv.h - Improv-WiFi protocol core (transport agnostic).
 *
 * A C port of the official C++ SDK (github.com/improv-wifi/sdk-cpp). It contains
 * only the wire-format logic: enums, checksums, the streaming serial parser, the
 * RPC payload parser, and the response/serial-frame builders. It has no
 * dependency on Zephyr, Wi-Fi, or any transport so it can be unit tested or
 * reused as-is.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IMPROV_H_
#define IMPROV_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMPROV_SERIAL_VERSION 1

/* Wi-Fi field limits (1 length byte each, plus our buffer ceilings). */
#define IMPROV_MAX_SSID_LEN     32
#define IMPROV_MAX_PASSWORD_LEN 64

/* Capability bitmask advertised over BLE. */
#define IMPROV_CAPABILITY_IDENTIFY 0x01

/* Largest serial frame: 6 (IMPROV) + 1 ver + 1 type + 1 len + 255 payload + 1 cksum. */
#define IMPROV_SERIAL_BUFFER_SIZE 265

enum improv_error {
	IMPROV_ERROR_NONE = 0x00,
	IMPROV_ERROR_INVALID_RPC = 0x01,
	IMPROV_ERROR_UNKNOWN_RPC = 0x02,
	IMPROV_ERROR_UNABLE_TO_CONNECT = 0x03,
	IMPROV_ERROR_NOT_AUTHORIZED = 0x04,
	IMPROV_ERROR_UNKNOWN = 0xFF,
};

enum improv_state {
	IMPROV_STATE_STOPPED = 0x00,
	IMPROV_STATE_AWAITING_AUTHORIZATION = 0x01,
	IMPROV_STATE_AUTHORIZED = 0x02,
	IMPROV_STATE_PROVISIONING = 0x03,
	IMPROV_STATE_PROVISIONED = 0x04,
};

enum improv_command {
	IMPROV_CMD_UNKNOWN = 0x00,
	IMPROV_CMD_WIFI_SETTINGS = 0x01,
	/* 0x02 is both IDENTIFY and GET_CURRENT_STATE in the protocol. Over serial
	 * the client sends it (with zero payload) to request the current state.
	 */
	IMPROV_CMD_GET_CURRENT_STATE = 0x02,
	IMPROV_CMD_GET_DEVICE_INFO = 0x03,
	IMPROV_CMD_GET_WIFI_NETWORKS = 0x04,
	IMPROV_CMD_BAD_CHECKSUM = 0xFF,
};

enum improv_serial_type {
	IMPROV_TYPE_CURRENT_STATE = 0x01,
	IMPROV_TYPE_ERROR_STATE = 0x02,
	IMPROV_TYPE_RPC = 0x03,
	IMPROV_TYPE_RPC_RESPONSE = 0x04,
};

/* A parsed RPC command. ssid/password are only populated for WIFI_SETTINGS and
 * are NUL-terminated for convenience.
 */
struct improv_rpc {
	enum improv_command command;
	uint8_t ssid[IMPROV_MAX_SSID_LEN + 1];
	uint8_t ssid_len;
	uint8_t password[IMPROV_MAX_PASSWORD_LEN + 1];
	uint8_t password_len;
};

/* A length-prefixed string used when building RPC responses. */
struct improv_str {
	const uint8_t *data;
	uint8_t len;
};

/* Streaming parser state for the serial transport. Zero-initialize before use. */
struct improv_serial_parser {
	uint8_t buffer[IMPROV_SERIAL_BUFFER_SIZE];
	size_t pos;
};

enum improv_parse_result {
	IMPROV_PARSE_INCOMPLETE = 0, /* Need more bytes. */
	IMPROV_PARSE_RPC_READY,      /* A complete RPC frame was parsed into *cmd. */
	IMPROV_PARSE_ERROR,          /* Frame error; *err holds the improv error. */
};

/* 8-bit additive checksum of the first @len bytes of @data. */
uint8_t improv_checksum(const uint8_t *data, size_t len);

/*
 * Parse an RPC payload (command byte, data-length byte, then payload).
 * @check_checksum: if true, the final byte is validated as a checksum (BLE).
 *                  if false, no checksum is present (serial, already validated).
 * Returns true on a well-formed command; on failure returns false and sets
 * out->command to IMPROV_CMD_UNKNOWN or IMPROV_CMD_BAD_CHECKSUM.
 */
bool improv_parse_rpc(const uint8_t *data, size_t length, bool check_checksum,
		      struct improv_rpc *out);

/*
 * Feed one received byte to the streaming serial parser.
 * Returns IMPROV_PARSE_RPC_READY with *cmd filled when a full RPC frame arrives,
 * IMPROV_PARSE_ERROR with *err set on a checksum/format error, or
 * IMPROV_PARSE_INCOMPLETE otherwise.
 */
enum improv_parse_result improv_serial_feed(struct improv_serial_parser *p, uint8_t byte,
					    struct improv_rpc *cmd, enum improv_error *err);

/*
 * Build an RPC response body: command, total data length, then each string
 * length-prefixed. If @add_checksum, an 8-bit checksum byte is appended.
 * Returns the number of bytes written to @out, or 0 if @out_size is too small.
 */
size_t improv_build_rpc(enum improv_command command, const struct improv_str *strings,
			size_t n_strings, bool add_checksum, uint8_t *out, size_t out_size);

/*
 * Wrap a payload in a complete serial frame:
 *   "IMPROV" + version + type + length + payload + checksum.
 * Returns total bytes written to @out, or 0 if @out_size is too small.
 */
size_t improv_wrap_serial(enum improv_serial_type type, const uint8_t *payload,
			  size_t payload_len, uint8_t *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* IMPROV_H_ */
