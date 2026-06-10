/*
 * transport_ble.c - Improv BLE (GATT) transport.
 *
 * Exposes the improv GATT service so the Web Bluetooth installer at
 * https://www.improv-wifi.com/ble/ can provision the device. Unlike the serial
 * transport there is no IMPROV frame wrapper: each logical message maps to a
 * GATT characteristic (current state, error state, RPC command, RPC result).
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <improv/improv.h>
#include "improv_handler.h"
#include "improv_transport.h"

LOG_MODULE_REGISTER(improv_ble, LOG_LEVEL_INF);

/* Base UUID 00467768-6228-2272-4663-2774782680xx; the last byte selects the
 * service (00) and each characteristic (01..05).
 */
#define IMPROV_UUID(last) BT_UUID_128_ENCODE(0x00467768, 0x6228, 0x2272, 0x4663, 0x2774782680##last)

static const struct bt_uuid_128 service_uuid = BT_UUID_INIT_128(IMPROV_UUID(00));
static const struct bt_uuid_128 status_uuid = BT_UUID_INIT_128(IMPROV_UUID(01));
static const struct bt_uuid_128 error_uuid = BT_UUID_INIT_128(IMPROV_UUID(02));
static const struct bt_uuid_128 rpc_cmd_uuid = BT_UUID_INIT_128(IMPROV_UUID(03));
static const struct bt_uuid_128 rpc_result_uuid = BT_UUID_INIT_128(IMPROV_UUID(04));
static const struct bt_uuid_128 capabilities_uuid = BT_UUID_INIT_128(IMPROV_UUID(05));

/* esp32s3_devkitc has no simple LED, so identify is not advertised. */
static const uint8_t capabilities_value;

/* Last RPC result so a GATT read (vs. notify) returns the current value. */
static uint8_t rpc_result_buf[IMPROV_SERIAL_BUFFER_SIZE];
static size_t rpc_result_len;

/* Deferred dispatch of a received RPC command (keeps net_mgmt off the BT thread). */
static uint8_t rpc_cmd_buf[IMPROV_SERIAL_BUFFER_SIZE];
static size_t rpc_cmd_len;
static void rpc_work_handler(struct k_work *work);
static K_WORK_DEFINE(rpc_work, rpc_work_handler);

static ssize_t read_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	uint8_t v = (uint8_t)improv_handler_get_state();

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &v, sizeof(v));
}

static ssize_t read_error(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			  uint16_t len, uint16_t offset)
{
	uint8_t v = improv_handler_get_error();

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &v, sizeof(v));
}

static ssize_t read_capabilities(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
				 uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, &capabilities_value,
				 sizeof(capabilities_value));
}

static ssize_t read_rpc_result(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf,
			       uint16_t len, uint16_t offset)
{
	return bt_gatt_attr_read(conn, attr, buf, len, offset, rpc_result_buf, rpc_result_len);
}

static ssize_t write_rpc_cmd(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf,
			     uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (len > sizeof(rpc_cmd_buf)) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	memcpy(rpc_cmd_buf, buf, len);
	rpc_cmd_len = len;
	k_work_submit(&rpc_work);

	return len;
}

/* Attribute layout (indexes into improv_svc.attrs used by bt_gatt_notify):
 *  0 service
 *  1 status decl, 2 status value, 3 status CCC
 *  4 error decl, 5 error value, 6 error CCC
 *  7 rpc-cmd decl, 8 rpc-cmd value
 *  9 rpc-result decl, 10 rpc-result value, 11 rpc-result CCC
 * 12 capabilities decl, 13 capabilities value
 */
BT_GATT_SERVICE_DEFINE(improv_svc,
	BT_GATT_PRIMARY_SERVICE(&service_uuid),
	BT_GATT_CHARACTERISTIC(&status_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_state, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&error_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_error, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&rpc_cmd_uuid.uuid, BT_GATT_CHRC_WRITE, BT_GATT_PERM_WRITE,
			       NULL, write_rpc_cmd, NULL),
	BT_GATT_CHARACTERISTIC(&rpc_result_uuid.uuid, BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_READ, read_rpc_result, NULL, NULL),
	BT_GATT_CCC(NULL, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&capabilities_uuid.uuid, BT_GATT_CHRC_READ, BT_GATT_PERM_READ,
			       read_capabilities, NULL, NULL),
);

#define ATTR_STATE      (&improv_svc.attrs[2])
#define ATTR_ERROR      (&improv_svc.attrs[5])
#define ATTR_RPC_RESULT (&improv_svc.attrs[10])

static void rpc_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	improv_handler_on_rpc_bytes(rpc_cmd_buf, rpc_cmd_len);
}

static void ble_send(enum improv_serial_type type, const uint8_t *payload, size_t payload_len)
{
	switch (type) {
	case IMPROV_TYPE_CURRENT_STATE:
		bt_gatt_notify(NULL, ATTR_STATE, payload, payload_len);
		break;
	case IMPROV_TYPE_ERROR_STATE:
		bt_gatt_notify(NULL, ATTR_ERROR, payload, payload_len);
		break;
	case IMPROV_TYPE_RPC_RESPONSE:
		if (payload_len <= sizeof(rpc_result_buf)) {
			memcpy(rpc_result_buf, payload, payload_len);
			rpc_result_len = payload_len;
			bt_gatt_notify(NULL, ATTR_RPC_RESULT, payload, payload_len);
		}
		break;
	default:
		break;
	}
}

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR),
	BT_DATA_BYTES(BT_DATA_UUID128_ALL, IMPROV_UUID(00)),
};

static const struct bt_data sd[] = {
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void bt_ready(int err)
{
	if (err) {
		LOG_ERR("Bluetooth init failed: %d", err);
		return;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err) {
		LOG_ERR("advertising failed to start: %d", err);
		return;
	}

	LOG_INF("improv BLE transport advertising");
}

int improv_ble_init(void)
{
	improv_handler_register_transport(ble_send);

	int err = bt_enable(bt_ready);

	if (err) {
		LOG_ERR("bt_enable failed: %d", err);
	}

	return err;
}
