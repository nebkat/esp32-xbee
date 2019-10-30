#ifndef ESP32_XBEE_UART_H
#define ESP32_XBEE_UART_H

#include <esp_event.h>

ESP_EVENT_DECLARE_BASE(UART_EVENTS);

#define UART_BUFFER_SIZE 8192

typedef struct uart_data {
    int len;
    uint8_t buffer[UART_BUFFER_SIZE];
} uart_data_t;

void uart_register_handler(esp_event_handler_t event_handler);
void uart_init();
void uart_task(void *ctx);
void uart_inject(void *data, size_t len);
int uart_log(char *buffer, size_t len);
int uart_nmea(const char *fmt, ...);
int uart_write(char *buffer, size_t len);

#endif //ESP32_XBEE_UART_H
