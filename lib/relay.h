/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <device.h>

typedef int (*relay_on_f)(const struct device *dev);
typedef int (*relay_off_f)(const struct device *dev);

struct relay_api {
	relay_on_f on;
	relay_off_f off;
};
