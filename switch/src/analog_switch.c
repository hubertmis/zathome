/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hubertmis_analog_switches

#include "analog_switch.h"

#include <drivers/adc.h>
#include <device.h>
#include <logging/log.h>

#include "led.h"

LOG_MODULE_REGISTER(analog_switches, CONFIG_SENSOR_LOG_LEVEL);

#define ADC_RESOLUTION 12

#define ADC_INTERVAL_MS 1
#define AVG_FACTOR 8
#define AVG_CUTOFF_FACTOR 8

#define THREAD_PRIORITY 4
#define THREAD_STACK_SIZE 1024

enum last_change {
	NONE,
	INC,
	DEC,
};

struct analog_switch_data {
    uint16_t raw;
    uint16_t events;
    uint32_t avg;

    uint16_t         det_iters;
    uint16_t         det_threshold;
    uint8_t          debounce_cnt; 
    enum last_change last_change;

    bool iter_led_indication;
    bool debouncing_led_indication;

    analog_switch_callback_t callback;
    void *                   callback_ctx;

    K_KERNEL_STACK_MEMBER(thread_stack, THREAD_STACK_SIZE);
    struct k_thread thread_data;
    k_tid_t thread_id;
};

struct analog_switch_cfg {
    struct adc_sequence adc_table;
    const struct device *adc;
    uint8_t input_id;
};

static struct adc_sequence_options options = {
	.extra_samplings = 0,
	.interval_us = 15,
};

#ifdef CONFIG_ADC_NRFX_SAADC
#define INPUT_CONFIG                                                           \
            .input_positive = drv_cfg->input_id + SAADC_CH_PSELP_PSELP_AnalogInput0,
#else
#define INPUT_CONFIG
#endif

static void thread_process(void *, void *, void *);

static int as_init(const struct device *dev)
{
	struct analog_switch_data *drv_data = dev->data;
	const struct analog_switch_cfg *drv_cfg = dev->config;

	if (drv_cfg->adc == NULL) {
		LOG_ERR("Failed to get ADC device.");
		return -EINVAL;
	}

	drv_data->avg           = UINT32_MAX;
	drv_data->det_iters     = 40;
	drv_data->det_threshold = 24;
	drv_data->debounce_cnt  = 3; 
	drv_data->last_change   = NONE;

	drv_data->iter_led_indication       = false;
	drv_data->debouncing_led_indication = false;

	drv_data->callback     = NULL;
	drv_data->callback_ctx = NULL;

	struct adc_channel_cfg ch_cfg = {
	    .gain = ADC_GAIN_1_4,
	    .reference = ADC_REF_VDD_1_4,
	    .acquisition_time = ADC_ACQ_TIME_DEFAULT,
	    .channel_id = drv_cfg->input_id,
	    INPUT_CONFIG
	};

	adc_channel_setup(drv_cfg->adc, &ch_cfg);

	drv_data->thread_id = k_thread_create(&drv_data->thread_data,
		                              drv_data->thread_stack,
					      K_THREAD_STACK_SIZEOF(drv_data->thread_stack),
					      thread_process,
					      (void*)dev, NULL, NULL,
					      THREAD_PRIORITY,
					      0,
					      K_FOREVER);

	return 0;
}

static uint32_t avg(uint32_t avg, uint16_t sample)
{
	if (avg == UINT32_MAX) return sample;

	if (sample > avg * AVG_CUTOFF_FACTOR) return avg;

	uint32_t divider = 1UL << AVG_FACTOR;
	uint32_t multiplier = divider - 1;

	uint64_t mult_result = (uint64_t)multiplier * (uint64_t)avg;
	uint64_t sum_result = mult_result + sample;
	uint32_t result = sum_result / divider;

	return result;
}

