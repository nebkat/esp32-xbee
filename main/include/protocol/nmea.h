#ifndef ESP32_XBEE_NMEA_H
#define ESP32_XBEE_NMEA_H

int nmea_asprintf(char **strp, const char *fmt, ...);
int nmea_vasprintf(char **strp, const char *fmt, va_list args);

#endif //ESP32_XBEE_NMEA_H
