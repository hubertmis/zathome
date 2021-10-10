/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gpio_relay

#include "relay.h"

#include <device.h>
#include <drivers/gpio.h>

struct cfg {
	const struct device *gpio_dev;
	gpio_pin_t gpio_pin;
	gpio_flags_t gpio_flags;
};

static int init_relay(const struct device *dev)
{
	const struct cfg *config = dev->config;
	if (!config) {
		return -ENODEV;
	}

	return gpio_pin_configure(config->gpio_dev, config->gpio_pin, config->gpio_flags);
}

static int relay_on(const struct device *dev)
{
	const struct cfg *config = dev->config;
	if (!config) {
		return -ENODEV;
	}

	return gpio_pin_set(config->gpio_dev, config->gpio_pin, 1);
}

static int relay_off(const struct device *dev)
{
	const struct cfg *config = dev->config;
	if (!config) {
		return -ENODEV;
	}

	return gpio_pin_set(config->gpio_dev, config->gpio_pin, 0);
}

static const struct relay_api relay_api = {
	.on = relay_on,
	.off = relay_off,
};

#define RELAY_DEV_DEFINE(inst) \
	static const struct cfg cfg##inst = {\
		.gpio_dev = DEVICE_DT_GET(DT_PHANDLE(DT_DRV_INST(inst), gpios)), \
		.gpio_pin = DT_GPIO_PIN(DT_DRV_INST(inst), gpios), \
		.gpio_flags = GPIO_OUTPUT | DT_GPIO_FLAGS(DT_DRV_INST(inst), gpios), \
	};                                   \
	DEVICE_DT_DEFINE(DT_DRV_INST(inst),  \
			init_relay,          \
			NULL,                \
			NULL,                \
			&cfg##inst,          \
			POST_KERNEL, 50,     \
			&relay_api           \
			);

DT_INST_FOREACH_STATUS_OKAY(RELAY_DEV_DEFINE)
