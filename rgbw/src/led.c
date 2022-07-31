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

struct led_data {
	int64_t start_ts;
	int64_t target_ts;
	uint8_t start_val;
	uint8_t curr_val;
	uint8_t target_val;
};

static struct led_data led_values[LEDS_NUM];


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

#define ANIM_STACK_SIZE 512
#define ANIM_PRIORITY 1

K_SEM_DEFINE(anim_sem, 0, 1);

static void animate_led(int64_t now, struct led_data *data)
{
	if (!data) return;

	float anim_dur = data->target_ts - data->start_ts;
	float anim_elapsed = now - data->start_ts;
	float anim_span = (int32_t)data->target_val - (int32_t)data->start_val;
	float y;
	float x = anim_elapsed;

	if (anim_elapsed >= anim_dur) {
		data->curr_val = data->target_val;
		return;
	}

#if GRACEFUL_ANIMATION || 1
	float a = 2 * anim_span / (anim_dur * anim_dur);
	if (x < anim_dur / 2) {
		y = a * x * x;
	} else {
		float anim_remaining = anim_dur - x;
		y = anim_span - a * (anim_remaining * anim_remaining);
	}
#else // LINEAR_ANIMATION
	float a = anim_span / anim_dur;
	y = a * x;
#endif

	uint8_t curr_val = (float)data->start_val + y;
	data->curr_val = curr_val;
}

static void anim_entry_point(void *, void *, void *)
{
	while (1) {
		bool ready = true;
		for (size_t i = 0; i < LEDS_NUM; i++) {
			if (led_values[i].curr_val != led_values[i].target_val) {
				ready = false;
				break;
			}
		}

		k_sem_take(&anim_sem, ready ? K_FOREVER : K_MSEC(20));

		int64_t now = k_uptime_get();
		for (size_t i = 0; i < LEDS_NUM; i++) {
			animate_led(now, led_values + i);
		}
		for (size_t i = 0; i < LEDS_NUM; i++) {
			led_set_brightness(i, led_values[i].curr_val);
		}
	}
}

K_THREAD_DEFINE(anim_tid, ANIM_STACK_SIZE,
                anim_entry_point, NULL, NULL, NULL,
                ANIM_PRIORITY, 0, 0);

void led_init(void)
{
	// Intentionally empty
}

int led_get(struct leds_brightness *leds)
{
	leds->r = led_values[RED].target_val;
	leds->g = led_values[GREEN].target_val;
	leds->b = led_values[BLUE].target_val;
	leds->w = led_values[WHITE].target_val;

	return 0;
}

int led_anim(const struct leds_brightness *leds, unsigned dur_ms)
{
	int64_t now = k_uptime_get();
	int64_t start_ts = now;
	int64_t target_ts = now + dur_ms;

	for (size_t i = 0; i < LEDS_NUM; i++) {
		led_values[i].start_val = led_values[i].curr_val;
		led_values[i].start_ts = start_ts;
		led_values[i].target_ts = target_ts;
	}

	led_values[RED].target_val = leds->r;
	led_values[GREEN].target_val = leds->g;
	led_values[BLUE].target_val = leds->b;
	led_values[WHITE].target_val = leds->w;
	
	k_sem_give(&anim_sem);

	return 0;
}

int led_set(const struct leds_brightness *leds)
{
    return led_anim(leds, 0);
}
