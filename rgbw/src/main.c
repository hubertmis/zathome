/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdbool.h>

#include <dfu/mcuboot.h>
//#include <net/fota_download.h>
//#include <net/openthread.h>
//#include <openthread/thread.h>
#include <power/reboot.h>

#define TX_POWER 8

#include <settings/settings.h>

#if 0
void fota_callback(const struct fota_download_evt *evt)
{
    if (evt->id == FOTA_DOWNLOAD_EVT_FINISHED) {
        sys_reboot(SYS_REBOOT_COLD);
    }
}
#endif

void main(void)
{
    //prov_init();

#if 0
    settings_subsys_init();
    settings_register(prov_get_settings_handler());
    settings_load();
#endif

#if 0
    otError error;
    struct otInstance *ot_instance = openthread_get_default_instance();

    error = otPlatRadioSetTransmitPower(ot_instance, TX_POWER);
    assert(error == OT_ERROR_NONE);

    struct otIp6Address site_local_all_nodes_addr = {.mFields = {.m8 =
            {0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
             0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01} }};

    error = otIp6SubscribeMulticastAddress(ot_instance, &site_local_all_nodes_addr);
    assert(error == OT_ERROR_NONE);
#endif

    //fota_download_init(fota_callback);
    //data_dispatcher_init();
    //conn_init();
    //coap_init();

    boot_write_img_confirmed();
}

