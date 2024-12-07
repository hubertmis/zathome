/*
 * Copyright (c) 2021 Hubert Miś
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "coap_sd.h"

#include <errno.h>
#include <stdint.h>

#include "cbor_utils.h"
#include "coap_server.h"
#include "ot_sed.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/random/random.h>

#define COAP_PORT 5683
#define MAX_COAP_MSG_LEN 256
#define MAX_COAP_PAYLOAD_LEN 64

#define SD_FLT_NAME "name"
#define SD_FLT_TYPE "type"
#define SD_RSRC "sd"
#define SD_NAME_MAX_LEN 8
#define SD_TYPE_MAX_LEN 8

// Server

#ifdef CONFIG_COAP_SD_MAX_NUM_RSRCS
#define NUM_RSRCS CONFIG_COAP_SD_MAX_NUM_RSRCS
#else
#define NUM_RSRCS 2
#endif

static struct {
    const char *name;
    const char *type;
} rsrcs[NUM_RSRCS];

static bool filter_sd_req(const uint8_t *payload, uint16_t payload_len)
{
    bool found = true;
    const char *expected_type = NULL;

    int r;
    char str_name[SD_NAME_MAX_LEN];
    char str_type[SD_TYPE_MAX_LEN];
    ZCBOR_STATE_D(cd, 2, payload, payload_len, 1, 0);

    if (!zcbor_unordered_map_start_decode(cd)) return false;

    // Handle name
    r = cbor_extract_from_map_string(cd, SD_FLT_NAME, str_name, sizeof(str_name));
    if (r > 0) {
        found = false;

        for (int i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
            if (strncmp(str_name, rsrcs[i].name, sizeof(str_name)) == 0) {
                found = true;
                expected_type = rsrcs[i].type;
                break;
            }
        }
    }

    // Handle type
    if (found) {
        r = cbor_extract_from_map_string(cd, SD_FLT_TYPE, str_type, sizeof(str_type));
        if (r > 0) {
            bool found = false;

            if (expected_type) {
                found = (strncmp(str_type, expected_type, sizeof(str_type)) == 0);
            }

            if (!found) {
                for (int i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
                    if (strncmp(str_type, rsrcs[i].type, sizeof(str_type)) == 0) {
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    zcbor_unordered_map_end_decode(cd);

    return found;
}

static int prepare_sd_rsp_payload(uint8_t *payload, size_t len)
{
    ZCBOR_STATE_E(ce, 3, payload, len, 1);
    int num_rsrcs = 0;

    // TODO: Mutex while accessing resources array?
    for (int i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
        if (rsrcs[i].name && rsrcs[i].type) {
            num_rsrcs++;
        }
    }

    if (!zcbor_map_start_encode(ce, num_rsrcs)) return -EINVAL;

    for (int i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
        if (rsrcs[i].name && rsrcs[i].type) {
            if (!zcbor_tstr_put_term(ce, rsrcs[i].name, SD_NAME_MAX_LEN)) return -EINVAL;
            if (!zcbor_map_start_encode(ce, 1)) return -EINVAL;

            if (!zcbor_tstr_put_lit(ce, SD_FLT_TYPE)) return -EINVAL;
            if (!zcbor_tstr_put_term(ce, rsrcs[i].type, SD_TYPE_MAX_LEN)) return -EINVAL;

	    if (!zcbor_map_end_encode(ce, 1)) return -EINVAL;
        }
    }

    if (!zcbor_map_end_encode(ce, num_rsrcs)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int send_sd_rsp(int sock,
                       const struct sockaddr *addr, socklen_t addr_len,
                       uint8_t *token, uint8_t tkl)
{
    uint8_t *data;
    int r = 0;
    struct coap_packet response;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&response, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_NON_CON, tkl, token,
                 COAP_RESPONSE_CODE_CONTENT, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_APP_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&response);
    if (r < 0) {
        goto end;
    }

    r = prepare_sd_rsp_payload(payload, MAX_COAP_PAYLOAD_LEN);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&response, payload, r);
    if (r < 0) {
        goto end;
    }

    r = coap_server_send_coap_reply(sock, &response, addr, addr_len);

end:
    k_free(data);

    return r;
}

int coap_sd_server(struct coap_resource *resource,
             struct coap_packet *request,
             struct sockaddr *addr, socklen_t addr_len)
{
    int sock = *(int*)resource->user_data;
    uint16_t id;
    uint8_t code;
    uint8_t type;
    uint8_t tkl;
    uint8_t token[8];
    int r = 0;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;

    bool opt_cf_present = false;
    bool opt_cf_correct = false;
    bool payload_present = false;
    bool filter_passed = true;

    code = coap_header_get_code(request);
    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    if (type != COAP_TYPE_NON_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, addr_len) && !sock_is_secure(sock)) {
        return -EINVAL;
    }
#endif

    r = coap_find_options(request, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r == 1) {
        opt_cf_present = true;
    }

    if (opt_cf_present && (coap_option_value_to_int(&option) == COAP_CONTENT_FORMAT_APP_CBOR)) {
        opt_cf_correct = true;
    }

    payload = coap_packet_get_payload(request, &payload_len);
    if (payload) {
        payload_present = true;
    }

    if (opt_cf_present && (!opt_cf_correct || !payload_present)) {
        return -EINVAL;
    }

    if (opt_cf_present && opt_cf_correct && payload_present) {
        filter_passed = filter_sd_req(payload, payload_len);
    }

    if (filter_passed) {
        k_sleep(K_MSEC(sys_rand32_get() % 512));
        r = send_sd_rsp(sock, addr, addr_len, token, tkl);
    } else {
        r = 0;
    }

    return r;
}

int coap_sd_server_register_rsrc(const char *name, const char *type)
{
    // TODO: Mutex?

    for (size_t i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
        if ((rsrcs[i].name == NULL) && (rsrcs[i].type == NULL)) {
            rsrcs[i].name = name;
            rsrcs[i].type = type;
            return 0;
        }
    }

    return -1;
}

void coap_sd_server_clear_all_rsrcs(void)
{
    // TODO: Mutex?

    for (size_t i = 0; i < ARRAY_SIZE(rsrcs); ++i) {
        rsrcs[i].name = NULL;
        rsrcs[i].type = NULL;
    }
}

// Client
static int prepare_sd_req_payload(uint8_t *payload, size_t len, const char *name, const char *type)
{
    ZCBOR_STATE_E(ce, 2, payload, len, 1);

    bool name_known = (name != NULL) && (strlen(name) > 0);
    bool type_known = (type != NULL) && (strlen(type) > 0);

    int num_filters = 0;

    if (name_known) num_filters++;
    if (type_known) num_filters++;

    if (!num_filters) {
        // There are no filters to add as payload
        return 0;
    }


    if (!zcbor_list_start_encode(ce, num_filters)) return -EINVAL;

    if (name_known) {
        if (!zcbor_tstr_put_lit(ce, SD_FLT_NAME)) return -EINVAL;
        if (!zcbor_tstr_put_term(ce, name, SD_NAME_MAX_LEN)) return -EINVAL;
    }

    if (type_known) {
        if (!zcbor_tstr_put_lit(ce, SD_FLT_TYPE)) return -EINVAL;
        if (!zcbor_tstr_put_term(ce, type, SD_TYPE_MAX_LEN)) return -EINVAL;
    }

    if (!zcbor_map_end_encode(ce, num_filters)) return -EINVAL;

    return (size_t)(ce->payload - payload);
}

static int coap_sd_send_req(const char *name, const char *type, int sock, bool mesh)
{
    int r;
    struct coap_packet cpkt;
    uint8_t *data;
    uint8_t payload[MAX_COAP_PAYLOAD_LEN];
    struct sockaddr_in6 addr = {
        .sin6_family = AF_INET6,
        .sin6_port = htons(COAP_PORT),
        .sin6_addr = {
            .s6_addr = {0xff, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01}
        },
    };

    if (mesh) {
        addr.sin6_addr.s6_addr[1] = 0x03;
    }

    data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
    if (!data) {
        return -ENOMEM;
    }

    r = coap_packet_init(&cpkt, data, MAX_COAP_MSG_LEN,
                 1, COAP_TYPE_NON_CON, 4, coap_next_token(),
                 COAP_METHOD_GET, coap_next_id());
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_option(&cpkt, COAP_OPTION_URI_PATH, SD_RSRC, strlen(SD_RSRC));
    if (r < 0) {
        goto end;
    }

    r = coap_append_option_int(&cpkt, COAP_OPTION_CONTENT_FORMAT,
            COAP_CONTENT_FORMAT_APP_CBOR);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload_marker(&cpkt);
    if (r < 0) {
        goto end;
    }

    r = prepare_sd_req_payload(payload, MAX_COAP_PAYLOAD_LEN, name, type);
    if (r < 0) {
        goto end;
    }

    r = coap_packet_append_payload(&cpkt, payload, r);
    if (r < 0) {
        goto end;
    }

    r = sendto(sock, cpkt.data, cpkt.offset, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (r < 0) {
        r = -errno;
    }

end:
    k_free(data);

    return r;
}

static int coap_sd_process_rsp(int sock, 
                               uint8_t *data, size_t data_len,
                               const coap_sd_found cb,
                               const struct sockaddr *addr,
                               const socklen_t *addr_len,
                               const char *name,
                               const char *type)
{
    struct coap_packet rsp;
    int r;
    uint8_t coap_type;
    struct coap_option option;
    const uint8_t *payload;
    uint16_t payload_len;

    r = coap_packet_parse(&rsp, data, data_len, NULL, 0);
    if (r < 0) {
        return r;
    }

    coap_type = coap_header_get_type(&rsp);

    if (coap_type != COAP_TYPE_NON_CON) {
        return -EINVAL;
    }

#if BLOCK_GLOBAL_ACCESS
    if (!addr_is_local(addr, *addr_len) && !sock_is_secure(sock)) {
        return -EINVAL;
    }
#endif

    r = coap_find_options(&rsp, COAP_OPTION_CONTENT_FORMAT, &option, 1); 
    if (r != 1) {
        return -EINVAL;
    }

    if (coap_option_value_to_int(&option) != COAP_CONTENT_FORMAT_APP_CBOR) {
        return -EINVAL;
    }

    payload = coap_packet_get_payload(&rsp, &payload_len);
    if (!payload) {
        return -EINVAL;
    }

    ZCBOR_STATE_D(cd, 3, payload, payload_len, 1, 0);

    if (!zcbor_map_start_decode(cd)) return -EINVAL;

    while (!zcbor_array_at_end(cd)) {
        struct zcbor_string rcvd_name;
        struct zcbor_string rcvd_type;

        if (!zcbor_tstr_decode(cd, &rcvd_name)) {
            // Skip key and value
            for (size_t j = 0; j < 2; ++j) {
                if (!zcbor_any_skip(cd, NULL)) return -EINVAL;
	    }
            // And check next key
            continue;
	}

        if (name && strlen(name)) {
            if (strncmp(name, rcvd_name.value, rcvd_name.len < SD_NAME_MAX_LEN ? rcvd_name.len : SD_NAME_MAX_LEN) != 0) {
                // Skip value
                if (!zcbor_any_skip(cd, NULL)) return -EINVAL;
                // And check next key
                continue;
            }
	}

        // Name is OK. Now get type
            
	if (!zcbor_unordered_map_start_decode(cd)) {
            // Skip value
            if (!zcbor_any_skip(cd, NULL)) return -EINVAL;
            // And check next key
            continue;
	}

        if (!zcbor_search_key_tstr_lit(cd, SD_FLT_TYPE)) {
            // Close unordered map
	    if (!zcbor_unordered_map_end_decode(cd)) return -EINVAL;
            // And check next key
            continue;
	}

        if (!zcbor_tstr_decode(cd, &rcvd_type)) {
            // Close unordered map
	    if (!zcbor_unordered_map_end_decode(cd)) return -EINVAL;
            // And check next key
            continue;
	}

        if (type && strlen(type)) {
            if (strncmp(type, rcvd_type.value, rcvd_type.len < SD_TYPE_MAX_LEN ? rcvd_type.len : SD_TYPE_MAX_LEN) != 0) {
                // Close unordered map
	        if (!zcbor_unordered_map_end_decode(cd)) return -EINVAL;
                // And check next key
                continue;
            }
        }

        // Type is also accepted. Notify higher layer
        if (cb) {
            cb(addr, addr_len, rcvd_name.value, rcvd_type.value);
        }

        // Close unordered map
        if (!zcbor_unordered_map_end_decode(cd)) return -EINVAL;
        // And check the next key in the next loop iteration
    }

    return 0;
}

static int coap_sd_receive_rsp(int sock,
                               const coap_sd_found cb,
                               const char *name,
                               const char *type)
{
    int r;
    struct sockaddr addr;
    socklen_t addr_len;
    uint8_t response[MAX_COAP_MSG_LEN];

    while (1) {
        addr_len = sizeof(addr);
        r = recvfrom(sock, response, sizeof(response), 0, &addr, &addr_len);
        if (r < 0) {
            if (errno == EAGAIN) {
                // Expecting timeout
                return 0;
            } else {
                return -errno;
            }
        }

        coap_sd_process_rsp(sock, response, r, cb, &addr, &addr_len, name, type);
    }
}


int coap_sd_start(const char *name, const char *type, coap_sd_found cb, bool mesh)
{
    // Prepare socket
    int r;
    int sock;
    struct timeval timeout = {
        .tv_sec = 4,
    };

    sock = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        return -errno;
    }

    r = setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (r < 0) {
        r = -errno;
        goto end;
    }

    // Send request
    r = coap_sd_send_req(name, type, sock, mesh);
    if (r < 0) {
        goto end;
    }

    // Get responses and execute callback for each valid one
    ot_sed_enter_fast_polling();
    r = coap_sd_receive_rsp(sock, cb, name, type);
    ot_sed_exit_fast_polling();
end:
    close(sock);

    return r;
}
