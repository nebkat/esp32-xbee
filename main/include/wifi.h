#ifndef ESP32_XBEE_WIFI_H
#define ESP32_XBEE_WIFI_H

#include <esp_wifi.h>

void wifi_init();
wifi_ap_record_t * wifi_scan(uint16_t *number);
char * wifi_auth_mode_name(wifi_auth_mode_t auth_mode);
void wait_for_ip();

#endif //ESP32_XBEE_WIFI_H
