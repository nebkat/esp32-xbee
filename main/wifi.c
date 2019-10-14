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

#include <tcpip_adapter.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <string.h>
#include <mdns.h>
#include <driver/gpio.h>
#include <sys/param.h>
#include <tasks.h>
#include <uart.h>
#include <lwip/inet.h>
#include <status_led.h>
#include <nmea.h>
#include "wifi.h"
#include "config.h"

static const char *TAG = "WIFI";

#define RSSI_LED_GPIO_NUM GPIO_NUM_18

static EventGroupHandle_t wifi_event_group;
const int GOT_IPV4_BIT = BIT0;
const int GOT_IPV6_BIT = BIT1;

static int rssi_led_duration = 0;
static TaskHandle_t rssi_task;

static status_led_handle_t status_led_ap;
static status_led_handle_t status_led_sta;

static void wifi_rssi_led_task(void *ctx) {
    gpio_set_direction(RSSI_LED_GPIO_NUM, GPIO_MODE_OUTPUT);
    int check_counter = 0;
    while (true) {
        gpio_set_level(RSSI_LED_GPIO_NUM, true);
        vTaskDelay(pdMS_TO_TICKS(rssi_led_duration));
        gpio_set_level(RSSI_LED_GPIO_NUM, false);
        vTaskDelay(pdMS_TO_TICKS(1000 - rssi_led_duration));

        check_counter++;
        if (check_counter > 5) {
            check_counter = 0;

            wifi_ap_record_t wifi_ap_record;
            if (esp_wifi_sta_get_ap_info(&wifi_ap_record) == ESP_OK) {
                rssi_led_duration = MAX(0, MIN(1, (((float) wifi_ap_record.rssi + 90.0f) / 60.0f))) * 1000;
            }
        }
    }
}

static void handle_sta_start(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WIFI_EVENT_STA_START");
    esp_wifi_connect();
}

static void handle_sta_stop(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP");
}

static void handle_sta_connected(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_sta_connected_t *event = (const wifi_event_sta_connected_t *) event_data;
    ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED: ssid: %.*s", event->ssid_len, event->ssid);

    // Keep track of signal strength
    xTaskCreate(wifi_rssi_led_task, "wifi_rssi_led", 1024, NULL, TASK_PRIORITY_STATUS_LED, rssi_task);

    tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_STA);

    uart_nmea("PESP,WIFI,STA,CONNECTED,%.*s", event->ssid_len, event->ssid);

    if (status_led_sta != NULL) status_led_sta->flashing_mode = STATUS_LED_FADE;
}

static void handle_sta_disconnected(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_sta_disconnected_t *event = (const wifi_event_sta_disconnected_t *) event_data;
    char *reason;
    switch (event->reason) {
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_ASSOC_EXPIRE:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            reason = "AUTH";
            break;
        case WIFI_REASON_NO_AP_FOUND:
            reason = "NOT_FOUND";
            break;
        default:
            reason = "UNKNOWN";

            // Try to reconnect
            esp_wifi_connect();
    }
    ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED: ssid: %.*s, reason: %d (%s)", event->ssid_len, event->ssid, event->reason, reason);

    uart_nmea("PESP,WIFI,STA,DISCONNECTED,%.*s,%d,%s", event->ssid_len, event->ssid, event->reason, reason);

    // No longer tracking signal strength
    gpio_set_level(RSSI_LED_GPIO_NUM, false);
    vTaskDelete(rssi_task);

    xEventGroupClearBits(wifi_event_group, GOT_IPV4_BIT);
    xEventGroupClearBits(wifi_event_group, GOT_IPV6_BIT);

    if (status_led_sta != NULL) status_led_sta->flashing_mode = STATUS_LED_STATIC;
}

static void handle_sta_auth_mode_change(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_sta_authmode_change_t *event = (const wifi_event_sta_authmode_change_t *) event_data;
    char *old_auth_mode = wifi_auth_mode_name(event->old_mode);
    char *new_auth_mode = wifi_auth_mode_name(event->new_mode);
    ESP_LOGI(TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE: old: %s, new: %s", old_auth_mode, new_auth_mode);

    uart_nmea("PESP,WIFI,STA,AUTH_MODE_CHANGED,%s,%s", old_auth_mode, new_auth_mode);
}

static void handle_ap_start(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WIFI_EVENT_AP_START");
    tcpip_adapter_create_ip6_linklocal(TCPIP_ADAPTER_IF_AP);
}

static void handle_ap_stop(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");
}

static void handle_ap_sta_connected(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *) event_data;
    ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED: mac: " MACSTR, MAC2STR(event->mac));

    uart_nmea("PESP,WIFI,AP,STA_CONNECTED," MACSTR, MAC2STR(event->mac));

    if (status_led_ap != NULL) status_led_ap->flashing_mode = STATUS_LED_FADE;
}

