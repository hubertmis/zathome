
/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ctlr.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>

#include "data_dispatcher.h"

#define PID_INTERVAL (1000UL * 60UL * 3UL)

#define PID_THREAD_STACK_SIZE 1024
#define PID_THREAD_PRIO       1
static void pid_thread_process(void *a1, void *a2, void *a3);

K_THREAD_DEFINE(pid_thread_id, PID_THREAD_STACK_SIZE,
                pid_thread_process, NULL, NULL, NULL,
                PID_THREAD_PRIO, K_ESSENTIAL, K_TICKS_FOREVER);

static volatile bool ctlr_running[DATA_LOC_NUM];

static void changed_temperature(const data_dispatcher_publish_t *data);
static void changed_setting(const data_dispatcher_publish_t *data);
static void changed_ctlr(const data_dispatcher_publish_t *data);

static bool check_forced_switching(data_loc_t loc);
static bool check_projector(data_loc_t loc);
static bool check_ctlr_running(data_loc_t loc);

static void onoff_ctrl(const data_dispatcher_publish_t *meas_data,
                       const data_dispatcher_publish_t *sett_data,
                       const data_dispatcher_publish_t *ctlr_data,
                       data_loc_t loc);

static void process_ctrl(const data_dispatcher_publish_t *meas_data,
                         const data_dispatcher_publish_t *sett_data,
                         const data_dispatcher_publish_t *ctlr_data,
                         data_loc_t loc)
{
    if (ctlr_data == NULL) {
        data_dispatcher_get(DATA_CONTROLLER, loc, &ctlr_data);
    }

    // Do not process controller if output is forced by other means
    if (!check_ctlr_running(loc)) return;

    // None of the override functions took control. Enable the controller.
    ctlr_running[loc] = true;

    switch (ctlr_data->controller.mode) {
        case DATA_CTLR_ONOFF:
            {
                if (meas_data == NULL) {
                    data_dispatcher_get(DATA_TEMP_MEASUREMENT, loc, &meas_data);
                }
                if (sett_data == NULL) {
                    data_dispatcher_get(DATA_TEMP_SETTING, loc, &sett_data);
                }
                onoff_ctrl(meas_data, sett_data, ctlr_data, loc);
            }
            break;

        case DATA_CTLR_PID:
            // Intentionally empty.
            // Temperature measurement is going to be checked
            // at the next timed controller event.
            break;
    }
}

static bool check_forced_switching(data_loc_t loc)
{
    const data_dispatcher_publish_t *frc_sw_data;
    data_dispatcher_get(DATA_FORCED_SWITCHING, loc, &frc_sw_data);

	return frc_sw_data->forced_switches > 0;
}

static bool check_projector(data_loc_t loc)
{
    const data_dispatcher_publish_t *prj_data;
    data_dispatcher_get(DATA_PRJ_ENABLED, loc, &prj_data);

    return prj_data->prj_validity > 0;
}

static bool check_ctlr_running(data_loc_t loc)
{
    if (check_forced_switching(loc)) {
        return false;
    }
    if (check_projector(loc)) {
        return false;
    }

    return true;
}

static void onoff_ctrl(const data_dispatcher_publish_t *meas_data,
                       const data_dispatcher_publish_t *sett_data,
                       const data_dispatcher_publish_t *ctlr_data,
                       data_loc_t loc)
{
    data_dispatcher_publish_t output_data = {
        .loc = loc,
        .type = DATA_OUTPUT,
    };

    if (meas_data->temp_measurement < TEMP_MIN) {
        // Sensor error
        output_data.output = 0;
        data_dispatcher_publish(&output_data);
        return;
    }

    if (!check_ctlr_running(loc)) {
        // Controller disabled
        return;
    }

    if (meas_data->temp_measurement >
            (sett_data->temp_setting
             + ctlr_data->controller.hysteresis)) {
        output_data.output = 0;
        data_dispatcher_publish(&output_data);
    }

    if (meas_data->temp_measurement <
            (sett_data->temp_setting
             - ctlr_data->controller.hysteresis)) {
        output_data.output = 1;
        data_dispatcher_publish(&output_data);
    }
}

