/**
 * @file uart_bridge.c
 * @author LiuChuansen (179712066@qq.com)
 * @brief TCP转串口桥接模块实现
 * @version 0.2
 * @date 2025-05-10
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "uart_bridge.h"
#include "tcp_server.h"
#include "bus_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/uart.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>

// 添加MIN宏定义
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "uart_bridge";

// NVS存储键名
#define NVS_NAMESPACE           "uart_bridge"
#define NVS_KEY_TCP_PORT        "tcp_port"
#define NVS_KEY_UART_BAUDRATE   "baudrate"

// 任务优先级和堆栈大小
#define UART_TASK_PRIORITY          5
#define UART_TASK_STACK_SIZE        4096

typedef struct {
    uint16_t tcp_port;
    uint32_t baudrate;
}uart_bridge_config_t;

// 模块状态结构体
typedef struct {
    uart_bridge_config_t config;
    uart_bridge_stats_t stats;
    bool initialized;
    bool running; // 任务是否在运行,由运行任务自已管理 
    uint8_t uart_port;
    tcp_server_handle_t tcp_server;
    SemaphoreHandle_t stats_mutex;
    TaskHandle_t uart_task;
    QueueHandle_t uart_queue;
    uint8_t *uart_buffer;
} uart_bridge_t;

static uart_bridge_t g_bridge = {0};

// 函数声明
static void uart_task(void *pvParameters);
static void on_tcp_data_received(tcp_client_t *client, const uint8_t *data, size_t len, void *user_ctx);
static void on_tcp_client_connected(tcp_client_t *client, void *user_ctx);
static void on_tcp_client_disconnected(tcp_client_t *client, void *user_ctx);
static esp_err_t uart_send_data(const uint8_t *data, size_t len);

static esp_err_t uart_bridge_load_config(uart_bridge_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s, using default config", esp_err_to_name(err));
        // 使用默认配置
        config->tcp_port = UART_BRIDGE_DEFAULT_PORT;
        config->baudrate = UART_BRIDGE_DEFAULT_BAUDRATE;
        return ESP_OK;
    }

    size_t required_size = sizeof(uint16_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_TCP_PORT, &config->tcp_port, &required_size);
    if (err != ESP_OK) {
        config->tcp_port = UART_BRIDGE_DEFAULT_PORT;
    }

    required_size = sizeof(uint32_t);
    err = nvs_get_blob(nvs_handle, NVS_KEY_UART_BAUDRATE, &config->baudrate, &required_size);
    if (err != ESP_OK) {
        config->baudrate = UART_BRIDGE_DEFAULT_BAUDRATE;
    }

    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Config loaded: TCP port=%d, baudrate=%lu", config->tcp_port, config->baudrate);
    return ESP_OK;
}

static esp_err_t uart_bridge_save_config(const uart_bridge_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY_TCP_PORT, &config->tcp_port, sizeof(config->tcp_port));
    if (err != ESP_OK) goto cleanup;

    err = nvs_set_blob(nvs_handle, NVS_KEY_UART_BAUDRATE, &config->baudrate, sizeof(config->baudrate));
    if (err != ESP_OK) goto cleanup;

    err = nvs_commit(nvs_handle);

cleanup:
    nvs_close(nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config saved successfully");
    } else {
        ESP_LOGE(TAG, "Config save failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t uart_bridge_init(uint8_t uart_id)
{
    if (g_bridge.initialized) {
        ESP_LOGW(TAG, "Module already initialized");
        return ESP_OK;
    }

    const uart_hw_config_t *hw_config = uart_hw_config_get(uart_id);
    if (!hw_config) {
        ESP_LOGE(TAG, "UART(%d) hardware config not found", uart_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "UART(%d) hardware config, port: %d, txd: %d, rxd: %d", 
             uart_id, hw_config->uart_port, hw_config->txd_pin, hw_config->rxd_pin);

    memset(&g_bridge, 0, sizeof(g_bridge));

    // 加载配置
    esp_err_t ret = uart_bridge_load_config(&g_bridge.config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Config load failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 创建互斥锁
    g_bridge.stats_mutex = xSemaphoreCreateMutex();
    if (!g_bridge.stats_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 分配缓冲区
    g_bridge.uart_buffer = malloc(UART_BRIDGE_DEFAULT_BUF_SIZE);
    if (!g_bridge.uart_buffer) {
        ESP_LOGE(TAG, "Failed to allocate transfer buffer");
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ESP_ERR_NO_MEM;
    }

    // 配置UART
    const uart_config_t uart_config = {
        .baud_rate = g_bridge.config.baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ret = uart_driver_install(hw_config->uart_port, UART_BRIDGE_DEFAULT_BUF_SIZE * 2, 
                                       UART_BRIDGE_DEFAULT_BUF_SIZE * 2, 20, &g_bridge.uart_queue, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        free(g_bridge.uart_buffer);
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ret;
    }

    ret = uart_param_config(hw_config->uart_port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        uart_driver_delete(hw_config->uart_port);
        free(g_bridge.uart_buffer);
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ret;
    }

    ret = uart_set_pin(hw_config->uart_port, hw_config->txd_pin, hw_config->rxd_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART pins: %s", esp_err_to_name(ret));
        uart_driver_delete(hw_config->uart_port);
        free(g_bridge.uart_buffer);
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ret;
    }

    // 创建UART任务
    BaseType_t task_ret = xTaskCreate(uart_task, "uart_task", UART_TASK_STACK_SIZE, 
                                     NULL, UART_TASK_PRIORITY, &g_bridge.uart_task);
    if (task_ret != pdTRUE) {
        ESP_LOGE(TAG, "Failed to create UART task");
        uart_driver_delete(hw_config->uart_port);
        free(g_bridge.uart_buffer);
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ESP_FAIL;
    }

    g_bridge.initialized = true;
    g_bridge.uart_port = hw_config->uart_port;
    ESP_LOGI(TAG, "UART(%d) bridge initialized success, baudrate: %d", uart_id, g_bridge.config.baudrate);
    
    return ESP_OK;
}

esp_err_t uart_bridge_deinit(void)
{
    if (!g_bridge.initialized) {
        return ESP_OK;
    }

    // 停止TCP服务器
    uart_bridge_stop_tcp_server();

    // 停止任务
    g_bridge.running = false;

    // 删除UART任务
    if (g_bridge.uart_task) {
        vTaskDelete(g_bridge.uart_task);
        g_bridge.uart_task = NULL;
    }

    // 删除UART驱动
    uart_driver_delete(g_bridge.uart_port);

    // 释放资源
    if (g_bridge.uart_buffer) {
        free(g_bridge.uart_buffer);
        g_bridge.uart_buffer = NULL;
    }

    if (g_bridge.stats_mutex) {
        vSemaphoreDelete(g_bridge.stats_mutex);
        g_bridge.stats_mutex = NULL;
    }

    g_bridge.initialized = false;
    ESP_LOGI(TAG, "UART bridge module deinitialized");
    return ESP_OK;
}

esp_err_t uart_bridge_get_status(uart_bridge_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_bridge.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    
    status->tcp_running = (g_bridge.tcp_server != NULL);
    status->uart_opened = g_bridge.initialized;
    status->baudrate = g_bridge.config.baudrate;
    status->tcp_port = g_bridge.config.tcp_port;
    status->tcp_client_num = (g_bridge.tcp_server != NULL) ? 
                            tcp_server_get_client_count(g_bridge.tcp_server) : 0;
    
    xSemaphoreGive(g_bridge.stats_mutex);
    return ESP_OK;
}

esp_err_t uart_bridge_get_stats(uart_bridge_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!g_bridge.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    memcpy(stats, &g_bridge.stats, sizeof(uart_bridge_stats_t));
    xSemaphoreGive(g_bridge.stats_mutex);

    return ESP_OK;
}

esp_err_t uart_bridge_reset_stats(void)
{
    if (!g_bridge.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    memset(&g_bridge.stats, 0, sizeof(uart_bridge_stats_t));
    xSemaphoreGive(g_bridge.stats_mutex);

    ESP_LOGI(TAG, "Statistics reset");
    return ESP_OK;
}

esp_err_t uart_bridge_set_baudrate(uint32_t baudrate)
{
    if (!g_bridge.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = uart_set_baudrate(g_bridge.uart_port, baudrate);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Baudrate set to %lu success", baudrate);
        if (g_bridge.config.baudrate != baudrate) {
            g_bridge.config.baudrate = baudrate;
            // 保存配置到NVS
            uart_bridge_save_config(&g_bridge.config);
        }        
    } else {
        ESP_LOGE(TAG, "Failed to set baudrate: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t uart_bridge_start_tcp_server(void)
{
    if (!g_bridge.initialized || !g_bridge.running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_bridge.tcp_server) {
        ESP_LOGW(TAG, "TCP server already running");
        return ESP_OK;
    }

    // 配置TCP服务器
    tcp_server_config_t tcp_config = {
        .port = g_bridge.config.tcp_port,
        .max_clients = UART_BRIDGE_MAX_CLIENTS,
        .recv_callback = on_tcp_data_received,
        .connect_callback = on_tcp_client_connected,
        .disconnect_callback = on_tcp_client_disconnected,
        .user_ctx = NULL,
        .stack_size = 4096,
        .task_priority = 5,
        .verbose = true
    };

    // 创建TCP服务器
    esp_err_t err;
    g_bridge.tcp_server = tcp_server_create(&tcp_config, &err);
    if (!g_bridge.tcp_server || err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create TCP server: %s", esp_err_to_name(err));
        return err;
    }

    // 启动TCP服务器
    err = tcp_server_start(g_bridge.tcp_server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start TCP server: %s", esp_err_to_name(err));
        tcp_server_destroy(g_bridge.tcp_server);
        g_bridge.tcp_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "TCP server started on port %d", g_bridge.config.tcp_port);
    return ESP_OK;
}

esp_err_t uart_bridge_stop_tcp_server(void)
{
    if (!g_bridge.tcp_server) {
        return ESP_OK;
    }

    // 停止并销毁TCP服务器
    tcp_server_stop(g_bridge.tcp_server);
    tcp_server_destroy(g_bridge.tcp_server);
    g_bridge.tcp_server = NULL;

    ESP_LOGI(TAG, "TCP server stopped");
    return ESP_OK;
}

static void on_tcp_data_received(tcp_client_t *client, const uint8_t *data, size_t len, void *user_ctx)
{
    if (!data || len == 0) {
        return;
    }

    ESP_LOGD(TAG, "Received %d bytes from TCP client %s:%d", 
             len, ipaddr_ntoa(&client->ip_addr), client->port);

    // 发送数据到UART
    esp_err_t ret = uart_send_data(data, len);
    if (ret == ESP_OK) {
        xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
        g_bridge.stats.tcp_rx_bytes += len;
        xSemaphoreGive(g_bridge.stats_mutex);
    }
}

static void on_tcp_client_connected(tcp_client_t *client, void *user_ctx)
{
    ESP_LOGI(TAG, "TCP client connected: %s:%d", 
             ipaddr_ntoa(&client->ip_addr), client->port);

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    g_bridge.stats.tcp_connect_count++;
    xSemaphoreGive(g_bridge.stats_mutex);
}

static void on_tcp_client_disconnected(tcp_client_t *client, void *user_ctx)
{
    ESP_LOGI(TAG, "TCP client disconnected: %s:%d", 
             ipaddr_ntoa(&client->ip_addr), client->port);

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    g_bridge.stats.tcp_disconnect_count++;
    xSemaphoreGive(g_bridge.stats_mutex);
}

static void uart_task(void *pvParameters)
{
    uart_event_t event;
    size_t buffered_size;

    ESP_LOGI(TAG, "UART task started");

    g_bridge.running = true;

    while (g_bridge.running) {
        // 等待UART事件
        if (xQueueReceive(g_bridge.uart_queue, (void *)&event, pdMS_TO_TICKS(100))) {
            bzero(g_bridge.uart_buffer, UART_BRIDGE_DEFAULT_BUF_SIZE);
            
            switch (event.type) {
                case UART_DATA:
                    uart_get_buffered_data_len(g_bridge.uart_port, &buffered_size);
                    if (buffered_size > 0) {
                        size_t read_len = uart_read_bytes(g_bridge.uart_port, g_bridge.uart_buffer, 
                                                        MIN(buffered_size, UART_BRIDGE_DEFAULT_BUF_SIZE), 
                                                        pdMS_TO_TICKS(100));
                        if (read_len > 0 && g_bridge.tcp_server) {
                            // 广播数据到所有TCP客户端
                            esp_err_t ret = tcp_server_broadcast(g_bridge.tcp_server, g_bridge.uart_buffer, read_len);
                            if (ret == ESP_OK) {
                                xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
                                g_bridge.stats.uart_rx_bytes += read_len;
                                // TCP发送字节数按客户端数量计算
                                int client_count = tcp_server_get_client_count(g_bridge.tcp_server);
                                if (client_count > 0) {
                                    g_bridge.stats.tcp_tx_bytes += read_len * client_count;
                                }
                                xSemaphoreGive(g_bridge.stats_mutex);
                                
                                ESP_LOGD(TAG, "Broadcasted %d bytes to %d TCP clients", read_len, client_count);
                            } else {
                                xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
                                g_bridge.stats.uart_rx_errors++;
                                xSemaphoreGive(g_bridge.stats_mutex);
                            }
                        }
                    }
                    break;
                    
                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "UART FIFO overflow");
                    uart_flush_input(g_bridge.uart_port);
                    xQueueReset(g_bridge.uart_queue);
                    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
                    g_bridge.stats.uart_rx_errors++;
                    xSemaphoreGive(g_bridge.stats_mutex);
                    break;
                    
                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "UART buffer full");
                    uart_flush_input(g_bridge.uart_port);
                    xQueueReset(g_bridge.uart_queue);
                    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
                    g_bridge.stats.uart_rx_errors++;
                    xSemaphoreGive(g_bridge.stats_mutex);
                    break;
                    
                default:
                    ESP_LOGD(TAG, "UART event type: %d", event.type);
                    break;
            }
        }
    }

    ESP_LOGI(TAG, "UART task stopped");
    g_bridge.uart_task = NULL;
    g_bridge.running = false;
    vTaskDelete(NULL);
}

static esp_err_t uart_send_data(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查缓冲区是否有足够空间
    size_t available_space;
    esp_err_t ret = uart_get_tx_buffer_free_size(g_bridge.uart_port, &available_space);
    if (ret != ESP_OK || available_space < len) {
        ESP_LOGW(TAG, "UART send buffer space insufficient, discarding data (%d/%d)", available_space, len);
        xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
        g_bridge.stats.buffer_overflow++;
        xSemaphoreGive(g_bridge.stats_mutex);
        return ESP_ERR_NO_MEM;
    }

    int bytes_written = uart_write_bytes(g_bridge.uart_port, data, len);
    if (bytes_written < 0) {
        ESP_LOGE(TAG, "UART send data failed");
        xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
        g_bridge.stats.uart_tx_errors++;
        xSemaphoreGive(g_bridge.stats_mutex);
        return ESP_FAIL;
    } else if (bytes_written != len) {
        ESP_LOGW(TAG, "UART send data incomplete: %d/%d", bytes_written, len);
        xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
        g_bridge.stats.uart_tx_errors++;
        xSemaphoreGive(g_bridge.stats_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    g_bridge.stats.uart_tx_bytes += bytes_written;
    xSemaphoreGive(g_bridge.stats_mutex);

    return ESP_OK;
}

