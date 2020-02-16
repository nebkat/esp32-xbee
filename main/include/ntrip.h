#ifndef ESP32_XBEE_NTRIP_H
#define ESP32_XBEE_NTRIP_H

#define NTRIP_GENERIC_NAME "ESP32-XBee"
#define NTRIP_CLIENT_NAME NTRIP_GENERIC_NAME "_Client"
#define NTRIP_SERVER_NAME NTRIP_GENERIC_NAME "_Server"
#define NTRIP_CASTER_NAME NTRIP_GENERIC_NAME "_Caster"

#define NTRIP_PORT_DEFAULT 2101
#define NTRIP_MOUNTPOINT_DEFAULT "DEFAULT"
#define NTRIP_KEEP_ALIVE_THRESHOLD 10000

#define NEWLINE "\r\n"
#define NEWLINE_LENGTH 2

void ntrip_server_init();
void ntrip_client_init();
void ntrip_caster_init();

bool ntrip_response_ok(void *response);
bool ntrip_response_sourcetable_ok(void *response);

#endif //ESP32_XBEE_NTRIP_H
