/*
 * This file is part of the ESP32-XBee distribution (https://github.com/nebkat/esp32-xbee).
 * Copyright (c) 2019 Nebojsa Cvetkovic.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <esp_err.h>
#include <nvs_flash.h>
#include <esp_log.h>
#include <string.h>
#include <driver/uart.h>
#include <esp_wifi_types.h>
#include <driver/gpio.h>
#include <uart.h>
#include <tasks.h>
#include "config.h"

static const char *TAG = "CONFIG";
static const char *STORAGE = "config";

nvs_handle_t config_handle;

const config_item_t CONFIG_ITEMS[] = {
        // Admin
        {
                .key = KEY_CONFIG_ADMIN_AUTH,
                .type = CONFIG_ITEM_TYPE_INT8,
                .def.int8 = 0
        },
        {
                .key = KEY_CONFIG_ADMIN_USERNAME,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_ADMIN_PASSWORD,
                .type = CONFIG_ITEM_TYPE_STRING,
                .secret = true,
                .def.str = ""
        },

        // Bluetooth
        {
                .key = KEY_CONFIG_BLUETOOTH_ACTIVE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_BLUETOOTH_DEVICE_NAME,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_BLUETOOTH_DEVICE_DISCOVERABLE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = true
        }, {
                .key = KEY_CONFIG_BLUETOOTH_PIN_CODE,
                .type = CONFIG_ITEM_TYPE_UINT16,
                .secret = true,
                .def.uint16 = 1234
        },

        // NTRIP
        {
                .key = KEY_CONFIG_NTRIP_SERVER_ACTIVE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_NTRIP_SERVER_COLOR,
                .type = CONFIG_ITEM_TYPE_COLOR,
                .def.color.rgba = 0x00000055u
        }, {
                .key = KEY_CONFIG_NTRIP_SERVER_HOST,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_NTRIP_SERVER_PORT,
                .type = CONFIG_ITEM_TYPE_UINT16,
                .def.uint16 = 2101
        }, {
                .key = KEY_CONFIG_NTRIP_SERVER_MOUNTPOINT,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_NTRIP_SERVER_USERNAME,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_NTRIP_SERVER_PASSWORD,
                .type = CONFIG_ITEM_TYPE_STRING,
                .secret = true,
                .def.str = ""
        },

        {
                .key = KEY_CONFIG_NTRIP_CLIENT_ACTIVE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_NTRIP_CLIENT_COLOR,
                .type = CONFIG_ITEM_TYPE_COLOR,
                .def.color.rgba = 0x00000055u
        }, {
                .key = KEY_CONFIG_NTRIP_CLIENT_HOST,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_NTRIP_CLIENT_PORT,
                .type = CONFIG_ITEM_TYPE_UINT16,
                .def.uint16 = 2101
        }, {
                .key = KEY_CONFIG_NTRIP_CLIENT_MOUNTPOINT,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_NTRIP_CLIENT_USERNAME,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_NTRIP_CLIENT_PASSWORD,
                .type = CONFIG_ITEM_TYPE_STRING,
                .secret = true,
                .def.str = ""
        },

        {
                .key = KEY_CONFIG_NTRIP_CASTER_ACTIVE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_NTRIP_CASTER_COLOR,
                .type = CONFIG_ITEM_TYPE_COLOR,
                .def.color.rgba = 0x00000055u
        }, {
                .key = KEY_CONFIG_NTRIP_CASTER_PORT,
                .type = CONFIG_ITEM_TYPE_UINT16,
                .def.uint16 = 2101
        }, {
                .key = KEY_CONFIG_NTRIP_CASTER_MOUNTPOINT,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_NTRIP_CASTER_USERNAME,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_NTRIP_CASTER_PASSWORD,
                .type = CONFIG_ITEM_TYPE_STRING,
                .secret = true,
                .def.str = ""
        },

        // Socket
        {
                .key = KEY_CONFIG_SOCKET_SERVER_ACTIVE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_SOCKET_SERVER_COLOR,
                .type = CONFIG_ITEM_TYPE_COLOR,
                .def.color.rgba = 0x00000055u
        }, {
                .key = KEY_CONFIG_SOCKET_SERVER_TCP_PORT,
                .type = CONFIG_ITEM_TYPE_UINT16,
                .def.uint16 = 23
        }, {
                .key = KEY_CONFIG_SOCKET_SERVER_UDP_PORT,
                .type = CONFIG_ITEM_TYPE_UINT16,
                .def.uint16 = 23
        },

        {
                .key = KEY_CONFIG_SOCKET_CLIENT_ACTIVE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_SOCKET_CLIENT_COLOR,
                .type = CONFIG_ITEM_TYPE_COLOR,
                .def.color.rgba = 0x00000055u
        }, {
                .key = KEY_CONFIG_SOCKET_CLIENT_HOST,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_SOCKET_CLIENT_PORT,
                .type = CONFIG_ITEM_TYPE_UINT16,
                .def.uint16 = 23
        }, {
                .key = KEY_CONFIG_SOCKET_CLIENT_TYPE_TCP_UDP,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = true
        }, {
                .key = KEY_CONFIG_SOCKET_CLIENT_CONNECT_MESSAGE,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = "\n"
        },

        // UART
        {
                .key = KEY_CONFIG_UART_NUM,
                .type = CONFIG_ITEM_TYPE_UINT8,
                .def.uint8 = UART_NUM_0
        }, {
                .key = KEY_CONFIG_UART_TX_PIN,
                .type = CONFIG_ITEM_TYPE_UINT8,
                .def.uint8 = GPIO_NUM_1
        }, {
                .key = KEY_CONFIG_UART_RX_PIN,
                .type = CONFIG_ITEM_TYPE_UINT8,
                .def.uint8 = GPIO_NUM_3
        }, {
                .key = KEY_CONFIG_UART_RTS_PIN,
                .type = CONFIG_ITEM_TYPE_UINT8,
                .def.uint8 = GPIO_NUM_14
        }, {
                .key = KEY_CONFIG_UART_CTS_PIN,
                .type = CONFIG_ITEM_TYPE_UINT8,
                .def.uint8 = GPIO_NUM_33
        }, {
                .key = KEY_CONFIG_UART_BAUD_RATE,
                .type = CONFIG_ITEM_TYPE_UINT32,
                .def.uint32 = 115200
        }, {
                .key = KEY_CONFIG_UART_DATA_BITS,
                .type = CONFIG_ITEM_TYPE_INT8,
                .def.int8 = UART_DATA_8_BITS
        }, {
                .key = KEY_CONFIG_UART_STOP_BITS,
                .type = CONFIG_ITEM_TYPE_INT8,
                .def.int8 = UART_STOP_BITS_1
        }, {
                .key = KEY_CONFIG_UART_PARITY,
                .type = CONFIG_ITEM_TYPE_INT8,
                .def.int8 = UART_PARITY_DISABLE
        }, {
                .key = KEY_CONFIG_UART_FLOW_CTRL_RTS,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_UART_FLOW_CTRL_CTS,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_UART_LOG_FORWARD,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        },

        // WiFi
        {
                .key = KEY_CONFIG_WIFI_AP_ACTIVE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = true
        }, {
                .key = KEY_CONFIG_WIFI_AP_COLOR,
                .type = CONFIG_ITEM_TYPE_COLOR,
                .def.color.rgba = 0x00000055u
        }, {
                .key = KEY_CONFIG_WIFI_AP_SSID,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_WIFI_AP_SSID_HIDDEN,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_WIFI_AP_AUTH_MODE,
                .type = CONFIG_ITEM_TYPE_UINT8,
                .def.uint8 = WIFI_AUTH_OPEN
        }, {
                .key = KEY_CONFIG_WIFI_AP_PASSWORD,
                .type = CONFIG_ITEM_TYPE_STRING,
                .secret = true,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_WIFI_AP_GATEWAY,
                .type = CONFIG_ITEM_TYPE_IP,
                .def.uint32 = esp_netif_htonl(esp_netif_ip4_makeu32(192, 168, 4, 1))
        }, {
                .key = KEY_CONFIG_WIFI_AP_SUBNET,
                .type = CONFIG_ITEM_TYPE_UINT8,
                .def.uint8 = 24
        }, {
                .key = KEY_CONFIG_WIFI_STA_ACTIVE,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_WIFI_STA_COLOR,
                .type = CONFIG_ITEM_TYPE_COLOR,
                .def.color.rgba = 0x0044ff55u
        }, {
                .key = KEY_CONFIG_WIFI_STA_SSID,
                .type = CONFIG_ITEM_TYPE_STRING,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_WIFI_STA_PASSWORD,
                .type = CONFIG_ITEM_TYPE_STRING,
                .secret = true,
                .def.str = ""
        }, {
                .key = KEY_CONFIG_WIFI_STA_SCAN_MODE_ALL,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_WIFI_STA_AP_FORWARD,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_WIFI_STA_STATIC,
                .type = CONFIG_ITEM_TYPE_BOOL,
                .def.bool1 = false
        }, {
                .key = KEY_CONFIG_WIFI_STA_IP,
                .type = CONFIG_ITEM_TYPE_IP,
                .def.uint32 = esp_netif_htonl(esp_netif_ip4_makeu32(192, 168, 0, 100))
        }, {
                .key = KEY_CONFIG_WIFI_STA_GATEWAY,
                .type = CONFIG_ITEM_TYPE_IP,
                .def.uint32 = esp_netif_htonl(esp_netif_ip4_makeu32(192, 168, 0, 1))
        }, {
                .key = KEY_CONFIG_WIFI_STA_SUBNET,
                .type = CONFIG_ITEM_TYPE_UINT8,
                .def.uint8 = 24
        }, {
                .key = KEY_CONFIG_WIFI_STA_DNS_A,
                .type = CONFIG_ITEM_TYPE_IP,
                .def.uint32 = esp_netif_htonl(esp_netif_ip4_makeu32(1, 1, 1, 1))
        }, {
                .key = KEY_CONFIG_WIFI_STA_DNS_B,
                .type = CONFIG_ITEM_TYPE_IP,
                .def.uint32 = esp_netif_htonl(esp_netif_ip4_makeu32(1, 0, 0, 1))
        }
};

const config_item_t *config_items_get(int *count) {
    *count = sizeof(CONFIG_ITEMS) / sizeof(config_item_t);
    return &CONFIG_ITEMS[0];
}

esp_err_t config_set(const config_item_t *item, void *value) {
    switch (item->type) {
        case CONFIG_ITEM_TYPE_BOOL:
            return config_set_bool1(item->key, *((bool *) value));
        case CONFIG_ITEM_TYPE_INT8:
            return config_set_i8(item->key, *((int8_t *)value));
        case CONFIG_ITEM_TYPE_INT16:
            return config_set_i16(item->key, *((int16_t *)value));
        case CONFIG_ITEM_TYPE_INT32:
            return config_set_i32(item->key, *((int32_t *)value));
        case CONFIG_ITEM_TYPE_INT64:
            return config_set_i64(item->key, *((int64_t *)value));
        case CONFIG_ITEM_TYPE_UINT8:
            return config_set_u8(item->key, *((uint8_t *)value));
        case CONFIG_ITEM_TYPE_UINT16:
            return config_set_u16(item->key, *((uint16_t *)value));
        case CONFIG_ITEM_TYPE_UINT32:
            return config_set_u32(item->key, *((uint32_t *)value));
        case CONFIG_ITEM_TYPE_UINT64:
            return config_set_u64(item->key, *((uint64_t *)value));
        case CONFIG_ITEM_TYPE_STRING:
            return config_set_str(item->key, (char *) value);
        default:
            return ESP_ERR_INVALID_ARG;
    }
}

esp_err_t config_set_i8(const char *key, int8_t value) {
    return nvs_set_i8(config_handle, key, value);
}

esp_err_t config_set_i16(const char *key, int16_t value) {
    return nvs_set_i16(config_handle, key, value);
}

esp_err_t config_set_i32(const char *key, int32_t value) {
    return nvs_set_i32(config_handle, key, value);
}

esp_err_t config_set_i64(const char *key, int64_t value) {
    return nvs_set_i64(config_handle, key, value);
}

esp_err_t config_set_u8(const char *key, uint8_t value) {
    return nvs_set_u8(config_handle, key, value);
}

esp_err_t config_set_u16(const char *key, uint16_t value) {
    return nvs_set_u16(config_handle, key, value);
}

esp_err_t config_set_u32(const char *key, uint32_t value) {
    return nvs_set_u32(config_handle, key, value);
}

esp_err_t config_set_u64(const char *key, uint64_t value) {
    return nvs_set_u64(config_handle, key, value);
}

esp_err_t config_set_color(const char *key, config_color_t value) {
    return nvs_set_u32(config_handle, key, value.rgba);
}

esp_err_t config_set_bool1(const char *key, bool value) {
    return nvs_set_i8(config_handle, key, value);
}

esp_err_t config_set_str(const char *key, char *value) {
    return nvs_set_str(config_handle, key, value);
}

esp_err_t config_set_blob(const char *key, char *value, size_t length) {
    return nvs_set_blob(config_handle, key, value, length);
}

esp_err_t config_init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_LOGD(TAG, "Opening Non-Volatile Storage (NVS) handle '%s'... ", STORAGE);
    return nvs_open(STORAGE, NVS_READWRITE, &config_handle);
}

esp_err_t config_reset() {
    uart_nmea("$PESP,CFG,RESET");

    return nvs_erase_all(config_handle);
}

int8_t config_get_i8(const config_item_t *item) {
    int8_t value = item->def.int8;
    nvs_get_i8(config_handle, item->key, &value);
    return value;
}

int16_t config_get_i16(const config_item_t *item) {
    int16_t value = item->def.int16;
    nvs_get_i16(config_handle, item->key, &value);
    return value;
}

int32_t config_get_i32(const config_item_t *item) {
    int32_t value = item->def.int32;
    nvs_get_i32(config_handle, item->key, &value);
    return value;
}

int64_t config_get_i64(const config_item_t *item) {
    int64_t value = item->def.int64;
    nvs_get_i64(config_handle, item->key, &value);
    return value;
}

uint8_t config_get_u8(const config_item_t *item) {
    uint8_t value = item->def.uint8;
    nvs_get_u8(config_handle, item->key, &value);
    return value;
}

uint16_t config_get_u16(const config_item_t *item) {
    uint16_t value = item->def.uint16;
    nvs_get_u16(config_handle, item->key, &value);
    return value;
}

uint32_t config_get_u32(const config_item_t *item) {
    uint32_t value = item->def.uint32;
    nvs_get_u32(config_handle, item->key, &value);
    return value;
}

uint64_t config_get_u64(const config_item_t *item) {
    uint64_t value = item->def.uint64;
    nvs_get_u64(config_handle, item->key, &value);
    return value;
}

config_color_t config_get_color(const config_item_t *item) {
    config_color_t value = item->def.color;
    nvs_get_u32(config_handle, item->key, &value.rgba);
    return value;
}

bool config_get_bool1(const config_item_t *item) {
    int8_t value = item->def.bool1;
    nvs_get_i8(config_handle, item->key, &value);
    return value > 0;
}

const config_item_t * config_get_item(const char *key) {
    for (unsigned int i = 0; i < sizeof(CONFIG_ITEMS) / sizeof(config_item_t); i++) {
        const config_item_t *item = &CONFIG_ITEMS[i];
        if (strcmp(item->key, key) == 0) {
            return item;
        }
    }

    // Fatal error
    ESP_ERROR_CHECK(ESP_FAIL);

    return NULL;
}

esp_err_t config_get_primitive(const config_item_t *item, void *out_value) {
    esp_err_t ret;
    switch (item->type) {
        case CONFIG_ITEM_TYPE_BOOL:
            *((bool *) out_value) = item->def.bool1;
            ret = nvs_get_i8(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_INT8:
            *((int8_t *) out_value) = item->def.int8;
            ret = nvs_get_i8(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_INT16:
            *((int16_t *) out_value) = item->def.int16;
            ret = nvs_get_i16(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_INT32:
            *((int32_t *) out_value) = item->def.int32;
            ret = nvs_get_i32(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_INT64:
            *((int64_t *) out_value) = item->def.int64;
            ret = nvs_get_i64(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_UINT8:
            *((uint8_t *) out_value) = item->def.uint8;
            ret = nvs_get_u8(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_UINT16:
            *((uint16_t *) out_value) = item->def.uint16;
            ret = nvs_get_u16(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_UINT32:
        case CONFIG_ITEM_TYPE_IP:
            *((uint32_t *) out_value) = item->def.uint32;
            ret = nvs_get_u32(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_UINT64:
            *((uint64_t *) out_value) = item->def.uint64;
            ret = nvs_get_u64(config_handle, item->key, out_value);
            break;
        case CONFIG_ITEM_TYPE_COLOR:
            *((config_color_t *) out_value) = item->def.color;
            ret = nvs_get_u32(config_handle, item->key, out_value);
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return (ret == ESP_OK || ret == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : ret;
}

esp_err_t config_get_str_blob_alloc(const config_item_t *item, void **out_value) {
    size_t length;
    esp_err_t ret = config_get_str_blob(item, NULL, &length);
    if (ret != ESP_OK) return ret;
    *out_value = malloc(length);
    return config_get_str_blob(item, *out_value, &length);
}

esp_err_t config_get_str_blob(const config_item_t *item, void *out_value, size_t *length) {
    esp_err_t ret;

    switch (item->type) {
        case CONFIG_ITEM_TYPE_STRING:
            ret = nvs_get_str(config_handle, item->key, out_value, length);
            if (ret == ESP_ERR_NVS_NOT_FOUND) {
                if (length != NULL) *length = strlen(item->def.str) + 1;
                if (out_value != NULL) strcpy(out_value, item->def.str);
            }
            break;
        case CONFIG_ITEM_TYPE_BLOB:
            ret = nvs_get_blob(config_handle, item->key, out_value, length);
            if (ret == ESP_ERR_NVS_NOT_FOUND) {
                if (length != NULL) *length = item->def.blob.length;
                if (out_value != NULL) memcpy(out_value, item->def.blob.data, item->def.blob.length);
            }
            break;
        default:
            return ESP_ERR_INVALID_ARG;
    }

    return (ret == ESP_OK || ret == ESP_ERR_NVS_NOT_FOUND) ? ESP_OK : ret;
}

esp_err_t config_commit() {
    uart_nmea("$PESP,CFG,UPDATED");

    return nvs_commit(config_handle);
}

static void config_restart_task() {
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

void config_restart() {
    uart_nmea("$PESP,CFG,RESTARTING");

    xTaskCreate(config_restart_task, "config_restart_task", 4096, NULL, TASK_PRIORITY_MAX, NULL);
}
