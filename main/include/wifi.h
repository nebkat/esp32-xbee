#ifndef ESP32_XBEE_WIFI_H
#define ESP32_XBEE_WIFI_H

#include <esp_wifi.h>

typedef struct wifi_ap_status {
    bool active;

    char ssid[33];
    wifi_auth_mode_t authmode;
    uint8_t devices;

    ip4_addr_t ip4_addr;
    ip6_addr_t ip6_addr;
} wifi_ap_status_t;

typedef struct wifi_sta_status {
    bool connected;

    char ssid[33];
    wifi_auth_mode_t authmode;
    int8_t rssi;

    ip4_addr_t ip4_addr;
    ip6_addr_t ip6_addr;
} wifi_sta_status_t;

void wifi_init();

wifi_ap_record_t * wifi_scan(uint16_t *number);

void wifi_ap_status(wifi_ap_status_t *status);
void wifi_sta_status(wifi_sta_status_t *status);

char * wifi_auth_mode_name(wifi_auth_mode_t auth_mode);
void wait_for_ip();

#endif //ESP32_XBEE_WIFI_H
