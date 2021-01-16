/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ntc,temperature

#include "ntc.h"

#include <drivers/sensor.h>

#include <math.h>

#include <drivers/adc.h>
#include <device.h>
#include <logging/log.h>

LOG_MODULE_REGISTER(ntc_temp, CONFIG_SENSOR_LOG_LEVEL);

#define C_OFFSET 273.15

#define ADC_RESOLUTION 12
#define NUM_SENSORS DT_INST_PROP_LEN(0, io_channels)

#define AVG_BASE_BITS 9UL
#define AVG_FRACT_BITS 16UL

struct ntc_data {
	const struct device *adc;
	uint16_t raw[NUM_SENSORS];
    uint32_t avg[NUM_SENSORS];
};

struct ntc_config {
	int16_t b_const;
    uint16_t r_ref;
    uint16_t r_nom;
    int8_t t_nom;
    bool ntc_before_r_ref;
};

static struct adc_sequence_options options = {
	.extra_samplings = 0,
	.interval_us = 15,
};

static struct adc_sequence adc_table = {
	.options = &options,
    .resolution = ADC_RESOLUTION,
};

static int ntc_sample_fetch(const struct device *dev,
			    enum sensor_channel chan)
{
	struct ntc_data *drv_data = dev->data;

	return adc_read(drv_data->adc, &adc_table);
}

static uint16_t avg(struct ntc_data *data, int idx)
{
    uint32_t raw = (uint32_t)data->raw[idx] << AVG_FRACT_BITS;

    if (data->avg[idx] == UINT32_MAX) {
        data->avg[idx] = raw;
    }
    else {
        uint64_t avg_base = 1ULL << AVG_BASE_BITS;
        uint64_t sum = (avg_base - 1ULL) * data->avg[idx] + raw;
        data->avg[idx] = sum / avg_base;
    }

    return (uint16_t)(data->avg[idx] >> AVG_FRACT_BITS);
}

static int ntc_channel_get(const struct device *dev,
			   enum sensor_channel chan,
			   struct sensor_value *val)
{
	struct ntc_data *drv_data = dev->data;
	const struct ntc_config *cfg = dev->config;
    uint32_t adc_max;
    double r;
	double dval;
    int idx;

    if (chan == SENSOR_CHAN_AMBIENT_TEMP) {
        idx = 0;
    }
    else if ((chan >= NTC_SENSOR(0)) && (chan < NTC_SENSOR(NUM_SENSORS))) {
        idx = (int)chan - (int)SENSOR_CHAN_PRIV_START;
    }
    else {
        return -EINVAL;
    }

    uint16_t avg_raw = avg(drv_data, idx);

    adc_max = BIT(ADC_RESOLUTION) - 1;
    r = (double)adc_max / avg_raw - 1.0;

    if (!cfg->ntc_before_r_ref) {
        r = 1/r;
    }

    r = cfg->r_ref * r;

	dval = (1.0 / (log(r / cfg->r_nom) / cfg->b_const
		     + (1 / (C_OFFSET + cfg->t_nom)))) - C_OFFSET;
	val->val1 = (int32_t)dval;
	val->val2 = ((int32_t)(dval * 1000000)) % 1000000;

	return 0;
}

static const struct sensor_driver_api ntc_api = {
	.sample_fetch = &ntc_sample_fetch,
	.channel_get = &ntc_channel_get,
};

#ifdef CONFIG_ADC_NRFX_SAADC
#define INPUT_CONFIG                                                           \
            .input_positive = input_id + SAADC_CH_PSELP_PSELP_AnalogInput0,
#else
#define INPUT_CONFIG
#endif

#define SENSOR_INPUT_DEFINE(id)                                                \
    do {                                                                       \
        uint8_t input_id = DT_INST_IO_CHANNELS_INPUT_BY_IDX(0, id);            \
        struct adc_channel_cfg ch_cfg = {                                      \
            .gain = ADC_GAIN_1_4,                                              \
            .reference = ADC_REF_VDD_1_4,                                      \
            .acquisition_time = ADC_ACQ_TIME_DEFAULT,                          \
            .channel_id = input_id,                                            \
            INPUT_CONFIG                                                       \
        };                                                                     \
                                                                               \
        adc_channel_setup(drv_data->adc, &ch_cfg);                             \
        adc_table.channels |= BIT(ch_cfg.channel_id);                          \
    } while(0)


static int ntc_init(const struct device *dev)
{
	struct ntc_data *drv_data = dev->data;

    // TODO: Find a way to handle multiple ADC peripherals by this module.
    // Current implementation takes ADC instance from the first sensor in
    // devicetree.
	drv_data->adc = device_get_binding(DT_INST_IO_CHANNELS_LABEL(0));
	if (drv_data->adc == NULL) {
		LOG_ERR("Failed to get ADC device.");
		return -EINVAL;
	}

    for (int i = 0; i < NUM_SENSORS; i++) {
        drv_data->avg[i] = UINT32_MAX;
    }

	adc_table.buffer = &drv_data->raw;
	adc_table.buffer_size = sizeof(drv_data->raw);
	adc_table.resolution = ADC_RESOLUTION;
	adc_table.channels = 0;

    // TODO: Find in DTS for what indices it should be called
    SENSOR_INPUT_DEFINE(0);
    SENSOR_INPUT_DEFINE(1);

	return 0;
}

static struct ntc_data ntc_data;
static const struct ntc_config ntc_cfg = {
    .ntc_before_r_ref = DT_INST_PROP(0, ntc_before_r_ref),
    .r_ref = DT_INST_PROP(0, r_ref),
    .r_nom = 10000,
    .t_nom = 25,
	.b_const = 3380.0,
};


DEVICE_AND_API_INIT(ntc_dev, DT_INST_LABEL(0), &ntc_init,
		&ntc_data, &ntc_cfg, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,
		&ntc_api);
