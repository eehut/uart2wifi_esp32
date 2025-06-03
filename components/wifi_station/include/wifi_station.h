#pragma once

/**
 * @file wifi_station.h
 * @author LiuChuansen (179712066@qq.com)
 * @brief WiFi Station 组件头文件
 * @version 0.1
 * @date 2025-05-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

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
#define WIFI_STATION_SSID_LEN   64
#define WIFI_STATION_PASSWORD_LEN 64
#define WIFI_STATION_BSSID_LEN 6

/**
 * @brief WiFi连接状态枚举
 */
typedef enum {
    WIFI_STATE_DISCONNECTED = 0,  ///< 未连接
    WIFI_STATE_CONNECTING,        ///< 连接中
    WIFI_STATE_CONNECTED          ///< 已连接
} wifi_station_state_t;

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
    uint16_t id;                             ///< 记录ID
    bool valid;                              ///< 记录是否有效
    bool ever_success;                       ///< 是否曾经连接成功
    bool user_disconnected;                  ///< 是否被用户主动断开
    uint32_t sequence;                       ///< 连接序号    
    char ssid[WIFI_STATION_SSID_LEN];        ///< SSID
    char password[WIFI_STATION_PASSWORD_LEN]; ///< 密码
} wifi_connection_record_t;

/**
 * @brief WiFi连接状态信息结构体
 */
typedef struct {
    wifi_station_state_t state;            ///< 连接状态
    char ssid[WIFI_STATION_SSID_LEN];        ///< 当前连接的SSID
    uint8_t bssid[WIFI_STATION_BSSID_LEN];   ///< 当前连接的BSSID
    int8_t rssi;                             ///< 信号质量
    uint32_t ip_addr;                        ///< IP地址
    uint32_t netmask;                        ///< 子网掩码
    uint32_t gateway;                        ///< 网关
    uint32_t dns1;                           ///< DNS服务器1
    uint32_t dns2;                           ///< DNS服务器2
    uint32_t connected_time;                 ///< 连接时长(秒)
} wifi_connection_status_t;

/**
 * @brief WiFi事件类型枚举
 */
typedef enum {
    WIFI_EVENT_CONNECTED,       ///< WiFi连接成功
    WIFI_EVENT_DISCONNECTED,    ///< WiFi连接断开
    WIFI_EVENT_GOT_IP          ///< 获取到IP地址
} wifi_station_event_t;

/**
 * @brief WiFi事件回调函数类型
 * 
 * @param event WiFi事件类型
 * @param status WiFi连接状态信息
 * @param user_ctx 用户上下文
 */
typedef void (*wifi_station_event_callback_t)(wifi_station_event_t event, 
                                              const wifi_connection_status_t *status, 
                                              void *user_ctx);

/**
 * @brief 初始化WiFi Station组件
 * 
 * @param event_callback WiFi事件回调函数（可选，可为NULL）
 * @param user_ctx 用户上下文，将传递给回调函数
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_init(wifi_station_event_callback_t event_callback, void *user_ctx);

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
 * @brief 扫描WiFi网络列表(同步) (不推荐使用, 不开启WIFI后台任务可以使用)
 * 
 * @param[out] networks 网络列表缓冲区
 * @param[in,out] count 输入时为缓冲区大小，输出时为实际扫描到的网络数量
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_scan_networks(wifi_network_info_t *networks, uint16_t *count);


/**
 * @brief 异步启动WiFi扫描
 * @return
 *     - ESP_OK: 成功启动扫描
 *     - ESP_ERR_INVALID_STATE: WiFi组件未初始化
 *     - ESP_ERR_WIFI_SCAN_IN_PROGRESS: 已有扫描正在进行中
 */
esp_err_t wifi_station_start_scan_async(void);


/**
 * @brief 异步扫描WiFi网络列表
 * 
 * @param networks 网络列表缓冲区
 * @param count 输入时为缓冲区大小，输出时为实际扫描到的网络数量
 * @param timeout_ms 超时时间，单位为毫秒
 * @return 
 *     - ESP_OK: 成功启动扫描
 *     - ESP_ERR_INVALID_STATE: WiFi组件未初始化
 *     - ESP_ERR_WIFI_SCAN_IN_PROGRESS: 已有扫描正在进行中
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_NO_MEM: 内存不足
 *     - ESP_ERR_TIMEOUT: 扫描超时
 */
esp_err_t wifi_station_scan_networks_async(wifi_network_info_t *networks, uint16_t *count, int timeout_ms);

/**
 * @brief 获取异步扫描结果
 * @param[out] networks 用于存储扫描到的网络信息的数组
 * @param[in,out] count 输入时表示networks数组的大小,输出时表示实际扫描到的网络数量
 * @return
 *     - ESP_OK: 成功获取扫描结果
 *     - ESP_ERR_INVALID_STATE: 未执行扫描或扫描未完成
 *     - ESP_ERR_INVALID_ARG: 参数无效
 *     - ESP_ERR_NO_MEM: 内存不足
 */
esp_err_t wifi_station_get_scan_result(wifi_network_info_t *networks, uint16_t *count); 

/**
 * @brief 检查异步扫描是否完成
 * @return
 *     - true: 扫描已完成，可以获取结果
 *     - false: 扫描未完成或未开始扫描
 */
bool wifi_station_is_scan_done(void);

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
 * @param[out] count 记录数量, 输入时为缓冲区大小, 输出时为实际记录数量
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_get_records(wifi_connection_record_t *records, uint8_t *count);

/**
 * @brief 删除WiFi连接记录
 * 
 * @param[in] ssid 要删除的记录SSID
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

/**
 * @brief 启用/禁用后台自动扫描连接功能
 * 
 * @param[in] enable true启用，false禁用
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t wifi_station_set_auto_connect(bool enable);

/**
 * @brief 尝试自动连接一次
 * 
 * @return 
 */
void wifi_station_try_auto_connect_once(void);

/**
 * @brief 重置指定网络的状态
 * 
 * 将指定网络的ever_success标记重置为true，清除user_disconnected标记，
 * 并重置重试状态。适用于网络密码修复后重新尝试连接的场景。
 * 
 * @param[in] ssid 要重置状态的网络SSID
 * @return ESP_OK 成功
 *         ESP_ERR_NOT_FOUND 未找到指定的网络记录
 *         其他错误码 失败
 */
esp_err_t wifi_station_reset_network_status(const char *ssid);

#ifdef __cplusplus
}
#endif 