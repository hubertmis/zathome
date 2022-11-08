/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "switch.h"

#include <stdbool.h>

#include <drivers/gpio.h>
#include <net/socket.h>

#include "analog_switch.h"
#include "coap_req.h"
#include "continuous_sd.h"
#include "led.h"
#include "prov.h"

#define OUT_RSRC_TYPE "rgbw"

#define SW1_NODE_ID DT_NODELABEL(sw1)
#define SW1_GPIO_NODE_ID DT_GPIO_CTLR(SW1_NODE_ID, gpios)
#define SW1_GPIO_PIN     DT_GPIO_PIN(SW1_NODE_ID, gpios)
#define SW1_GPIO_FLAGS   DT_GPIO_FLAGS(SW1_NODE_ID, gpios)
#define SW2_NODE_ID DT_NODELABEL(sw2)
#define SW2_GPIO_NODE_ID DT_GPIO_CTLR(SW2_NODE_ID, gpios)
#define SW2_GPIO_PIN     DT_GPIO_PIN(SW2_NODE_ID, gpios)
#define SW2_GPIO_FLAGS   DT_GPIO_FLAGS(SW2_NODE_ID, gpios)
#define AS1_DEV DEVICE_DT_GET(DT_NODELABEL(as1))
#define AS2_DEV DEVICE_DT_GET(DT_NODELABEL(as2))
K_SEM_DEFINE(sw1_sem, 0, 1);
#if DT_NODE_HAS_STATUS(SW2_NODE_ID, okay) || DT_NODE_HAS_STATUS(DT_NODELABEL(as2), okay)
K_SEM_DEFINE(sw2_sem, 0, 1);
#endif
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
#if DT_NODE_HAS_STATUS(SW2_NODE_ID, okay)
	if (pins & BIT(SW2_GPIO_PIN)) {
		k_sem_give(&sw2_sem);
	}
#endif
}

int button_init(const struct device *sw_gpio, gpio_pin_t pin, gpio_flags_t pin_flags)
{
	int ret;

	if (!sw_gpio) return -EINVAL;

	ret = gpio_pin_configure(sw_gpio, pin, GPIO_INPUT | pin_flags);
	if (ret < 0) return ret;

	gpio_flags_t int_flags = prov_get_monostable() ? GPIO_INT_EDGE_TO_ACTIVE : GPIO_INT_EDGE_BOTH;
	int_flags |= GPIO_INT_DEBOUNCE;
	ret = gpio_pin_interrupt_configure(sw_gpio, pin, int_flags);
	if (ret < 0) return ret;

	return 0;
}

void buttons_init(void)
{
	const struct device *sw1_gpio = DEVICE_DT_GET(SW1_GPIO_NODE_ID);
#if DT_NODE_HAS_STATUS(SW2_NODE_ID, okay)
	const struct device *sw2_gpio = DEVICE_DT_GET(SW2_GPIO_NODE_ID);
#endif

	if (button_init(sw1_gpio, SW1_GPIO_PIN, SW1_GPIO_FLAGS) < 0) return;
#if DT_NODE_HAS_STATUS(SW2_NODE_ID, okay)
	if (button_init(sw2_gpio, SW2_GPIO_PIN, SW2_GPIO_FLAGS) < 0) return;
#endif

#if DT_NODE_HAS_STATUS(SW2_NODE_ID, okay)
	BUILD_ASSERT(DEVICE_DT_GET(SW1_GPIO_NODE_ID) == DEVICE_DT_GET(SW2_GPIO_NODE_ID), "Switches in different ports are not supported yet");
#endif

#if DT_NODE_HAS_STATUS(SW2_NODE_ID, okay)
	gpio_init_callback(&sw_callback, sw_event, BIT(SW1_GPIO_PIN) | BIT(SW2_GPIO_PIN));
#else
	gpio_init_callback(&sw_callback, sw_event, BIT(SW1_GPIO_PIN));
#endif
	if (gpio_add_callback(sw1_gpio, &sw_callback) < 0) return;
	/* It is enough to add callback to one of the ports, because both pins use the same port. */
}

#define SWITCH_STACK_SIZE 2048

void sw_proc(void *arg1, void *arg2, void *)
{
	struct k_sem *sw_sem = arg1;
	int sw_id = (int)arg2;
	int r;

	while (1)
	{
		unsigned int num_toggles = 0;
		const char *rsrc_name;
		struct in6_addr out_addr;

		k_sem_take(sw_sem, K_FOREVER);
		num_toggles = 0;
		// TODO: Notify toggle
		led_set_pulses(0);
		rsrc_name = prov_get_output_rsrc_label(sw_id);
		r = continuous_sd_get_addr(rsrc_name, OUT_RSRC_TYPE, &out_addr);
		if (r < 0) continue;
		r = coap_req_preset(&out_addr, rsrc_name, 0);
		if (r < 0) continue;

		while (1) {
			int sem_result = k_sem_take(sw_sem, K_MSEC(1000));

			if (sem_result != 0) {
				if (num_toggles) {
					// TODO: Notify number of toggles in the series
					led_set_pulses(num_toggles);
					r = coap_req_preset(&out_addr, rsrc_name, num_toggles);
					if (r < 0) break;
					// TODO: Some retries?
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

#if DT_NODE_HAS_STATUS(SW2_NODE_ID, okay) || DT_NODE_HAS_STATUS(DT_NODELABEL(as2), okay)
K_THREAD_STACK_DEFINE(sw2_thread_stack, SWITCH_STACK_SIZE);
static struct k_thread sw2_thread_data;
#endif

void analog_switch_callback(bool on, void *ctx)
{
	(void)on;
	struct k_sem *sw_sem = ctx;

	k_sem_give(sw_sem);
}

void switch_init(void)
{
	buttons_init();
	k_thread_create(&sw1_thread_data, sw1_thread_stack, K_THREAD_STACK_SIZEOF(sw1_thread_stack),
			sw_proc, &sw1_sem, (void*)0, NULL, 5, 0, K_NO_WAIT);
#if DT_NODE_HAS_STATUS(SW2_NODE_ID, okay) || DT_NODE_HAS_STATUS(DT_NODELABEL(as2), okay)
	k_thread_create(&sw2_thread_data, sw2_thread_stack, K_THREAD_STACK_SIZEOF(sw2_thread_stack),
			sw_proc, &sw2_sem, (void*)1, NULL, 5, 0, K_NO_WAIT);
#endif

#if DT_NODE_HAS_STATUS(DT_NODELABEL(as1), okay)
	const struct device *as1 = AS1_DEV;
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(as2), okay)
	const struct device *as2 = AS2_DEV;
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(as1), okay)
	const struct analog_switch_driver_api *api = as1->api;
	api->register_callback(as1, analog_switch_callback, &sw1_sem);
#if DT_NODE_HAS_STATUS(DT_NODELABEL(as2), okay)
	api->register_callback(as2, analog_switch_callback, &sw2_sem);
#endif
#endif
}
