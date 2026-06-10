/*
 * improv_handler.h - Transport-agnostic improv state machine and RPC dispatch.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IMPROV_HANDLER_H_
#define IMPROV_HANDLER_H_

#include <stddef.h>
#include <stdint.h>

#include "improv.h"
#include "improv_transport.h"

/* Initialise the handler. Sets the initial state to PROVISIONED if Wi-Fi is
 * already connected (e.g. auto-connect from stored credentials), else AUTHORIZED.
 */
void improv_handler_init(void);

/* Register a transport send callback. Up to two transports (serial + BLE) may be
 * active simultaneously; all registered transports receive every message.
 */
void improv_handler_register_transport(improv_send_fn fn);

/* Current state accessor (used by the BLE transport for the state characteristic). */
enum improv_state improv_handler_get_state(void);
uint8_t improv_handler_get_error(void);

/* Dispatch a fully-parsed RPC command (used by the serial transport). */
void improv_handler_on_command(const struct improv_rpc *cmd);

/* Emit an error-state message (e.g. a serial frame checksum failure). */
void improv_handler_report_error(enum improv_error err);

/* Parse a raw RPC payload (command, len, ..., checksum) then dispatch it (used by
 * the BLE transport, whose characteristic carries the checksum-suffixed body).
 */
void improv_handler_on_rpc_bytes(const uint8_t *data, size_t len);

/* Wi-Fi result hooks, called from the Wi-Fi management layer. */
void improv_handler_on_connect_result(bool success);
void improv_handler_on_got_ip(const char *ip_str);

#endif /* IMPROV_HANDLER_H_ */
