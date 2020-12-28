/*
 * Copyright (c) 2020 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ft8xx_common.h"

#include "ft8xx_drv.h"

static void encode_u16(uint8_t *buffer, uint16_t data)
{
    buffer[0] = (uint8_t)data;
    buffer[1] = (uint8_t)(data >> 8);
}

static void encode_u32(uint8_t *buffer, uint32_t data)
{
    buffer[0] = (uint8_t)data;
    buffer[1] = (uint8_t)(data >> 8);
    buffer[2] = (uint8_t)(data >> 16);
    buffer[3] = (uint8_t)(data >> 24);
}

static uint16_t parse_u16(const uint8_t *buffer)
{
    uint16_t result;

    result = (uint16_t)buffer[0];
    result |= (uint16_t)buffer[1] << 8;

    return result;
}

static uint32_t parse_u32(const uint8_t *buffer)
{
    uint32_t result;

    result = (uint32_t)buffer[0];
    result |= (uint32_t)buffer[1] << 8;
    result |= (uint32_t)buffer[2] << 16;
    result |= (uint32_t)buffer[3] << 24;

    return result;
}

void ft8xx_wr8(uint32_t address, uint8_t data)
{
    (void)ft8xx_drv_write(address, &data, sizeof(data));
}

void ft8xx_wr16(uint32_t address, uint16_t data)
{
    uint8_t buffer[2];

    encode_u16(buffer, data);
    (void)ft8xx_drv_write(address, buffer, sizeof(buffer));
}

void ft8xx_wr32(uint32_t address, uint32_t data)
{
    uint8_t buffer[4];

    encode_u32(buffer, data);
    (void)ft8xx_drv_write(address, buffer, sizeof(buffer));
}

uint8_t ft8xx_rd8(uint32_t address)
{
    uint8_t data = 0;

    (void)ft8xx_drv_read(address, &data, sizeof(data));

    return data;
}

uint16_t ft8xx_rd16(uint32_t address)
{
    uint8_t buffer[2] = {0};
    
    (void)ft8xx_drv_read(address, buffer, sizeof(buffer));
    return parse_u16(buffer);
}

uint32_t ft8xx_rd32(uint32_t address)
{
    uint8_t buffer[4] = {0};
    
    (void)ft8xx_drv_read(address, buffer, sizeof(buffer));
    return parse_u32(buffer);
}
