/*
 * Copyright (c) 2022 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hubertmis_analog_switches

#include "analog_switch.h"

#include <nrfx_saadc.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include "led.h"

LOG_MODULE_REGISTER(analog_switches, CONFIG_SENSOR_LOG_LEVEL);

#define ADC_RESOLUTION 12
#define ADC_INTERVAL_MS 1
#define ADC_ERR_RETRIES 100

/* Enable one of the algorithms */
#define ALG_AVG_HYST 0 /* Average variation detection with hysteresis */
#define ALG_COUNTER  0 /* Counter of values exceeding threshold */
#define ALG_DBL_AVG  0 /* Double moving averages comparison */
#define ALG_SLD_W    1 /* Double sliding window averages comparison */

#define AVG_DIV_VAL (200)
#define AVG_CUTOFF_FACTOR 8
#define AVG_DIV_REF (400)

#define REF_SAMPLES 1024
#define VAL_FACTOR  4
#define VAL_SAMPLES (REF_SAMPLES / VAL_FACTOR)
#define MAX_SAMPLE_VAL 1024

#define THREAD_PRIORITY 7
#define THREAD_STACK_SIZE 1024

enum last_change {
	NONE,
	INC,
	DEC,
};

struct analog_switch_data {
    uint16_t raw;
    uint16_t events;
    struct {
        uint32_t avg;
        uint32_t ref;
    } debug;
#if ALG_AVG_HYST
    struct {
        uint32_t avg;
        uint32_t prev_avg;
        int debouncing;
        int i;
	} avg_hyst_data;
#endif
#if ALG_COUNTER
	struct {
		int i;
		int j;
		uint64_t sum;
		uint32_t ref;
		uint32_t num_exceeded;
		int debouncing;
	} cntr_data;
#endif
#if ALG_DBL_AVG
	struct {
		uint32_t avg;
		uint32_t ref;
		int debouncing;
		int num_exceeded;
	} dbl_avg_data;
#endif
#if ALG_SLD_W
	struct {
	    uint16_t samples[REF_SAMPLES];
		uint16_t ref_idx;
		uint32_t val;
		uint32_t ref;
		int num_exceeded;
	} sld_w;
#endif

    uint16_t         det_iters;
    uint16_t         sum_iters;
    uint16_t         min_events;
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

    bool thread_enabled;
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
            .input_positive = drv_cfg->input_id + NRFX_ANALOG_EXTERNAL_AIN0,
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

#if ALG_AVG_HYST
	drv_data->avg_hyst_data.avg        = UINT32_MAX;
	drv_data->avg_hyst_data.prev_avg   = UINT32_MAX;
	drv_data->avg_hyst_data.debouncing = 0;
	drv_data->avg_hyst_data.i          = 0;
#endif

#if ALG_COUNTER
	drv_data->cntr_data.i            = 0;
	drv_data->cntr_data.j            = 0;
	drv_data->cntr_data.sum          = 0;
	drv_data->cntr_data.ref          = UINT32_MAX;
	drv_data->cntr_data.num_exceeded = 0;
	drv_data->cntr_data.debouncing   = 0;
#endif

#if ALG_DBL_AVG
	drv_data->dbl_avg_data.avg          = UINT32_MAX;
	drv_data->dbl_avg_data.ref          = UINT32_MAX;
	drv_data->dbl_avg_data.debouncing   = 0;
	drv_data->dbl_avg_data.num_exceeded = 0;
#endif

#if ALG_SLD_W
	memset(&drv_data->sld_w.samples, 0, sizeof(drv_data->sld_w.samples));
	drv_data->sld_w.ref_idx = 0;
	drv_data->sld_w.val     = 0;
	drv_data->sld_w.ref     = 0;
#endif

	drv_data->det_iters     = 40;
	drv_data->sum_iters     = 8;
	drv_data->min_events    = 50;
	drv_data->det_threshold = 12000;
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

	drv_data->thread_enabled = false;

	return 0;
}

static void call_detected_callback(struct analog_switch_data *drv_data)
{
    drv_data->events++;

	if (drv_data->callback) {
		drv_data->callback(drv_data->last_change == DEC, drv_data->callback_ctx);
	}
}

#if ALG_AVG_HYST || ALG_COUNTER || ALG_DBL_AVG
static bool gross_error(uint32_t prev_avg, uint32_t sample)
{
    if (prev_avg == UINT32_MAX) {
        return false;
    }

    return sample > prev_avg * AVG_CUTOFF_FACTOR;
}
#endif

