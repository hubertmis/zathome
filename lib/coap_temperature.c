/*
 * Copyright (c) 2025 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_temperature.h"
#include "cbor_utils.h"

#include <coap_server.h>

#include <zcbor_encode.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap.h>

#define MAX_COAP_PAYLOAD_LEN 64

#define DIE_TEMP_KEY "dt"

static int prepare_temperature_payload(uint8_t *payload, size_t len)
{
    ZCBOR_STATE_E(ce, 3, payload, len, 1);

    const struct device *temp_sensor = DEVICE_DT_GET_ANY(nordic_nrf_temp);
    struct sensor_value temp_value;

    if (!device_is_ready(temp_sensor)) {
        return -ENODEV;
    }

    if (sensor_sample_fetch(temp_sensor) < 0 ||
        sensor_channel_get(temp_sensor, SENSOR_CHAN_DIE_TEMP, &temp_value) < 0) {
            return -EIO;
    }

    size_t map_len = 1;

    if (!zcbor_map_start_encode(ce, map_len)) return -EINVAL;

    if (!zcbor_tstr_put_lit(ce, DIE_TEMP_KEY)) return -EINVAL;
    if (cbor_encode_dec_frac_num(ce, -3, temp_value.val1 * 1000 + temp_value.val2 / 1000)) {
        return -EINVAL;
    }

    if (!zcbor_map_end_encode(ce, map_len)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

int coap_temperature_get(struct coap_resource *resource,
                         struct coap_packet *request,
                         struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;

    int r = 0;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    size_t payload_len = 0;

    r = prepare_temperature_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        return r;
    }
    payload_len = r;

    return coap_server_handle_simple_getter(sock, resource, request, addr, addr_len,
        payload, payload_len);
}
