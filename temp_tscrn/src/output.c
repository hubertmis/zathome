/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "output.h"

#include <stddef.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "data_dispatcher.h"

// DEBUG:
#include "display.h"

#define RLY_NODE   DT_NODELABEL(relay0)

#define RLY_GPIO_LABEL DT_GPIO_LABEL(RLY_NODE, gpios)
#define RLY_GPIO_PIN   DT_GPIO_PIN(RLY_NODE, gpios)
#define RLY_GPIO_FLAGS (GPIO_OUTPUT | DT_GPIO_FLAGS(RLY_NODE, gpios))

static const struct gpio_dt_spec rly_gpio_spec = GPIO_DT_SPEC_GET(RLY_NODE, gpios);

static void forced_switch(struct k_work *item);
K_WORK_DELAYABLE_DEFINE(forced_switching_dwork, forced_switch);

static void out_changed(const data_dispatcher_publish_t *data);
static void ctlr_changed(const data_dispatcher_publish_t *data);
static void frc_sw_changed(const data_dispatcher_publish_t *data);

static data_dispatcher_subscribe_t out_sbscr = {
    .callback = out_changed,
};
static data_dispatcher_subscribe_t ctlr_sbscr = {
    .callback = ctlr_changed,
};
static data_dispatcher_subscribe_t frc_sw_sbscr = {
    .callback = frc_sw_changed,
};

#define PWM_THREAD_STACK_SIZE 1024
#define PWM_THREAD_PRIO       1
static void pwm_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(pwm_thread_id, PWM_THREAD_STACK_SIZE,
                pwm_thread_process, NULL, NULL, NULL,
                PWM_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

#define PWM_INTERVAL (1000UL * 60UL * 2UL)

#define CTLR_LOC DATA_LOC_REMOTE

static volatile data_ctlr_mode_t ctlr_mode;

static void onoff_process(const data_dispatcher_publish_t *out_data)
{
    int val = 0;
    const data_dispatcher_publish_t *prj_data;
    const data_dispatcher_publish_t *frc_sw_data;

    if (out_data == NULL) {
        data_dispatcher_get(DATA_OUTPUT, CTLR_LOC, &out_data);
    }
    data_dispatcher_get(DATA_PRJ_ENABLED, CTLR_LOC, &prj_data);
    data_dispatcher_get(DATA_FORCED_SWITCHING, CTLR_LOC, &frc_sw_data);

    if (frc_sw_data->forced_switches > 0) {
        // Relay is already set by forced switching handling
        return;
    } else  if (prj_data->prj_validity > 0) {
        // Disable relay if the projector is enabled
        val = 0;
    } else if (out_data->output) {
        val = 1;
    }
    else {
        val = 0;
    }

    gpio_pin_set_dt(&rly_gpio_spec, val);
}

static void pwm_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    while (1) {
        if (ctlr_mode != DATA_CTLR_PID) {
            gpio_pin_set_dt(&rly_gpio_spec, 0);
            k_sleep(K_FOREVER);
            continue;
        }

        const data_dispatcher_publish_t *out_data;
        const data_dispatcher_publish_t *prj_data;
        const data_dispatcher_publish_t *frc_sw_data;
        data_dispatcher_get(DATA_OUTPUT, CTLR_LOC, &out_data);
        data_dispatcher_get(DATA_PRJ_ENABLED, CTLR_LOC, &prj_data);
        data_dispatcher_get(DATA_FORCED_SWITCHING, CTLR_LOC, &frc_sw_data);

        if (frc_sw_data->forced_switches) {
            // Relay is already set by forced switching handling
            k_sleep(K_MSEC(PWM_INTERVAL));
            continue;
        }

        if (prj_data->prj_validity) {
            // Disable relay if the projector is enabled
            gpio_pin_set_dt(&rly_gpio_spec, 0);
            k_sleep(K_MSEC(PWM_INTERVAL));
            continue;
        }

        uint32_t time_on  = (uint64_t)(out_data->output) * PWM_INTERVAL / UINT16_MAX;
        uint32_t time_off = PWM_INTERVAL - time_on;

        if (time_on > PWM_INTERVAL) {
            time_on = PWM_INTERVAL;
            time_off = 0;
        }

        if (time_on > 0) {
            gpio_pin_set_dt(&rly_gpio_spec, 1);
            k_sleep(K_MSEC(time_on));
        }

        if (time_off > 0) {
            gpio_pin_set_dt(&rly_gpio_spec, 0);
            k_sleep(K_MSEC(time_off));
        }
    }
}

