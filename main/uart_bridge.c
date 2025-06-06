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
#include "hex_dump.h"
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
#include "driver/gpio.h"

// 添加MIN宏定义
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

static const char *TAG = "uart_bridge";

// NVS存储键名
#define NVS_NAMESPACE           "uart_bridge"
#define NVS_KEY_TCP_PORT        "tcp_port"
#define NVS_KEY_UART_BAUDRATE   "baudrate"

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
    bool uart_tx_verbose;
    bool uart_rx_verbose;
    uint8_t uart_port;
    tcp_server_handle_t tcp_server;
    SemaphoreHandle_t stats_mutex;
    TaskHandle_t task_handle;
} uart_bridge_t;

static uart_bridge_t g_bridge = {0};

// 函数声明
static void uart_bridge_task(void *pvParameters);
static void on_tcp_data_received(tcp_client_t *client, const uint8_t *data, size_t len, void *user_ctx);
static void on_tcp_client_connected(tcp_client_t *client, void *user_ctx);
static void on_tcp_client_disconnected(tcp_client_t *client, void *user_ctx);
static esp_err_t uart_bridge_load_config(uart_bridge_config_t *config);
static esp_err_t uart_bridge_save_config(const uart_bridge_config_t *config);
static esp_err_t send_data_to_uart(const uint8_t *data, size_t len);


/**
 * @brief 初始化UART桥接模块
 * 
 * @param uart_id 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_init(uint8_t uart_id)
{
    if (g_bridge.initialized) {
        ESP_LOGW(TAG, "Module already initialized");
        return ESP_OK;
    }

    const uart_hw_config_t *hw_config = uart_hw_config_get(uart_id);
    if (!hw_config) {
        ESP_LOGE(TAG, "uart(%d) hardware config not found", uart_id);
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGD(TAG, "uart(%d) hardware config, port: %d, txd: %d, rxd: %d", 
             uart_id, hw_config->uart_port, hw_config->txd_pin, hw_config->rxd_pin);

    memset(&g_bridge, 0, sizeof(g_bridge));

    // 加载配置
    esp_err_t ret = uart_bridge_load_config(&g_bridge.config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "config load failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 创建互斥锁
    g_bridge.stats_mutex = xSemaphoreCreateMutex();
    if (!g_bridge.stats_mutex) {
        ESP_LOGE(TAG, "failed to create stats mutex");
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

    
    // Configure internal pull-up for RX pin to avoid noise
    gpio_set_pull_mode(hw_config->rxd_pin, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "enabled internal pull-up for RX pin(%d)", hw_config->rxd_pin);

    ret = uart_driver_install(hw_config->uart_port, UART_BRIDGE_BUFFER_SIZE * 2, 
        UART_BRIDGE_BUFFER_SIZE * 2, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to install port(%d) driver: %s", hw_config->uart_port, esp_err_to_name(ret));
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ret;
    }

    ret = uart_param_config(hw_config->uart_port, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure port(%d) params: %s", hw_config->uart_port, esp_err_to_name(ret));
        uart_driver_delete(hw_config->uart_port);
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ret;
    }

    ret = uart_set_pin(hw_config->uart_port, hw_config->txd_pin, hw_config->rxd_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "failed to configure port(%d) pins: %s", hw_config->uart_port, esp_err_to_name(ret));
        uart_driver_delete(hw_config->uart_port);
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ret;
    }


    g_bridge.uart_port = hw_config->uart_port;

    g_bridge.uart_tx_verbose = false;
    g_bridge.uart_rx_verbose = false;


    // 创建UART任务
    BaseType_t task_ret = xTaskCreate(uart_bridge_task, "uart_bridge", 
            UART_BRIDGE_TASK_STACK_SIZE, NULL, UART_BRIDGE_TASK_PRIORITY, &g_bridge.task_handle);
    if (task_ret != pdTRUE) {
        ESP_LOGE(TAG, "failed to create task");
        uart_driver_delete(hw_config->uart_port);
        vSemaphoreDelete(g_bridge.stats_mutex);
        return ESP_FAIL;
    }

    g_bridge.initialized = true;
    ESP_LOGI(TAG, "uart-bridge(%d) initialized success, baudrate: %d, tcp-port:%d", 
            uart_id, g_bridge.config.baudrate, g_bridge.config.tcp_port);
    
    return ESP_OK;
}

/**
 * @brief 反初始化UART桥接模块
 * 
 * @return esp_err_t 
 */
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
    if (g_bridge.task_handle) {
        vTaskDelete(g_bridge.task_handle);
        g_bridge.task_handle = NULL;
    }

    // 删除UART驱动
    uart_driver_delete(g_bridge.uart_port);

    if (g_bridge.stats_mutex) {
        vSemaphoreDelete(g_bridge.stats_mutex);
        g_bridge.stats_mutex = NULL;
    }

    g_bridge.initialized = false;
    ESP_LOGI(TAG, "uart-bridge deinitialized");
    return ESP_OK;
}

