/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_ctlr.h"

#include <stdbool.h>

#include <kernel.h>

#define DEACTIVATED_LED (MAX_BRIGHTNESS + 1)
#define AUTO_ANIM_DUR_MS 3000

static struct leds_brightness leds_auto;
static struct leds_brightness leds_manual;
static unsigned manual_anim_dur_ms;

static void timer_handler_invalidate_manual(struct k_timer *timer_id);
static void work_handler_invalidate_manual(struct k_work *work);

K_TIMER_DEFINE(manual_invalidate_timer, timer_handler_invalidate_manual, NULL);
K_WORK_DEFINE(manual_invalidator, work_handler_invalidate_manual);

static void deactivate_leds(struct leds_brightness *leds)
{
	leds->w = DEACTIVATED_LED;
}

static bool are_leds_active(const struct leds_brightness *leds)
{
	return leds->w < DEACTIVATED_LED;
}

static void disable_leds(struct leds_brightness *leds)
{
	leds->r = 0;
	leds->g = 0;
	leds->b = 0;
	leds->w = 0;
}

static void process(void)
{
	if (are_leds_active(&leds_manual)) {
		led_anim(&leds_manual, manual_anim_dur_ms);
	} else {
		led_anim(&leds_auto, AUTO_ANIM_DUR_MS);
	}
}

static void timer_handler_invalidate_manual(struct k_timer *timer_id)
{
	(void)timer_id;

	k_work_submit(&manual_invalidator);
}

static void work_handler_invalidate_manual(struct k_work *work)
{
	(void)work;

	deactivate_leds(&leds_manual);
	process();
}


void led_ctlr_init(void)
{
	disable_leds(&leds_auto);
	disable_leds(&leds_manual);
	deactivate_leds(&leds_manual);
}

int led_ctlr_set_auto(const struct leds_brightness *leds)
{
	leds_auto = *leds;
	process();
}

int led_ctlr_set_manual(const struct leds_brightness *leds, unsigned anim_dur_ms, unsigned long validity_ms)
{
	leds_manual = *leds;
	manual_anim_dur_ms = anim_dur_ms;
	process();

	k_timer_start(&manual_invalidate_timer, K_MSEC(validity_ms), K_NO_WAIT);
}

int led_ctlr_reset_manual(void)
{
	deactivate_leds(&leds_manual);
	process();
}