void output_init(void)
{
    int ret;

    if (!gpio_is_ready_dt(&rly_gpio_spec)) {
        return;
    }

    if (rly_gpio_spec.port == NULL) {
        return;
    }

    ret = gpio_pin_configure_dt(&rly_gpio_spec, GPIO_OUTPUT_INACTIVE);
    if (ret != 0) {
        return;
    }

    const data_dispatcher_publish_t *ctlr_data;
    data_dispatcher_get(DATA_CONTROLLER, CTLR_LOC, &ctlr_data);
    ctlr_mode = ctlr_data->controller.mode;
    k_thread_start(pwm_thread_id);

    k_work_init_delayable(&forced_switching_dwork, forced_switch);

    data_dispatcher_subscribe(DATA_OUTPUT, &out_sbscr);
    data_dispatcher_subscribe(DATA_CONTROLLER, &ctlr_sbscr);
    data_dispatcher_subscribe(DATA_FORCED_SWITCHING, &frc_sw_sbscr);
}

void output_relay_toggle(void)
{
    gpio_pin_toggle_dt(&rly_gpio_spec);
}

static void out_changed(const data_dispatcher_publish_t *data)
{
    if (ctlr_mode != DATA_CTLR_ONOFF) {
        return;
    }
    if (data->loc != CTLR_LOC) {
        return;
    }

    onoff_process(data);
}

static void ctlr_changed(const data_dispatcher_publish_t *data)
{
    if (data->loc != CTLR_LOC) {
        return;
    }

    if (data->controller.mode == ctlr_mode) {
        if (ctlr_mode == DATA_CTLR_ONOFF) {
            onoff_process(NULL);
        }

        return;
    }

    data_ctlr_mode_t prev_mode = ctlr_mode;
    ctlr_mode = data->controller.mode;

    // Stop previous output controller
    switch (prev_mode) {
        case DATA_CTLR_ONOFF:
            // Intentionally empty
            break;

        case DATA_CTLR_PID:
            // Wakeup PWM thread to stop its execution and sleep forever
            k_wakeup(pwm_thread_id);
            break;
    }

    // Start new output controller
    switch (ctlr_mode) {
        case DATA_CTLR_ONOFF:
            onoff_process(NULL);
            break;

        case DATA_CTLR_PID:
            // Wakeup PWM thread to start its execution
            k_wakeup(pwm_thread_id);
            break;
    }
}

static void forced_switch(struct k_work *item)
{
    (void)item;

    const data_dispatcher_publish_t *current_frc_sw;
    data_dispatcher_get(DATA_FORCED_SWITCHING, CTLR_LOC, &current_frc_sw);

    uint16_t remaining_frc_sw = current_frc_sw->forced_switches;

    if (remaining_frc_sw > 0) {
        data_dispatcher_publish_t new_frc_sw = {
            .type = DATA_FORCED_SWITCHING,
            .loc = CTLR_LOC,
            .forced_switches = remaining_frc_sw - 1,
        };

        data_dispatcher_publish(&new_frc_sw);
    }
}

static void frc_sw_changed(const data_dispatcher_publish_t *data)
{
    uint16_t num_switches = data->forced_switches;

    if (data->loc != CTLR_LOC) return;

    if (num_switches) {
        gpio_pin_set_dt(&rly_gpio_spec, num_switches % 2);

        k_work_schedule(&forced_switching_dwork, K_MSEC(500));
    } else {
        if (ctlr_mode == DATA_CTLR_ONOFF) {
            const data_dispatcher_publish_t *out_data;
            data_dispatcher_get(DATA_OUTPUT, CTLR_LOC, &out_data);
            onoff_process(out_data);
        } else {
            // Disable the relay and wait. PWM thread is still iterating. It will pick up controlling the relay.
            gpio_pin_set_dt(&rly_gpio_spec, 0);
        }
    }
}
