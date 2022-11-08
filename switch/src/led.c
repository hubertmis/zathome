/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.h"

#include <device.h>
#include <drivers/gpio.h>

#define HEARTBEAT_STACK_SIZE 512
#define LED_NODE_ID DT_NODELABEL(led)
#define GPIO_NODE_ID DT_GPIO_CTLR(LED_NODE_ID, gpios)
#define GPIO_PIN     DT_GPIO_PIN(LED_NODE_ID, gpios)
#define GPIO_FLAGS   DT_GPIO_FLAGS(LED_NODE_ID, gpios)

K_SEM_DEFINE(led_ctrl_sem, 0, 1);

static uint32_t pulses = 2;
static bool analog_control = false;

static void hb_proc(void *, void *, void *)
{
#if DT_NODE_HAS_STATUS(LED_NODE_ID, okay)
	const struct device *status_led_gpio = DEVICE_DT_GET(GPIO_NODE_ID);

	if (!status_led_gpio) return;
	if (gpio_pin_configure(status_led_gpio, GPIO_PIN, GPIO_OUTPUT_ACTIVE | GPIO_FLAGS) < 0) return;

	while (1)
	{
		for (unsigned long i = 0; i < pulses; i++) {
			gpio_pin_set(status_led_gpio, GPIO_PIN, 1);
			k_sleep(K_MSEC(100));
			gpio_pin_set(status_led_gpio, GPIO_PIN, 0);
			k_sleep(K_MSEC(100));
		}

		gpio_pin_set(status_led_gpio, GPIO_PIN, 0);
		k_sleep(K_MSEC(900));

		while (analog_control) {
			k_sem_take(&led_ctrl_sem, K_FOREVER);
		}
	}
#endif
}

K_THREAD_STACK_DEFINE(hb_thread_stack, HEARTBEAT_STACK_SIZE);
static struct k_thread hb_thread_data;

void led_init(void)
{
	k_thread_create(&hb_thread_data, hb_thread_stack, K_THREAD_STACK_SIZEOF(hb_thread_stack),
			hb_proc, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
}

void led_set_pulses(uint32_t req_pulses)
{
	pulses = req_pulses;
}

void led_take_analog_control(void)
{
	analog_control = true;
}

void led_release_analog_control(void)
{
	analog_control = false;
	k_sem_give(&led_ctrl_sem);
}

void led_analog_toggle(void)
{
#if DT_NODE_HAS_STATUS(LED_NODE_ID, okay)
	static bool led_on;
	const struct device *status_led_gpio = DEVICE_DT_GET(GPIO_NODE_ID);
	if (!status_led_gpio) return;
	if (!analog_control) return;

	led_on = !led_on;
	gpio_pin_set(status_led_gpio, GPIO_PIN, led_on);
#endif
}
