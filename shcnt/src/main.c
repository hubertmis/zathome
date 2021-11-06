/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include <relay.h>
#include "coap.h"
#include "mot_cnt.h"
#include "prov.h"

#include <dfu/mcuboot.h>
#include <drivers/gpio.h>
#include <net/fota_download.h>
#include <net/openthread.h>
#include <openthread/thread.h>
#include <power/reboot.h>
#include <settings/settings.h>

#define TX_POWER 8

// Heartbeat
#define HEARTBEAT_STACK_SIZE 512
#define LED_NODE_ID DT_NODELABEL(led_status)
#define GPIO_NODE_ID DT_GPIO_CTLR(LED_NODE_ID, gpios)
#define GPIO_PIN     DT_GPIO_PIN(LED_NODE_ID, gpios)
#define GPIO_FLAGS   DT_GPIO_FLAGS(LED_NODE_ID, gpios)

void hb_proc(void *, void *, void *)
{
	const struct device *status_led_gpio = DEVICE_DT_GET(GPIO_NODE_ID);

	if (!status_led_gpio) return;
	if (gpio_pin_configure(status_led_gpio, GPIO_PIN, GPIO_OUTPUT_ACTIVE | GPIO_FLAGS) < 0) return;

	while (1)
	{
		gpio_pin_set(status_led_gpio, GPIO_PIN, 1);
		k_sleep(K_MSEC(100));
		gpio_pin_set(status_led_gpio, GPIO_PIN, 0);
		k_sleep(K_MSEC(100));
		gpio_pin_set(status_led_gpio, GPIO_PIN, 1);
		k_sleep(K_MSEC(100));
		gpio_pin_set(status_led_gpio, GPIO_PIN, 0);
		k_sleep(K_MSEC(4700));
	}
}

K_THREAD_STACK_DEFINE(hb_thread_stack, HEARTBEAT_STACK_SIZE);
static struct k_thread hb_thread_data;

// Fota
void fota_callback(const struct fota_download_evt *evt)
{
    if (evt->id == FOTA_DOWNLOAD_EVT_FINISHED) {
        sys_reboot(SYS_REBOOT_COLD);
    }
}

// Main
void main(void)
{
	prov_init();

	settings_subsys_init();
	settings_register(prov_get_settings_handler());
	settings_load();

	otError error;
	struct otInstance *ot_instance = openthread_get_default_instance();

	error = otPlatRadioSetTransmitPower(ot_instance, TX_POWER);
	assert(error == OT_ERROR_NONE);

	struct otIp6Address site_local_all_nodes_addr = {.mFields = {.m8 =
			{0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01} }};

	error = otIp6SubscribeMulticastAddress(ot_instance, &site_local_all_nodes_addr);
	assert(error == OT_ERROR_NONE);

	fota_download_init(fota_callback);
	coap_init();


	k_thread_create(&hb_thread_data, hb_thread_stack, K_THREAD_STACK_SIZEOF(hb_thread_stack),
			hb_proc, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

	boot_write_img_confirmed();
}
