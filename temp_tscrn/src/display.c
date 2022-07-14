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

#include <continuous_sd.h>

#include "data_dispatcher.h"
#include "light_conn.h"
#include "prov.h"
#include "ft8xx/ft8xx.h"
#include "ft8xx/ft8xx_common.h"
#include "ft8xx/ft8xx_copro.h"
#include "ft8xx/ft8xx_dl.h"
#include "ft8xx/ft8xx_memory.h"

#define CLOCK_LINE_WIDTH   10
#define CLOCK_LINE_LENGTH  60
#define CLOCK_DIGIT_SPACE  50
#define CLOCK_NUMBER_SPACE 60

#define CLOCK_BRIGHTNESS  0x02
#define SCREEN_BRIGHTNESS 0x20

#define LIGHT_TYPE "rgbw"

// TODO: Create a thread to display screen. Such thread should prevent preemption of display procedure with another display procedure.
K_SEM_DEFINE(spi_sem, 1, 1);

enum screen_t {
    SCREEN_CLOCK,
    SCREEN_MENU,
    SCREEN_LIGHTS_MENU,
    SCREEN_LIGHT_CONTROL,
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

K_SEM_DEFINE(touch_sem, 0, 1);

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
static void display_menu(void);
static void display_lights_menu(void);
static void display_light_control(void);
static void display_curr_temps(void);
static void temp_changed(const data_dispatcher_publish_t *data);
static void vent_changed(const data_dispatcher_publish_t *data);
static void light_changed(const data_dispatcher_publish_t *data);

static data_dispatcher_subscribe_t temp_sbscr = {
    .callback = temp_changed,
};

static data_dispatcher_subscribe_t vent_sbscr = {
    .callback = vent_changed,
};

static data_dispatcher_subscribe_t light_sbscr = {
    .callback = light_changed,
};

void display_init(void)
{
    data_dispatcher_subscribe(DATA_TEMP_MEASUREMENT, &temp_sbscr);
    data_dispatcher_subscribe(DATA_TEMP_SETTING, &temp_sbscr);
    data_dispatcher_subscribe(DATA_VENT_CURR, &vent_sbscr);
    data_dispatcher_subscribe(DATA_LIGHT_CURR, &light_sbscr);
    display_clock();
    ft8xx_register_int (touch_irq);
    k_thread_start(touch_thread_id);
}

uint32_t temp;

static void process_touch_menu(uint8_t tag,uint32_t iteration)
{
	bool publish_vent = false;
	const data_dispatcher_publish_t *p_data;
	data_dispatcher_publish_t data;

	switch (tag) {
		case 1:
			curr_screen = SCREEN_LIGHTS_MENU;
			display_lights_menu();
			break;

		case 2:
			curr_screen = SCREEN_TEMPS;
			display_curr_temps();
			break;

		case 5:
			publish_vent = true;
			break;

		default:
			// TODO: Log error
			return;
	}

    if (publish_vent) {
        data_vent_sm_t next_sm = VENT_SM_NONE;
        static int64_t last_vent_time;
        int64_t now = k_uptime_get();

        if ((now - last_vent_time) > 500LL) {
            last_vent_time = now;

            data_dispatcher_get(DATA_VENT_CURR, 0, &p_data);
            switch (p_data->vent_mode) {
                case VENT_SM_UNAVAILABLE:
                case VENT_SM_NONE:
                    next_sm = VENT_SM_AIRING;
                    break;

                case VENT_SM_AIRING:
                    next_sm = VENT_SM_NONE;
                    break;
            }

            memset(&data, 0, sizeof(data));
            data.type = DATA_VENT_REQ;
            data.vent_mode = next_sm;
            data_dispatcher_publish(&data);

            data.type = DATA_VENT_CURR;
            data.vent_mode = next_sm;
            data_dispatcher_publish(&data);
        }
    }
}

static void process_touch_lights_menu(uint8_t tag,uint32_t iteration)
{
	switch (tag) {
		case 1:
			curr_screen = SCREEN_LIGHT_CONTROL;
			light_conn_enable_polling(LIGHT_CONN_ITEM_BEDROOM_BED);
			display_light_control();
			break;

		case 2:
			curr_screen = SCREEN_LIGHT_CONTROL;
			light_conn_enable_polling(LIGHT_CONN_ITEM_LIVINGROOM);
			display_light_control();
			break;

		case 3:
			curr_screen = SCREEN_LIGHT_CONTROL;
			light_conn_enable_polling(LIGHT_CONN_ITEM_BEDROOM_WARDROBE);
			display_light_control();
			break;

		case 4:
			curr_screen = SCREEN_LIGHT_CONTROL;
			light_conn_enable_polling(LIGHT_CONN_ITEM_DININGROOM);
			display_light_control();
			break;

		case 253:
			if (iteration) break;
			curr_screen = SCREEN_MENU;
			display_menu();
			break;

		default:
			// TODO: Log error
			return;
	}
}

static int get_tracker_val(uint8_t tag)
{
	uint32_t tracker = ft8xx_get_tracker_value();
	if ((tracker & 0xff) != tag) return -1;
	return tracker >> 16;
}

static bool is_light_on(const data_light_t *light)
{
	return light->r > 0 || light->g > 0 || light->b > 0 || light->w > 0;
}

static void publish_light(data_dispatcher_publish_t *p_curr_data)
{
	data_dispatcher_publish_t req_data = *p_curr_data;
	req_data.type = DATA_LIGHT_REQ;

	data_dispatcher_publish(&req_data);
	data_dispatcher_publish(p_curr_data);
}

static void set_light(uint8_t tag, data_dispatcher_publish_t *publish_data, uint8_t *val)
{
	int tracker_val = get_tracker_val(tag);
	if (tracker_val < 0) return;

	const data_dispatcher_publish_t *p_data;
	data_dispatcher_get(DATA_LIGHT_CURR, 0, &p_data);
	*publish_data = *p_data;
	*val = tracker_val >> 8;
	publish_light(publish_data);
}

static void toggle_light(void)
{
	const data_dispatcher_publish_t *p_data;
	data_dispatcher_publish_t publish_data;

	data_dispatcher_get(DATA_LIGHT_CURR, 0, &p_data);
	publish_data = *p_data;

	if (is_light_on(&p_data->light)) {
		publish_data.light.r = 0;
		publish_data.light.g = 0;
		publish_data.light.b = 0;
		publish_data.light.w = 0;
	} else {
		publish_data.light.w = 255;
	}

	publish_light(&publish_data);
}

static void process_touch_light_control(uint8_t tag,uint32_t iteration)
{
	data_dispatcher_publish_t publish_data;

	switch (tag) {
		case 1:
			set_light(tag, &publish_data, &publish_data.light.r);
			break;
		case 2:
			set_light(tag, &publish_data, &publish_data.light.g);
			break;
		case 3:
			set_light(tag, &publish_data, &publish_data.light.b);
			break;
		case 4:
			set_light(tag, &publish_data, &publish_data.light.w);
			break;

		case 10:
			if (iteration) break;
			toggle_light();
			break;

		case 253:
			light_conn_disable_polling();
			curr_screen = SCREEN_LIGHTS_MENU;
			display_lights_menu();
			break;

		default:
			// TODO: Log error
			return;
	}
}

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

