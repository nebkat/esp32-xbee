#ifndef ESP32_XBEE_SOCKET_CLIENT_H
#define ESP32_XBEE_SOCKET_CLIENT_H

#include <stdint.h>
#include <esp_event_base.h>

void socket_client_uart_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data);
void socket_client_task(void *);

#endif //ESP32_XBEE_SOCKET_CLIENT_H
