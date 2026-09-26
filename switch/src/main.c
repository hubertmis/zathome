/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>

#include "coap.h"
#include "led.h"
#include "prov.h"
#include "switch.h"

#include "ot_sed.h"

#include <net/fota_download.h>
#include <openthread/thread.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/net/openthread.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>

#define TX_POWER 8

// Fota
void fota_callback(const struct fota_download_evt *evt)
{
    if (evt->id == FOTA_DOWNLOAD_EVT_FINISHED) {
        sys_reboot(SYS_REBOOT_COLD);
    }
}

// Main
int main(void)
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

	ot_sed_init(ot_instance);
	fota_download_init(fota_callback);
	coap_init();

	switch_init();
	led_init();

	boot_write_img_confirmed();

	return 0;
}