/**
 * @brief 获取UART桥接模块状态
 * 
 * @param status 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_get_status(uart_bridge_status_t *status)
{
    if (!status) {
        return ESP_ERR_INVALID_ARG;
    }
    
    status->tcp_standby = (g_bridge.tcp_server != NULL);
    status->uart_opened = g_bridge.initialized;
    status->forwarding = g_bridge.running && (g_bridge.tcp_server != NULL);
    status->uart_baudrate = g_bridge.config.baudrate;
    status->tcp_port = g_bridge.config.tcp_port;
    status->tcp_client_num = (g_bridge.tcp_server != NULL) ? 
                            tcp_server_get_client_count(g_bridge.tcp_server) : 0;
    
    return ESP_OK;
}

/**
 * @brief 获取统计信息
 * 
 * @param stats 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_get_stats(uart_bridge_stats_t *stats)
{
    if (!stats) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    memcpy(stats, &g_bridge.stats, sizeof(uart_bridge_stats_t));
    xSemaphoreGive(g_bridge.stats_mutex);

    return ESP_OK;
}

/**
 * @brief 重置统计信息
 * 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_reset_stats(void)
{
    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    memset(&g_bridge.stats, 0, sizeof(uart_bridge_stats_t));
    xSemaphoreGive(g_bridge.stats_mutex);

    ESP_LOGI(TAG, "statistics reset");
    return ESP_OK;
}


/**
 * @brief 设置UART波特率
 * 
 * @param baudrate 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_set_baudrate(uint32_t baudrate)
{
    if (!g_bridge.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = uart_set_baudrate(g_bridge.uart_port, baudrate);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "set baudrate(%d) success", baudrate);
        if (g_bridge.config.baudrate != baudrate) {
            g_bridge.config.baudrate = baudrate;
            // 保存配置到NVS
            uart_bridge_save_config(&g_bridge.config);
        }        
    } else {
        ESP_LOGE(TAG, "failed to set baudrate(%d): %s", baudrate, esp_err_to_name(ret));
    }

    return ret;
}

/**
 * @brief 启动TCP服务器
 * 
 * @return esp_err_t 
 */
esp_err_t uart_bridge_start_tcp_server(void)
{
    if (!g_bridge.initialized || !g_bridge.running) {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_bridge.tcp_server) {
        ESP_LOGW(TAG, "tcp server already running");
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
        .verbose = false
    };

    // 创建TCP服务器
    esp_err_t err;
    g_bridge.tcp_server = tcp_server_create(&tcp_config, &err);
    if (!g_bridge.tcp_server || err != ESP_OK) {
        ESP_LOGE(TAG, "failed to create tcp server: %s", esp_err_to_name(err));
        return err;
    }

    // 启动TCP服务器
    err = tcp_server_start(g_bridge.tcp_server);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to start tcp server: %s", esp_err_to_name(err));
        tcp_server_destroy(g_bridge.tcp_server);
        g_bridge.tcp_server = NULL;
        return err;
    }

    ESP_LOGI(TAG, "tcp server started on port(%d)", g_bridge.config.tcp_port);
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

    ESP_LOGI(TAG, "tcp server stopped");
    return ESP_OK;
}


bool uart_bridge_set_tcp_verbose(bool tx_verbose, bool rx_verbose)
{
    if (g_bridge.tcp_server) {
        tcp_server_set_verbose(g_bridge.tcp_server, tx_verbose, rx_verbose);
        return true;
    }

    return false;
}

bool uart_bridge_set_uart_verbose(bool tx_verbose, bool rx_verbose)
{
    g_bridge.uart_tx_verbose = tx_verbose;
    g_bridge.uart_rx_verbose = rx_verbose;
    return true;
}


static void on_tcp_data_received(tcp_client_t *client, const uint8_t *data, size_t len, void *user_ctx)
{
    if (!data || len == 0) {
        return;
    }

    ESP_LOGD(TAG, "received %d bytes from client(%s:%d)", 
             len, ipaddr_ntoa(&client->ip_addr), client->port);

    // 发送数据到UART
    send_data_to_uart(data, len);

    // 更新统计信息
    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    g_bridge.stats.tcp_rx_bytes += len;
    xSemaphoreGive(g_bridge.stats_mutex);    
}

static void on_tcp_client_connected(tcp_client_t *client, void *user_ctx)
{
    ESP_LOGI(TAG, "tcp client(%s:%d) connected", 
             ipaddr_ntoa(&client->ip_addr), client->port);

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    g_bridge.stats.tcp_connect_count++;
    xSemaphoreGive(g_bridge.stats_mutex);
}

static void on_tcp_client_disconnected(tcp_client_t *client, void *user_ctx)
{
    ESP_LOGI(TAG, "tcp client(%s:%d) disconnected", 
             ipaddr_ntoa(&client->ip_addr), client->port);

    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    g_bridge.stats.tcp_disconnect_count++;
    xSemaphoreGive(g_bridge.stats_mutex);
}

/**
 * @brief UART桥接数据转发任务
 * 
 * @param pvParameters 
 */
