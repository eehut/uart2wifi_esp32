/**
 * WiFi Station 组件集成示例
 * 演示如何在主项目中使用 wifi_station 组件
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

// 包含 WiFi Station 组件
#include "wifi_station.h"

static const char *TAG = "wifi_integration";

void wifi_status_monitor_task(void *pvParameters)
{
    wifi_connection_status_t last_status = {0};
    
    while (1) {
        wifi_connection_status_t current_status;
        
        if (wifi_station_get_status(&current_status) == ESP_OK) {
            // 状态发生变化时打印信息
            if (current_status.status != last_status.status) {
                switch (current_status.status) {
                    case WIFI_STATION_STATUS_DISCONNECTED:
                        ESP_LOGI(TAG, "WiFi状态: 未连接");
                        break;
                    case WIFI_STATION_STATUS_CONNECTING:
                        ESP_LOGI(TAG, "WiFi状态: 连接中...");
                        break;
                    case WIFI_STATION_STATUS_CONNECTED:
                        ESP_LOGI(TAG, "WiFi状态: 已连接");
                        ESP_LOGI(TAG, "  SSID: %s", current_status.ssid);
                        ESP_LOGI(TAG, "  IP: " IPSTR, IP2STR((esp_ip4_addr_t*)&current_status.ip_addr));
                        ESP_LOGI(TAG, "  信号强度: %d dBm", current_status.rssi);
                        break;
                }
                last_status = current_status;
            }
            
            // 如果已连接，每30秒打印一次连接信息
            if (current_status.status == WIFI_STATION_STATUS_CONNECTED) {
                static int counter = 0;
                if (++counter >= 30) { // 30 * 1秒 = 30秒
                    ESP_LOGI(TAG, "连接信息 - SSID: %s, IP: " IPSTR ", 连接时长: %u秒", 
                            current_status.ssid, 
                            IP2STR((esp_ip4_addr_t*)&current_status.ip_addr),
                            current_status.connected_time);
                    counter = 0;
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // 每秒检查一次
    }
}

void wifi_management_example(void)
{
    ESP_LOGI(TAG, "WiFi管理示例开始");
    
    // 等待5秒让自动连接尝试完成
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // 获取当前连接记录
    wifi_connection_record_t records[WIFI_STATION_MAX_RECORDS];
    uint8_t record_count;
    
    if (wifi_station_get_records(records, &record_count) == ESP_OK) {
        ESP_LOGI(TAG, "当前保存的WiFi记录数量: %d", record_count);
        for (uint8_t i = 0; i < record_count; i++) {
            if (records[i].valid) {
                ESP_LOGI(TAG, "  记录 %d: %s (序号: %u)", i+1, records[i].ssid, records[i].sequence);
            }
        }
    }
    
    // 扫描可用网络
    ESP_LOGI(TAG, "扫描可用WiFi网络...");
    wifi_network_info_t networks[15];
    uint16_t network_count = 15;
    
    if (wifi_station_scan_networks(networks, &network_count) == ESP_OK) {
        ESP_LOGI(TAG, "发现 %d 个WiFi网络:", network_count);
        for (uint16_t i = 0; i < network_count && i < 10; i++) { // 只显示前10个
            ESP_LOGI(TAG, "  %d. %s (信号: %d dBm)", i+1, networks[i].ssid, networks[i].rssi);
        }
    }
    
    // 示例：
    const char *target_ssid = "NICEHOME";
    const char *target_password = "Jinhaian2020";
    
    ESP_LOGI(TAG, "尝试连接到指定WiFi: %s", target_ssid);
    esp_err_t result = wifi_station_connect(target_ssid, target_password);
    if (result == ESP_OK) {
        ESP_LOGI(TAG, "连接成功!");
    } else {
        ESP_LOGE(TAG, "连接失败: %s", esp_err_to_name(result));
    }
    
    ESP_LOGI(TAG, "WiFi管理示例完成，状态监控继续运行...");
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== WiFi Station 组件集成示例 ===");
    
    // 第1步: 初始化NVS
    ESP_LOGI(TAG, "1. 初始化NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // 第2步: 初始化网络接口
    ESP_LOGI(TAG, "2. 初始化网络接口...");
    ESP_ERROR_CHECK(esp_netif_init());
    
    // 第3步: 创建事件循环
    ESP_LOGI(TAG, "3. 创建事件循环...");
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    // 第4步: 初始化WiFi Station组件
    ESP_LOGI(TAG, "4. 初始化WiFi Station组件...");
    ESP_ERROR_CHECK(wifi_station_init());
    ESP_LOGI(TAG, "WiFi Station组件初始化完成，后台任务已启动");
    
    // 第5步: 创建状态监控任务
    ESP_LOGI(TAG, "5. 创建WiFi状态监控任务...");
    xTaskCreate(wifi_status_monitor_task, "wifi_monitor", 3072, NULL, 4, NULL);
    
    // 第6步: 执行WiFi管理示例
    ESP_LOGI(TAG, "6. 执行WiFi管理示例...");
    wifi_management_example();
    
    ESP_LOGI(TAG, "主初始化完成，系统正常运行");
    ESP_LOGI(TAG, "WiFi组件将自动处理连接管理");
    
    // 主循环 - 可以在这里添加您的应用逻辑
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000)); // 每10秒输出一次运行状态
        ESP_LOGI(TAG, "应用正在运行...");
    }
}