#if ALG_AVG_HYST || ALG_COUNTER || ALG_DBL_AVG
static uint32_t avg(uint32_t prev_avg, uint32_t sample, uint32_t divider)
{
	if (prev_avg == UINT32_MAX) return sample;

	uint32_t multiplier = divider - 1;

	uint64_t mult_result = (uint64_t)multiplier * (uint64_t)prev_avg;
	uint64_t sum_result = mult_result + sample;
	uint64_t rnd_result = sum_result + divider / 2;
	uint32_t result = rnd_result / divider;

	return result;
}
#endif

#if ALG_AVG_HYST || ALG_DBL_AVG
static uint32_t avg_val(uint32_t prev_avg, uint32_t sample)
{
    return avg(prev_avg, sample, AVG_DIV_VAL);
}
#endif

#if ALG_COUNTER || ALG_DBL_AVG
static uint32_t avg_ref(uint32_t prev_avg, uint32_t sample)
{
    return avg(prev_avg, sample, AVG_DIV_REF);
}
#endif

#if ALG_AVG_HYST
static void avg_hyst_process(struct analog_switch_data *drv_data)
{
    uint32_t avg_delta;
    bool detected = false;

    if (!gross_error(drv_data->avg_hyst_data.avg, drv_data->raw)) {
        drv_data->avg_hyst_data.avg = avg_val(drv_data->avg_hyst_data.avg, drv_data->raw);
    }

	if (drv_data->avg_hyst_data.prev_avg == UINT32_MAX) {
		drv_data->avg_hyst_data.prev_avg = drv_data->avg_hyst_data.avg;
	}

	drv_data->debug.avg = drv_data->avg_hyst_data.avg;

	if (++drv_data->avg_hyst_data.i < drv_data->det_iters) {
		return;
	}

	drv_data->avg_hyst_data.i = 0;

	if (drv_data->iter_led_indication) {
		led_analog_toggle();
	}

	if (drv_data->avg_hyst_data.debouncing > 0) {
		drv_data->avg_hyst_data.debouncing--;
		drv_data->avg_hyst_data.prev_avg = drv_data->avg_hyst_data.avg;
		return;
	}

	if (drv_data->debouncing_led_indication) {
		led_analog_toggle();
	}

	avg_delta = drv_data->det_threshold;
	if ((drv_data->avg_hyst_data.avg > drv_data->avg_hyst_data.prev_avg + avg_delta) &&
	    (drv_data->last_change != INC)) {
		detected = true;
		drv_data->last_change = INC;
	}
	if (drv_data->avg_hyst_data.avg < drv_data->avg_hyst_data.prev_avg - avg_delta &&
	    drv_data->last_change != DEC) {
		detected = true;
		drv_data->last_change = DEC;
	}

	if (detected) {
		drv_data->avg_hyst_data.debouncing = drv_data->debounce_cnt;
		call_detected_callback(drv_data);
	}

	drv_data->avg_hyst_data.prev_avg = drv_data->avg_hyst_data.avg;
}
#endif

#if ALG_COUNTER
static void cntr_process(struct analog_switch_data *drv_data)
{
    uint64_t sum;

    if (gross_error(drv_data->cntr_data.ref, drv_data->raw)) {
        return;
    }

    if (drv_data->cntr_data.i < drv_data->det_iters) {
    	drv_data->cntr_data.i++;
    	drv_data->cntr_data.sum += drv_data->raw;
        return;
    }

    sum = drv_data->cntr_data.sum;
    drv_data->cntr_data.sum = 0;
    drv_data->cntr_data.i = 0;

    if (drv_data->cntr_data.ref != UINT32_MAX) {
        drv_data->cntr_data.ref = avg_ref(drv_data->cntr_data.ref, sum);
    }

    drv_data->debug.avg = sum
    drv_data->debug.ref = drv_data->cntr_data.ref;

    // Ignore potential detected switches for a few averaging periods after last detected switch
    if (drv_data->cntr_data.debouncing) {
        drv_data->cntr_data.debouncing--;
        return;
    }

    // After debouncing, reinitialize reference value
    if (drv_data->cntr_data.ref == UINT32_MAX) {
        drv_data->cntr_data.ref = sum;
    }

    int32_t lower_thr = (int32_t)drv_data->cntr_data.ref - (int32_t)drv_data->det_iters * (int32_t)drv_data->det_threshold;
    int32_t upper_thr = (int32_t)drv_data->cntr_data.ref + (int32_t)drv_data->det_iters * (int32_t)drv_data->det_threshold;

    lower_thr = lower_thr < 0 ? 0 : lower_thr;
    upper_thr = upper_thr < 0 ? INT32_MAX : upper_thr;

    uint64_t lower_cmp = lower_thr;
    uint64_t upper_cmp = upper_thr;

    enum last_change this_change = NONE;
    if (sum < lower_cmp && drv_data->last_change != DEC) {
        this_change = DEC;
    } else if (sum > upper_cmp && drv_data->last_change != INC) {
        this_change = INC;
    }

    if (this_change != NONE) {
        drv_data->cntr_data.num_exceeded++;
    }

    if (drv_data->cntr_data.j < drv_data->sum_iters) {
        drv_data->cntr_data.j++;
        return;
    }

    drv_data->cntr_data.j = 0;

    if (drv_data->cntr_data.num_exceeded >= drv_data->min_events) {
        drv_data->last_change = drv_data->last_change == DEC ? INC : DEC;
        drv_data->cntr_data.debouncing = drv_data->debounce_cnt;
        drv_data->cntr_data.ref = UINT32_MAX;
        call_detected_callback(drv_data);
    }

    drv_data->cntr_data.num_exceeded = 0;
}
#endif

