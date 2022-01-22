/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT pwm_leds

#include "led.h"

#include <device.h>
#include <drivers/pwm.h>
#include <zephyr.h>

// Calculate PWM value as polynomial for more linear user experience.
#define MAX_BRIGHTNESS 255U
#define MAX_PERIOD 32767U
#define CALC_PWM_VALUE(VAL) (((uint32_t)(VAL) * (uint32_t)(VAL)) / 2U)
#define PERIOD CALC_PWM_VALUE(MAX_BRIGHTNESS)
BUILD_ASSERT(PERIOD <= MAX_PERIOD, "Invalid PWM period configuration");

struct led_pwm {
	const struct device *dev;
	uint32_t channel;
	uint32_t period;
	pwm_flags_t flags;
};

struct led_pwm_config {
	int num_leds;
	const struct led_pwm *led;
};

#define LED_PWM(led_node_id)						\
{									\
	.dev		= DEVICE_DT_GET(DT_PWMS_CTLR(led_node_id)),	\
	.channel	= DT_PWMS_CHANNEL(led_node_id),			\
	.period		= DT_PHA_OR(led_node_id, pwms, period, 100),	\
	.flags		= DT_PHA_OR(led_node_id, pwms, flags,		\
				    PWM_POLARITY_NORMAL),		\
},

#define LED_PWM_DEVICE(id)					\
								\
static const struct led_pwm led_pwm_##id[] = {			\
	DT_INST_FOREACH_CHILD(id, LED_PWM)			\
};								\
								\
static const struct led_pwm_config led_pwm_config_##id = {	\
	.num_leds	= ARRAY_SIZE(led_pwm_##id),		\
	.led		= led_pwm_##id,				\
};

DT_INST_FOREACH_STATUS_OKAY(LED_PWM_DEVICE)

enum { RED, GREEN, BLUE, WHITE, LEDS_NUM };

static uint8_t led_values[LEDS_NUM];


static int led_set_brightness(uint32_t channel, uint8_t brightness)
{
    if (channel >= led_pwm_config_0.num_leds) {
	    return -EINVAL;
    }
    if (brightness > MAX_BRIGHTNESS) {
	    return -EINVAL;
    }

    const struct device *dev = led_pwm_0[channel].dev;
    if (!dev) {
        return -ENODEV;
    }
    if (!device_is_ready(dev)) {
        return -ENODEV;
    }

    const uint32_t pulse = PERIOD - CALC_PWM_VALUE(brightness);

    return pwm_pin_set_cycles(dev, led_pwm_0[channel].channel,
		    PERIOD, pulse, led_pwm_0[channel].flags);
}

void led_init(void)
{
	// Intentionally empty
}

int led_get(unsigned *red, unsigned *green, unsigned *blue, unsigned *white)
{
	*red   = led_values[RED];
	*green = led_values[GREEN];
	*blue  = led_values[BLUE];
	*white = led_values[WHITE];

	return 0;
}

int led_set(unsigned red, unsigned green, unsigned blue, unsigned white)
{
    int err;

    err = led_set_brightness(RED, red);
    if (err < 0) {
        return err;
    }
    led_values[RED] = red;

    err = led_set_brightness(GREEN, green);
    if (err < 0) {
        return err;
    }
    led_values[GREEN] = green;

    err = led_set_brightness(BLUE, blue);
    if (err < 0) {
        return err;
    }
    led_values[BLUE] = blue;

    err = led_set_brightness(WHITE, white);
    if (err < 0) {
        return err;
    }
    led_values[WHITE] = white;

    return 0;
}
