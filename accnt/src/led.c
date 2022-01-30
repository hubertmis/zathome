/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.h"

#include <drivers/gpio.h>

#define SUCC_NODE_ID      DT_NODELABEL(led0)
#define SUCC_GPIO_NODE_ID DT_GPIO_CTLR(SUCC_NODE_ID, gpios)
#define SUCC_GPIO_PIN     DT_GPIO_PIN(SUCC_NODE_ID, gpios)
#define SUCC_GPIO_FLAGS   DT_GPIO_FLAGS(SUCC_NODE_ID, gpios)

#define FAIL_NODE_ID      DT_NODELABEL(led1)
#define FAIL_GPIO_NODE_ID DT_GPIO_CTLR(FAIL_NODE_ID, gpios)
#define FAIL_GPIO_PIN     DT_GPIO_PIN(FAIL_NODE_ID, gpios)
#define FAIL_GPIO_FLAGS   DT_GPIO_FLAGS(FAIL_NODE_ID, gpios)

#define LED_STACK_SIZE 512

K_SEM_DEFINE(led_sem, 0, 1);
static volatile bool success;
static volatile bool failure;

void led_proc(void *, void *, void *)
{
	const struct device *succ_led_gpio = DEVICE_DT_GET(SUCC_GPIO_NODE_ID);
	const struct device *fail_led_gpio = DEVICE_DT_GET(FAIL_GPIO_NODE_ID);

	if (!succ_led_gpio || !fail_led_gpio) return;
	if (gpio_pin_configure(succ_led_gpio, SUCC_GPIO_PIN, GPIO_OUTPUT_ACTIVE | SUCC_GPIO_FLAGS) < 0) return;
	if (gpio_pin_configure(fail_led_gpio, FAIL_GPIO_PIN, GPIO_OUTPUT_ACTIVE | FAIL_GPIO_FLAGS) < 0) return;

	for (unsigned int i = 0; i < 2; ++i) {
		gpio_pin_set(succ_led_gpio, SUCC_GPIO_PIN, 1);
		k_sleep(K_MSEC(100));
		gpio_pin_set(succ_led_gpio, SUCC_GPIO_PIN, 0);
		gpio_pin_set(fail_led_gpio, FAIL_GPIO_PIN, 1);
		k_sleep(K_MSEC(100));
		gpio_pin_set(fail_led_gpio, FAIL_GPIO_PIN, 0);
	}
	gpio_pin_set(succ_led_gpio, SUCC_GPIO_PIN, 0);
	gpio_pin_set(fail_led_gpio, FAIL_GPIO_PIN, 0);

	while (1)
	{
		k_sem_take(&led_sem, K_FOREVER);

		if (success) {
			success = false;
			gpio_pin_set(succ_led_gpio, SUCC_GPIO_PIN, 1);
			k_sleep(K_MSEC(100));
			gpio_pin_set(succ_led_gpio, SUCC_GPIO_PIN, 0);
		}
		if (failure) {
			failure = false;
			gpio_pin_set(fail_led_gpio, FAIL_GPIO_PIN, 1);
			k_sleep(K_MSEC(100));
			gpio_pin_set(fail_led_gpio, FAIL_GPIO_PIN, 0);
		}
	}
}

#define LED_STACK_SIZE 512
#define LED_PRIORITY 5

K_THREAD_DEFINE(led_tid, LED_STACK_SIZE,
                led_proc, NULL, NULL, NULL,
                LED_PRIORITY, 0, 0);


int led_success(void)
{
	success = true;
	k_sem_give(&led_sem);

	return 0;
}

int led_failure(void)
{
	failure = true;
	k_sem_give(&led_sem);

	return 0;
}