static void handle_ap_sta_disconnected(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_ap_stadisconnected_t *event = (const wifi_event_ap_stadisconnected_t *) event_data;
    ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED: mac: "
            MACSTR, MAC2STR(event->mac));

    uart_nmea("PESP,WIFI,AP,STA_DISCONNECTED," MACSTR, MAC2STR(event->mac));

    wifi_sta_list_t ap_sta_list;
    esp_wifi_ap_get_sta_list(&ap_sta_list);

    if (status_led_ap != NULL) status_led_ap->flashing_mode = ap_sta_list.num > 0 ? STATUS_LED_FADE : STATUS_LED_STATIC;
}

static void handle_sta_got_ip(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) event_data;
    ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: ip: " IPSTR ", mask: " IPSTR ", gw: " IPSTR,
            IP2STR(&event->ip_info.ip),
            IP2STR(&event->ip_info.netmask),
            IP2STR(&event->ip_info.gw));

    uart_nmea("PESP,WIFI,STA,IP," IPSTR "," IPSTR "," IPSTR,
            IP2STR(&event->ip_info.ip),
            IP2STR(&event->ip_info.netmask),
            IP2STR(&event->ip_info.gw));

    xEventGroupSetBits(wifi_event_group, GOT_IPV4_BIT);
}

static void handle_sta_lost_ip(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP6");
    xEventGroupClearBits(wifi_event_group, GOT_IPV4_BIT);
}

static void handle_ap_sta_ip_assigned(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    const ip_event_ap_staipassigned_t *event = (const ip_event_ap_staipassigned_t *) event_data;
    ESP_LOGI(TAG, "IP_EVENT_AP_STAIPASSIGNED: ip: " IPSTR, IP2STR(&event->ip));

    uart_nmea("PESP,WIFI,AP,STA_IP_ASSIGNED," IPSTR, IP2STR(&event->ip));
}

static void handle_got_ip6(void *arg, esp_event_base_t base, int32_t event_id, void *event_data) {
    const ip_event_got_ip6_t *event = (const ip_event_got_ip6_t *) event_data;
    char *if_name;
    switch (event->if_index) {
        case TCPIP_ADAPTER_IF_STA:
            if_name = "STA";
            break;
        case TCPIP_ADAPTER_IF_AP:
            if_name = "AP";
            break;
        case TCPIP_ADAPTER_IF_ETH:
            if_name = "ETH";
            break;
        default:
            if_name = "UNKNOWN";
            break;
    }
    ESP_LOGI(TAG, "IP_EVENT_GOT_IP6: if: %s, ip: " IPV6STR, if_name, IPV62STR(event->ip6_info.ip));

    uart_nmea("PESP,WIFI,%s,IP," IPV6STR, if_name, IPV62STR(event->ip6_info.ip));

    xEventGroupSetBits(wifi_event_group, GOT_IPV6_BIT);
}

void wait_for_ip() {
    xEventGroupWaitBits(wifi_event_group, GOT_IPV4_BIT | GOT_IPV6_BIT, false, true, portMAX_DELAY);
}

