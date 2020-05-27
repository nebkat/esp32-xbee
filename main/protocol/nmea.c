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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include "protocol/nmea.h"

uint8_t nmea_calculate_checksum(char *sentence) {
    uint8_t checksum = 0;
    unsigned int length = strlen(sentence);
    for (unsigned int i = 1; i < length; i++) {
        checksum ^= (uint8_t) sentence[i];
    }

    return checksum;
}

int nmea_asprintf(char **strp, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    int l = nmea_vasprintf(strp, fmt, args);

    va_end(args);

    return l;
}

int nmea_vasprintf(char **strp, const char *fmt, va_list args) {
    char *sentence;
    vasprintf(&sentence, fmt, args);
    uint8_t checksum = nmea_calculate_checksum(sentence);
    int l = asprintf(strp, "%s*%02X\r\n", sentence, checksum);
    free(sentence);

    return l;
}
