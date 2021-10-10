/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hubertmis_mot_cnt

#include "mot_cnt.h"

#include <relay.h>

struct cfg {
	const struct device *sw_dev;
	const struct device *dir_dev;
};

static int init_mot_cnt(const struct device *dev)
{
	return 0;
}

static int min(const struct device *dev)
{
	return 0;
}

static int max(const struct device *dev)
{
	return 0;
}

static const struct mot_cnt_api mot_cnt_api = {
	.min = min,
	.max = max,
};

#define MOT_CNT_DEV_DEFINE(inst) \
	static const struct cfg cfg##inst = {\
		.sw_dev = DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(inst), sw)), \
		.dir_dev = DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(inst), dir)), \
	};                                   \
	DEVICE_DT_DEFINE(DT_DRV_INST(inst),  \
			init_mot_cnt,        \
			NULL,                \
			NULL,                \
			&cfg##inst,          \
			POST_KERNEL, 50,     \
			&mot_cnt_api         \
			);

DT_INST_FOREACH_STATUS_OKAY(MOT_CNT_DEV_DEFINE)
