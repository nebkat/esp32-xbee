#ifndef ESP32_XBEE_CONFIG_H
#define ESP32_XBEE_CONFIG_H

#include <stddef.h>

typedef enum {
    CONFIG_ITEM_TYPE_BOOL = 0,
    CONFIG_ITEM_TYPE_INT8,
    CONFIG_ITEM_TYPE_INT16,
    CONFIG_ITEM_TYPE_INT32,
    CONFIG_ITEM_TYPE_INT64,
    CONFIG_ITEM_TYPE_UINT8,
    CONFIG_ITEM_TYPE_UINT16,
    CONFIG_ITEM_TYPE_UINT32,
    CONFIG_ITEM_TYPE_UINT64,
    CONFIG_ITEM_TYPE_STRING,
    CONFIG_ITEM_TYPE_BLOB,
    CONFIG_ITEM_TYPE_COLOR,
    CONFIG_ITEM_TYPE_IP,
    CONFIG_ITEM_TYPE_MAX
} config_item_type_t;

typedef union {
    struct values {
        uint8_t alpha;
        uint8_t blue;
        uint8_t green;
        uint8_t red;
    } values;
    uint32_t rgba;
} config_color_t;

typedef union {
    bool bool1;
    int8_t int8;
    int16_t int16;
    int32_t int32;
    int64_t int64;
    uint8_t uint8;
    uint16_t uint16;
    uint32_t uint32;
    uint64_t uint64;
    config_color_t color;
    char *str;
    struct blob {
        uint8_t *data;
        size_t length;
    } blob;

} config_item_value_t;

typedef struct config_item {
    char *key;
    config_item_type_t type;
    bool secret;
    config_item_value_t def;
} config_item_t;

#define CONFIG_VALUE_UNCHANGED "\x1a\x1a\x1a\x1a\x1a\x1a\x1a\x1a"

// Admin
#define KEY_CONFIG_ADMIN_AUTH "adm_auth"
#define KEY_CONFIG_ADMIN_USERNAME "adm_user"
#define KEY_CONFIG_ADMIN_PASSWORD "adm_pass"

// Bluetooth
#define KEY_CONFIG_BLUETOOTH_ACTIVE "bt_active"
#define KEY_CONFIG_BLUETOOTH_DEVICE_NAME "bt_dev_name"
#define KEY_CONFIG_BLUETOOTH_DEVICE_DISCOVERABLE "bt_dev_vis"
#define KEY_CONFIG_BLUETOOTH_PIN_CODE "bt_pin_code"

// NTRIP
#define KEY_CONFIG_NTRIP_SERVER_ACTIVE "ntr_srv_active"
#define KEY_CONFIG_NTRIP_SERVER_COLOR "ntr_srv_color"
#define KEY_CONFIG_NTRIP_SERVER_HOST "ntr_srv_host"
#define KEY_CONFIG_NTRIP_SERVER_PORT "ntr_srv_port"
#define KEY_CONFIG_NTRIP_SERVER_MOUNTPOINT "ntr_srv_mp"
#define KEY_CONFIG_NTRIP_SERVER_USERNAME "ntr_srv_user"
#define KEY_CONFIG_NTRIP_SERVER_PASSWORD "ntr_srv_pass"

#define KEY_CONFIG_NTRIP_CLIENT_ACTIVE "ntr_cli_active"
#define KEY_CONFIG_NTRIP_CLIENT_COLOR "ntr_cli_color"
#define KEY_CONFIG_NTRIP_CLIENT_HOST "ntr_cli_host"
#define KEY_CONFIG_NTRIP_CLIENT_PORT "ntr_cli_port"
#define KEY_CONFIG_NTRIP_CLIENT_MOUNTPOINT "ntr_cli_mp"
#define KEY_CONFIG_NTRIP_CLIENT_USERNAME "ntr_cli_user"
#define KEY_CONFIG_NTRIP_CLIENT_PASSWORD "ntr_cli_pass"

#define KEY_CONFIG_NTRIP_CASTER_ACTIVE "ntr_cst_active"
#define KEY_CONFIG_NTRIP_CASTER_COLOR "ntr_cst_color"
#define KEY_CONFIG_NTRIP_CASTER_PORT "ntr_cst_port"
#define KEY_CONFIG_NTRIP_CASTER_MOUNTPOINT "ntr_cst_mp"
#define KEY_CONFIG_NTRIP_CASTER_USERNAME "ntr_cst_user"
#define KEY_CONFIG_NTRIP_CASTER_PASSWORD "ntr_cst_pass"

// Socket
#define KEY_CONFIG_SOCKET_SERVER_ACTIVE "sck_srv_active"
#define KEY_CONFIG_SOCKET_SERVER_COLOR "sck_srv_color"
#define KEY_CONFIG_SOCKET_SERVER_TCP_PORT "sck_srv_t_port"
#define KEY_CONFIG_SOCKET_SERVER_UDP_PORT "sck_srv_u_port"

