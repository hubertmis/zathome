/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <relay.h>
#include "mot_cnt.h"

void main(void)
{
#if 0
	const struct device *r1 = DEVICE_DT_GET(DT_NODELABEL(m0_sw));
	const struct device *r2 = DEVICE_DT_GET(DT_NODELABEL(m0_dir));
	const struct device *r3 = DEVICE_DT_GET(DT_NODELABEL(m1_sw));
	const struct device *r4 = DEVICE_DT_GET(DT_NODELABEL(m1_dir));
#endif
	const struct device *mc1 = DEVICE_DT_GET(DT_NODELABEL(m0));

	const struct mot_cnt_api *api = mc1->api;

	while (1) {
		k_sleep(K_MSEC(500));
		api->max(mc1);
		k_sleep(K_MSEC(4000));
		api->stop(mc1);
		k_sleep(K_MSEC(2000));
		api->min(mc1);
		k_sleep(K_MSEC(5000));
		api->max(mc1);
		k_sleep(K_MSEC(6000));
		api->min(mc1);
		k_sleep(K_MSEC(10));
		api->max(mc1);
		k_sleep(K_MSEC(120000));
	}
}
