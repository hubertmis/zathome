/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>

typedef int (*mot_cnt_min_t)(const struct device *dev);
typedef int (*mot_cnt_max_t)(const struct device *dev);

// TODO: Stop function
// TODO: Set target

struct mot_cnt_api {
	mot_cnt_min_t min;
	mot_cnt_max_t max;
};