static void thread_process(void *arg1, void *, void *)
{
	const struct device *dev = arg1;
	struct analog_switch_data *drv_data = dev->data;
	const struct analog_switch_cfg *drv_cfg = dev->config;
	int r;
	int i = 0;
	int debouncing = 0;
	uint32_t prev_avg = UINT32_MAX;
	uint32_t avg_delta;

	while (1) {
		r = adc_read(drv_cfg->adc, &drv_cfg->adc_table);
		if (r) continue; // TODO: If happens multiple times in a raw, report error somewhere?
 
		drv_data->avg = avg(drv_data->avg, drv_data->raw);

		if (prev_avg == UINT32_MAX) {
			prev_avg = drv_data->avg;
		}

		if (++i >= drv_data->det_iters) {
			if (drv_data->iter_led_indication) {
				led_analog_toggle();
			}

			i = 0;

			if (debouncing <= 0) {
				bool detected = false;

				if (drv_data->debouncing_led_indication) {
					led_analog_toggle();
				}
				
				avg_delta = drv_data->det_threshold;
				if ((drv_data->avg > prev_avg + avg_delta) &&
				    (drv_data->last_change != INC)) {
					detected = true;
					drv_data->last_change = INC;
				}
				if (drv_data->avg < prev_avg - avg_delta &&
				    drv_data->last_change != DEC) {
					detected = true;
					drv_data->last_change = DEC;
				}

				if (detected) {
					drv_data->events++;
					debouncing = drv_data->debounce_cnt;
					if (drv_data->callback) drv_data->callback(
							drv_data->last_change == DEC,
							drv_data->callback_ctx);
				}
			} else {
				debouncing--;
			}

			prev_avg = drv_data->avg;
		}

		k_sleep(K_MSEC(ADC_INTERVAL_MS));
	}
}

static int get(const struct device *dev, uint16_t *result)
{
	int ret = 0;
	struct analog_switch_data *drv_data = dev->data;
	const struct analog_switch_cfg *drv_cfg = dev->config;

	ret = adc_read(drv_cfg->adc, &drv_cfg->adc_table);
	if (ret) return ret;

	*result = drv_data->raw;
	return 0;
}

static int register_callback(const struct device *dev,
                             analog_switch_callback_t callback,
			     void *ctx)
{
	struct analog_switch_data *drv_data = dev->data;

	drv_data->callback = callback;
	drv_data->callback_ctx = ctx;

	return 0;
}

static int enable(const struct device *dev)
{
	struct analog_switch_data *drv_data = dev->data;
	k_thread_start(drv_data->thread_id);

	return 0;
}

static int get_avg(const struct device *dev, uint16_t *result)
{
	struct analog_switch_data *drv_data = dev->data;

	*result = drv_data->avg;
	return 0;
}

static int get_events(const struct device *dev, uint16_t *result)
{
	struct analog_switch_data *drv_data = dev->data;

	*result = drv_data->events;
	drv_data->events = 0;
	return 0;
}

static int set_config(const struct device *dev, int iters, int threshold, int debounce_cnt,
		      bool debounce_led, bool iter_led)
{
	struct analog_switch_data *drv_data = dev->data;

	drv_data->det_iters = iters;
	drv_data->det_threshold = threshold;
	drv_data->debounce_cnt = debounce_cnt;
	drv_data->debouncing_led_indication = debounce_led;
	drv_data->iter_led_indication = iter_led;

	if (debounce_led || iter_led) {
		led_take_analog_control();
	} else {
		led_release_analog_control();
	}

	return 0;
}

static int get_config(const struct device *dev, int *iters, int *threshold, int *debounce_cnt)
{
	struct analog_switch_data *drv_data = dev->data;

	*iters = drv_data->det_iters;
	*threshold = drv_data->det_threshold;
	*debounce_cnt = drv_data->debounce_cnt;

	return 0;
}

static const struct analog_switch_driver_api as_api = {
	.get = &get,
	.register_callback = register_callback,
	.enable = &enable,
	.get_avg = &get_avg,
	.get_events = get_events,
	.set_config = set_config,
	.get_config = get_config,
};

#define ANALOG_SWITCH_INIT(idx)                                                \
    static struct analog_switch_data as_##idx##_data;                          \
    static const struct analog_switch_cfg as_##idx##_cfg = {                   \
        .adc = DEVICE_DT_GET(DT_INST_PHANDLE(idx, io_channels)),               \
        .input_id = DT_INST_IO_CHANNELS_INPUT_BY_IDX(idx, 0),                  \
        .adc_table = {                                                         \
            .options = &options,                                               \
            .resolution = ADC_RESOLUTION,                                      \
            .buffer = &as_##idx##_data.raw,                                    \
            .buffer_size = sizeof(as_##idx##_data.raw),                        \
            .channels = BIT(DT_INST_IO_CHANNELS_INPUT_BY_IDX(idx, 0)),         \
        },                                                                     \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(idx, &as_init, NULL,                                 \
		          &as_##idx##_data, &as_##idx##_cfg,                   \
                          POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY, &as_api);  \

DT_INST_FOREACH_STATUS_OKAY(ANALOG_SWITCH_INIT)