#if ALG_DBL_AVG
static void dbl_avg_process(struct analog_switch_data *drv_data)
{
    if (drv_data->dbl_avg_data.debouncing) {
        drv_data->dbl_avg_data.debouncing--;
        return;
    }

    if (gross_error(drv_data->dbl_avg_data.avg, drv_data->raw)) {
        return;
    }

    drv_data->dbl_avg_data.avg = avg_val(drv_data->dbl_avg_data.avg, drv_data->raw);
    drv_data->dbl_avg_data.ref = avg_ref(drv_data->dbl_avg_data.ref, drv_data->raw);

    drv_data->debug.avg = drv_data->dbl_avg_data.avg;
    drv_data->drbug.ref = drv_data->dbl_avg_data.ref;

    int32_t lower_thr = (int32_t)drv_data->dbl_avg_data.ref - (int32_t)drv_data->det_threshold;
    int32_t upper_thr = (int32_t)drv_data->dbl_avg_data.ref + (int32_t)drv_data->det_threshold;

    if (lower_thr < 0) {
        lower_thr = 0;
    }
    if (upper_thr > 0xffff) {
        upper_thr = 0xffff;
    }

    if ((drv_data->dbl_avg_data.avg < lower_thr) && (drv_data->last_change != DEC)) {
        drv_data->dbl_avg_data.num_exceeded++;
    }
    else if ((drv_data->dbl_avg_data.avg > upper_thr) && (drv_data->last_change != INC)) {
        drv_data->dbl_avg_data.num_exceeded++;
    } else if (drv_data->dbl_avg_data.num_exceeded) {
        drv_data->dbl_avg_data.num_exceeded--;
    }

    if (drv_data->dbl_avg_data.num_exceeded >= drv_data->min_events) {
        drv_data->dbl_avg_data.num_exceeded = 0;
        drv_data->last_change = drv_data->last_change == DEC ? INC : DEC;
        drv_data->dbl_avg_data.debouncing = drv_data->debounce_cnt;
        call_detected_callback(drv_data);
    }
}
#endif

#if ALG_SLD_W
static uint16_t prev_idx(uint16_t curr, uint16_t dist)
{
    return (REF_SAMPLES + curr - (dist - 1)) % REF_SAMPLES;
}

static uint16_t next_idx(uint16_t curr)
{
    return (curr + 1) % REF_SAMPLES;
}

