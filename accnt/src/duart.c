/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "duart.h"

#include <stdbool.h>
#include <string.h>

#include <drivers/uart.h>

#define MAX_FRAME_LEN DUART_MAX_FRAME_LEN
#define NUM_FRAMES 4

static const struct device *uart_dev;
static volatile unsigned int rx_err_cnt;
static volatile unsigned int too_long_frames_cnt;
static volatile unsigned int invalid_state_cnt;

struct frame_t {
	size_t data_len;
	unsigned char data[MAX_FRAME_LEN];
};

static struct frame_t frames[NUM_FRAMES];
static size_t w_idx;
static size_t r_idx;

enum state_t {
	STATE_IDLE,
	STATE_PAYLOAD,
	STATE_NEXT,
};
static enum state_t state;

K_SEM_DEFINE(rx_sem, 0, 1);

static bool queue_is_empty(void)
{
	return r_idx == w_idx;
}

static bool queue_is_full(void)
{
	return (NUM_FRAMES + r_idx - w_idx) % NUM_FRAMES == 1;
}

static size_t incr_idx(size_t idx)
{
	return (idx + 1) % NUM_FRAMES;
}

static int uart_tx_frame(const unsigned char *payload, size_t payload_len)
{
	unsigned char cs = 0;

	uart_poll_out(uart_dev, 0x02);

	for (size_t i = 0; i < payload_len; ++i) {
		cs += payload[i];
		uart_poll_out(uart_dev, payload[i]);
	}

	uart_poll_out(uart_dev, cs);
	uart_poll_out(uart_dev, 0x03);

	return payload_len;
}

static int uart_tx_ack(void)
{
	uart_poll_out(uart_dev, 0x06);
	return 0;
}

int rx(unsigned char payload[MAX_FRAME_LEN])
{
	while (queue_is_empty()) {
		int sem_ret = k_sem_take(&rx_sem, K_MSEC(100));

		if (sem_ret == -EAGAIN) return sem_ret;
	}
	
	struct frame_t *frame = frames + r_idx;
	size_t copy_size = frame->data_len <= MAX_FRAME_LEN ? frame->data_len : MAX_FRAME_LEN;

	memcpy(payload, frame->data, copy_size);
	r_idx = incr_idx(r_idx);
	return copy_size;
}

static enum state_t search_for_start(unsigned char byte, struct frame_t *frame)
{
	switch (byte) {
		case 0x02: // Start of data frame
			frame->data_len = 0;
			return STATE_PAYLOAD;

		case 0x06: // 1 byte ack
			frame->data_len = 0;
			return STATE_NEXT;

		default:
			return STATE_IDLE;
	}
}

static enum state_t insert_payload(unsigned char byte, struct frame_t *frame)
{
	switch (byte) {
		case 0x03: // End of data frame
			return STATE_NEXT;

		default:
			if (frame->data_len < MAX_FRAME_LEN) {
				frame->data[frame->data_len] = byte;
				frame->data_len++;
				return STATE_PAYLOAD;
			} else {
				// Discard too long frame
				frame->data_len = 0;
				too_long_frames_cnt++;
				return STATE_IDLE;
			}
	}
}

static void finish_frame(struct frame_t *frame)
{
	bool success = false;

	if (!frame->data_len) {
		// Ack or empty frame
		success = true;
	} else {
		unsigned char cs = 0;
		for (size_t i = 0; i < frame->data_len - 1; i++) {
			cs += frame->data[i];
		}

		if (cs == frame->data[frame->data_len - 1]) {
			frame->data_len--; // Remove check sum from the frame
			success = true;

			uart_tx_ack();
		} else {
			// Discard frame
			frame->data_len = 0;
		}
	}

	if (success) {
		w_idx = incr_idx(w_idx);
		k_sem_give(&rx_sem);
	}

	state = STATE_IDLE;
}

static void byte_received(unsigned char byte)
{
	if (queue_is_full()) return;
	struct frame_t *frame = frames + w_idx;

	switch (state) {
		case STATE_IDLE:
			state = search_for_start(byte, frame);
			break;

		case STATE_PAYLOAD:
			state = insert_payload(byte, frame);
			break;

		case STATE_NEXT:
			invalid_state_cnt++;
			state = STATE_IDLE;
			break;
	}

	if (state == STATE_NEXT) {
		finish_frame(frame);
	}
}

void rx_thread(void *, void *, void *)
{
	while (1) {
		int ret;
		unsigned char byte;

		ret = uart_poll_in(uart_dev, &byte);

		if (!ret) {
			byte_received(byte);
		} else if (ret == -1) {
			k_sleep(K_MSEC(4));
		} else {
			rx_err_cnt++;
		}
	}
}

#define RX_STACK_SIZE 512
#define RX_PRIORITY 1

K_THREAD_DEFINE(rx_tid, RX_STACK_SIZE,
                rx_thread, NULL, NULL, NULL,
                RX_PRIORITY, K_ESSENTIAL, K_TICKS_FOREVER);

void duart_init(void)
{
	int ret;

	uart_dev = DEVICE_DT_GET(DT_CHOSEN(hubertmis_accnt_uart));

	if (!uart_dev) return;
	if (!device_is_ready(uart_dev)) return;

	const struct uart_config cfg = {
		.baudrate  = 2400,
		.parity    = UART_CFG_PARITY_EVEN,
		.stop_bits = UART_CFG_STOP_BITS_2,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	ret = uart_configure(uart_dev, &cfg);

	k_thread_start(rx_tid);
}

int duart_rx(unsigned char payload[MAX_FRAME_LEN])
{
	int ret;

	// A loop to skip received acks
	do {
		ret = rx(payload);
	} while (ret == 0);

	return ret;
}

int duart_tx(const char *payload, size_t payload_len)
{
	unsigned char dummy_payload[MAX_FRAME_LEN];

	int ret = uart_tx_frame(payload, payload_len);
	if (ret < 0) return ret;

	// RX ack
	ret = rx(dummy_payload);
	ret = ret > 0 ? -EIO : ret; // Received data frame instead of ack
	
	return ret;
}
