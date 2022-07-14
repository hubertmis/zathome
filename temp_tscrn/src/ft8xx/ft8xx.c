/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ft8xx.h"

#include <stddef.h>
#include <stdint.h>

#include <device.h>
#include <kernel.h>
#include <logging/log.h>

#include "ft8xx_common.h"
#include "ft8xx_copro.h"
#include "ft8xx_dl.h"
#include "ft8xx_drv.h"
#include "ft8xx_host_commands.h"
#include "ft8xx_memory.h"

#define LOG_MODULE_NAME ft8xx
LOG_MODULE_REGISTER(LOG_MODULE_NAME);

#define DLSWAP_FRAME 0x02

#define FT8XX_EXPECTED_ID 0x7C

static int_callback irq_callback;

static void command(uint8_t cmd)
{
    (void)ft8xx_drv_command(cmd);
}

static void busy_wait(void)
{
    k_sleep(K_MSEC(20));
}

static bool verify_chip(void)
{
    uint32_t id = rd32(REG_ID);

    return (id & 0xff) == FT8XX_EXPECTED_ID;
}

static int ft8xx_init(const struct device *unused)
{
    ARG_UNUSED(unused);

    int ret = ft8xx_drv_init();
    if (ret < 0) {
        LOG_ERR("FT8xx driver initialization failed with %d", ret);
        return ret;
    }

    command(CORERST);
    command(ACTIVE);
    busy_wait();
    command(CLKEXT);
    command(CLK48M);
    busy_wait();

    command(CORERST);
    command(ACTIVE);
    busy_wait();
    command(CLKEXT);
    command(CLK48M);
    busy_wait();

    while (!verify_chip());

    // Disable LCD
    wr8(REG_GPIO, 0);
    wr8(REG_PCLK, 0);

    // TODO: Read configuration from device tree or Kconfig
    // Configure LCD
    wr16(REG_HSIZE, 480);
    wr16(REG_HCYCLE, 548);
    wr16(REG_HOFFSET, 43);
    wr16(REG_HSYNC0, 0);
    wr16(REG_HSYNC1, 41);
    wr16(REG_VSIZE, 272);
    wr16(REG_VCYCLE, 292);
    wr16(REG_VOFFSET, 12);
    wr16(REG_VSYNC0, 0);
    wr16(REG_VSYNC1, 10);
    wr8(REG_SWIZZLE, 0);
    wr8(REG_PCLK_POL, 1);
    wr8(REG_CSPREAD, 1);

    // TODO: Configure touch
    // TODO: Display initial screen
    wr32(RAM_DL + 0, CLEAR_COLOR_RGB(0, 0x80, 0));//Set the initial color
    wr32(RAM_DL + 4, CLEAR(1, 1, 1)); //Clear to the initial color
    wr32(RAM_DL + 8, DISPLAY()); //End the display list
    wr8(REG_DLSWAP, DLSWAP_FRAME);

    // Enable LCD
    wr8(REG_GPIO_DIR, 0x80);
    wr8(REG_GPIO, 0x80);
    wr16(REG_PWM_HZ, 0x00FA);
    wr8(REG_PWM_DUTY, 0x10);
    wr8(REG_PCLK, 0x05);

    return 0;
}

SYS_DEVICE_DEFINE("ft8xx_spi", ft8xx_init, NULL,
          APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEVICE);

int ft8xx_get_touch_tag(void)
{
    // Read REG_INT_FLAGS to clear IRQ
    uint8_t int_flags = rd8(REG_INT_FLAGS);
    (void)int_flags; // It should contain only TAG interrupt

    return (int)rd8(REG_TOUCH_TAG);
}

void ft8xx_drv_irq_triggered(const struct device *dev, struct gpio_callback *cb,
            uint32_t pins)
{
    if (irq_callback != NULL) {
        irq_callback();
    }
}

void ft8xx_register_int(int_callback callback)
{
    if (irq_callback != NULL) {
        return;
    }

    irq_callback = callback;
    wr8(REG_INT_MASK, 0x04);
    wr8(REG_INT_EN, 0x01);
}

uint32_t ft8xx_get_tracker_value(void)
{
    return rd32(REG_TRACKER);
}

void ft8xx_calibrate(struct ft8xx_touch_transform *data)
{
    uint32_t result = 0;

    do {
        cmd_dlstart();
        cmd(CLEAR_COLOR_RGB(0x00, 0x00, 0x00));
        cmd(CLEAR(1, 1, 1));
        cmd_calibrate(&result);
    } while (result == 0);

    data->a = rd32(REG_TOUCH_TRANSFORM_A);
    data->b = rd32(REG_TOUCH_TRANSFORM_B);
    data->c = rd32(REG_TOUCH_TRANSFORM_C);
    data->d = rd32(REG_TOUCH_TRANSFORM_D);
    data->e = rd32(REG_TOUCH_TRANSFORM_E);
    data->f = rd32(REG_TOUCH_TRANSFORM_F);
}

void ft8xx_touch_transform_set(const struct ft8xx_touch_transform *data)
{
    wr32(REG_TOUCH_TRANSFORM_A, data->a);
    wr32(REG_TOUCH_TRANSFORM_B, data->b);
    wr32(REG_TOUCH_TRANSFORM_C, data->c);
    wr32(REG_TOUCH_TRANSFORM_D, data->d);
    wr32(REG_TOUCH_TRANSFORM_E, data->e);
    wr32(REG_TOUCH_TRANSFORM_F, data->f);
}
