#ifndef ESP32_XBEE_UTIL_H
#define ESP32_XBEE_UTIL_H

#define DEBUG( level, tag ) ESP_LOG_LEVEL(level, tag, "This is line %d of file %s (function %s)", __LINE__, __FILE__, __func__)

#define CONNECT_SOCKET_ERROR_RESOLVE -2
#define CONNECT_SOCKET_ERROR_CONNECT -1

void dump_hex(const void* data, size_t size);
void destroy_socket(int *socket);
int connect_socket(char *host, int port, int socktype);
char *http_auth_basic(const char *username, const char *password);

#endif //ESP32_XBEE_UTIL_H
