/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdbool.h>

#include "coap.h"
#include "conn.h"
#include "ctlr.h"
#include "data_dispatcher.h"
#include "display.h"
#include "output.h"
#include "prov.h"
#include "rmt_out.h"
#include "ft8xx/ft8xx.h"
#include "sensor.h"

#include <dfu/mcuboot.h>
#include <net/fota_download.h>
#include <net/openthread.h>
#include <openthread/thread.h>
#include <power/reboot.h>

#define TX_POWER 8

#include <settings/settings.h>

static struct ft8xx_touch_transform tt;
static bool tt_known = false;

static int app_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(name, "tt", &next) && !next) {
        if (len != sizeof(tt)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &tt, sizeof(tt));
        if (rc >= 0) {
            tt_known = true;
            return 0;
        }

        return rc;
    }

    return -ENOENT;
}

struct settings_handler sett_app_conf = {
    .name = "app",
    .h_set = app_settings_set,
};

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
    settings_register(&sett_app_conf);
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

    // TODO: Why is this delay needed?
    k_sleep(K_MSEC(50));
    if (tt_known) {
        ft8xx_touch_transform_set(&tt);
    } else {
        ft8xx_calibrate(&tt);
        settings_save_one("app/tt", &tt, sizeof(tt));
    }

    fota_download_init(fota_callback);
    data_dispatcher_init();
    conn_init();
    display_init();
    sensor_init();
    output_init();
    ctlr_init();
    coap_init();
    rmt_out_init();

    boot_write_img_confirmed();
}

