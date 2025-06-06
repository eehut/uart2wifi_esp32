#ifndef TCP_SERVER_H
#define TCP_SERVER_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "lwip/ip_addr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TCP客户端对象结构体
 */
typedef struct {
    int socket_fd;           // 客户端socket文件描述符
    ip_addr_t ip_addr;       // 客户端IP地址
    uint16_t port;           // 客户端端口号
    void *user_data;         // 用户自定义数据
} tcp_client_t;

/**
 * @brief TCP服务器句柄
 */
typedef struct tcp_server_s *tcp_server_handle_t;

/**
 * @brief 数据接收回调函数类型
 * 
 * @param client 客户端对象指针
 * @param data 接收到的数据
 * @param len 数据长度
 * @param user_ctx 用户上下文
 */
typedef void (*tcp_recv_callback_t)(tcp_client_t *client, const uint8_t *data, size_t len, void *user_ctx);

/**
 * @brief 客户端连接回调函数类型
 * 
 * @param client 新连接的客户端对象指针
 * @param user_ctx 用户上下文
 */
typedef void (*tcp_connect_callback_t)(tcp_client_t *client, void *user_ctx);

/**
 * @brief 客户端断开连接回调函数类型
 * 
 * @param client 断开连接的客户端对象指针
 * @param user_ctx 用户上下文
 */
typedef void (*tcp_disconnect_callback_t)(tcp_client_t *client, void *user_ctx);

/**
 * @brief TCP服务器配置结构体
 */
typedef struct {
    uint16_t port;                              // 监听端口
    uint32_t max_clients;                       // 最大客户端连接数
    tcp_recv_callback_t recv_callback;          // 数据接收回调函数
    tcp_connect_callback_t connect_callback;    // 客户端连接回调函数（可选）
    tcp_disconnect_callback_t disconnect_callback; // 客户端断开连接回调函数（可选）
    void *user_ctx;                             // 用户上下文
    uint32_t stack_size;                        // 任务栈大小，默认4096
    uint32_t task_priority;                     // 任务优先级，默认5
    bool verbose;                               // 是否打印收发数据
} tcp_server_config_t;

/**
 * @brief 创建TCP服务器
 * 
 * @param config TCP服务器配置
 * @param err 错误码
 * @return tcp_server_handle_t TCP服务器句柄
 */
tcp_server_handle_t tcp_server_create(const tcp_server_config_t *config, esp_err_t *err);

/**
 * @brief 启动TCP服务器
 * 
 * @param server_handle TCP服务器句柄
 * @return esp_err_t 错误码
 */
esp_err_t tcp_server_start(tcp_server_handle_t server_handle);

/**
 * @brief 停止TCP服务器
 * 
 * @param server_handle TCP服务器句柄
 * @return esp_err_t 错误码
 */
esp_err_t tcp_server_stop(tcp_server_handle_t server_handle);

/**
 * @brief 销毁TCP服务器
 * 
 * @param server_handle TCP服务器句柄
 * @return esp_err_t 错误码
 */
esp_err_t tcp_server_destroy(tcp_server_handle_t server_handle);

/**
 * @brief 向指定客户端发送数据
 * 
 * @param server_handle TCP服务器句柄
 * @param client 目标客户端
 * @param data 要发送的数据
 * @param len 数据长度
 * @return esp_err_t 错误码
 */
esp_err_t tcp_server_send_to_client(tcp_server_handle_t server_handle, tcp_client_t *client, 
                                   const uint8_t *data, size_t len);

/**
 * @brief 向所有客户端广播数据
 * 
 * @param server_handle TCP服务器句柄
 * @param data 要发送的数据
 * @param len 数据长度
 * @return esp_err_t 错误码
 */
esp_err_t tcp_server_broadcast(tcp_server_handle_t server_handle, const uint8_t *data, size_t len);

/**
 * @brief 获取当前连接的客户端数量
 * 
 * @param server_handle TCP服务器句柄
 * @return int 客户端数量，-1表示错误
 */
int tcp_server_get_client_count(tcp_server_handle_t server_handle);

/**
 * @brief 断开指定客户端连接
 * 
 * @param server_handle TCP服务器句柄
 * @param client 要断开的客户端
 * @return esp_err_t 错误码
 */
esp_err_t tcp_server_disconnect_client(tcp_server_handle_t server_handle, tcp_client_t *client);

/**
 * @brief 设置TCP服务器日志级别
 * 
 * @param server_handle TCP服务器句柄
 * @param tx_verbose 是否打印发送数据
 * @param rx_verbose 是否打印接收数据
 */
void tcp_server_set_verbose(tcp_server_handle_t server_handle, bool tx_verbose, bool rx_verbose);   

#ifdef __cplusplus
}
#endif

#endif // TCP_SERVER_H 