        case 253:
            curr_screen = SCREEN_MENU;
            display_menu();
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
            curr_screen = SCREEN_MENU;
            display_menu();
            wr8(REG_PWM_DUTY, SCREEN_BRIGHTNESS);
            break;

	case SCREEN_MENU:
	    process_touch_menu(tag, iteration);
	    break;

	case SCREEN_LIGHTS_MENU:
	    process_touch_lights_menu(tag, iteration);
	    break;

	case SCREEN_LIGHT_CONTROL:
	    process_touch_light_control(tag, iteration);
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

            k_sem_take(&touch_sem, K_MSEC(100));
        } else {
            k_sem_take(&touch_sem, K_FOREVER);
        }

    }
}

static void touch_irq(void)
{
    k_sem_give(&touch_sem);
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

    cmd(TAG(253));
    cmd_text(2, 20, 29, 0, "Back");

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

    wr8(REG_PWM_DUTY, CLOCK_BRIGHTNESS);

    cmd_dlstart();
    cmd(CLEAR_COLOR_RGB(0x00, 0x00, 0x00));
    cmd(CLEAR(1, 1, 1));

    // Draw black rectangle to capture touch events
    cmd(COLOR_RGB(0x00, 0x00, 0x00));
    cmd(LINE_WIDTH(1 * 16));
    cmd(BEGIN(RECTS));
    cmd(VERTEX2II(0, 0, 0, 0));
    cmd(VERTEX2II(480, 272, 0, 0));
    cmd(END());

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

static void display_updated_menu(const data_dispatcher_publish_t *vent)
{
    k_sem_take(&spi_sem, K_FOREVER);

    cmd_dlstart();
    cmd(CLEAR_COLOR_RGB(0x00, 0x00, 0x00));
    cmd(CLEAR(1, 1, 1));
    cmd(COLOR_RGB(0xf0, 0xf0, 0xf0));

    cmd(TAG(1));
    cmd_text(20, 40, 29, 0, "Lights");
    cmd(TAG(2));
    cmd_text(260, 40, 29, 0, "Heat");

    switch (vent->vent_mode) {
        case VENT_SM_UNAVAILABLE:
            cmd(COLOR_RGB(0x70, 0x70, 0x70));
	    cmd(TAG(0));
            cmd_text(20, 220, 29, 0, "Airing");
            break;

        case VENT_SM_NONE:
            cmd(TAG(5));
            cmd_text(20, 220, 29, 0, "Airing");
            cmd(TAG(0));
            break;

        case VENT_SM_AIRING:
            cmd(COLOR_RGB(0xf0, 0x00, 0x00));
            cmd(TAG(5));
            cmd_text(20, 220, 29, 0, "Airing");
            cmd(TAG(0));
            break;
    }

    cmd(DISPLAY());
    cmd_swap();

    k_sem_give(&spi_sem);
}

static void display_menu(void)
{
    const data_dispatcher_publish_t *vent;

    data_dispatcher_get(DATA_VENT_CURR, 0, &vent);

    display_updated_menu(vent);
}


static void display_light_menu_entry(int16_t x, int16_t y, uint8_t tag,
	       	const char *label, const char *srv_name)
{
    struct in6_addr in6_addr = {0};
    int r = continuous_sd_get_addr(srv_name, LIGHT_TYPE, &in6_addr);

    if (r) {
        cmd(TAG(0));
        cmd(COLOR_RGB(0x70, 0x70, 0x70));
    } else {
        cmd(TAG(tag));
        cmd(COLOR_RGB(0xf0, 0xf0, 0xf0));
    }

    cmd_text(x, y, 29, 0, label);
}

static void display_lights_menu(void)
{
    k_sem_take(&spi_sem, K_FOREVER);

    cmd_dlstart();
    cmd(CLEAR_COLOR_RGB(0x00, 0x00, 0x00));
    cmd(CLEAR(1, 1, 1));

#if 0
    static char addr[LIGHT_CONN_ITEM_NUM][64];
    for (int i = 0; i < 4; i++) {
	    struct in6_addr in6_addr = {0};
	    continuous_sd_get_addr(names[i], LIGHT_TYPE, &in6_addr);
	    snprintf(addr[i], 64, "%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x:%02x%02x",
		     in6_addr.s6_addr[0], in6_addr.s6_addr[1], in6_addr.s6_addr[2], in6_addr.s6_addr[3],
		     in6_addr.s6_addr[4], in6_addr.s6_addr[5], in6_addr.s6_addr[6], in6_addr.s6_addr[7],
		     in6_addr.s6_addr[8], in6_addr.s6_addr[9], in6_addr.s6_addr[10], in6_addr.s6_addr[11],
		     in6_addr.s6_addr[12], in6_addr.s6_addr[13], in6_addr.s6_addr[14], in6_addr.s6_addr[15]);
    }
    cmd_text(20, 110, 27, 0, addr[0]);
    cmd_text(20, 170, 27, 0, addr[1]);
    cmd_text(20, 200, 27, 0, addr[2]);
    cmd_text(20, 220, 27, 0, addr[3]);
#endif

    display_light_menu_entry(20, 80, 1, "Bed room: bed", "bbl");
    display_light_menu_entry(260, 80, 2, "Living room", "ll");
    display_light_menu_entry(20, 140, 3, "Bed room: wardrobe", "bwl");
    display_light_menu_entry(260, 140, 4, "Dining room", "drl");

    cmd(COLOR_RGB(0xf0, 0xf0, 0xf0));
    cmd(TAG(253));
    cmd_text(2, 20, 29, 0, "Back");

    cmd(DISPLAY());
    cmd_swap();

    k_sem_give(&spi_sem);
}

static void update_light_control(const data_light_t *light)
{
    const char *labels[4] = { "R", "G", "B", "W" };
    bool on = light->r > 0 || light->g > 0 || light->b > 0 || light->w > 0;
    uint8_t vals[] = {
	    light->r,
	    light->g,
	    light->b,
	    light->w,
    };

    k_sem_take(&spi_sem, K_FOREVER);

    cmd_dlstart();
    cmd(CLEAR_COLOR_RGB(0x00, 0x00, 0x00));
    cmd(CLEAR(1, 1, 1));
    cmd(COLOR_RGB(0xf0, 0xf0, 0xf0));

    for (int i = 0; i < sizeof(labels) / sizeof(labels[0]); i++) {
	    int x = 480 / 4 * (2 * i + 1) / 2;

	    cmd_track(x-10, 80, 20, 120, i+1);

	    cmd_text(x, 60, 29, OPT_CENTER, labels[i]);
	    cmd(TAG(i+1));
	    cmd_slider(x - 6, 80, 12, 120, OPT_FLAT, vals[i], 255);
	    cmd(TAG(0));
	    cmd_number(x, 220, 29, OPT_CENTER, vals[i]);
    }

    cmd(TAG(10));
    cmd_toggle(220, 20, 40, 27, OPT_FLAT, on ? 65535:0, "off" "\xff" "on");

    cmd(TAG(253));
    cmd_text(2, 20, 29, 0, "Back");

    cmd(DISPLAY());
    cmd_swap();

    k_sem_give(&spi_sem);
}

static void display_light_control(void)
{
	// TODO: loading display until get a new value?
    const data_dispatcher_publish_t *p_data;
    data_dispatcher_get(DATA_LIGHT_CURR, 0, &p_data);

    update_light_control(&p_data->light);
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

static void vent_changed(const data_dispatcher_publish_t *data)
{
    const data_dispatcher_publish_t *vent = data;

    switch (curr_screen) {
        case SCREEN_MENU:
            break;

        default:
            // Do not display if currently something else is on screen
            return;
    }

    display_updated_menu(vent);
}

static void light_changed(const data_dispatcher_publish_t *data)
{
    switch (curr_screen) {
        case SCREEN_LIGHT_CONTROL:
            break;

        default:
            // Do not display if currently something else is on screen
            return;
    }

    update_light_control(&data->light);
}

void inactivity_work_handler(struct k_work *work)
{
    (void)work;

    light_conn_disable_polling();
    curr_screen = SCREEN_CLOCK;
    display_clock();
}
