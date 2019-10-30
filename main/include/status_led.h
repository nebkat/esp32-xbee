#ifndef ESP32_XBEE_STATUS_LED_H
#define ESP32_XBEE_STATUS_LED_H

#include <sys/queue.h>

typedef enum {
    STATUS_LED_STATIC = 0,
    STATUS_LED_FADE,
    STATUS_LED_BLINK
} status_led_flashing_mode_t;

typedef struct status_led_color_t *status_led_handle_t;
struct status_led_color_t {
    uint8_t red;
    uint8_t green;
    uint8_t blue;

    status_led_flashing_mode_t flashing_mode;
    uint32_t interval;
    uint32_t duration;
    uint8_t expire;

    bool remove;
    bool active;

    SLIST_ENTRY(status_led_color_t) next;
};

status_led_handle_t status_led_add(uint32_t rgba, status_led_flashing_mode_t flashing_mode, uint32_t interval, uint32_t duration, uint8_t expire);
void status_led_remove(status_led_handle_t color);
void status_led_init();

void rssi_led_set(uint8_t value);
void rssi_led_fade(uint8_t value, int max_fade_time_ms);
void assoc_led_set(uint8_t value);
void assoc_led_fade(uint8_t value, int max_fade_time_ms);
void sleep_led_set(uint8_t value);
void sleep_led_fade(uint8_t value, int max_fade_time_ms);

#endif //ESP32_XBEE_STATUS_LED_H
