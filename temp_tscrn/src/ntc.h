/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief NTC thermistor sensor driver
 */
    
#ifndef NTC_H_
#define NTC_H_
    
#include <drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NTC_SENSOR(i) (SENSOR_CHAN_PRIV_START + (i))

#ifdef __cplusplus
}   
#endif

#endif // NTC_H_
