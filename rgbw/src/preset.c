/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "preset.h"

#include "led.h"

#include <errno.h>

int preset_get(unsigned preset,
		unsigned *r,
		unsigned *g,
		unsigned *b,
		unsigned *w,
		unsigned *duration)
{
	switch (preset) {
		case 0:
		{
			int r0, g0, b0, w0;
			led_get(&r0, &g0, &b0, &w0);

			if (r0 || g0 || b0 || w0) {
				// Something is enabled. Swtich leds off.
				*r = 0;
				*g = 0;
				*b = 0;
				*w = 0;
			} else {
				// LEDs are switched off. Switch them on.
				*r = 0;
				*g = 0;
				*b = 0;
				*w = 255;
			}
			*duration = 2000;

			return 0;
		}

		case 1:
			*r = 0;
			*g = 0;
			*b = 0;
			*w = 2;
			*duration = 1000;
			return 0;

		case 2:
			*r = 0;
			*g = 0;
			*b = 0;
			*w = 80;
			*duration = 1000;
			return 0;

		case 3:
			*r = 0;
			*g = 186;
			*b = 235;
			*w = 0;
			*duration = 1000;
			return 0;

		default:
			return -ENOENT;
	}
}
