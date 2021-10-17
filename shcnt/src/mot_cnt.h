/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>

typedef int (*mot_cnt_min_t)(const struct device *dev);
typedef int (*mot_cnt_max_t)(const struct device *dev);
typedef int (*mot_cnt_stop_t)(const struct device *dev);

typedef int (*mot_cnt_set_run_time_t)(const struct device *dev, uint32_t run_time_ms);
typedef int (*mot_cnt_go_to_t)(const struct device *dev, uint32_t target);

// TODO: Set target

struct mot_cnt_api {
	mot_cnt_min_t min;
	mot_cnt_max_t max;
	mot_cnt_stop_t stop;

	mot_cnt_set_run_time_t set_run_time;
	mot_cnt_go_to_t go_to;
};
