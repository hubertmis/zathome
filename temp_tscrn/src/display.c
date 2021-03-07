/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <date_time.h>
#include <kernel.h>
#include <net/net_if.h>

#include "data_dispatcher.h"
#include "prov.h"
#include "ft8xx/ft8xx.h"
#include "ft8xx/ft8xx_copro.h"
#include "ft8xx/ft8xx_dl.h"

#define CLOCK_LINE_WIDTH   10
#define CLOCK_LINE_LENGTH  60
#define CLOCK_DIGIT_SPACE  50
#define CLOCK_NUMBER_SPACE 60

// TODO: Create a thread to display screen. Such thread should prevent preemption of display procedure with another display procedure.
K_SEM_DEFINE(spi_sem, 1, 1);

enum screen_t {
    SCREEN_CLOCK,
    SCREEN_TEMPS,
};

static enum screen_t curr_screen = SCREEN_CLOCK;

#define TOUCH_THREAD_STACK_SIZE 1024
#define TOUCH_THREAD_PRIO       0
static void touch_thread_process(void *a1, void *a2, void *a3);
static void touch_irq(void);

K_THREAD_DEFINE(touch_thread_id, TOUCH_THREAD_STACK_SIZE,
                touch_thread_process, NULL, NULL, NULL,
                TOUCH_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

#define INACTIVITY_TIME_MS (1000UL * 60UL)
#define CLOCK_REFRESH_MS   1000UL
void inactivity_work_handler(struct k_work *work);
K_WORK_DEFINE(inactivity_work, inactivity_work_handler);

static void inactivity_timer_handler(struct k_timer *timer)
{
    k_work_submit(&inactivity_work);
}
K_TIMER_DEFINE(inactivity_timer, inactivity_timer_handler, NULL);

static void display_clock(void);
static void display_curr_temps(void);
static void temp_changed(const data_dispatcher_publish_t *data);

static data_dispatcher_subscribe_t temp_sbscr = {
    .callback = temp_changed,
};

void display_init(void)
{
    data_dispatcher_subscribe(DATA_TEMP_MEASUREMENT, &temp_sbscr);
    data_dispatcher_subscribe(DATA_TEMP_SETTING, &temp_sbscr);
    display_clock();
    ft8xx_register_int (touch_irq);
    k_thread_start(touch_thread_id);
}

uint32_t temp;

static void process_touch_temps(uint8_t tag, uint32_t iteration)
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

static void process_touch(uint8_t tag, uint32_t iteration)
{
    k_timer_start(&inactivity_timer, K_MSEC(INACTIVITY_TIME_MS), K_NO_WAIT);

    switch (curr_screen) {
        case SCREEN_CLOCK:
            curr_screen = SCREEN_TEMPS;
            display_curr_temps();
            break;

        case SCREEN_TEMPS:
            process_touch_temps(tag, iteration);
            break;
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

static void draw_segment(int x, int y, int segment)
{
    const int seg_len = CLOCK_LINE_LENGTH * 16;

    const int x_offset = seg_len / 2;
    const int y_offset = seg_len;

    x *= 16;
    y *= 16;

    switch (segment) {
        case 0:
            cmd(VERTEX2F(x - x_offset, y - y_offset));
            cmd(VERTEX2F(x + x_offset, y - y_offset));
            break;

        case 1:
            cmd(VERTEX2F(x + x_offset, y - y_offset));
            cmd(VERTEX2F(x + x_offset, y));
            break;

        case 2:
            cmd(VERTEX2F(x + x_offset, y));
            cmd(VERTEX2F(x + x_offset, y + y_offset));
            break;

        case 3:
            cmd(VERTEX2F(x - x_offset, y + y_offset));
            cmd(VERTEX2F(x + x_offset, y + y_offset));
            break;

        case 4:
            cmd(VERTEX2F(x - x_offset, y));
            cmd(VERTEX2F(x - x_offset, y + y_offset));
            break;

        case 5:
            cmd(VERTEX2F(x - x_offset, y - y_offset));
            cmd(VERTEX2F(x - x_offset, y));
            break;

        case 6:
            cmd(VERTEX2F(x - x_offset, y));
            cmd(VERTEX2F(x + x_offset, y));
            break;
    }
}

static void seven_segment_digit(int x, int y, int val)
{
    switch (val) {
        case 0:
            draw_segment(x, y, 0);
            draw_segment(x, y, 1);
            draw_segment(x, y, 2);
            draw_segment(x, y, 3);
            draw_segment(x, y, 4);
            draw_segment(x, y, 5);
            break;

        case 1:
            draw_segment(x, y, 1);
            draw_segment(x, y, 2);
            break;

        case 2:
            draw_segment(x, y, 0);
            draw_segment(x, y, 1);
            draw_segment(x, y, 3);
            draw_segment(x, y, 4);
            draw_segment(x, y, 6);
            break;

        case 3:
            draw_segment(x, y, 0);
            draw_segment(x, y, 1);
            draw_segment(x, y, 2);
            draw_segment(x, y, 3);
            draw_segment(x, y, 6);
            break;

        case 4:
            draw_segment(x, y, 1);
            draw_segment(x, y, 2);
            draw_segment(x, y, 5);
            draw_segment(x, y, 6);
            break;

        case 5:
            draw_segment(x, y, 0);
            draw_segment(x, y, 2);
            draw_segment(x, y, 3);
            draw_segment(x, y, 5);
            draw_segment(x, y, 6);
            break;

        case 6:
            draw_segment(x, y, 0);
            draw_segment(x, y, 2);
            draw_segment(x, y, 3);
            draw_segment(x, y, 4);
            draw_segment(x, y, 5);
            draw_segment(x, y, 6);
            break;

        case 7:
            draw_segment(x, y, 0);
            draw_segment(x, y, 1);
            draw_segment(x, y, 2);
            break;

        case 8:
            draw_segment(x, y, 0);
            draw_segment(x, y, 1);
            draw_segment(x, y, 2);
            draw_segment(x, y, 3);
            draw_segment(x, y, 4);
            draw_segment(x, y, 5);
            draw_segment(x, y, 6);
            break;

        case 9:
            draw_segment(x, y, 0);
            draw_segment(x, y, 1);
            draw_segment(x, y, 2);
            draw_segment(x, y, 3);
            draw_segment(x, y, 5);
            draw_segment(x, y, 6);
            break;
    }
}

static void iface_cb(struct net_if *iface, void *user_data)
{
    bool *if_up = user_data;

    if (net_if_is_up(iface)) {
        *if_up = true;
    }
}

static void display_clock(void)
{
    int r;
    int64_t now_ms;
    int refresh_ms = CLOCK_REFRESH_MS;
    bool refresh_time = false;

    k_sem_take(&spi_sem, K_FOREVER);

    cmd_dlstart();
    cmd(CLEAR_COLOR_RGB(0x00, 0x00, 0x00));
    cmd(CLEAR(1, 1, 1));
    cmd(COLOR_RGB(0xf0, 0xf0, 0xf0));

    r = date_time_now(&now_ms);

    if (r) {
        cmd_text(240, 120, 29, OPT_CENTERX, "Unknown time");
        refresh_ms = 2500;
        refresh_time = true;
    }
    else {
        int64_t now_s = now_ms / 1000;

        // Handling EU timezone
        int tz_diff = 1;
        struct tm *now = gmtime(&now_s);
        if (now->tm_mon > 2 && now->tm_mon < 9) {
            // April to September
            tz_diff = 2;
        } else if (now->tm_mon == 2 && (now->tm_mday - now->tm_wday) >= 25 && now->tm_wday != 0) {
            // After last March Sunday
            tz_diff = 2;
        } else if (now->tm_mon == 2 && (now->tm_mday - now->tm_wday) >= 25 && now->tm_wday == 0 &&
                now->tm_hour >= 1) {
            // Last March Sunday after 01:00 UTC
            tz_diff = 2;
        } else if (now->tm_mon == 9 && (now->tm_mday - now->tm_wday) < 25) {
            // Before last October Sunday
            tz_diff = 2;
        } else if (now->tm_mon == 9 && (now->tm_mday - now->tm_wday) >= 25 && now->tm_wday == 0 &&
                now->tm_hour < 1) {
            // Last October Sunday before 01:00 UTC
            tz_diff = 2;
        }

        now_s += tz_diff * 3600;
        now = gmtime(&now_s);

        int hour = now->tm_hour;
        int min  = now->tm_min;

        cmd(LINE_WIDTH(CLOCK_LINE_WIDTH * 16));
        cmd(BEGIN(LINES));

        seven_segment_digit(240 - CLOCK_NUMBER_SPACE / 2 - CLOCK_LINE_LENGTH / 2 -
                CLOCK_DIGIT_SPACE - CLOCK_LINE_LENGTH, 130, hour / 10);
        seven_segment_digit(240 - CLOCK_NUMBER_SPACE / 2 - CLOCK_LINE_LENGTH / 2, 130, hour % 10);

        seven_segment_digit(240 + CLOCK_NUMBER_SPACE / 2 + CLOCK_LINE_LENGTH / 2, 130, min / 10);
        seven_segment_digit(240 + CLOCK_NUMBER_SPACE / 2 + CLOCK_LINE_LENGTH / 2 +
                CLOCK_DIGIT_SPACE + CLOCK_LINE_LENGTH, 130, min % 10);

        cmd(END());
    }

    cmd_number(20, 20, 29, OPT_SIGNED, temp);

    cmd_text(460, 20, 29, OPT_RIGHTX, CONFIG_MCUBOOT_IMAGE_VERSION);

    cmd(DISPLAY());
    cmd_swap();

    k_sem_give(&spi_sem);

    if (refresh_time) {
        bool if_up = false;

        net_if_foreach(iface_cb, &if_up);

        if (if_up) {
            date_time_update_async(NULL);
        }
    }

    k_timer_start(&inactivity_timer, K_MSEC(refresh_ms), K_NO_WAIT);
}

static void display_curr_temps(void)
{
    const data_dispatcher_publish_t *meas[DATA_LOC_NUM];
    const data_dispatcher_publish_t *setting[DATA_LOC_NUM];

    for (int i = 0; i < DATA_LOC_NUM; ++i) {
        data_dispatcher_get(DATA_TEMP_MEASUREMENT, i, &meas[i]);
        data_dispatcher_get(DATA_TEMP_SETTING, i, &setting[i]);
    }

    display_temps(&meas, &setting);
}

static void temp_changed(const data_dispatcher_publish_t *data)
{
    const data_dispatcher_publish_t *meas[DATA_LOC_NUM];
    const data_dispatcher_publish_t *setting[DATA_LOC_NUM];

    switch (curr_screen) {
        case SCREEN_TEMPS:
            break;

        default:
            // Do not display if currently something else is on screen
            return;
    }

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

void inactivity_work_handler(struct k_work *work)
{
    (void)work;

    curr_screen = SCREEN_CLOCK;
    display_clock();
}
