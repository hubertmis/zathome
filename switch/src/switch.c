/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "switch.h"

#include <drivers/gpio.h>

#define SW1_NODE_ID DT_NODELABEL(sw1)
#define SW1_GPIO_NODE_ID DT_GPIO_CTLR(SW1_NODE_ID, gpios)
#define SW1_GPIO_PIN     DT_GPIO_PIN(SW1_NODE_ID, gpios)
#define SW1_GPIO_FLAGS   DT_GPIO_FLAGS(SW1_NODE_ID, gpios)
#define SW2_NODE_ID DT_NODELABEL(sw2)
#define SW2_GPIO_NODE_ID DT_GPIO_CTLR(SW2_NODE_ID, gpios)
#define SW2_GPIO_PIN     DT_GPIO_PIN(SW2_NODE_ID, gpios)
#define SW2_GPIO_FLAGS   DT_GPIO_FLAGS(SW2_NODE_ID, gpios)
K_SEM_DEFINE(sw1_sem, 0, 1);
K_SEM_DEFINE(sw2_sem, 0, 1);
static struct gpio_callback sw_callback;

void sw_event(const struct device *dev, struct gpio_callback *cb,
		uint32_t pins)
{
	static int64_t last_evt;

	/* Debouncing if driver do not support it */
	int64_t uptime = k_uptime_get();
	if (uptime - last_evt < 2) {
		last_evt = uptime;
		return;
	} else {
		last_evt = uptime;
	}

	if (pins & BIT(SW1_GPIO_PIN)) {
		k_sem_give(&sw1_sem);
	}
	if (pins & BIT(SW2_GPIO_PIN)) {
		k_sem_give(&sw2_sem);
	}
}

int button_init(const struct device *sw_gpio, gpio_pin_t pin, gpio_flags_t pin_flags)
{
	int ret;

	if (!sw_gpio) return -EINVAL;

	ret = gpio_pin_configure(sw_gpio, pin, GPIO_INPUT | pin_flags);
	if (ret < 0) return ret;

	ret = gpio_pin_interrupt_configure(sw_gpio, pin, GPIO_INT_EDGE_BOTH | GPIO_INT_DEBOUNCE);
	if (ret < 0) return ret;

	return 0;
}

void buttons_init(void)
{
	const struct device *sw1_gpio = DEVICE_DT_GET(SW1_GPIO_NODE_ID);
	const struct device *sw2_gpio = DEVICE_DT_GET(SW2_GPIO_NODE_ID);

	if (button_init(sw1_gpio, SW1_GPIO_PIN, SW1_GPIO_FLAGS) < 0) return;
	if (button_init(sw2_gpio, SW2_GPIO_PIN, SW2_GPIO_FLAGS) < 0) return;

	BUILD_ASSERT(DEVICE_DT_GET(SW1_GPIO_NODE_ID) == DEVICE_DT_GET(SW2_GPIO_NODE_ID), "Switches in different ports are not supported yet");

	gpio_init_callback(&sw_callback, sw_event, BIT(SW1_GPIO_PIN) | BIT(SW2_GPIO_PIN));
	if (gpio_add_callback(sw1_gpio, &sw_callback) < 0) return;
	/* It is enough to add callback to one of the ports, because both pins use the same port. */
}

#define SWITCH_STACK_SIZE 512

void pulses_set(int p);

void sw_proc(void *arg1, void *, void *)
{
	struct k_sem *sw_sem = arg1;
	while (1)
	{
		unsigned int num_toggles = 0;

		k_sem_take(sw_sem, K_FOREVER);
		num_toggles = 0;
		// TODO: Notify toggle
		pulses_set(0);

		while (1) {
			int sem_result = k_sem_take(sw_sem, K_MSEC(1000));

			if (sem_result != 0) {
				if (num_toggles) {
					// TODO: Notify number of toggles in the series
					pulses_set(num_toggles);
				}
				break;
			} else {
				num_toggles++;
			}
		}
	}
}

K_THREAD_STACK_DEFINE(sw1_thread_stack, SWITCH_STACK_SIZE);
static struct k_thread sw1_thread_data;

K_THREAD_STACK_DEFINE(sw2_thread_stack, SWITCH_STACK_SIZE);
static struct k_thread sw2_thread_data;

void switch_init(void)
{
	buttons_init();
	k_thread_create(&sw1_thread_data, sw1_thread_stack, K_THREAD_STACK_SIZEOF(sw1_thread_stack),
			sw_proc, &sw1_sem, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_create(&sw2_thread_data, sw2_thread_stack, K_THREAD_STACK_SIZEOF(sw2_thread_stack),
			sw_proc, &sw2_sem, NULL, NULL, 5, 0, K_NO_WAIT);

}
