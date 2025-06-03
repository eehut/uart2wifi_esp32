#include "wifi_station.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

static const char *TAG = "wifi_example";

void wifi_station_example(void)
{
    ESP_LOGI(TAG, "WiFi Station组件使用示例");

    // 初始化NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化WiFi Station组件
    ESP_ERROR_CHECK(wifi_station_init(NULL, NULL));

    // 等待一段时间让后台任务工作
    vTaskDelay(pdMS_TO_TICKS(5000));

    // 示例1: 获取WiFi连接状态
    wifi_connection_status_t status;
    if (wifi_station_get_status(&status) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi状态: %d", status.status);
        if (status.status == WIFI_STATION_STATUS_CONNECTED) {
            ESP_LOGI(TAG, "已连接到: %s", status.ssid);
            ESP_LOGI(TAG, "信号强度: %d dBm", status.rssi);
            ESP_LOGI(TAG, "IP地址: " IPSTR, IP2STR((ip4_addr_t*)&status.ip_addr));
            ESP_LOGI(TAG, "连接时长: %u秒", status.connected_time);
        }
    }

    // 示例2: 扫描WiFi网络
    ESP_LOGI(TAG, "开始扫描WiFi网络...");
    wifi_network_info_t networks[20];
    uint16_t count = 20;
    if (wifi_station_scan_networks(networks, &count) == ESP_OK) {
        ESP_LOGI(TAG, "扫描到 %d 个WiFi网络:", count);
        for (uint16_t i = 0; i < count; i++) {
            ESP_LOGI(TAG, "  %d. %s (RSSI: %d)", i+1, networks[i].ssid, networks[i].rssi);
        }
    }

    // 示例3: 手动连接到指定WiFi (请根据实际情况修改SSID和密码)
    const char *test_ssid = "YourWiFiSSID";
    const char *test_password = "YourWiFiPassword";
    ESP_LOGI(TAG, "尝试连接到WiFi: %s", test_ssid);
    esp_err_t connect_result = wifi_station_connect(test_ssid, test_password);
    if (connect_result == ESP_OK) {
        ESP_LOGI(TAG, "WiFi连接成功!");
        
        // 再次获取状态
        if (wifi_station_get_status(&status) == ESP_OK) {
            ESP_LOGI(TAG, "连接信息:");
            ESP_LOGI(TAG, "  SSID: %s", status.ssid);
            ESP_LOGI(TAG, "  RSSI: %d dBm", status.rssi);
            ESP_LOGI(TAG, "  IP: " IPSTR, IP2STR((ip4_addr_t*)&status.ip_addr));
            ESP_LOGI(TAG, "  网关: " IPSTR, IP2STR((ip4_addr_t*)&status.gateway));
            ESP_LOGI(TAG, "  子网掩码: " IPSTR, IP2STR((ip4_addr_t*)&status.netmask));
        }
    } else {
        ESP_LOGE(TAG, "WiFi连接失败: %s", esp_err_to_name(connect_result));
    }

    // 示例4: 获取连接记录
    wifi_connection_record_t records[WIFI_STATION_MAX_RECORDS];
    uint8_t record_count;
    if (wifi_station_get_records(records, &record_count) == ESP_OK) {
        ESP_LOGI(TAG, "已保存的WiFi记录 (%d个):", record_count);
        for (uint8_t i = 0; i < record_count; i++) {
            if (records[i].valid) {
                ESP_LOGI(TAG, "  %d. %s (序号: %u)", i+1, records[i].ssid, records[i].sequence);
            }
        }
    }

    // 示例5: 添加WiFi记录
    ESP_LOGI(TAG, "添加测试WiFi记录...");
    wifi_station_add_record("TestAP", "testpassword");

    // 等待几秒钟观察日志
    vTaskDelay(pdMS_TO_TICKS(10000));

    // 示例6: 断开WiFi连接
    ESP_LOGI(TAG, "断开WiFi连接...");
    wifi_station_disconnect();

    // 等待后台任务尝试重新连接
    ESP_LOGI(TAG, "等待自动重连...");
    vTaskDelay(pdMS_TO_TICKS(60000)); // 等待1分钟观察自动连接行为

    // 清理资源
    wifi_station_deinit();
    ESP_LOGI(TAG, "WiFi Station组件示例结束");
}

// 用于在main函数中调用的任务函数
void wifi_example_task(void *pvParameters)
{
    wifi_station_example();
    vTaskDelete(NULL);
}

/* 在您的main.c中添加以下代码来运行示例:

#include "wifi_station_example.h"

void app_main()
{
    // 创建WiFi示例任务
    xTaskCreate(wifi_example_task, "wifi_example", 8192, NULL, 5, NULL);
}

*/ 