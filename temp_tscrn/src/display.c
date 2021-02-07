/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <kernel.h>

#include "data_dispatcher.h"
#include "prov.h"
#include "ft8xx/ft8xx.h"
#include "ft8xx/ft8xx_copro.h"
#include "ft8xx/ft8xx_dl.h"

// TODO: Create a thread to display screen. Such thread should prevent preemption of display procedure with another display procedure.
K_SEM_DEFINE(spi_sem, 1, 1);

#define TOUCH_THREAD_STACK_SIZE 1024
#define TOUCH_THREAD_PRIO       0
static void touch_thread_process(void *a1, void *a2, void *a3);
static void touch_irq(void);

K_THREAD_DEFINE(touch_thread_id, TOUCH_THREAD_STACK_SIZE,
                touch_thread_process, NULL, NULL, NULL,
                TOUCH_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static void temp_changed(const data_dispatcher_publish_t *data);

static data_dispatcher_subscribe_t temp_sbscr = {
    .callback = temp_changed,
};

void display_init(void)
{
    data_dispatcher_subscribe(DATA_TEMP_MEASUREMENT, &temp_sbscr);
    data_dispatcher_subscribe(DATA_TEMP_SETTING, &temp_sbscr);
    ft8xx_register_int (touch_irq);
    k_thread_start(touch_thread_id);
}

uint32_t temp;

static void process_touch(uint8_t tag, uint32_t iteration)
{
    // TODO: Refactor interface and body of this function.
    // It should handle click and holding a button.

    const data_dispatcher_publish_t *p_data;
    data_dispatcher_publish_t data;
    data_loc_t loc = DATA_LOC_NUM;
    int16_t diff = 0;
    bool publish_setting = false;

    switch (tag) {
        case 1:
            loc = DATA_LOC_LOCAL;
            diff = 1;
            publish_setting = true;
            break;

        case 2:
            loc = DATA_LOC_LOCAL;
            diff = -1;
            publish_setting = true;
            break;

        case 3:
            loc = DATA_LOC_REMOTE;
            diff = 1;
            publish_setting = true;
            break;

        case 4:
            loc = DATA_LOC_REMOTE;
            diff = -1;
            publish_setting = true;
            break;

        default:
            // TODO: Log error
            return;
    }

    if (publish_setting) {
        data_dispatcher_get(DATA_TEMP_SETTING, loc, &p_data);
        data = *p_data;
        data.temp_setting += diff;
        data_dispatcher_publish(&data);
    }
}

static void touch_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    const uint8_t no_touch = 0;
    uint8_t last_tag = no_touch;
    uint32_t iteration = 0;
    
    while (1) {
        int tag = ft8xx_get_touch_tag();
        
        if (tag < 0) {
            // Error
            break;
        }

        if (last_tag != tag) {
            last_tag = tag;
            iteration = 0;
        }
        else if (tag != no_touch) {
            iteration++;
        }

        if (tag != no_touch) {
            process_touch((uint8_t)tag, iteration);

            k_sleep(K_MSEC(100));
        } else {
            k_sleep(K_FOREVER);
        }

    }
}

static void touch_irq(void)
{
    k_wakeup(touch_thread_id);
}

static void display_temps(const data_dispatcher_publish_t *(*meas)[DATA_LOC_NUM],
                          const data_dispatcher_publish_t *(*settings)[DATA_LOC_NUM])
{
    const char *out_lbl = prov_get_loc_output_label();

    k_sem_take(&spi_sem, K_FOREVER);

    cmd_dlstart();
    cmd(CLEAR_COLOR_RGB(0x00, 0x00, 0x00));
    cmd(CLEAR(1, 1, 1));
    cmd(COLOR_RGB(0xf0, 0xf0, 0xf0));

    for (int i = 0; i < DATA_LOC_NUM; ++i) {
        const int str_length = 20;
        char text[str_length];

        // Measurement display
        int16_t dC = (*meas)[i]->temp_measurement;
        uint16_t x = 120;
        uint16_t y = 120 + i * 40;

        if (dC < TEMP_MIN) {
            cmd_text(x, y, 29, 0, "Sensor error");
        } else {
            snprintf(text, str_length, "%d.%d", dC / 10, abs(dC % 10));
            cmd_text(x, y, 31, 0, text);
        }

        if ((i == DATA_LOC_REMOTE) || (out_lbl && strlen(out_lbl))) {
            // Setting display
            dC = (*settings)[i]->temp_setting;
            x = 370;
            snprintf(text, str_length, "%d.%d", dC / 10, abs(dC % 10));
            cmd_text(x, y, 27, 0, text);

            // Buttons
            x = 300;
            y = 100 + i * 40;
            cmd(TAG(1 + i * 2));
            cmd_text(x, y, 31, 0, "+");

            x = 340;
            cmd(TAG(2 + i * 2));
            cmd_text(x, y, 31, 0, "-");

            cmd(TAG(0));
        }
    }

    cmd_number(20, 20, 29, OPT_SIGNED, temp);

    cmd_text(460, 20, 29, OPT_RIGHTX, CONFIG_MCUBOOT_IMAGE_VERSION);

    cmd(DISPLAY());
    cmd_swap();

    k_sem_give(&spi_sem);
}

static void temp_changed(const data_dispatcher_publish_t *data)
{
    const data_dispatcher_publish_t *meas[DATA_LOC_NUM];
    const data_dispatcher_publish_t *setting[DATA_LOC_NUM];

    switch (data->type) {
        case DATA_TEMP_MEASUREMENT:
            for (int i = 0; i < DATA_LOC_NUM; ++i) {
                if (data->loc == i) {
                    meas[i] = data;
                }
                else {
                    data_dispatcher_get(DATA_TEMP_MEASUREMENT, i, &meas[i]);
                }

                data_dispatcher_get(DATA_TEMP_SETTING, i, &setting[i]);
            }
            break;

        case DATA_TEMP_SETTING:
            for (int i = 0; i < DATA_LOC_NUM; ++i) {
                data_dispatcher_get(DATA_TEMP_MEASUREMENT, i, &meas[i]);

                if (data->loc == i) {
                    setting[i] = data;
                }
                else {
                    data_dispatcher_get(DATA_TEMP_SETTING, i, &setting[i]);
                }
            }
            break;

        default:
            return;
    }

    display_temps(&meas, &setting);
}
