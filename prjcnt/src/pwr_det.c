/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "pwr_det.h"

#include <zephyr.h>
#include <device.h>
#include <drivers/gpio.h>

#include "notification.h"

#define SW0_NODE	DT_NODELABEL(button0)
#if !DT_NODE_HAS_STATUS(SW0_NODE, okay)
#error "Unsupported board: button0 devicetree node label is not defined"
#endif
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static struct gpio_callback button_cb_data;

static void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	int val = gpio_pin_get_dt(&button);
	bool prj_state = val > 0;

	notification_set_prj_state(prj_state);
}

static void init_gpio(void)
{
	int ret;

	if (!device_is_ready(button.port)) {
		return;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (ret != 0) {
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
}

void pwr_det_init(void)
{
	init_gpio();
}
