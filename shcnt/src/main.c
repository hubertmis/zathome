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
#include <net/fota_download.h>
#include <net/openthread.h>
#include <openthread/thread.h>
#include <power/reboot.h>
#include <settings/settings.h>

#define TX_POWER 8

void fota_callback(const struct fota_download_evt *evt)
{
    if (evt->id == FOTA_DOWNLOAD_EVT_FINISHED) {
        sys_reboot(SYS_REBOOT_COLD);
    }
}

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

	boot_write_img_confirmed();

	const struct device *mc1 = DEVICE_DT_GET(DT_NODELABEL(m0));

	const struct mot_cnt_api *api = mc1->api;

	api->set_run_time(mc1, 10000);

	while (1) {
		k_sleep(K_MSEC(500));
		api->max(mc1);
		k_sleep(K_MSEC(4000));
		api->stop(mc1);
		k_sleep(K_MSEC(2000));
		api->min(mc1);
		k_sleep(K_MSEC(5000));
		api->max(mc1);
		k_sleep(K_MSEC(6000));
		api->min(mc1);
		k_sleep(K_MSEC(10));
		api->max(mc1);
		k_sleep(K_MSEC(20000));
		api->go_to(mc1, 128);
		k_sleep(K_MSEC(20000));
		api->go_to(mc1, 10);
		k_sleep(K_MSEC(1000));
		api->go_to(mc1, 228);
		k_sleep(K_MSEC(20000));
	}
}
