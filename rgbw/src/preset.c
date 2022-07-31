/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "preset.h"

#include "led.h"

#include <errno.h>

int preset_get(unsigned preset,
		struct leds_brightness *leds,
		unsigned *duration)
{
	switch (preset) {
		case 0:
		{
			struct leds_brightness leds0;
			led_get(&leds0);

			if (leds0.r || leds0.g || leds0.b || leds0.w) {
				// Something is enabled. Swtich leds off.
				leds->r = 0;
				leds->g = 0;
				leds->b = 0;
				leds->w = 0;
			} else {
				// LEDs are switched off. Switch them on.
				leds->r = 0;
				leds->g = 0;
				leds->b = 0;
				leds->w = 255;
			}
			*duration = 2000;

			return 0;
		}

		case 1:
			leds->r = 0;
			leds->g = 0;
			leds->b = 0;
			leds->w = 20;
			*duration = 1000;
			return 0;

		case 2:
			leds->r = 0;
			leds->g = 0;
			leds->b = 0;
			leds->w = 80;
			*duration = 1000;
			return 0;

		case 3:
			leds->r = 235;
			leds->g = 0;
			leds->b = 185;
			leds->w = 0;
			*duration = 1000;
			return 0;

		default:
			return -ENOENT;
	}
}
