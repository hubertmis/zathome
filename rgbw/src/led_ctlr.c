/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "led_ctlr.h"

#include <stdbool.h>

#include <kernel.h>

#define AUTO_ANIM_DUR_MS 3000
#define DIMMED_ANIM_DUR_MS 10000

static struct leds_brightness leds_auto;
static struct leds_brightness leds_manual;
static unsigned manual_anim_dur_ms;

int64_t manual_timestamp;
int64_t dimmed_timestamp;

static void timer_handler_invalidate_manual(struct k_timer *timer_id);
static void work_handler_invalidate_manual(struct k_work *work);
static void timer_handler_dimmer(struct k_timer *timer_id);
static void work_handler_dimmer(struct k_work *work);

K_TIMER_DEFINE(manual_invalidate_timer, timer_handler_invalidate_manual, NULL);
K_WORK_DEFINE(manual_invalidator, work_handler_invalidate_manual);

K_TIMER_DEFINE(dimmer_timer, timer_handler_dimmer, NULL);
K_WORK_DEFINE(dimmer_invalidator, work_handler_dimmer);

static void disable_leds(struct leds_brightness *leds)
{
	leds->r = 0;
	leds->g = 0;
	leds->b = 0;
	leds->w = 0;
}

static void process(void)
{
	struct leds_brightness dimmed;
	disable_leds(&dimmed);

	if (dimmed_timestamp > manual_timestamp) {
		led_anim(&dimmed, DIMMED_ANIM_DUR_MS);
	} else if (manual_timestamp > 0) {
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

	manual_timestamp = 0;
	process();
}

static void timer_handler_dimmer(struct k_timer *timer_id)
{
	(void)timer_id;
	k_work_submit(&dimmer_invalidator);
}

static void work_handler_dimmer(struct k_work *work)
{
	(void)work;

	dimmed_timestamp = 0;
	process();
}

void led_ctlr_init(void)
{
	manual_timestamp = 0;
	dimmed_timestamp = 0;
	disable_leds(&leds_auto);
	disable_leds(&leds_manual);
}

int led_ctlr_set_auto(const struct leds_brightness *leds)
{
	leds_auto = *leds;
	process();
	return 0;
}

int led_ctlr_set_manual(const struct leds_brightness *leds, unsigned anim_dur_ms, unsigned long validity_ms)
{
	struct leds_brightness dimmed;
	disable_leds(&dimmed);

	if (leds_brightness_equal(leds, &dimmed) && dimmed_timestamp) {
		dimmed_timestamp = k_uptime_get();
	} else {
		manual_timestamp = k_uptime_get();
		leds_manual = *leds;
		manual_anim_dur_ms = anim_dur_ms;
	}

	process();

	k_timer_start(&manual_invalidate_timer, K_MSEC(validity_ms), K_NO_WAIT);
	return 0;
}

int led_ctlr_reset_manual(void)
{
	manual_timestamp = 0;
	process();
	return 0;
}

int led_ctlr_dim(unsigned long validity_ms)
{
	if (!dimmed_timestamp) {
		dimmed_timestamp = k_uptime_get();
	}

	process();

	k_timer_start(&dimmer_timer, K_MSEC(validity_ms), K_NO_WAIT);
	return 0;
}

int led_ctlr_reset_dimmer(void)
{
	dimmed_timestamp = 0;
	process();
	return 0;
}
