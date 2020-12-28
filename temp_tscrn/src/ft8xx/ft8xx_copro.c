/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ft8xx_copro.h"

#include <stdint.h>
#include <string.h>

#include "ft8xx_common.h"
#include "ft8xx_drv.h"
#include "ft8xx_memory.h"

#define RAM_CMD_SIZE 4096UL

enum {
    CMD_DLSTART   = 0xffffff00,
    CMD_SWAP      = 0xffffff01,
    CMD_TEXT      = 0xffffff0c,
    CMD_NUMBER    = 0xffffff2e,
    CMD_CALIBRATE = 0xffffff15,
} ft8xx_cmd;

static uint16_t reg_cmd_read;
static uint16_t reg_cmd_write;

static uint16_t ram_cmd_fullness(void)
{
    return (reg_cmd_write - reg_cmd_read) % 4096UL;
}

static uint16_t ram_cmd_freespace(void)
{
    return (RAM_CMD_SIZE - 4UL) - ram_cmd_fullness();
}

static void refresh_reg_cmd_read(void)
{
    reg_cmd_read = rd32(REG_CMD_READ);
}

static void flush_reg_cmd_write(void)
{
    wr32(REG_CMD_WRITE, reg_cmd_write);
}

static void increase_reg_cmd_write(uint16_t value)
{
    reg_cmd_write = (reg_cmd_write + value) % RAM_CMD_SIZE;
}

void ft8xx_copro_cmd(uint32_t cmd)
{
    while (ram_cmd_freespace() < sizeof(cmd)) {
        refresh_reg_cmd_read();
    }

    wr32(RAM_CMD + reg_cmd_write, cmd);
    increase_reg_cmd_write(sizeof(cmd));

    flush_reg_cmd_write();
}

void ft8xx_copro_cmd_dlstart(void)
{
    ft8xx_copro_cmd(CMD_DLSTART);
}

void ft8xx_copro_cmd_swap(void)
{
    ft8xx_copro_cmd(CMD_SWAP);
}

void ft8xx_copro_cmd_text(int16_t x,
                          int16_t y,
                          int16_t font,
                          uint16_t options,
                          const char *s)
{
    const uint16_t str_bytes = strlen(s) + 1;
    const uint16_t padding_bytes = (4 - (str_bytes % 4)) % 4; // 0 up to 3 padding bytes
    const uint16_t cmd_size = sizeof(CMD_TEXT) +
                              sizeof(x) +
                              sizeof(y) +
                              sizeof(font) +
                              sizeof(options) +
                              str_bytes +
                              padding_bytes;

    while (ram_cmd_freespace() < cmd_size) {
        refresh_reg_cmd_read();
    }

    wr32(RAM_CMD + reg_cmd_write, CMD_TEXT);
    increase_reg_cmd_write(sizeof(CMD_TEXT));

    wr16(RAM_CMD + reg_cmd_write, x);
    increase_reg_cmd_write(sizeof(x));

    wr16(RAM_CMD + reg_cmd_write, y);
    increase_reg_cmd_write(sizeof(y));

    wr16(RAM_CMD + reg_cmd_write, font);
    increase_reg_cmd_write(sizeof(font));

    wr16(RAM_CMD + reg_cmd_write, options);
    increase_reg_cmd_write(sizeof(options));

    (void)ft8xx_drv_write(RAM_CMD + reg_cmd_write, s, str_bytes);
    increase_reg_cmd_write(str_bytes + padding_bytes);

    flush_reg_cmd_write();
}

void ft8xx_copro_cmd_number(int16_t x,
                            int16_t y,
                            int16_t font,
                            uint16_t options,
                            int32_t n)
{
    const uint16_t cmd_size = sizeof(CMD_NUMBER) +
                              sizeof(x) +
                              sizeof(y) +
                              sizeof(font) +
                              sizeof(options) +
                              sizeof(n);

    while (ram_cmd_freespace() < cmd_size) {
        refresh_reg_cmd_read();
    }

    wr32(RAM_CMD + reg_cmd_write, CMD_NUMBER);
    increase_reg_cmd_write(sizeof(CMD_NUMBER));

    wr16(RAM_CMD + reg_cmd_write, x);
    increase_reg_cmd_write(sizeof(x));

    wr16(RAM_CMD + reg_cmd_write, y);
    increase_reg_cmd_write(sizeof(y));

    wr16(RAM_CMD + reg_cmd_write, font);
    increase_reg_cmd_write(sizeof(font));

    wr16(RAM_CMD + reg_cmd_write, options);
    increase_reg_cmd_write(sizeof(options));

    wr32(RAM_CMD + reg_cmd_write, n);
    increase_reg_cmd_write(sizeof(n));

    flush_reg_cmd_write();
}

static volatile uint32_t cal_cnt;

void ft8xx_copro_cmd_calibrate(uint32_t *result)
{
    const uint16_t cmd_size = sizeof(CMD_CALIBRATE) +
                              sizeof(uint32_t);
    uint32_t result_address;

    while (ram_cmd_freespace() < cmd_size) {
        refresh_reg_cmd_read();
    }

    wr32(RAM_CMD + reg_cmd_write, CMD_CALIBRATE);
    increase_reg_cmd_write(sizeof(CMD_CALIBRATE));

    result_address = RAM_CMD + reg_cmd_write;
    wr32(result_address, 1UL);
    increase_reg_cmd_write(sizeof(uint32_t));

    flush_reg_cmd_write();

    /* Wait until calibration is finished. */
    while (ram_cmd_fullness() > 0) {
        cal_cnt++;
        refresh_reg_cmd_read();
    }

    *result = rd32(result_address);
}
