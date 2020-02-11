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

#include <freertos/FreeRTOS.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <string.h>
#include <mdns.h>
#include <math.h>
#include <driver/gpio.h>
#include <sys/param.h>
#include <tasks.h>
#include <uart.h>
#include <status_led.h>
#include <retry.h>
#include <freertos/event_groups.h>
#include <esp_netif_ip_addr.h>
#include "wifi.h"
#include "config.h"

static const char *TAG = "WIFI";

static EventGroupHandle_t wifi_event_group;
const int WIFI_STA_GOT_IPV4_BIT = BIT0;
const int WIFI_STA_GOT_IPV6_BIT = BIT1;
const int WIFI_AP_STA_CONNECTED_BIT = BIT2;

static TaskHandle_t sta_status_task = NULL;
static TaskHandle_t sta_reconnect_task = NULL;

static status_led_handle_t status_led_ap;
static status_led_handle_t status_led_sta;

static wifi_config_t config_ap;
static wifi_config_t config_sta;

static retry_delay_handle_t delay_handle;

static bool ap_active = false;
static bool sta_active = false;

static bool sta_connected;
static wifi_ap_record_t sta_ap_info;
static wifi_sta_list_t ap_sta_list;

static esp_netif_t *esp_netif_ap;
static esp_netif_t *esp_netif_sta;

static void wifi_sta_status_task(void *ctx) {
    uint8_t rssi_duty = 0;
    while (true) {
        sta_connected = esp_wifi_sta_get_ap_info(&sta_ap_info) == ESP_OK;

        if (sta_connected) {
            float rssi_percentage = ((float) sta_ap_info.rssi + 90.0f) / 60.0f;
            rssi_percentage = MAX(0, MIN(1, rssi_percentage));
            rssi_duty = powf(rssi_percentage, 3) * 255;
        } else {
            rssi_duty = 0;
        }

        rssi_led_fade(rssi_duty, 100);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void wifi_sta_reconnect_task(void *ctx) {
    while (true) {
        int attempts = retry_delay(delay_handle);

        ESP_LOGI(TAG, "Station Reconnecting: %s, attempts: %d", config_sta.sta.ssid, attempts);
        uart_nmea("$PESP,WIFI,STA,RECONNECTING,%s,%d", config_sta.sta.ssid, attempts);

        esp_wifi_connect();

        // Wait for next disconnect event
        vTaskSuspend(NULL);
    }
}

static void handle_sta_start(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WIFI_EVENT_STA_START");

    sta_active = true;

    esp_wifi_connect();
}

static void handle_sta_stop(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WIFI_EVENT_STA_STOP");

    sta_active = false;
}

static void handle_sta_connected(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_sta_connected_t *event = (const wifi_event_sta_connected_t *) event_data;

    ESP_LOGI(TAG, "WIFI_EVENT_STA_CONNECTED: ssid: %.*s", event->ssid_len, event->ssid);
    uart_nmea("$PESP,WIFI,STA,CONNECTED,%.*s", event->ssid_len, event->ssid);

    sta_connected = true;

    retry_reset(delay_handle);

    // Tracking status
    if (sta_status_task != NULL) vTaskResume(sta_status_task);

    // No longer attempting to reconnect
    if (sta_reconnect_task != NULL) vTaskSuspend(sta_reconnect_task);

    if (status_led_sta != NULL) status_led_sta->flashing_mode = STATUS_LED_FADE;
}

static void handle_sta_disconnected(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
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
    }

    ESP_LOGI(TAG, "WIFI_EVENT_STA_DISCONNECTED: ssid: %.*s, reason: %d (%s)", event->ssid_len, event->ssid, event->reason, reason);
    uart_nmea("$PESP,WIFI,STA,DISCONNECTED,%.*s,%d,%s", event->ssid_len, event->ssid, event->reason, reason);

    sta_connected = false;

    // No longer tracking status
    if (sta_status_task != NULL) vTaskSuspend(sta_status_task);

    // Attempting to reconnect
    if (sta_reconnect_task != NULL) vTaskResume(sta_reconnect_task);

    // Disable RSSI led
    rssi_led_set(0);

    xEventGroupClearBits(wifi_event_group, WIFI_STA_GOT_IPV4_BIT);
    xEventGroupClearBits(wifi_event_group, WIFI_STA_GOT_IPV6_BIT);

    if (status_led_sta != NULL) status_led_sta->flashing_mode = STATUS_LED_STATIC;
}

static void handle_sta_auth_mode_change(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_sta_authmode_change_t *event = (const wifi_event_sta_authmode_change_t *) event_data;
    const char *old_auth_mode = wifi_auth_mode_name(event->old_mode);
    const char *new_auth_mode = wifi_auth_mode_name(event->new_mode);

    ESP_LOGI(TAG, "WIFI_EVENT_STA_AUTHMODE_CHANGE: old: %s, new: %s", old_auth_mode, new_auth_mode);
    uart_nmea("$PESP,WIFI,STA,AUTH_MODE_CHANGED,%s,%s", old_auth_mode, new_auth_mode);
}

static void handle_ap_start(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WIFI_EVENT_AP_START");

    ap_active = true;
}

static void handle_ap_stop(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "WIFI_EVENT_AP_STOP");

    ap_active = false;
}

