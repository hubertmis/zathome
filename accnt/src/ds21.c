/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ds21.h"

#include <errno.h>
#include "duart.h"
#include "led.h"

#define GETTER_REQ_FRAME_LEN 2
#define INIT_FRAME_LEN 3
#define BASIC_STATE_FRAME_LEN 6

static bool ready;

static uint8_t dc_to_f(int16_t dc)
{
	return (dc + 3) / 5 + 28;
}

static int16_t f_to_dc(uint8_t f)
{
	return ((int16_t)f - 28) * 5;
}

void ds21_init(void)
{
	int ret;

	duart_init();

	do {
		const unsigned char cmd[INIT_FRAME_LEN] = {'D', '2', '0'};
		ret = duart_tx(cmd, sizeof(cmd));
		if (ret < 0) led_failure();
	} while (ret < 0);

	led_success();
	ready = true;
}

int ds21_get_basic_state(struct ds21_basic_state *state)
{
	unsigned char req[GETTER_REQ_FRAME_LEN] = {'F', '1'};
	unsigned char rsp[DUART_MAX_FRAME_LEN];
	int ret = 0;

	if (!ready) {
		ret = -ENODEV;
		goto fail;
	}

	ret = duart_tx(req, sizeof(req));
	if (ret < 0) goto fail;

	ret = duart_rx(rsp);
	if (ret < 0) goto fail;

	if (ret != BASIC_STATE_FRAME_LEN) {
		ret = -EIO;
		goto fail;
	}
	ret = -EIO;
	if (rsp[0] != 'G') goto fail;
	if (rsp[1] != '1') goto fail;

	switch (rsp[2]) {
		case '0':
			state->enabled = false;
			break;

		case '1':
			state->enabled = true;
			break;

		default:
			goto fail;
	}

	state->mode = rsp[3];
	state->target_temp = f_to_dc(rsp[4]);
	state->fan = rsp[5];

	led_success();
	return 0;

fail:
	led_failure();
	return ret;
}

int ds21_set_basic_state(const struct ds21_basic_state *state)
{
	unsigned char frame[BASIC_STATE_FRAME_LEN] = {'D', '1'};
	int ret = -ENODEV;

	if (!ready) goto fail;

	frame[2] = state->enabled ? '1' : '0';
	frame[3] = state->mode;
	frame[4] = dc_to_f(state->target_temp);
	frame[5] = state->fan;

	ret = duart_tx(frame, sizeof(frame));
	if (ret < 0) goto fail;

	led_success();
	return ret;

fail:
	led_failure();
	return ret;
}
