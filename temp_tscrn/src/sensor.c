/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sensor.h"

#include <math.h>

#include <drivers/sensor.h>
#include <kernel.h>

#include "data_dispatcher.h"
#include "ntc.h"

#define NUM_SENSORS 2

#define SENSOR_THREAD_STACK_SIZE 1024
#define SENSOR_THREAD_PRIO       0
static void sensor_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(sensor_thread_id, SENSOR_THREAD_STACK_SIZE,
                sensor_thread_process, NULL, NULL, NULL,
                SENSOR_THREAD_PRIO, K_ESSENTIAL | K_FP_REGS, K_TICKS_FOREVER);

void sensor_init(void)
{
    k_thread_start(sensor_thread_id);
}

static void sensor_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

	const struct device *sensor = device_get_binding("NTC sensors");
    struct sensor_value val;
    int16_t dC;

    struct sensor_value store[NUM_SENSORS];
    sensor_sample_fetch(sensor);

    for (int i = 0; i < NUM_SENSORS; i++) {
        sensor_channel_get(sensor, NTC_SENSOR(i), &val);
        store[i] = val;
    }

    while (1) {
        sensor_sample_fetch(sensor);

        for (int i = 0; i < NUM_SENSORS; i++) {
            sensor_channel_get(sensor, NTC_SENSOR(i), &val);

            double dval = val.val1 + 0.000001 * val.val2;
            double prev_dval = store[i].val1 + 0.000001 * store[i].val2;

            if (fabs(dval - prev_dval) < 0.07) {
                continue;
            }

            store[i] = val;
            dC = val.val1 * 10 + val.val2 / 100000;

            data_dispatcher_publish_t data = {
                .loc = i,
                .type = DATA_TEMP_MEASUREMENT,
                .temp_measurement = dC,
            };

            data_dispatcher_publish(&data);
        }
    }
}