#define KEY_CONFIG_SOCKET_CLIENT_ACTIVE "sck_cli_active"
#define KEY_CONFIG_SOCKET_CLIENT_COLOR "sck_cli_color"
#define KEY_CONFIG_SOCKET_CLIENT_HOST "sck_cli_host"
#define KEY_CONFIG_SOCKET_CLIENT_PORT "sck_cli_port"
#define KEY_CONFIG_SOCKET_CLIENT_TYPE_TCP_UDP "sck_cli_type"
#define KEY_CONFIG_SOCKET_CLIENT_CONNECT_MESSAGE "sck_cli_msg"

// UART
#define KEY_CONFIG_UART_NUM "uart_num"
#define KEY_CONFIG_UART_TX_PIN "uart_tx_pin"
#define KEY_CONFIG_UART_RX_PIN "uart_rx_pin"
#define KEY_CONFIG_UART_RTS_PIN "uart_rts_pin"
#define KEY_CONFIG_UART_CTS_PIN "uart_cts_pin"
#define KEY_CONFIG_UART_BAUD_RATE "uart_baud_rate"
#define KEY_CONFIG_UART_DATA_BITS "uart_data_bits"
#define KEY_CONFIG_UART_STOP_BITS "uart_stop_bits"
#define KEY_CONFIG_UART_PARITY "uart_parity"
#define KEY_CONFIG_UART_FLOW_CTRL_RTS "uart_fc_rts"
#define KEY_CONFIG_UART_FLOW_CTRL_CTS "uart_fc_cts"
#define KEY_CONFIG_UART_LOG_FORWARD "uart_log_fwd"

// WiFi
#define KEY_CONFIG_WIFI_AP_ACTIVE "w_ap_active"
#define KEY_CONFIG_WIFI_AP_COLOR "w_ap_color"
#define KEY_CONFIG_WIFI_AP_SSID "w_ap_ssid"
#define KEY_CONFIG_WIFI_AP_SSID_HIDDEN "w_ap_ssid_hid"
#define KEY_CONFIG_WIFI_AP_AUTH_MODE "w_ap_auth_mode"
#define KEY_CONFIG_WIFI_AP_PASSWORD "w_ap_pass"
#define KEY_CONFIG_WIFI_AP_GATEWAY "w_ap_gw"
#define KEY_CONFIG_WIFI_AP_SUBNET "w_ap_subnet"

#define KEY_CONFIG_WIFI_STA_ACTIVE "w_sta_active"
#define KEY_CONFIG_WIFI_STA_COLOR "w_sta_color"
#define KEY_CONFIG_WIFI_STA_SSID "w_sta_ssid"
#define KEY_CONFIG_WIFI_STA_PASSWORD "w_sta_pass"
#define KEY_CONFIG_WIFI_STA_SCAN_MODE_ALL "w_sta_scan_mode"
#define KEY_CONFIG_WIFI_STA_AP_FORWARD "w_sta_ap_fwd"
#define KEY_CONFIG_WIFI_STA_STATIC "w_sta_static"
#define KEY_CONFIG_WIFI_STA_IP "w_sta_ip"
#define KEY_CONFIG_WIFI_STA_GATEWAY "w_sta_gw"
#define KEY_CONFIG_WIFI_STA_SUBNET "w_sta_subnet"
#define KEY_CONFIG_WIFI_STA_DNS_A "w_sta_dns_a"
#define KEY_CONFIG_WIFI_STA_DNS_B "w_sta_dns_b"

esp_err_t config_init();
esp_err_t config_reset();

const config_item_t *config_items_get(int *count);
const config_item_t * config_get_item(const char *key);

#define CONF_ITEM( key ) config_get_item(key)

bool config_get_bool1(const config_item_t *item);
int8_t config_get_i8(const config_item_t *item);
int16_t config_get_i16(const config_item_t *item);
int32_t config_get_i32(const config_item_t *item);
int64_t config_get_i64(const config_item_t *item);
uint8_t config_get_u8(const config_item_t *item);
uint16_t config_get_u16(const config_item_t *item);
uint32_t config_get_u32(const config_item_t *item);
uint64_t config_get_u64(const config_item_t *item);
config_color_t config_get_color(const config_item_t *item);

esp_err_t config_set(const config_item_t *item, void *value);
esp_err_t config_set_bool1(const char *key, bool value);
esp_err_t config_set_i8(const char *key, int8_t value);
esp_err_t config_set_i16(const char *key, int16_t value);
esp_err_t config_set_i32(const char *key, int32_t value);
esp_err_t config_set_i64(const char *key, int64_t value);
esp_err_t config_set_u8(const char *key, uint8_t value);
esp_err_t config_set_u16(const char *key, uint16_t value);
esp_err_t config_set_u32(const char *key, uint32_t value);
esp_err_t config_set_u64(const char *key, uint64_t value);
esp_err_t config_set_color(const char *key, config_color_t value);
esp_err_t config_set_str(const char *key, char *value);
esp_err_t config_set_blob(const char *key, char *value, size_t length);

esp_err_t config_get_str_blob_alloc(const config_item_t *item, void **out_value);
esp_err_t config_get_str_blob(const config_item_t *item, void *out_value, size_t *length);
esp_err_t config_get_primitive(const config_item_t *item, void *out_value);

esp_err_t config_commit();
void config_restart();

#endif //ESP32_XBEE_CONFIG_H
