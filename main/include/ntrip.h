#ifndef ESP32_XBEE_NTRIP_H
#define ESP32_XBEE_NTRIP_H

#define NTRIP_GENERIC_NAME "ESP32_XBee_Ntrip"
#define NTRIP_CLIENT_NAME NTRIP_GENERIC_NAME "_Client"
#define NTRIP_SERVER_NAME NTRIP_GENERIC_NAME "_Server"
#define NTRIP_CASTER_NAME NTRIP_GENERIC_NAME "_Caster"

#define NTRIP_PORT_DEFAULT 2101
#define NTRIP_MOUNTPOINT_DEFAULT "DEFAULT"
#define NTRIP_KEEP_ALIVE_THRESHOLD 10000

#define NEWLINE "\r\n"
#define NEWLINE_LENGTH 2

#ifndef CONFIG_EXAMPLE_IPV6
#define SOCKET_ADDR_FAMILY AF_INET
#define SOCKET_IP_PROTOCOL IPPROTO_IP
#elif
#define SOCKET_ADDR_FAMILY AF_INET6
#define SOCKET_IP_PROTOCOL IPPROTO_IP6
#endif

void ntrip_server_init();
void ntrip_client_init();
void ntrip_caster_init();

bool ntrip_response_ok(void *response);

#endif //ESP32_XBEE_NTRIP_H
