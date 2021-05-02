/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led.h"

#include <device.h>
#include <drivers/led.h>
#include <zephyr.h>

#if DT_NODE_HAS_STATUS(DT_INST(0, pwm_leds), okay)
#define LED_PWM_NODE_ID     DT_INST(0, pwm_leds)
#define LED_PWM_DEV_NAME    DT_INST_PROP_OR(0, label, "LED_PWM_0")
#else
#error "No LED PWM device found"
#endif

enum { RED, GREEN, BLUE, WHITE, LEDS_NUM };

static const struct device *led_pwm;

void led_init(void)
{
    led_pwm = device_get_binding(LED_PWM_DEV_NAME);
}

int led_set(unsigned red, unsigned green, unsigned blue, unsigned white)
{
    int err;

    if (!led_pwm) {
        return -ENODEV;
    }

    err = led_set_brightness(led_pwm, RED, 100 - red);
    if (err < 0) {
        return err;
    }

    err = led_set_brightness(led_pwm, GREEN, 100 - green);
    if (err < 0) {
        return err;
    }

    err = led_set_brightness(led_pwm, BLUE, 100 - blue);
    if (err < 0) {
        return err;
    }

    err = led_set_brightness(led_pwm, WHITE, 100 - white);
    if (err < 0) {
        return err;
    }

    return 0;
}
