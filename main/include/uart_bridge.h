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
#define UART_BRIDGE_BUFFER_SIZE        1024
#define UART_BRIDGE_MAX_CLIENTS        5
#define UART_BRIDGE_TASK_STACK_SIZE    4096
#define UART_BRIDGE_TASK_PRIORITY      5


// TCP转串口桥接状态结构体
typedef struct {
    bool tcp_standby; // TCP服务是否就绪
    bool uart_opened; // 串口是否打开
    bool forwarding; // 是否转发数据
    uint32_t uart_baudrate; // 串口波特率
    uint16_t tcp_port;     // TCP端口
    uint16_t tcp_client_num; // TCP客户端数量
}uart_bridge_status_t;

// 统计信息结构体
typedef struct {
    uint32_t uart_tx_bytes;     // 串口发送字节数
    uint32_t uart_rx_bytes;     // 串口接收字节数
    uint32_t uart_tx_drop_bytes;     // 串口发送丢弃字节数(缓冲区不可用)
    //uint32_t uart_rx_drop_bytes;     // 串口接收丢弃字节数(缓冲区不可用)
    uint32_t uart_tx_error_bytes;    // 串口发送错误字节数
    //uint32_t uart_rx_error_bytes;    // 串口接收错误字节数
    uint32_t tcp_tx_bytes;      // TCP发送字节数
    uint32_t tcp_tx_error_bytes; // TCP发送错误字节数
    uint32_t tcp_rx_bytes;      // TCP接收字节数  
    //uint32_t tcp_rx_error_bytes; // TCP接收错误字节数
    uint32_t tcp_connect_count; // TCP连接次数
    uint32_t tcp_disconnect_count; // TCP断开次数
    //uint32_t buffer_overflow;   // 缓冲区溢出次数
} uart_bridge_stats_t;


/**
 * @brief 初始化TCP转串口桥接模块
 * 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_init(uint8_t port_id);

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

/**
 * @brief 设置TCP数据显示
 * 
 * @param tx_verbose 
 * @param rx_verbose 
 * @return true 
 * @return false 
 */
bool uart_bridge_set_tcp_verbose(bool tx_verbose, bool rx_verbose);

/**
 * @brief 设置串口数据显示
 * 
 * @param tx_verbose 
 * @param rx_verbose 
 * @return true 
 * @return false 
 */
bool uart_bridge_set_uart_verbose(bool tx_verbose, bool rx_verbose);


#ifdef __cplusplus
}
#endif

#endif // TCP_UART_BRIDGE_H 