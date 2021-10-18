/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>

static inline const struct device * mot_cnt_map_from_id(int id)
{
	switch (id) {
		case 0:
			return DEVICE_DT_GET(DT_NODELABEL(m0));

		case 1:
			return DEVICE_DT_GET(DT_NODELABEL(m1));

		default:
			return NULL;
	}
}