static void handle_ap_sta_connected(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_ap_staconnected_t *event = (const wifi_event_ap_staconnected_t *) event_data;

    ESP_LOGI(TAG, "WIFI_EVENT_AP_STACONNECTED: mac: " MACSTR, MAC2STR(event->mac));
    uart_nmea("$PESP,WIFI,AP,STA_CONNECTED," MACSTR, MAC2STR(event->mac));

    xEventGroupSetBits(wifi_event_group, WIFI_AP_STA_CONNECTED_BIT);

    if (status_led_ap != NULL) status_led_ap->flashing_mode = STATUS_LED_FADE;
}

static void handle_ap_sta_disconnected(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    const wifi_event_ap_stadisconnected_t *event = (const wifi_event_ap_stadisconnected_t *) event_data;

    ESP_LOGI(TAG, "WIFI_EVENT_AP_STADISCONNECTED: mac: " MACSTR, MAC2STR(event->mac));
    uart_nmea("$PESP,WIFI,AP,STA_DISCONNECTED," MACSTR, MAC2STR(event->mac));

    wifi_ap_sta_list();
    if (ap_sta_list.num == 0) {
        xEventGroupClearBits(wifi_event_group, WIFI_AP_STA_CONNECTED_BIT);

        if (status_led_ap != NULL) status_led_ap->flashing_mode = STATUS_LED_STATIC;
    }
}

static void handle_sta_got_ip(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    const ip_event_got_ip_t *event = (const ip_event_got_ip_t *) event_data;

    ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP: ip: " IPSTR "/%d, gw: " IPSTR,
            IP2STR(&event->ip_info.ip),
            ffs(~event->ip_info.netmask.addr) - 1,
            IP2STR(&event->ip_info.gw));
    uart_nmea("$PESP,WIFI,STA,IP," IPSTR "/%d," IPSTR,
            IP2STR(&event->ip_info.ip),
            ffs(~event->ip_info.netmask.addr) - 1,
            IP2STR(&event->ip_info.gw));

    xEventGroupSetBits(wifi_event_group, WIFI_STA_GOT_IPV4_BIT);
}

static void handle_sta_lost_ip(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGI(TAG, "IP_EVENT_STA_LOST_IP");
    uart_nmea("$PESP,WIFI,STA,IP_LOST");

    xEventGroupClearBits(wifi_event_group, WIFI_STA_GOT_IPV4_BIT);
}

static void handle_ap_sta_ip_assigned(void *esp_netif, esp_event_base_t base, int32_t event_id, void *event_data) {
    const ip_event_ap_staipassigned_t *event = (const ip_event_ap_staipassigned_t *) event_data;

    ESP_LOGI(TAG, "IP_EVENT_AP_STAIPASSIGNED: ip: " IPSTR, IP2STR(&event->ip));
    uart_nmea("$PESP,WIFI,AP,STA_IP_ASSIGNED," IPSTR, IP2STR(&event->ip));
}

void wait_for_ip() {
    xEventGroupWaitBits(wifi_event_group, WIFI_STA_GOT_IPV4_BIT, false, false, portMAX_DELAY);
}

void wait_for_network() {
    xEventGroupWaitBits(wifi_event_group, WIFI_STA_GOT_IPV4_BIT | WIFI_AP_STA_CONNECTED_BIT, false, false, portMAX_DELAY);
}

