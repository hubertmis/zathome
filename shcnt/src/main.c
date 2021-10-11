/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <relay.h>

void main(void)
{
	const struct device *r1 = DEVICE_DT_GET(DT_NODELABEL(m0_sw));
	const struct device *r2 = DEVICE_DT_GET(DT_NODELABEL(m0_dir));
	const struct device *r3 = DEVICE_DT_GET(DT_NODELABEL(m1_sw));
	const struct device *r4 = DEVICE_DT_GET(DT_NODELABEL(m1_dir));

	const struct relay_api *relay_api = r1->api;

	while (1) {
		relay_api->on(r1);
		k_sleep(K_MSEC(500));
		relay_api->on(r2);
		k_sleep(K_MSEC(500));
		relay_api->on(r3);
		k_sleep(K_MSEC(500));
		relay_api->on(r4);
		k_sleep(K_MSEC(500));

		relay_api->off(r1);
		k_sleep(K_MSEC(500));
		relay_api->off(r2);
		k_sleep(K_MSEC(500));
		relay_api->off(r3);
		k_sleep(K_MSEC(500));
		relay_api->off(r4);
		k_sleep(K_MSEC(500));
	}
}
