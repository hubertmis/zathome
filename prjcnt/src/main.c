/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdbool.h>

#include <kernel.h> // Needed by k_sleep

#include <dfu/mcuboot.h>
#include <net/fota_download.h>
#include <net/openthread.h>
#include <openthread/thread.h>
#include <power/reboot.h>

#include "coap.h"
#include "notification.h"
#include "prov.h"
#include "pwr_det.h"

#define TX_POWER 8

#include <settings/settings.h>

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
    notification_init();
    pwr_det_init();

    boot_write_img_confirmed();
}