void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // SoftAP
    wifi_config_t wifi_config_ap = {};
    wifi_config_ap.ap.max_connection = 4;
    bool ap_enable = config_get_bool1(CONF_ITEM(KEY_CONFIG_WIFI_AP_ACTIVE));
    size_t ap_ssid_len = sizeof(wifi_config_ap.ap.ssid);
    config_get_str_blob(CONF_ITEM(KEY_CONFIG_WIFI_AP_SSID), &wifi_config_ap.ap.ssid, &ap_ssid_len);
    wifi_config_ap.ap.ssid_len = ap_ssid_len;
    if (ap_ssid_len == 0) {
        uint8_t mac[6];
        esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
        snprintf((char *) wifi_config_ap.ap.ssid, sizeof(wifi_config_ap.ap.ssid), "ESP_XBee_%02X%02X%02X",
                mac[3], mac[4], mac[5]);

        config_set_str(KEY_CONFIG_WIFI_AP_SSID, (char *) wifi_config_ap.ap.ssid);
    }
    config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_AP_SSID_HIDDEN), &wifi_config_ap.ap.ssid_hidden);
    size_t ap_password_len = sizeof(wifi_config_ap.ap.password);
    config_get_str_blob(CONF_ITEM(KEY_CONFIG_WIFI_AP_PASSWORD), &wifi_config_ap.ap.password, &ap_password_len);
    ap_password_len--; // Remove null terminator from length
    config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_AP_AUTH_MODE), &wifi_config_ap.ap.authmode);

    // STA
    wifi_config_t wifi_config_sta = {};
    bool sta_enable = config_get_bool1(CONF_ITEM(KEY_CONFIG_WIFI_STA_ACTIVE));
    size_t sta_ssid_len = sizeof(wifi_config_sta.sta.ssid);
    config_get_str_blob(CONF_ITEM(KEY_CONFIG_WIFI_STA_SSID), &wifi_config_sta.sta.ssid, &sta_ssid_len);
    size_t sta_password_len = sizeof(wifi_config_sta.sta.password);
    config_get_str_blob(CONF_ITEM(KEY_CONFIG_WIFI_STA_PASSWORD), &wifi_config_sta.sta.password, &sta_password_len);
    sta_password_len--; // Remove null terminator from length

    // Listen for WiFi and IP events
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &handle_sta_start, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_STOP, &handle_sta_stop, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_CONNECTED, &handle_sta_connected, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &handle_sta_disconnected, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_AUTHMODE_CHANGE, &handle_sta_auth_mode_change, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, &handle_ap_start, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STOP, &handle_ap_stop, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STACONNECTED, &handle_ap_sta_connected, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED, &handle_ap_sta_disconnected, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &handle_sta_got_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &handle_sta_lost_ip, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_AP_STAIPASSIGNED, &handle_ap_sta_ip_assigned, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_GOT_IP6, &handle_got_ip6, NULL));

    // Configure and connect
    ESP_LOGI(TAG, "SoftAP: %s (%s)", wifi_config_ap.ap.ssid, ap_password_len == 0 ? "open" : "with password");
    ESP_LOGI(TAG, "Station: %s (%s)", wifi_config_sta.sta.ssid, sta_password_len == 0 ? "open" : "with password");

    wifi_mode_t wifi_mode;
    if (sta_enable && ap_enable) {
        wifi_mode = WIFI_MODE_APSTA;
    } else if (ap_enable) {
        wifi_mode = WIFI_MODE_AP;
    } else if (sta_enable) {
        wifi_mode = WIFI_MODE_STA;
    } else {
        return;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(wifi_mode));
    if (ap_enable) ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config_ap));
    if (sta_enable) ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config_sta));
    ESP_ERROR_CHECK(esp_wifi_start());

    if (ap_enable) {
        uart_nmea("PESP,WIFI,AP,SSID,%s,%s", wifi_config_ap.ap.ssid, ap_password_len == 0 ? "OPEN" : "PASSWORD");
        config_color_t ap_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_WIFI_AP_COLOR));
        if (ap_led_color.rgba != 0) status_led_ap = status_led_add(ap_led_color.rgba, STATUS_LED_STATIC, 250, 2500, 0);
    }
    if (sta_enable) {
        uart_nmea("PESP,WIFI,STA,SSID,%s,%s", wifi_config_sta.sta.ssid, sta_password_len == 0 ? "OPEN" : "PASSWORD");
        config_color_t sta_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_WIFI_STA_COLOR));
        if (sta_led_color.rgba != 0) status_led_sta = status_led_add(sta_led_color.rgba, STATUS_LED_STATIC, 250, 2500, 0);
    }
}

wifi_ap_record_t *wifi_scan(uint16_t *number) {
    wifi_mode_t wifi_mode;
    esp_wifi_get_mode(&wifi_mode);

    // Ensure
    if (wifi_mode != WIFI_MODE_APSTA && wifi_mode != WIFI_MODE_STA) {
        esp_wifi_set_mode(wifi_mode == WIFI_MODE_AP ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    }

    wifi_scan_config_t wifi_scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = 0
    };

    ESP_LOGI(TAG, "Scan starting");

    esp_wifi_scan_start(&wifi_scan_config, true);

    esp_wifi_scan_get_ap_num(number);
    ESP_LOGI(TAG, "Scan complete %d found", *number);
    if (*number <= 0) {
        return NULL;
    }

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *) malloc(*number * sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(number, ap_records);

    return ap_records;
}

char *wifi_auth_mode_name(wifi_auth_mode_t auth_mode) {
    switch (auth_mode) {
        case WIFI_AUTH_OPEN:
            return "OPEN";
        case WIFI_AUTH_WEP:
            return "WEP";
        case WIFI_AUTH_WPA_PSK:
            return "WPA_PSK";
        case WIFI_AUTH_WPA2_PSK:
            return "WPA2_PSK";
        case WIFI_AUTH_WPA_WPA2_PSK:
            return "WPA/2_PSK";
        case WIFI_AUTH_WPA2_ENTERPRISE:
            return "WPA2_ENTERPRISE";
        default:
            return "Unknown";
    }
}