void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    // Reconnect delay timer
    delay_handle = retry_init(true, 5, 2000, 60000);

    // SoftAP
    bool ap_enable = config_get_bool1(CONF_ITEM(KEY_CONFIG_WIFI_AP_ACTIVE));
    if (ap_enable) {
        esp_netif_ap = esp_netif_create_default_wifi_ap();

        // IP configuration
        esp_netif_ip_info_t ip_info_ap;
        config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_AP_GATEWAY), &ip_info_ap.ip);
        ip_info_ap.gw = ip_info_ap.ip;
        uint8_t subnet = config_get_u8(CONF_ITEM(KEY_CONFIG_WIFI_STA_SUBNET));
        ip_info_ap.netmask.addr = esp_netif_htonl(0xffffffffu << (32u - subnet));

        esp_netif_dhcps_stop(esp_netif_ap);
        esp_netif_set_ip_info(esp_netif_ap, &ip_info_ap);
        esp_netif_dhcps_start(esp_netif_ap);

        config_ap.ap.max_connection = 4;
        size_t ap_ssid_len = sizeof(config_ap.ap.ssid);
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_WIFI_AP_SSID), &config_ap.ap.ssid, &ap_ssid_len);
        ap_ssid_len--; // Remove null terminator from length
        config_ap.ap.ssid_len = ap_ssid_len;
        if (ap_ssid_len == 0) {
            // Generate a default AP SSID based on MAC address and store
            uint8_t mac[6];
            esp_wifi_get_mac(ESP_IF_WIFI_AP, mac);
            snprintf((char *) config_ap.ap.ssid, sizeof(config_ap.ap.ssid), "ESP_XBee_%02X%02X%02X",
                    mac[3], mac[4], mac[5]);
            config_ap.ap.ssid_len = strlen((char *) config_ap.ap.ssid);

            config_set_str(KEY_CONFIG_WIFI_AP_SSID, (char *) config_ap.ap.ssid);
        }
        config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_AP_SSID_HIDDEN), &config_ap.ap.ssid_hidden);
        size_t ap_password_len = sizeof(config_ap.ap.password);
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_WIFI_AP_PASSWORD), &config_ap.ap.password, &ap_password_len);
        ap_password_len--; // Remove null terminator from length
        config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_AP_AUTH_MODE), &config_ap.ap.authmode);

        ESP_LOGI(TAG, "WIFI_AP_SSID: %s %s(%s)", config_ap.ap.ssid,
                config_ap.ap.ssid_hidden ? "(hidden) " : "",
                ap_password_len == 0 ? "open" : "with password");
        uart_nmea("$PESP,WIFI,AP,SSID,%s,%c,%c", config_ap.ap.ssid,
                config_ap.ap.ssid_hidden ? 'H' : 'V',
                ap_password_len == 0 ? 'O' : 'P');

        ESP_LOGI(TAG, "WIFI_AP_IP: ip: " IPSTR "/%d, gw: " IPSTR,
                IP2STR(&ip_info_ap.ip),
                ffs(~ip_info_ap.netmask.addr) - 1,
                IP2STR(&ip_info_ap.gw));
        uart_nmea("$PESP,WIFI,AP,IP," IPSTR "/%d",
                IP2STR(&ip_info_ap.ip),
                ffs(~ip_info_ap.netmask.addr) - 1);
    }

    // STA
    bool sta_enable = config_get_bool1(CONF_ITEM(KEY_CONFIG_WIFI_STA_ACTIVE));
    if (sta_enable) {
        esp_netif_sta = esp_netif_create_default_wifi_sta();

        // Static IP configuration
        if (config_get_bool1(CONF_ITEM(KEY_CONFIG_WIFI_STA_STATIC))) {
            esp_netif_ip_info_t ip_info_sta;
            config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_STA_IP), &ip_info_sta.ip);
            config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_STA_GATEWAY), &ip_info_sta.gw);
            uint8_t subnet = config_get_u8(CONF_ITEM(KEY_CONFIG_WIFI_STA_SUBNET));
            ip_info_sta.netmask.addr = esp_netif_htonl(0xffffffffu << (32u - subnet));

            esp_netif_dns_info_t dns_info_sta_main, dns_info_sta_backup;
            config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_STA_DNS_A), &dns_info_sta_main.ip.u_addr.ip4.addr);
            config_get_primitive(CONF_ITEM(KEY_CONFIG_WIFI_STA_DNS_B), &dns_info_sta_backup.ip.u_addr.ip4.addr);

            esp_netif_dhcpc_stop(esp_netif_sta);
            esp_netif_set_ip_info(esp_netif_sta, &ip_info_sta);
            esp_netif_set_dns_info(esp_netif_sta, ESP_NETIF_DNS_MAIN, &dns_info_sta_main);
            esp_netif_set_dns_info(esp_netif_sta, ESP_NETIF_DNS_BACKUP, &dns_info_sta_backup);
        }

        size_t sta_ssid_len = sizeof(config_sta.sta.ssid);
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_WIFI_STA_SSID), &config_sta.sta.ssid, &sta_ssid_len);
        sta_ssid_len--; // Remove null terminator from length
        if (sta_ssid_len == 0) sta_enable = false;
        size_t sta_password_len = sizeof(config_sta.sta.password);
        config_get_str_blob(CONF_ITEM(KEY_CONFIG_WIFI_STA_PASSWORD), &config_sta.sta.password, &sta_password_len);
        sta_password_len--; // Remove null terminator from length
        config_sta.sta.scan_method = config_get_bool1(CONF_ITEM(KEY_CONFIG_WIFI_STA_SCAN_MODE_ALL))
                ? WIFI_ALL_CHANNEL_SCAN : WIFI_FAST_SCAN;

        ESP_LOGI(TAG, "WIFI_STA_CONNECTING: %s (%s), %s scan", config_sta.sta.ssid,
                sta_password_len == 0 ? "open" : "with password",
                config_sta.sta.scan_method == WIFI_ALL_CHANNEL_SCAN ? "all channel" : "fast");
        uart_nmea("$PESP,WIFI,STA,CONNECTING,%s,%c,%c", config_sta.sta.ssid,
                sta_password_len == 0 ? 'O' : 'P',
                config_sta.sta.scan_method == WIFI_ALL_CHANNEL_SCAN ? 'A' : 'F');
    }

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

    // Configure and connect
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

    if (ap_enable) {
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &config_ap));
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_AP, WIFI_BW_HT20));

        config_color_t ap_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_WIFI_AP_COLOR));
        if (ap_led_color.rgba != 0) status_led_ap = status_led_add(ap_led_color.rgba, STATUS_LED_STATIC, 500, 2000, 0);
    }

    if (sta_enable) {
        ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &config_sta));
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(ESP_IF_WIFI_STA, WIFI_BW_HT20));

        // Keep track of connection for RSSI indicator, but suspend until connected
        xTaskCreate(wifi_sta_status_task, "wifi_sta_status", 2048, NULL, TASK_PRIORITY_WIFI_STATUS, &sta_status_task);
        vTaskSuspend(sta_status_task);

        // Reconnect when disconnected
        xTaskCreate(wifi_sta_reconnect_task, "wifi_sta_reconnect", 4096, NULL, TASK_PRIORITY_WIFI_STATUS, &sta_reconnect_task);
        vTaskSuspend(sta_reconnect_task);

        config_color_t sta_led_color = config_get_color(CONF_ITEM(KEY_CONFIG_WIFI_STA_COLOR));
        if (sta_led_color.rgba != 0) status_led_sta = status_led_add(sta_led_color.rgba, STATUS_LED_STATIC, 500, 2000, 0);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
}

