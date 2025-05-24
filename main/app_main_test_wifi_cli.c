/**
 * 完整的APP MAIN示例 - 集成WiFi Station组件和命令菜单
 * 
 * 本文件展示如何在ESP32-C3项目中集成:
 * 1. WiFi Station组件 (自动WiFi管理)
 * 2. 命令菜单系统 (UART交互界面)
 * 3. 保持原有的系统功能
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "driver/uart.h"

// 包含WiFi Station组件和命令菜单
#include "wifi_station.h"
#include "cli_menu.h"

// 包含原有的模块（如果需要）
// #include "display.h"
// #include "uptime.h"
// 等等...

static const char *TAG = "app_main";

/**
 * 初始化UART用于日志和命令输入
 * 注意：ESP32-C3默认使用UART0进行日志输出和下载
 */
static void init_uart(void)
{
    // UART0的配置通常由ESP-IDF自动处理
    // 这里可以根据需要调整缓冲区大小等参数
    
    // const uart_config_t uart_config = {
    //     .baud_rate = 115200,
    //     .data_bits = UART_DATA_8_BITS,
    //     .parity = UART_PARITY_DISABLE,
    //     .stop_bits = UART_STOP_BITS_1,
    //     .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    // };
    
    // // 重新配置UART0（可选）
    // uart_param_config(UART_NUM_0, &uart_config);
    // uart_set_pin(UART_NUM_0, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, 
    //              UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    
    // // 安装UART驱动（使用较大的缓冲区以支持命令输入）
    // uart_driver_install(UART_NUM_0, 1024, 1024, 0, NULL, 0);
    
    //ESP_LOGI(TAG, "UART initialized for logging and command input");
}

/**
 * 系统状态监控任务
 * 定期输出系统运行状态，便于调试
 */
static void system_monitor_task(void *pvParameters)
{
    uint32_t counter = 0;
    
    while (1) {
        counter++;
        
        // 每60秒输出一次系统状态
        if (counter % 60 == 0) {
            ESP_LOGI(TAG, "系统运行正常 - 运行时间: %u 分钟", counter);
            
            // 获取WiFi状态
            wifi_connection_status_t wifi_status;
            if (wifi_station_get_status(&wifi_status) == ESP_OK) {
                if (wifi_status.status == WIFI_STATION_STATUS_CONNECTED) {
                    ESP_LOGI(TAG, "WiFi已连接: %s, IP: " IPSTR, 
                            wifi_status.ssid, IP2STR((esp_ip4_addr_t*)&wifi_status.ip_addr));
                } else {
                    ESP_LOGI(TAG, "WiFi状态: 未连接");
                }
            }
            
            // 输出内存信息
            ESP_LOGI(TAG, "空闲堆内存: %u bytes", esp_get_free_heap_size());
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每秒检查一次
    }
}

/**
 * 主应用程序入口
 * 
 * 初始化顺序很重要：
 * 1. 基础系统组件
 * 2. NVS和网络
 * 3. WiFi Station组件
 * 4. 命令菜单系统
 * 5. 应用特定功能
 */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-C3 Serial2IP 设备启动 ===");
    ESP_LOGI(TAG, "编译时间: %s %s", __DATE__, __TIME__);
    
    // ==================== 第1步：基础初始化 ====================
    
    ESP_LOGI(TAG, "1. 初始化基础系统...");
    
    // 初始化UART（用于日志和命令输入）
    init_uart();
    
    // 初始化NVS（WiFi配置需要）
    ESP_LOGI(TAG, "初始化NVS存储...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // ==================== 第2步：网络初始化 ====================
    
    ESP_LOGI(TAG, "2. 初始化网络组件...");
    
    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 创建事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // ==================== 第3步：WiFi Station组件 ====================
    
    ESP_LOGI(TAG, "3. 初始化WiFi Station组件...");
    
    // 初始化WiFi Station组件
    ESP_ERROR_CHECK(wifi_station_init());
    ESP_LOGI(TAG, "WiFi Station组件初始化完成，自动连接已启动");
    
    // ==================== 第4步：其他系统组件 ====================
    
    ESP_LOGI(TAG, "4. 初始化其他系统组件...");
    
    // 这里可以初始化您的其他组件
    // 例如：显示器、传感器、通信模块等
    /*
    display_init();
    uptime_init();
    // 其他组件...
    */
    
    // ==================== 第5步：命令菜单系统 ====================
    
    ESP_LOGI(TAG, "5. 初始化命令菜单系统...");
    
    // 初始化命令菜单
    ESP_ERROR_CHECK(command_menu_init());
    
    // 等待WiFi组件稳定（可选）
    vTaskDelay(pdMS_TO_TICKS(2000));
    
    // 启动命令菜单
    ESP_ERROR_CHECK(command_menu_start());
    
    // ==================== 第6步：启动应用任务 ====================
    
    ESP_LOGI(TAG, "6. 启动应用任务...");
    
    // 创建系统监控任务
    xTaskCreate(system_monitor_task, "sys_monitor", 2048, NULL, 3, NULL);
    
    // 这里可以创建您的应用特定任务
    /*
    xTaskCreate(serial_to_ip_task, "serial2ip", 4096, NULL, 5, NULL);
    xTaskCreate(network_server_task, "net_server", 4096, NULL, 5, NULL);
    // 其他任务...
    */
    
    // ==================== 启动完成 ====================
    
    ESP_LOGI(TAG, "=== 系统初始化完成 ===");
    ESP_LOGI(TAG, "WiFi: 自动连接已启用");
    ESP_LOGI(TAG, "命令菜单: 按回车键显示菜单");
    ESP_LOGI(TAG, "日志输出: 与命令菜单共用UART0");
    ESP_LOGI(TAG, "设备就绪，等待操作...");
    
    // 主循环 - 可以执行周期性任务或保持空闲
    while (1) {
        // 执行主循环任务（如果有的话）
        // 例如：检查网络状态、处理串口数据等
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // 1秒循环
    }
}

/*
 * 使用说明：
 * 
 * 1. 编译和烧录：
 *    idf.py build flash monitor
 * 
 * 2. 设备启动后：
 *    - 系统会自动尝试连接保存的WiFi网络
 *    - 按回车键可以显示命令菜单
 *    - 日志信息会正常输出到串口
 * 
 * 3. 菜单操作：
 *    - 输入数字选择菜单项
 *    - 支持WiFi扫描、连接、管理等功能
 *    - 可以查看设备信息和帮助
 * 
 * 4. 集成到现有项目：
 *    - 将本文件的初始化代码集成到您的app_main()函数中
 *    - 根据需要调整组件初始化顺序
 *    - 添加您的应用特定功能
 * 
 * 注意事项：
 * - UART0既用于日志输出又用于命令输入
 * - 确保NVS分区有足够空间保存WiFi配置
 * - 命令菜单和日志输出可能会交错显示
 * - 可以通过调整日志级别来减少日志输出
 */ 