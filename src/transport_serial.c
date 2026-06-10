/*
 * transport_serial.c - Improv serial transport over the console UART.
 *
 * Listens for improv frames on zephyr,console using the interrupt-driven UART
 * API and writes framed responses back. Compatible with the Web Serial installer
 * at https://www.improv-wifi.com/serial/. Incoming bytes are parsed in the RX
 * ISR (cheap, pure computation) and completed commands are handed to a worker
 * thread so that blocking work (Wi-Fi connect, logging) runs in thread context.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "improv.h"
#include "improv_handler.h"
#include "improv_transport.h"

LOG_MODULE_REGISTER(improv_serial, LOG_LEVEL_INF);

#define WORKER_STACK_SIZE 2048
#define WORKER_PRIORITY   7

static const struct device *const uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));

static struct improv_serial_parser parser;
static struct k_mutex tx_lock;

/* Carries a parsed command (or an error) from the RX ISR to the worker thread. */
struct serial_msg {
	bool is_error;
	enum improv_error err;
	struct improv_rpc cmd;
};

K_MSGQ_DEFINE(serial_msgq, sizeof(struct serial_msg), 4, 4);

static void serial_send(enum improv_serial_type type, const uint8_t *payload, size_t payload_len)
{
	uint8_t frame[IMPROV_SERIAL_BUFFER_SIZE];
	size_t n = improv_wrap_serial(type, payload, payload_len, frame, sizeof(frame));

	if (n == 0) {
		LOG_ERR("frame too large (type %u, len %zu)", type, payload_len);
		return;
	}

	k_mutex_lock(&tx_lock, K_FOREVER);
	for (size_t i = 0; i < n; i++) {
		uart_poll_out(uart_dev, frame[i]);
	}
	k_mutex_unlock(&tx_lock);
}

static void uart_isr(const struct device *dev, void *user_data)
{
	ARG_UNUSED(user_data);

	if (!uart_irq_update(dev)) {
		return;
	}

	while (uart_irq_rx_ready(dev)) {
		uint8_t c;

		if (uart_fifo_read(dev, &c, 1) != 1) {
			break;
		}

		struct improv_rpc cmd;
		enum improv_error err;
		enum improv_parse_result r = improv_serial_feed(&parser, c, &cmd, &err);

		if (r == IMPROV_PARSE_RPC_READY) {
			struct serial_msg m = { .is_error = false, .cmd = cmd };

			(void)k_msgq_put(&serial_msgq, &m, K_NO_WAIT);
		} else if (r == IMPROV_PARSE_ERROR) {
			struct serial_msg m = { .is_error = true, .err = err };

			(void)k_msgq_put(&serial_msgq, &m, K_NO_WAIT);
		}
	}
}

static void worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	for (;;) {
		struct serial_msg m;

		k_msgq_get(&serial_msgq, &m, K_FOREVER);

		if (m.is_error) {
			improv_handler_report_error(m.err);
		} else {
			improv_handler_on_command(&m.cmd);
		}
	}
}

K_THREAD_DEFINE(improv_serial_worker, WORKER_STACK_SIZE, worker, NULL, NULL, NULL,
		WORKER_PRIORITY, 0, 0);

int improv_serial_init(void)
{
	if (!device_is_ready(uart_dev)) {
		LOG_ERR("console UART not ready");
		return -ENODEV;
	}

	k_mutex_init(&tx_lock);

	uart_irq_callback_user_data_set(uart_dev, uart_isr, NULL);
	uart_irq_rx_enable(uart_dev);

	improv_handler_register_transport(serial_send);

	LOG_INF("improv serial transport ready on %s", uart_dev->name);
	return 0;
}
