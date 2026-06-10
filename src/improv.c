/*
 * improv.c - Improv-WiFi protocol core implementation.
 *
 * Faithful C port of github.com/improv-wifi/sdk-cpp src/improv.cpp.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <improv/improv.h>

#include <string.h>

static const uint8_t improv_header[6] = {'I', 'M', 'P', 'R', 'O', 'V'};

uint8_t improv_checksum(const uint8_t *data, size_t len)
{
	uint32_t sum = 0;

	for (size_t i = 0; i < len; i++) {
		sum += data[i];
	}

	return (uint8_t)(sum & 0xFF);
}

bool improv_parse_rpc(const uint8_t *data, size_t length, bool check_checksum,
		      struct improv_rpc *out)
{
	memset(out, 0, sizeof(*out));

	/* Need at least the command and data-length bytes. */
	if (length < 2) {
		out->command = IMPROV_CMD_UNKNOWN;
		return false;
	}

	enum improv_command command = (enum improv_command)data[0];
	uint8_t data_length = data[1];

	/* data_length counts the payload after the two header bytes, excluding
	 * the optional trailing checksum byte.
	 */
	if (data_length != length - 2 - (check_checksum ? 1 : 0)) {
		out->command = IMPROV_CMD_UNKNOWN;
		return false;
	}

	if (check_checksum) {
		uint8_t expected = data[length - 1];

		if (improv_checksum(data, length - 1) != expected) {
			out->command = IMPROV_CMD_BAD_CHECKSUM;
			return false;
		}
	}

	if (command == IMPROV_CMD_WIFI_SETTINGS) {
		uint8_t ssid_length = data[2];
		size_t ssid_start = 3;
		size_t ssid_end = ssid_start + ssid_length;

		if (ssid_end > length) {
			out->command = IMPROV_CMD_UNKNOWN;
			return false;
		}

		uint8_t pass_length = data[ssid_end];
		size_t pass_start = ssid_end + 1;
		size_t pass_end = pass_start + pass_length;

		if (pass_end > length) {
			out->command = IMPROV_CMD_UNKNOWN;
			return false;
		}

		if (ssid_length > IMPROV_MAX_SSID_LEN ||
		    pass_length > IMPROV_MAX_PASSWORD_LEN) {
			out->command = IMPROV_CMD_UNKNOWN;
			return false;
		}

		memcpy(out->ssid, &data[ssid_start], ssid_length);
		out->ssid[ssid_length] = '\0';
		out->ssid_len = ssid_length;

		memcpy(out->password, &data[pass_start], pass_length);
		out->password[pass_length] = '\0';
		out->password_len = pass_length;
	}

	out->command = command;
	return true;
}

enum improv_parse_result improv_serial_feed(struct improv_serial_parser *p, uint8_t byte,
					    struct improv_rpc *cmd, enum improv_error *err)
{
	/* Defensive: never index past the buffer. */
	if (p->pos >= IMPROV_SERIAL_BUFFER_SIZE) {
		p->pos = 0;
	}

	size_t pos = p->pos;

	p->buffer[pos] = byte;

	bool ok;

	if (pos < 6) {
		ok = (byte == improv_header[pos]);
	} else if (pos == 6) {
		ok = (byte == IMPROV_SERIAL_VERSION);
	} else if (pos <= 8) {
		/* pos 7 = type, pos 8 = data length: always accepted. */
		ok = true;
	} else {
		uint8_t data_len = p->buffer[8];

		if (pos < 9 + (size_t)data_len) {
			/* Still reading the payload. */
			ok = true;
		} else {
			/* This byte is the checksum (pos == 9 + data_len). */
			uint8_t calc = improv_checksum(p->buffer, pos);

			if (calc != byte) {
				p->pos = 0;
				*err = IMPROV_ERROR_INVALID_RPC;
				return IMPROV_PARSE_ERROR;
			}

			enum improv_serial_type type =
				(enum improv_serial_type)p->buffer[7];

			p->pos = 0;

			if (type == IMPROV_TYPE_RPC) {
				/* Payload begins at index 9; no embedded checksum. */
				improv_parse_rpc(&p->buffer[9], data_len, false, cmd);
				return IMPROV_PARSE_RPC_READY;
			}

			/* Non-RPC frames from the host are ignored. */
			return IMPROV_PARSE_INCOMPLETE;
		}
	}

	if (!ok) {
		p->pos = 0;
		return IMPROV_PARSE_INCOMPLETE;
	}

	p->pos = pos + 1;
	return IMPROV_PARSE_INCOMPLETE;
}

size_t improv_build_rpc(enum improv_command command, const struct improv_str *strings,
			size_t n_strings, bool add_checksum, uint8_t *out, size_t out_size)
{
	/* command(1) + datalen(1) + sum(len byte + bytes) + optional checksum(1). */
	size_t needed = 2 + (add_checksum ? 1 : 0);

	for (size_t i = 0; i < n_strings; i++) {
		needed += 1 + strings[i].len;
	}

	if (needed > out_size) {
		return 0;
	}

	out[0] = (uint8_t)command;

	size_t pos = 2;

	for (size_t i = 0; i < n_strings; i++) {
		out[pos++] = strings[i].len;
		memcpy(&out[pos], strings[i].data, strings[i].len);
		pos += strings[i].len;
	}

	out[1] = (uint8_t)(pos - 2);

	if (add_checksum) {
		out[pos] = improv_checksum(out, pos);
		pos++;
	}

	return pos;
}

size_t improv_wrap_serial(enum improv_serial_type type, const uint8_t *payload,
			  size_t payload_len, uint8_t *out, size_t out_size)
{
	/* header(6) + version(1) + type(1) + len(1) + payload + checksum(1). */
	size_t total = 10 + payload_len;

	if (payload_len > 255 || total > out_size) {
		return 0;
	}

	memcpy(out, improv_header, sizeof(improv_header));
	out[6] = IMPROV_SERIAL_VERSION;
	out[7] = (uint8_t)type;
	out[8] = (uint8_t)payload_len;
	memcpy(&out[9], payload, payload_len);
	out[9 + payload_len] = improv_checksum(out, 9 + payload_len);

	return total;
}
