/*
 * improv_transport.h - Interface between the protocol handler and transports.
 *
 * A transport (serial, BLE) registers a send callback with the handler. The
 * handler emits "logical" messages as a (type, payload) pair; each transport
 * renders them appropriately: the serial transport wraps them in an IMPROV
 * frame, the BLE transport notifies the matching characteristic.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IMPROV_TRANSPORT_H_
#define IMPROV_TRANSPORT_H_

#include <stddef.h>
#include <stdint.h>

#include <improv/improv.h>

/* Sends a logical improv message. @payload is the un-framed body:
 *  - IMPROV_TYPE_CURRENT_STATE / IMPROV_TYPE_ERROR_STATE: a single state/error byte
 *  - IMPROV_TYPE_RPC_RESPONSE: an RPC response body (command, len, strings...)
 */
typedef void (*improv_send_fn)(enum improv_serial_type type, const uint8_t *payload,
			       size_t payload_len);

/* Transport initialisers. Each registers its send callback with the handler.
 * Stubbed out by the build when the corresponding transport is disabled.
 */
#if defined(CONFIG_IMPROV_SERIAL)
int improv_serial_init(void);
#else
static inline int improv_serial_init(void)
{
	return 0;
}
#endif

#if defined(CONFIG_IMPROV_BLE)
int improv_ble_init(void);
#else
static inline int improv_ble_init(void)
{
	return 0;
}
#endif

#endif /* IMPROV_TRANSPORT_H_ */