static void sld_w_process(struct analog_switch_data *drv_data)
{
    uint16_t curr_idx = drv_data->sld_w.ref_idx;
    uint16_t prev_ref_idx = prev_idx(curr_idx, REF_SAMPLES);
    uint16_t prev_val_idx = prev_idx(curr_idx, VAL_SAMPLES);

    uint16_t curr_sample = drv_data->raw;

    if (curr_sample > MAX_SAMPLE_VAL) {
        return;
    }

    drv_data->sld_w.samples[curr_idx] = curr_sample;
    drv_data->sld_w.ref -= drv_data->sld_w.samples[prev_ref_idx];
    drv_data->sld_w.ref += drv_data->sld_w.samples[curr_idx];
    drv_data->sld_w.val -= drv_data->sld_w.samples[prev_val_idx];
    drv_data->sld_w.val += drv_data->sld_w.samples[curr_idx];

    drv_data->sld_w.ref_idx = next_idx(drv_data->sld_w.ref_idx);

    drv_data->debug.avg = drv_data->sld_w.val * VAL_FACTOR;
    drv_data->debug.ref = drv_data->sld_w.ref;

    // TODO: debouncing here?

    int32_t lower_thr = (int32_t)drv_data->sld_w.ref - (int32_t)drv_data->det_threshold;
    int32_t upper_thr = (int32_t)drv_data->sld_w.ref + (int32_t)drv_data->det_threshold;

    if (lower_thr < 0) {
        lower_thr = 0;
    }
    if (upper_thr > (MAX_SAMPLE_VAL * REF_SAMPLES)) {
        upper_thr = MAX_SAMPLE_VAL * REF_SAMPLES;
    }

    int32_t cmp_val = drv_data->sld_w.val * VAL_FACTOR;

    if ((cmp_val < lower_thr) && (drv_data->last_change != DEC)) {
        drv_data->sld_w.num_exceeded++;
    }
    else if ((cmp_val > upper_thr) && (drv_data->last_change != INC)) {
        drv_data->sld_w.num_exceeded++;
    } else if (drv_data->sld_w.num_exceeded) {
        drv_data->sld_w.num_exceeded--;
    }

    if (drv_data->sld_w.num_exceeded >= drv_data->min_events) {
        drv_data->sld_w.num_exceeded = 0;
        drv_data->last_change = drv_data->last_change == DEC ? INC : DEC;
        call_detected_callback(drv_data);
    }
}
#endif

static void thread_process(void *arg1, void *, void *)
{
	const struct device *dev = arg1;
	struct analog_switch_data *drv_data = dev->data;
	const struct analog_switch_cfg *drv_cfg = dev->config;
	int r;
	int adc_err_cnt = 0;

#if ALG_AVG_HYST
	drv_data->avg_hyst_data.prev_avg = UINT32_MAX;
	drv_data->avg_hyst_data.debouncing = 0;
	drv_data->avg_hyst_data.i = 0;
#endif

	while (1) {
		r = adc_read(drv_cfg->adc, &drv_cfg->adc_table);
		if (r) {
		    if (adc_err_cnt < ADC_ERR_RETRIES) {
				adc_err_cnt++;
			}
			else {
				k_sleep(K_MSEC(ADC_INTERVAL_MS));
			}
		    continue;
		} else {
		    adc_err_cnt = 0;
		}

#if ALG_AVG_HYST
        avg_hyst_process(drv_data);
#endif
#if ALG_COUNTER
        cntr_process(drv_data);
#endif
#if ALG_DBL_AVG
        dbl_avg_process(drv_data);
#endif
#if ALG_SLD_W
        sld_w_process(drv_data);
#endif

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
	drv_data->thread_enabled = true;
	k_thread_start(drv_data->thread_id);

	return 0;
}

static int get_enabled(const struct device *dev, bool *enabled)
{
    struct analog_switch_data *drv_data = dev->data;

    *enabled = drv_data->thread_enabled;

    return 0;
}

static int get_avg(const struct device *dev, uint16_t *result, uint16_t *ref)
{
	struct analog_switch_data *drv_data = dev->data;

	*result = drv_data->debug.avg;
	*ref = drv_data->debug.ref;

	return 0;
}

static int get_events(const struct device *dev, uint16_t *result)
{
	struct analog_switch_data *drv_data = dev->data;

	*result = drv_data->events;
	drv_data->events = 0;
	return 0;
}

static int set_config(const struct device *dev, int iters, int sum_iters, int min_events,
            int threshold, int debounce_cnt, bool debounce_led, bool iter_led)
{
	struct analog_switch_data *drv_data = dev->data;

	drv_data->det_iters = iters;
	drv_data->sum_iters = sum_iters;
	drv_data->min_events = min_events;
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

static int get_config(const struct device *dev, int *iters, int *sum_iters, int *min_events,
            int *threshold, int *debounce_cnt)
{
	struct analog_switch_data *drv_data = dev->data;

	*iters = drv_data->det_iters;
	*sum_iters = drv_data->sum_iters;
	*min_events = drv_data->min_events;
	*threshold = drv_data->det_threshold;
	*debounce_cnt = drv_data->debounce_cnt;

	return 0;
}

static const struct analog_switch_driver_api as_api = {
	.get = &get,
	.register_callback = register_callback,
	.enable = &enable,
	.get_enabled = &get_enabled,
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
