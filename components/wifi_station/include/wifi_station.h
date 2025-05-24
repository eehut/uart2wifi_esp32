#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_STATION_MAX_RECORDS 8
#define WIFI_STATION_SSID_LEN 32
#define WIFI_STATION_PASSWORD_LEN 64
#define WIFI_STATION_BSSID_LEN 6

/**
 * @brief WiFi连接状态枚举
 */
typedef enum {
    WIFI_STATION_STATUS_DISCONNECTED = 0,  ///< 未连接
    WIFI_STATION_STATUS_CONNECTING,        ///< 连接中
    WIFI_STATION_STATUS_CONNECTED          ///< 已连接
} wifi_station_status_t;

/**
 * @brief WiFi网络信息结构体
 */
typedef struct {
    char ssid[WIFI_STATION_SSID_LEN];      ///< SSID
    uint8_t bssid[WIFI_STATION_BSSID_LEN]; ///< BSSID
    int8_t rssi;                           ///< 信号强度
} wifi_network_info_t;

/**
 * @brief WiFi连接记录结构体
 */
typedef struct {
    char ssid[WIFI_STATION_SSID_LEN];        ///< SSID
    char password[WIFI_STATION_PASSWORD_LEN]; ///< 密码
    uint32_t sequence;                       ///< 连接序号
    bool valid;                              ///< 记录是否有效
} wifi_connection_record_t;

/**
 * @brief WiFi连接状态信息结构体
 */
typedef struct {
    wifi_station_status_t status;            ///< 连接状态
    char ssid[WIFI_STATION_SSID_LEN];        ///< 当前连接的SSID
    uint8_t bssid[WIFI_STATION_BSSID_LEN];   ///< 当前连接的BSSID
    int8_t rssi;                             ///< 信号质量
    uint32_t ip_addr;                        ///< IP地址
    uint32_t netmask;                        ///< 子网掩码
    uint32_t gateway;                        ///< 网关
    uint32_t dns;                            ///< DNS服务器
    uint32_t connected_time;                 ///< 连接时长(秒)
} wifi_connection_status_t;

/**
 * @brief 初始化WiFi Station组件
 * 
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_init(void);

/**
 * @brief 反初始化WiFi Station组件
 * 
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_deinit(void);

/**
 * @brief 获取WiFi连接状态
 * 
 * @param[out] status 连接状态信息
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_get_status(wifi_connection_status_t *status);

/**
 * @brief 扫描WiFi网络列表(同步)
 * 
 * @param[out] networks 网络列表缓冲区
 * @param[in,out] count 输入时为缓冲区大小，输出时为实际扫描到的网络数量
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_scan_networks(wifi_network_info_t *networks, uint16_t *count);

/**
 * @brief 断开当前WiFi连接
 * 
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_disconnect(void);

/**
 * @brief 连接到指定的WiFi网络(同步)
 * 
 * @param[in] ssid SSID名称
 * @param[in] password 密码
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_connect(const char *ssid, const char *password);

/**
 * @brief 获取WiFi连接记录列表
 * 
 * @param[out] records 连接记录数组
 * @param[out] count 记录数量
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_get_records(wifi_connection_record_t records[WIFI_STATION_MAX_RECORDS], uint8_t *count);

/**
 * @brief 删除WiFi连接记录
 * 
 * @param[in] ssid 要删除的SSID
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_delete_record(const char *ssid);

/**
 * @brief 添加WiFi连接记录
 * 
 * @param[in] ssid SSID
 * @param[in] password 密码
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_add_record(const char *ssid, const char *password);

#ifdef __cplusplus
}
#endif 