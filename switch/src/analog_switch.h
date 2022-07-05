/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Analog switch detector device driver
 */
    
#ifndef ANALOG_SWITCH_H_
#define ANALOG_SWITCHC_H_

#include <stdint.h>
#include <device.h>
    
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*analog_switch_callback_t)(bool on, void *ctx);

typedef int (*analog_switch_api_get)(const struct device *dev,
                                     uint16_t *result);

typedef int (*analog_switch_api_register_callback)(const struct device *dev,
                                                   analog_switch_callback_t callback,
						   void *ctx);

typedef int (*analog_switch_api_enable)(const struct device *dev);

typedef int (*analog_switch_api_get_avg)(const struct device *dev,
                                         uint16_t *result);

typedef int (*analog_switch_api_get_events)(const struct device *dev,
                                            uint16_t *result);

typedef int (*analog_switch_api_set_config)(const struct device *dev,
					    int det_iters,
					    int det_threshold,
					    int debounce_cnt,
					    bool debounce_led,
					    bool iter_led);

typedef int (*analog_switch_api_get_config)(const struct device *dev,
                                            int *det_iters,
					    int *det_threshold,
					    int *debounce_cnt);

struct analog_switch_driver_api {
	analog_switch_api_get               get;
	analog_switch_api_register_callback register_callback;
	analog_switch_api_enable            enable;
	analog_switch_api_get_avg           get_avg;
	analog_switch_api_get_events        get_events;
	analog_switch_api_set_config        set_config;
	analog_switch_api_get_config        get_config;
};

#ifdef __cplusplus
}   
#endif

#endif // ANALOG_SWITCH_H_