static void uart_bridge_task(void *pvParameters)
{
    esp_err_t err = ESP_OK;

    ESP_LOGI(TAG, "uart-bridge task started");
    g_bridge.running = true;

    uint8_t* rx_buf = (uint8_t*) malloc(UART_BRIDGE_BUFFER_SIZE);
    if (!rx_buf) {
        ESP_LOGE(TAG, "failed to allocate rx buffer, uart-bridge task exit");
        g_bridge.running = false;
        vTaskDelete(NULL);
        return;
    }

    while (g_bridge.running) {
        const int rx_bytes = uart_read_bytes(g_bridge.uart_port, rx_buf, UART_BRIDGE_BUFFER_SIZE, 100 / portTICK_PERIOD_MS);
        if (rx_bytes > 0) {

            if (g_bridge.uart_rx_verbose) {
                char prefix[32];
                sprintf(prefix, "rx from uart[len=%d]:", rx_bytes);
                hex_dump(rx_buf, rx_bytes, prefix);
            }

            // 如果TCP服务器已就绪,则广播数据到所有TCP客户端
            if (g_bridge.tcp_server && tcp_server_get_client_count(g_bridge.tcp_server) > 0) {
                // TCP服务有效, 转发到所有TCP客户端
                err = tcp_server_broadcast(g_bridge.tcp_server, rx_buf, rx_bytes);
                xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
                g_bridge.stats.uart_rx_bytes += rx_bytes;
                if (err == ESP_OK) {
                    g_bridge.stats.tcp_tx_bytes += rx_bytes;// 以客户端收到的数据长度为视角 
                } else {
                    g_bridge.stats.tcp_tx_error_bytes += rx_bytes;
                }
                xSemaphoreGive(g_bridge.stats_mutex);
            } else {
                // TCP服务无效, 仅更新UART 接收数据
                xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
                g_bridge.stats.uart_rx_bytes += rx_bytes;
                xSemaphoreGive(g_bridge.stats_mutex);
            }
        } else if (rx_bytes < 0) {
            // 读取数据失败, 需要等待一段时间 
            ESP_LOGE(TAG, "uart read data failed:%d, wait 100ms", rx_bytes);
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
    free(rx_buf);

    ESP_LOGW(TAG, "uart-bridge task stopped");
    g_bridge.task_handle = NULL;
    g_bridge.running = false;
    vTaskDelete(NULL);
}

static esp_err_t send_data_to_uart(const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查缓冲区是否有足够空间
    size_t available_space = 0;
    esp_err_t ret = uart_get_tx_buffer_free_size(g_bridge.uart_port, &available_space);
    if (ret != ESP_OK) {
        // unexpected error, should not happen
        xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
        g_bridge.stats.uart_tx_drop_bytes += len;
        xSemaphoreGive(g_bridge.stats_mutex);        
        return ret;
    }

    // 如果缓冲区空间不足，丢弃超出部分数据
    size_t nice_len = MIN(available_space, len);
    size_t drop_len = len - nice_len;

    if (drop_len > 0) {
        ESP_LOGW(TAG, "uart tx buffer overflow, discarding %d bytes", drop_len);
        xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
        g_bridge.stats.uart_tx_drop_bytes += drop_len;
        xSemaphoreGive(g_bridge.stats_mutex);
    }

    if (nice_len <= 0) {
        // 没有任何数据可写入
        return ESP_ERR_NO_MEM;
    }

    if (g_bridge.uart_tx_verbose) {
        char prefix[32];
        sprintf(prefix, "tx to uart[len=%d]:", nice_len);
        hex_dump(data, nice_len, prefix);
    }

    int bytes_written = uart_write_bytes(g_bridge.uart_port, data, nice_len);
    if (bytes_written < 0) {
        ESP_LOGE(TAG, "uart send data failed");
        xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
        g_bridge.stats.uart_tx_error_bytes += nice_len;
        xSemaphoreGive(g_bridge.stats_mutex);
        return ESP_FAIL;
    } else if (bytes_written != nice_len) {
        ESP_LOGW(TAG, "uart send data incomplete: expected(%d), actual(%d)", nice_len, bytes_written);
        xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
        g_bridge.stats.uart_tx_bytes += bytes_written;
        g_bridge.stats.uart_tx_error_bytes += (nice_len - bytes_written);
        xSemaphoreGive(g_bridge.stats_mutex);
        return ESP_ERR_INVALID_SIZE;
    }

    // 到这里,表示所有数据完成写入
    xSemaphoreTake(g_bridge.stats_mutex, portMAX_DELAY);
    g_bridge.stats.uart_tx_bytes += bytes_written;
    xSemaphoreGive(g_bridge.stats_mutex);

    return ESP_OK;
}

static esp_err_t uart_bridge_load_config(uart_bridge_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "failed to open nvs namespace(%s), using default config", esp_err_to_name(err));
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
    ESP_LOGI(TAG, "config loaded: tcp-port(%d), baudrate(%lu)", config->tcp_port, config->baudrate);
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
        ESP_LOGE(TAG, "failed to open nvs namespace(%s)", esp_err_to_name(err));
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
        ESP_LOGI(TAG, "config saved successfully");
    } else {
        ESP_LOGE(TAG, "config save failed: %s", esp_err_to_name(err));
    }
    return err;
}
