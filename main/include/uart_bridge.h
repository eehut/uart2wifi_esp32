/**
 * @file uart_bridge.h
 * @author LiuChuansen (179712066@qq.com)
 * @brief TCP转串口桥接模块
 * @version 0.2
 * @date 2025-05-10
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#ifndef TCP_UART_BRIDGE_H
#define TCP_UART_BRIDGE_H

#include "esp_err.h"
#include "esp_types.h"

#ifdef __cplusplus
extern "C" {
#endif

// 默认配置
#define UART_BRIDGE_DEFAULT_PORT       5678
#define UART_BRIDGE_DEFAULT_BAUDRATE   115200
#define UART_BRIDGE_DEFAULT_BUF_SIZE   1024
#define UART_BRIDGE_MAX_CLIENTS        5


// TCP转串口桥接状态结构体
typedef struct {
    bool tcp_running;
    bool uart_opened;
    uint32_t baudrate;
    uint16_t tcp_port;
    uint16_t tcp_client_num;
}uart_bridge_status_t;

// 统计信息结构体
typedef struct {
    uint32_t uart_tx_bytes;     // 串口发送字节数
    uint32_t uart_rx_bytes;     // 串口接收字节数
    uint32_t uart_tx_errors;    // 串口发送错误次数
    uint32_t uart_rx_errors;    // 串口接收错误次数
    uint32_t tcp_tx_bytes;      // TCP发送字节数
    uint32_t tcp_rx_bytes;      // TCP接收字节数    
    uint32_t tcp_connect_count; // TCP连接次数
    uint32_t tcp_disconnect_count; // TCP断开次数
    uint32_t buffer_overflow;   // 缓冲区溢出次数
} uart_bridge_stats_t;


/**
 * @brief 初始化TCP转串口桥接模块
 * 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_init(uint8_t uart_id);

/**
 * @brief 反初始化TCP转串口桥接模块
 * 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_deinit(void);

/**
 * @brief 获取桥接状态
 * 
 * @param status 桥接状态结构体指针
 * @return esp_err_t 
 */
esp_err_t uart_bridge_get_status(uart_bridge_status_t *status);

/**
 * @brief 获取统计信息
 * 
 * @param stats 统计信息结构体指针
 * @return esp_err_t 
 */
esp_err_t uart_bridge_get_stats(uart_bridge_stats_t *stats);

/**
 * @brief 重置统计信息
 * 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_reset_stats(void);

/**
 * @brief 从NVS加载配置
 * 
 * @param config 配置结构体指针
 * @return esp_err_t 
 */
esp_err_t uart_bridge_set_baudrate(uint32_t baudrate);


/**
 * @brief 启动TCP服务器
 * 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_start_tcp_server(void);

/**
 * @brief 停止TCP服务器
 * 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_stop_tcp_server(void);

// /**
//  * @brief WiFi Station事件处理函数
//  * 
//  * @param event WiFi事件类型
//  * @param status WiFi连接状态信息
//  * @param user_ctx 用户上下文
//  */
// void uart_bridge_wifi_event_handler(wifi_station_event_t event, 
//                                    const wifi_connection_status_t *status, 
//                                    void *user_ctx);

#ifdef __cplusplus
}
#endif

#endif // TCP_UART_BRIDGE_H 