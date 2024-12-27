/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "output.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include "data_dispatcher.h"

#define RLY_NODE   DT_NODELABEL(relay0)

#define RLY_GPIO_LABEL DT_GPIO_LABEL(RLY_NODE, gpios)
#define RLY_GPIO_PIN   DT_GPIO_PIN(RLY_NODE, gpios)
#define RLY_GPIO_FLAGS (GPIO_OUTPUT | DT_GPIO_FLAGS(RLY_NODE, gpios))

static const struct gpio_dt_spec rly_gpio_spec = GPIO_DT_SPEC_GET(RLY_NODE, gpios);

static void out_changed(const data_dispatcher_publish_t *data);
static void ctlr_changed(const data_dispatcher_publish_t *data);

static data_dispatcher_subscribe_t out_sbscr = {
    .callback = out_changed,
};
static data_dispatcher_subscribe_t ctlr_sbscr = {
    .callback = ctlr_changed,
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

static void onoff_process(const data_dispatcher_publish_t *data)
{
    int val = 0;

    if (data == NULL) {
        data_dispatcher_get(DATA_OUTPUT, CTLR_LOC, &data);
    }

    if (data->output) {
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
        data_dispatcher_get(DATA_OUTPUT, CTLR_LOC, &out_data);

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

    if (rly_gpio_spec.port == NULL) {
        // TODO: report error
        return;
    }

    ret = gpio_pin_configure_dt(&rly_gpio_spec, 0);
    if (ret != 0) {
        // TODO: report error
        return;
    }

    const data_dispatcher_publish_t *ctlr_data;
    data_dispatcher_get(DATA_CONTROLLER, CTLR_LOC, &ctlr_data);
    ctlr_mode = ctlr_data->controller.mode;
    k_thread_start(pwm_thread_id);

    data_dispatcher_subscribe(DATA_OUTPUT, &out_sbscr);
    data_dispatcher_subscribe(DATA_CONTROLLER, &ctlr_sbscr);
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