static void pid_thread_process(void *a1, void *a2, void *a3)
{
    (void)a1;
    (void)a2;
    (void)a3;

    struct {
        int32_t i;
    } pid_data[DATA_LOC_NUM];

    memset(pid_data, 0, sizeof(pid_data));

    while (1) {
        for (int i = 0; i < DATA_LOC_NUM; ++i) {
            const data_dispatcher_publish_t *meas_data;
            const data_dispatcher_publish_t *sett_data;
            const data_dispatcher_publish_t *ctlr_data;
            data_loc_t loc = (data_loc_t)i;

            data_dispatcher_publish_t out_data = {
                .type   = DATA_OUTPUT,
                .loc    = loc,
                .output = 0,
            };

            if (!check_ctlr_running(loc)) {
                // Controller disabled. Skip algorithm iteration
                continue;
            }

            data_dispatcher_get(DATA_CONTROLLER, loc, &ctlr_data);
            if (ctlr_data->controller.mode != DATA_CTLR_PID) {
                continue;
            }

            data_dispatcher_get(DATA_TEMP_MEASUREMENT, loc, &meas_data);
            data_dispatcher_get(DATA_TEMP_SETTING, loc, &sett_data);

            if (meas_data->temp_measurement < TEMP_MIN) {
                // Sensor error
                out_data.output = 0;
                data_dispatcher_publish(&out_data);
                continue;
            }

            int32_t diff = (int32_t)sett_data->temp_setting - (int32_t)meas_data->temp_measurement;

            // P
            int32_t output = diff * (int32_t)ctlr_data->controller.p;

            // I
            int32_t prev_i = pid_data[i].i;
            int32_t new_i  = prev_i + diff * (int32_t)ctlr_data->controller.i;
            // Unwinding algorithm
            if (diff > 0) {
                int32_t max_i = (int32_t)UINT16_MAX - output;

                if (new_i > max_i) {
                    new_i = prev_i < max_i ? max_i : prev_i;
                }
            }
            else if (diff < 0) {
                int32_t min_i = 0;

                if (new_i < min_i) {
                    new_i = 0;
                }
            }
            pid_data[i].i = new_i;

            output += new_i;

            // Trim output
            output = output < 0 ? 0 : output;
            output = output > UINT16_MAX ? UINT16_MAX : output;

            // Publish new data
            out_data.output = output;
            data_dispatcher_publish(&out_data);
        }

        k_sleep(K_MSEC(PID_INTERVAL));
    }
}

static data_dispatcher_subscribe_t sbscr_temp_meas = {
    .callback = changed_temperature,
};
static data_dispatcher_subscribe_t sbscr_temp_setting = {
    .callback = changed_setting,
};
static data_dispatcher_subscribe_t sbscr_ctlr_setting = {
    .callback = changed_ctlr,
};

void ctlr_init(void)
{
    data_dispatcher_subscribe(DATA_TEMP_MEASUREMENT, &sbscr_temp_meas);
    data_dispatcher_subscribe(DATA_TEMP_SETTING, &sbscr_temp_setting);
    data_dispatcher_subscribe(DATA_CONTROLLER, &sbscr_ctlr_setting);

    k_thread_start(pid_thread_id);
}

static void changed_temperature(const data_dispatcher_publish_t *data)
{
    const data_dispatcher_publish_t *ctlr_data;
    data_loc_t loc = data->loc;

    data_dispatcher_get(DATA_CONTROLLER, loc, &ctlr_data);

    switch (ctlr_data->controller.mode) {
        case DATA_CTLR_ONOFF:
            {
                process_ctrl(data, NULL, ctlr_data, loc);
            }
            break;

        case DATA_CTLR_PID:
            // Intentionally empty.
            // Temperature measurement is going to be checked
            // at the next timed controller event.
            break;
    }
}

static void changed_setting(const data_dispatcher_publish_t *data)
{
    const data_dispatcher_publish_t *ctlr_data;
    data_loc_t loc = data->loc;

    data_dispatcher_get(DATA_CONTROLLER, loc, &ctlr_data);

    switch (ctlr_data->controller.mode) {
        case DATA_CTLR_ONOFF:
            {
                process_ctrl(NULL, data, ctlr_data, loc);
            }
            break;

        case DATA_CTLR_PID:
            // Intentionally empty.
            // Temperature measurement is going to be checked
            // at the next timed controller event.
            break;
    }
}

static void changed_ctlr(const data_dispatcher_publish_t *data)
{
    data_loc_t loc = data->loc;

    switch (data->controller.mode) {
        case DATA_CTLR_ONOFF:
            {
                process_ctrl(NULL, NULL, data, loc);
            }
            break;

        case DATA_CTLR_PID:
            // Intentionally empty.
            // Temperature measurement is going to be checked
            // at the next timed controller event.
            break;
    }
}
