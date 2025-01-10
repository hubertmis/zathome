/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Application data dispatcher
 */

#ifndef DATA_DISPATCHER_H_
#define DATA_DISPATCHER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEMP_MIN (-500)
#define DATA_SHADES_VAL_UNKNOWN UINT16_MAX

typedef enum {
    DATA_TEMP_MEASUREMENT,
    DATA_TEMP_SETTING,
    DATA_OUTPUT,
    DATA_CONTROLLER,
    DATA_PRJ_ENABLED,
    DATA_FORCED_SWITCHING,

    DATA_VENT_REQ,
    DATA_VENT_CURR,

    DATA_LIGHT_REQ,
    DATA_LIGHT_CURR,

    DATA_SHADES_REQ,
    DATA_SHADES_CURR,

    DATA_NUM
} data_t;

typedef enum {
    DATA_LOC_LOCAL,
    DATA_LOC_REMOTE,

    DATA_LOC_NUM
} data_loc_t;

typedef enum {
    DATA_CTLR_ONOFF,
    DATA_CTLR_PID,
} data_ctlr_mode_t;

typedef enum {
    VENT_SM_UNAVAILABLE,
    VENT_SM_NONE,
    VENT_SM_AIRING,
} data_vent_sm_t;

typedef enum data_shade_id {
    DATA_SHADE_ID_DR_L,
    DATA_SHADE_ID_DR_C,
    DATA_SHADE_ID_DR_R,
    DATA_SHADE_ID_K,
    DATA_SHADE_ID_LR,
    DATA_SHADE_ID_BR,

    DATA_SHADE_ID_NUM
} data_shade_id_t;

typedef struct data_light {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;
} data_light_t;

typedef struct {
    uint16_t        value;
    data_shade_id_t id;
} data_shades_req_t;

typedef struct {
    uint16_t values[DATA_SHADE_ID_NUM];
} data_shades_curr_t;

typedef struct {
    data_loc_t loc;
    data_t     type;
    union {
        int16_t temp_measurement;
        int16_t temp_setting;
        uint16_t output;
        uint32_t prj_validity;
        uint16_t forced_switches;

        struct {
            data_ctlr_mode_t mode;

            union {
                struct {
                    uint16_t p;
                    uint16_t i;
                };

                uint16_t hysteresis;
            };
        } controller;

        data_vent_sm_t     vent_mode;
        data_light_t       light;
        data_shades_req_t  shades_req;
        data_shades_curr_t shades_curr;
    };
} data_dispatcher_publish_t;

typedef void (*data_dispatcher_callback_t)(const data_dispatcher_publish_t *data);

typedef struct data_dispatcher_subscribe {
    data_dispatcher_callback_t        callback;
    struct data_dispatcher_subscribe *next;
} data_dispatcher_subscribe_t;

void data_dispatcher_init(void);

void data_dispatcher_subscribe(data_t type, data_dispatcher_subscribe_t *subscribe);
void data_dispatcher_unsubscribe(data_t type, data_dispatcher_subscribe_t *subscribe);
void data_dispatcher_publish(data_dispatcher_publish_t *data);

void data_dispatcher_get(data_t type, data_loc_t loc, const data_dispatcher_publish_t **data);

#ifdef __cplusplus
}
#endif

#endif // DATA_DISPATCHER_H_