wifi_sta_list_t *wifi_ap_sta_list() {
    esp_wifi_ap_get_sta_list(&ap_sta_list);
    return &ap_sta_list;
}

void wifi_ap_status(wifi_ap_status_t *status) {
    status->active = ap_active;
    if (!ap_active) return;

    memcpy(status->ssid, config_ap.ap.ssid, sizeof(config_ap.ap.ssid));
    status->authmode = config_ap.ap.authmode;

    wifi_ap_sta_list();
    status->devices = ap_sta_list.num;


    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_ap, &ip_info);
    status->ip4_addr = ip_info.ip;

    esp_netif_get_ip6_linklocal(esp_netif_ap, &status->ip6_addr);
}

void wifi_sta_status(wifi_sta_status_t *status) {
    status->active = sta_active;
    status->connected = sta_connected;
    if (!sta_connected) {
        memcpy(status->ssid, config_sta.sta.ssid, sizeof(config_sta.sta.ssid));
        return;
    }

    memcpy(status->ssid, sta_ap_info.ssid, sizeof(sta_ap_info.ssid));
    status->rssi = sta_ap_info.rssi;
    status->authmode = sta_ap_info.authmode;

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(esp_netif_sta, &ip_info);
    status->ip4_addr = ip_info.ip;

    esp_netif_get_ip6_linklocal(esp_netif_sta, &status->ip6_addr);
}

wifi_ap_record_t *wifi_scan(uint16_t *number) {
    wifi_mode_t wifi_mode;
    esp_wifi_get_mode(&wifi_mode);

    // Ensure STA is enabled
    if (wifi_mode != WIFI_MODE_APSTA && wifi_mode != WIFI_MODE_STA) {
        esp_wifi_set_mode(wifi_mode == WIFI_MODE_AP ? WIFI_MODE_APSTA : WIFI_MODE_STA);
    }

    wifi_scan_config_t wifi_scan_config = {
            .ssid = NULL,
            .bssid = NULL,
            .channel = 0,
            .show_hidden = 0
    };

    esp_wifi_scan_start(&wifi_scan_config, true);

    esp_wifi_scan_get_ap_num(number);
    if (*number <= 0) {
        return NULL;
    }

    wifi_ap_record_t *ap_records = (wifi_ap_record_t *) malloc(*number * sizeof(wifi_ap_record_t));
    esp_wifi_scan_get_ap_records(number, ap_records);

    return ap_records;
}

const char *wifi_auth_mode_name(wifi_auth_mode_t auth_mode) {
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
