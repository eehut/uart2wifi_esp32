/**
 * @file wifi_station.c
 * @author LiuChuansen (179712066@qq.com)
 * @brief WiFi Station 组件
 * @version 0.1
 * @date 2025-05-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "wifi_station.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "lwip/err.h"
#include "lwip/sys.h"

#include <inttypes.h>
#include <stdlib.h>

static const char *TAG = "wifi_station";

// 比较函数，用于qsort按RSSI降序排序
static int compare_ap_by_rssi(const void *a, const void *b) {
    const wifi_ap_record_t *ap_a = (const wifi_ap_record_t *)a;
    const wifi_ap_record_t *ap_b = (const wifi_ap_record_t *)b;
    
    // 返回负值表示a应该排在b前面（RSSI越高越好，所以用降序）
    return ap_b->rssi - ap_a->rssi;
}

// NVS命名空间
#define NVS_NAMESPACE "wifi_records"
#define NVS_SEQUENCE_KEY "sequence"

// WiFi事件位
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// 组件状态
typedef struct {
    bool initialized;
    bool auto_connect_enabled;
    bool auto_connect_one_shot;
    TickType_t next_scan_time;
    wifi_station_state_t state;
    char current_ssid[WIFI_STATION_SSID_LEN];
    uint8_t current_bssid[WIFI_STATION_BSSID_LEN];
    int8_t current_rssi;
    uint32_t connect_start_time;
    uint32_t connected_time;
    esp_netif_t *netif;
    EventGroupHandle_t wifi_event_group;
    TaskHandle_t background_task_handle;
    SemaphoreHandle_t mutex;
    uint32_t current_sequence;
    wifi_connection_record_t records[WIFI_STATION_MAX_RECORDS];
    uint8_t record_count;
    
    // 新增扫描相关字段
    bool scan_in_progress;           // 扫描是否正在进行
    bool scan_done;                  // 扫描是否完成
    uint16_t last_scan_ap_count;     // 上次扫描到的AP数量
    wifi_ap_record_t *last_scan_result; // 上次扫描结果
    
    // 新增扫描协调字段
    bool user_scan_requested;        // 用户是否请求了异步扫描
    bool background_scan_requested;  // 后台扫描是否被请求
    TickType_t scan_start_time;      // 扫描开始时间
    
    // 新增重试机制字段
    char retry_target_ssid[WIFI_STATION_SSID_LEN];  // 当前尝试连接的网络SSID
    uint8_t retry_count;             // 当前网络的重试次数
    uint8_t consecutive_failures;    // 当前网络的连续失败次数
    bool use_short_interval;         // 是否使用短间隔（10秒）
    
    // 新增回调相关字段
    wifi_station_event_callback_t event_callback;  // 事件回调函数
    void *user_ctx;                  // 用户上下文
} wifi_station_ctx_t;

static wifi_station_ctx_t s_wifi_ctx = {0};

// 前向声明
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void background_task(void *pvParameters);
static esp_err_t load_records_from_nvs(void);
static esp_err_t save_records_to_nvs(void);
static esp_err_t load_sequence_from_nvs(void);
static esp_err_t save_sequence_to_nvs(void);
static int find_best_network(wifi_ap_record_t *ap_list, uint16_t ap_count);
static void add_or_update_record_internal(const char *ssid, const char *password, bool ever_success);
static esp_err_t start_scan_internal(bool is_background_scan);

esp_err_t wifi_station_init(wifi_station_event_callback_t event_callback, void *user_ctx)
{
    if (s_wifi_ctx.initialized) {
        ESP_LOGW(TAG, "WiFi station already initialized");
        return ESP_OK;
    }

    memset(&s_wifi_ctx, 0, sizeof(s_wifi_ctx));

    // 初始化扫描相关字段
    s_wifi_ctx.scan_in_progress = false;
    s_wifi_ctx.scan_done = false;
    s_wifi_ctx.last_scan_ap_count = 0;
    s_wifi_ctx.last_scan_result = NULL;
    s_wifi_ctx.user_scan_requested = false;
    s_wifi_ctx.background_scan_requested = false;
    s_wifi_ctx.scan_start_time = 0;

    // 初始化重试机制字段
    memset(s_wifi_ctx.retry_target_ssid, 0, sizeof(s_wifi_ctx.retry_target_ssid));
    s_wifi_ctx.retry_count = 0;
    s_wifi_ctx.consecutive_failures = 0;
    s_wifi_ctx.use_short_interval = true;  // 默认使用短间隔

    // 设置回调函数
    s_wifi_ctx.event_callback = event_callback;
    s_wifi_ctx.user_ctx = user_ctx;

    // 创建互斥锁
    s_wifi_ctx.mutex = xSemaphoreCreateMutex();
    if (s_wifi_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // 创建事件组
    s_wifi_ctx.wifi_event_group = xEventGroupCreate();
    if (s_wifi_ctx.wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        vSemaphoreDelete(s_wifi_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    // 初始化网络接口
    ESP_ERROR_CHECK(esp_netif_init());
    s_wifi_ctx.netif = esp_netif_create_default_wifi_sta();

    // 创建默认WiFi配置
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // 注册事件处理器
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    // 设置WiFi模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    // 加载NVS数据
    load_sequence_from_nvs();
    load_records_from_nvs();

    s_wifi_ctx.state = WIFI_STATE_DISCONNECTED;
    s_wifi_ctx.auto_connect_enabled = true;  // 默认启用后台扫描
    s_wifi_ctx.auto_connect_one_shot = false;  // 默认不启用一次自动连接
    s_wifi_ctx.initialized = true;

    // 创建后台任务
    BaseType_t ret = xTaskCreate(background_task, "wifi_bg_task", 4096, NULL, 5, &s_wifi_ctx.background_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create background task");
        wifi_station_deinit();
        return ESP_ERR_NO_MEM;
    }

    // 设置1秒后开始扫描并连接
    s_wifi_ctx.next_scan_time = xTaskGetTickCount() + pdMS_TO_TICKS(1000);

    ESP_LOGI(TAG, "WiFi station initialized successfully");
    return ESP_OK;
}

esp_err_t wifi_station_deinit(void)
{
    if (!s_wifi_ctx.initialized) {
        return ESP_OK;
    }

    // 释放扫描结果
    if (s_wifi_ctx.last_scan_result) {
        free(s_wifi_ctx.last_scan_result);
        s_wifi_ctx.last_scan_result = NULL;
    }

    // 删除后台任务
    if (s_wifi_ctx.background_task_handle) {
        vTaskDelete(s_wifi_ctx.background_task_handle);
        s_wifi_ctx.background_task_handle = NULL;
    }

    // 断开WiFi连接
    esp_wifi_disconnect();
    esp_wifi_stop();

    // 注销事件处理器
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler);

    // 清理资源
    esp_wifi_deinit();
    if (s_wifi_ctx.netif) {
        esp_netif_destroy(s_wifi_ctx.netif);
    }

    if (s_wifi_ctx.wifi_event_group) {
        vEventGroupDelete(s_wifi_ctx.wifi_event_group);
    }

    if (s_wifi_ctx.mutex) {
        vSemaphoreDelete(s_wifi_ctx.mutex);
    }

    s_wifi_ctx.initialized = false;
    ESP_LOGI(TAG, "WiFi station deinitialized");
    return ESP_OK;
}

esp_err_t wifi_station_get_status(wifi_connection_status_t *status)
{
    if (!s_wifi_ctx.initialized || !status) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    memset(status, 0, sizeof(wifi_connection_status_t));
    status->state = s_wifi_ctx.state;

    if (s_wifi_ctx.state == WIFI_STATE_CONNECTING) {
        strcpy(status->ssid, s_wifi_ctx.current_ssid);
        memcpy(status->bssid, s_wifi_ctx.current_bssid, WIFI_STATION_BSSID_LEN);
        status->rssi = s_wifi_ctx.current_rssi;
    } else if (s_wifi_ctx.state == WIFI_STATE_CONNECTED) {
        strcpy(status->ssid, s_wifi_ctx.current_ssid);
        memcpy(status->bssid, s_wifi_ctx.current_bssid, WIFI_STATION_BSSID_LEN);
        status->rssi = s_wifi_ctx.current_rssi;
        
        // 获取IP信息
        esp_netif_ip_info_t ip_info;
        if (esp_netif_get_ip_info(s_wifi_ctx.netif, &ip_info) == ESP_OK) {
            status->ip_addr = ip_info.ip.addr;
            status->netmask = ip_info.netmask.addr;
            status->gateway = ip_info.gw.addr;
        }

        // 获取DNS信息
        esp_netif_dns_info_t dns_info;
        if (esp_netif_get_dns_info(s_wifi_ctx.netif, ESP_NETIF_DNS_MAIN, &dns_info) == ESP_OK) {
            status->dns1 = dns_info.ip.u_addr.ip4.addr;
            if (esp_netif_get_dns_info(s_wifi_ctx.netif, ESP_NETIF_DNS_BACKUP, &dns_info) == ESP_OK) {
                status->dns2 = dns_info.ip.u_addr.ip4.addr;
            }
        }

        // 计算连接时长
        if (s_wifi_ctx.connected_time > 0) {
            uint32_t current_time = esp_log_timestamp() / 1000;
            status->connected_time = current_time - s_wifi_ctx.connected_time;
        }
    }

    xSemaphoreGive(s_wifi_ctx.mutex);
    return ESP_OK;
}

esp_err_t wifi_station_scan_networks(wifi_network_info_t *networks, uint16_t *count)
{
    if (!s_wifi_ctx.initialized || !networks || !count || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t max_count = *count;
    *count = 0;

    // 开始扫描
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, true));

    // 获取扫描结果
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));

    if (ap_count == 0) {
        ESP_LOGW(TAG, "No WiFi networks found");
        return ESP_OK;
    }

    wifi_ap_record_t *ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!ap_list) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, ap_list));

    // 转换为组件格式
    uint16_t output_count = (ap_count < max_count) ? ap_count : max_count;
    for (uint16_t i = 0; i < output_count; i++) {
        strncpy(networks[i].ssid, (char*)ap_list[i].ssid, WIFI_STATION_SSID_LEN - 1);
        networks[i].ssid[WIFI_STATION_SSID_LEN - 1] = '\0';
        memcpy(networks[i].bssid, ap_list[i].bssid, WIFI_STATION_BSSID_LEN);
        networks[i].rssi = ap_list[i].rssi;
    }

    *count = output_count;
    free(ap_list);

    ESP_LOGI(TAG, "Scanned %d WiFi networks", *count);
    return ESP_OK;
}

esp_err_t wifi_station_disconnect(void)
{
    if (!s_wifi_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    s_wifi_ctx.state = WIFI_STATE_DISCONNECTED;
    s_wifi_ctx.connected_time = 0;
    
    // 设置当前SSID的user_disconnected标记
    if (s_wifi_ctx.current_ssid[0] != '\0') {
        for (int i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
            if (s_wifi_ctx.records[i].valid && 
                strcmp(s_wifi_ctx.records[i].ssid, s_wifi_ctx.current_ssid) == 0) {
                s_wifi_ctx.records[i].user_disconnected = true;
                ESP_LOGI(TAG, "Marked SSID %s as user disconnected", s_wifi_ctx.current_ssid);
                break;
            }
        }
    }
    
    memset(s_wifi_ctx.current_ssid, 0, sizeof(s_wifi_ctx.current_ssid));
    xSemaphoreGive(s_wifi_ctx.mutex);

    esp_err_t ret = esp_wifi_disconnect();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "WiFi disconnected");
    }

    return ret;
}

esp_err_t wifi_station_connect(const char *ssid, const char *password)
{
    if (!s_wifi_ctx.initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(ssid) >= WIFI_STATION_SSID_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (password && strlen(password) >= WIFI_STATION_PASSWORD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    // 如果WIFI已连接,则先断开连接 
    if (s_wifi_ctx.state == WIFI_STATE_CONNECTED) {
        ESP_LOGD(TAG, "WiFi is connected, disconnect first");
        esp_wifi_disconnect();
    }

    // 配置WiFi连接参数
    wifi_config_t wifi_config = {0};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    if (password) {
        strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    }
    // 设置为WIFI_AUTH_OPEN以允许自动检测认证模式
    wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    ESP_LOGD(TAG, "Started WiFi connection, ssid: %s, password: %s, authmode: %d", ssid, password ? password : "", wifi_config.sta.threshold.authmode);

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    
    // 清除user_disconnected标记
    for (int i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
        if (s_wifi_ctx.records[i].valid && 
            strcmp(s_wifi_ctx.records[i].ssid, ssid) == 0) {
            if (s_wifi_ctx.records[i].user_disconnected) {
                s_wifi_ctx.records[i].user_disconnected = false;
                ESP_LOGI(TAG, "Cleared user disconnected flag for SSID %s", ssid);
            }
            break;
        }
    }
    
    s_wifi_ctx.state = WIFI_STATE_CONNECTING;
    s_wifi_ctx.connect_start_time = esp_log_timestamp() / 1000;
    strcpy(s_wifi_ctx.current_ssid, ssid);
    xSemaphoreGive(s_wifi_ctx.mutex);

    // 清除事件位
    xEventGroupClearBits(s_wifi_ctx.wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    // 等待连接结果
    EventBits_t bits = xEventGroupWaitBits(s_wifi_ctx.wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi: %s", ssid);
        // 自动添加或更新连接记录
        add_or_update_record_internal(ssid, password ? password : "", true);
        return ESP_OK;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi: %s", ssid);
        xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
        s_wifi_ctx.state = WIFI_STATE_DISCONNECTED;
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout: %s", ssid);
        esp_wifi_disconnect();
        xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
        s_wifi_ctx.state = WIFI_STATE_DISCONNECTED;
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ESP_ERR_TIMEOUT;
    }
}

esp_err_t wifi_station_get_records(wifi_connection_record_t *records, uint8_t *count)
{
    if (!s_wifi_ctx.initialized || !records || !count || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int return_limit = *count;
    if (return_limit > s_wifi_ctx.record_count) {
        return_limit = s_wifi_ctx.record_count;
    }

    int return_index = 0;

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    for (int i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
        if (s_wifi_ctx.records[i].valid && (return_index < return_limit)) {
            records[return_index] = s_wifi_ctx.records[i];
            return_index++;
        }
    }
    *count = return_index;
    xSemaphoreGive(s_wifi_ctx.mutex);

    return ESP_OK;
}

esp_err_t wifi_station_delete_record(const char *ssid)
{
    if (!s_wifi_ctx.initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);

    bool found = false;

    for (int i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
        if (s_wifi_ctx.records[i].valid && strcmp(s_wifi_ctx.records[i].ssid, ssid) == 0) {
            memset(&s_wifi_ctx.records[i], 0, sizeof(wifi_connection_record_t));
            s_wifi_ctx.record_count--;
            found = true;
            break;
        }
    }

    if (found) {
        ESP_LOGI(TAG, "Deleted WiFi record: %s", ssid);
        save_records_to_nvs();
    } else {
        ESP_LOGW(TAG, "Failed to delete WiFi record: %s", ssid);
    }

    xSemaphoreGive(s_wifi_ctx.mutex);

    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t wifi_station_add_record(const char *ssid, const char *password)
{
    if (!s_wifi_ctx.initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(ssid) >= WIFI_STATION_SSID_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    if (password && strlen(password) >= WIFI_STATION_PASSWORD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    add_or_update_record_internal(ssid, password ? password : "", false);
    xSemaphoreGive(s_wifi_ctx.mutex);

    return ESP_OK;
}

// 静态函数实现

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi station started");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        wifi_event_sta_scan_done_t *event = (wifi_event_sta_scan_done_t *)event_data;
        
        xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
        s_wifi_ctx.scan_in_progress = false;
        
        if (event->status == 0) {
            // 扫描成功
            uint16_t ap_count = 0;
            ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
            
            // 释放之前的扫描结果
            if (s_wifi_ctx.last_scan_result) {
                free(s_wifi_ctx.last_scan_result);
                s_wifi_ctx.last_scan_result = NULL;
            }
            
            if (ap_count > 0) {
                s_wifi_ctx.last_scan_result = malloc(sizeof(wifi_ap_record_t) * ap_count);
                if (s_wifi_ctx.last_scan_result) {
                    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_count, s_wifi_ctx.last_scan_result));
                    s_wifi_ctx.last_scan_ap_count = ap_count;
                    s_wifi_ctx.scan_done = true;

                    // 根据RSSI信号强度排序（从强到弱）
                    if (ap_count > 1) {
                        qsort(s_wifi_ctx.last_scan_result, ap_count, sizeof(wifi_ap_record_t), compare_ap_by_rssi);
                        ESP_LOGI(TAG, "Sorted %d APs by RSSI signal strength", ap_count);
                    }
                    
                    const char* scan_type = "";
                    if (s_wifi_ctx.user_scan_requested && s_wifi_ctx.background_scan_requested) {
                        scan_type = "user+background";
                    } else if (s_wifi_ctx.user_scan_requested) {
                        scan_type = "user";
                    } else if (s_wifi_ctx.background_scan_requested) {
                        scan_type = "background";
                    }
                    
                    ESP_LOGI(TAG, "Async scan (%s) completed, found %d APs", scan_type, ap_count);
                } else {
                    ESP_LOGE(TAG, "Failed to allocate memory for scan results");
                    s_wifi_ctx.scan_done = false;
                }
            } else {
                s_wifi_ctx.scan_done = true;
                s_wifi_ctx.last_scan_ap_count = 0;
                ESP_LOGI(TAG, "Async scan completed, no APs found");
            }
        } else {
            ESP_LOGW(TAG, "Scan failed with status %d", event->status);
            s_wifi_ctx.scan_done = false;
        }
        
        // 重置扫描请求标志
        s_wifi_ctx.user_scan_requested = false;
        s_wifi_ctx.background_scan_requested = false;
        xSemaphoreGive(s_wifi_ctx.mutex);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
        ESP_LOGI(TAG, "Disconnected from WiFi SSID:%s, reason:%d", event->ssid, event->reason);
        
        xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
        s_wifi_ctx.state = WIFI_STATE_DISCONNECTED;
        s_wifi_ctx.connected_time = 0;
        
        // 调用断开连接回调
        if (s_wifi_ctx.event_callback) {
            wifi_connection_status_t status = {0};
            status.state = s_wifi_ctx.state;
            strncpy(status.ssid, s_wifi_ctx.current_ssid, sizeof(status.ssid) - 1);
            s_wifi_ctx.event_callback(WIFI_EVENT_DISCONNECTED, &status, s_wifi_ctx.user_ctx);
        }
        
        xSemaphoreGive(s_wifi_ctx.mutex);
        
        xEventGroupSetBits(s_wifi_ctx.wifi_event_group, WIFI_FAIL_BIT);

        // 断开后重连, 5秒后开始扫描, 防止过度扫描
        s_wifi_ctx.next_scan_time = xTaskGetTickCount() + pdMS_TO_TICKS(5000);

    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*) event_data;
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s", event->ssid);
        
        xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
        s_wifi_ctx.state = WIFI_STATE_CONNECTED;
        memcpy(s_wifi_ctx.current_bssid, event->bssid, WIFI_STATION_BSSID_LEN);
        s_wifi_ctx.connected_time = esp_log_timestamp() / 1000;
        
        // 重置重试机制状态
        memset(s_wifi_ctx.retry_target_ssid, 0, sizeof(s_wifi_ctx.retry_target_ssid));
        s_wifi_ctx.retry_count = 0;
        s_wifi_ctx.consecutive_failures = 0;
        s_wifi_ctx.use_short_interval = true;
        
        // 获取RSSI
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_wifi_ctx.current_rssi = ap_info.rssi;
        }

        // 更新连接记录的valid标志
        for (uint8_t i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
            if (s_wifi_ctx.records[i].ssid[0] != '\0' && 
                strcmp(s_wifi_ctx.records[i].ssid, (char*)event->ssid) == 0) {
                if (!s_wifi_ctx.records[i].valid) {
                    s_wifi_ctx.records[i].valid = true;  // 标记为曾经连接成功
                    save_records_to_nvs();  // 保存更新
                }
                break;
            }
        }
        
        // 调用连接成功回调
        if (s_wifi_ctx.event_callback) {
            wifi_connection_status_t status = {0};
            status.state = s_wifi_ctx.state;
            strncpy(status.ssid, s_wifi_ctx.current_ssid, sizeof(status.ssid) - 1);
            memcpy(status.bssid, s_wifi_ctx.current_bssid, sizeof(status.bssid));
            status.rssi = s_wifi_ctx.current_rssi;
            s_wifi_ctx.event_callback(WIFI_EVENT_CONNECTED, &status, s_wifi_ctx.user_ctx);
        }
        
        xSemaphoreGive(s_wifi_ctx.mutex);
        
        xEventGroupSetBits(s_wifi_ctx.wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        
        // 调用获取IP回调
        if (s_wifi_ctx.event_callback) {
            wifi_connection_status_t status = {0};
            xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
            status.state = s_wifi_ctx.state;
            strncpy(status.ssid, s_wifi_ctx.current_ssid, sizeof(status.ssid) - 1);
            memcpy(status.bssid, s_wifi_ctx.current_bssid, sizeof(status.bssid));
            status.rssi = s_wifi_ctx.current_rssi;
            status.ip_addr = event->ip_info.ip.addr;
            status.netmask = event->ip_info.netmask.addr;
            status.gateway = event->ip_info.gw.addr;
            xSemaphoreGive(s_wifi_ctx.mutex);
            
            s_wifi_ctx.event_callback(WIFI_EVENT_GOT_IP, &status, s_wifi_ctx.user_ctx);
        }
    }
}

static void background_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Background WiFi task started");
    
    const TickType_t short_scan_interval = pdMS_TO_TICKS(10000); // 10秒短间隔
    const TickType_t long_scan_interval = pdMS_TO_TICKS(30000);  // 30秒长间隔

    while (1) {
        TickType_t current_time = xTaskGetTickCount();
        
        // 如果后台扫描启用且未连接且距离上次扫描超过间隔时间
        if ((s_wifi_ctx.auto_connect_one_shot) || (s_wifi_ctx.auto_connect_enabled && 
            s_wifi_ctx.state == WIFI_STATE_DISCONNECTED && 
            current_time > s_wifi_ctx.next_scan_time)) {
            
            // 确定使用的扫描间隔
            TickType_t scan_interval = s_wifi_ctx.use_short_interval ? short_scan_interval : long_scan_interval;
            
            ESP_LOGI(TAG, "Background scan for auto-connect(%s), interval: %ds", 
                    s_wifi_ctx.auto_connect_one_shot ? "one-shot" : "enabled",
                    (int)(scan_interval / pdMS_TO_TICKS(1000)));
            
            // 自动连接一次后, 自动连接标志位清零
            s_wifi_ctx.auto_connect_one_shot = false;

            // 执行WiFi扫描 - 使用统一的扫描协调机制
            if (start_scan_internal(true) == ESP_OK) {  // true表示后台扫描
                // 等待扫描完成，最多等待10秒
                const TickType_t max_wait = pdMS_TO_TICKS(10000);
                TickType_t wait_start = xTaskGetTickCount();
                
                while ((xTaskGetTickCount() - wait_start) < max_wait) {
                    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
                    bool scan_done = s_wifi_ctx.scan_done;
                    bool scan_in_progress = s_wifi_ctx.scan_in_progress;
                    xSemaphoreGive(s_wifi_ctx.mutex);
                    
                    if (scan_done && !scan_in_progress) {
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100)); // 100ms检查一次
                }
                
                // 处理扫描结果
                xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
                if (s_wifi_ctx.scan_done && s_wifi_ctx.last_scan_result && s_wifi_ctx.last_scan_ap_count > 0) {
                    ESP_LOGD(TAG, "Background scan found %d WiFi networks", s_wifi_ctx.last_scan_ap_count);
                    
                    // 查找最佳网络进行连接
                    int best_index = find_best_network(s_wifi_ctx.last_scan_result, s_wifi_ctx.last_scan_ap_count);
                    if (best_index >= 0) {
                        // 查找对应的连接记录
                        uint8_t i;
                        for (i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
                            if (s_wifi_ctx.records[i].valid && 
                                strcmp(s_wifi_ctx.records[i].ssid, (char*)s_wifi_ctx.last_scan_result[best_index].ssid) == 0) {
                                
                                char *target_ssid = (char*)s_wifi_ctx.last_scan_result[best_index].ssid;
                                
                                // 检查是否是同一个网络的重试
                                bool is_same_network = (strlen(s_wifi_ctx.retry_target_ssid) > 0 && 
                                                       strcmp(s_wifi_ctx.retry_target_ssid, target_ssid) == 0);
                                
                                if (!is_same_network) {
                                    // 新网络，重置重试计数
                                    strncpy(s_wifi_ctx.retry_target_ssid, target_ssid, sizeof(s_wifi_ctx.retry_target_ssid) - 1);
                                    s_wifi_ctx.retry_target_ssid[sizeof(s_wifi_ctx.retry_target_ssid) - 1] = '\0';
                                    s_wifi_ctx.retry_count = 0;
                                    s_wifi_ctx.consecutive_failures = 0;
                                    s_wifi_ctx.use_short_interval = true;
                                }
                                
                                // 检查重试次数
                                if (s_wifi_ctx.retry_count < 3) {
                                    s_wifi_ctx.retry_count++;
                                    
                                    ESP_LOGI(TAG, "Auto-connecting to: %s (attempt %d/3)", 
                                            s_wifi_ctx.records[i].ssid, s_wifi_ctx.retry_count);
                                    
                                    // 复制连接信息，避免在释放锁后访问
                                    char ssid_copy[WIFI_STATION_SSID_LEN];
                                    char password_copy[WIFI_STATION_PASSWORD_LEN];
                                    strncpy(ssid_copy, s_wifi_ctx.records[i].ssid, sizeof(ssid_copy) - 1);
                                    strncpy(password_copy, s_wifi_ctx.records[i].password, sizeof(password_copy) - 1);
                                    ssid_copy[sizeof(ssid_copy) - 1] = '\0';
                                    password_copy[sizeof(password_copy) - 1] = '\0';
                                    
                                    xSemaphoreGive(s_wifi_ctx.mutex);
                                    
                                    // 尝试连接
                                    esp_err_t connect_result = wifi_station_connect(ssid_copy, password_copy);
                                    
                                    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
                                    if (connect_result == ESP_OK) {
                                        // 连接成功，重置重试状态
                                        memset(s_wifi_ctx.retry_target_ssid, 0, sizeof(s_wifi_ctx.retry_target_ssid));
                                        s_wifi_ctx.retry_count = 0;
                                        s_wifi_ctx.consecutive_failures = 0;
                                        s_wifi_ctx.use_short_interval = true;
                                    } else {
                                        // 连接失败，增加失败计数
                                        s_wifi_ctx.consecutive_failures++;
                                        
                                        // 如果已经尝试3次都失败，标记网络为不可用
                                        if (s_wifi_ctx.retry_count >= 3) {
                                            ESP_LOGW(TAG, "Network %s failed 3 times, marking as unavailable", target_ssid);
                                            s_wifi_ctx.records[i].ever_success = false;
                                            save_records_to_nvs();
                                            
                                            // 重置重试状态，切换到长间隔
                                            memset(s_wifi_ctx.retry_target_ssid, 0, sizeof(s_wifi_ctx.retry_target_ssid));
                                            s_wifi_ctx.retry_count = 0;
                                            s_wifi_ctx.consecutive_failures = 0;
                                            s_wifi_ctx.use_short_interval = false;
                                        }
                                    }
                                    xSemaphoreGive(s_wifi_ctx.mutex);
                                } else {
                                    // 已经重试3次，跳过此网络
                                    ESP_LOGD(TAG, "Network %s already tried 3 times, skipping", target_ssid);
                                    xSemaphoreGive(s_wifi_ctx.mutex);
                                }
                                break;
                            }
                        }
                        if (i == WIFI_STATION_MAX_RECORDS) {
                            xSemaphoreGive(s_wifi_ctx.mutex);
                        }
                    } else {
                        xSemaphoreGive(s_wifi_ctx.mutex);
                        ESP_LOGW(TAG, "No suitable network found, try in %ds", 
                                (int)(scan_interval / pdMS_TO_TICKS(1000)));
                    }
                } else {
                    xSemaphoreGive(s_wifi_ctx.mutex);
                    ESP_LOGW(TAG, "Background scan failed or no results");
                }
            }
            
            // 设置下次扫描时间
            TickType_t next_interval = s_wifi_ctx.use_short_interval ? short_scan_interval : long_scan_interval;
            s_wifi_ctx.next_scan_time = current_time + next_interval;
        }
        
        vTaskDelay(pdMS_TO_TICKS(500)); // 500ms检查一次
    }
}

static esp_err_t load_records_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    // 清空记录
    memset(s_wifi_ctx.records, 0, sizeof(s_wifi_ctx.records));
    s_wifi_ctx.record_count = 0;

    char key_ssid[16];
    char key_passwd[16];
    char key_record[16];
    char record_str[32];
    size_t str_size;

    // 遍历所有可能的记录ID
    for (uint16_t id = 0; id < WIFI_STATION_MAX_RECORDS; id++) {
        // 构造key名称
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%u", id);
        snprintf(key_passwd, sizeof(key_passwd), "passwd_%u", id);
        snprintf(key_record, sizeof(key_record), "record_%u", id);

        // 读取SSID
        str_size = sizeof(s_wifi_ctx.records[id].ssid);
        err = nvs_get_str(nvs_handle, key_ssid, s_wifi_ctx.records[id].ssid, &str_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;  // 该ID没有记录
        }
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read SSID for id %u: %s", id, esp_err_to_name(err));
            continue;
        }

        // 读取密码
        str_size = sizeof(s_wifi_ctx.records[id].password);
        err = nvs_get_str(nvs_handle, key_passwd, s_wifi_ctx.records[id].password, &str_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read password for id %u: %s", id, esp_err_to_name(err));
            continue;
        }

        // 读取记录状态
        str_size = sizeof(record_str);
        err = nvs_get_str(nvs_handle, key_record, record_str, &str_size);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to read record for id %u: %s", id, esp_err_to_name(err));
            continue;
        }
        
        uint32_t sequence;
        int ever_success;
        if (sscanf(record_str, "%d;%" PRIu32, &ever_success, &sequence) != 2) {
            ESP_LOGW(TAG, "Invalid record format for id %u", id);
            continue;
        }
        // 设置记录
        s_wifi_ctx.records[id].id = id;
        s_wifi_ctx.records[id].ever_success = (bool)ever_success;
        s_wifi_ctx.records[id].sequence = sequence;
        s_wifi_ctx.records[id].valid = true;
        s_wifi_ctx.record_count++;

        ESP_LOGD(TAG, "nvs record-%u: ssid: \"%s\", password: \"%s\", ever_success: %d, sequence: %" PRIu32, 
                 id, s_wifi_ctx.records[id].ssid, s_wifi_ctx.records[id].password, s_wifi_ctx.records[id].ever_success, s_wifi_ctx.records[id].sequence);
    }

    ESP_LOGI(TAG, "Loaded %d WiFi records from NVS", s_wifi_ctx.record_count);
    nvs_close(nvs_handle);
    return ESP_OK;
}

static esp_err_t save_records_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace: %s", esp_err_to_name(err));
        return err;
    }

    char key_ssid[16];
    char key_passwd[16];
    char key_record[16];
    char record_str[32];

    // 遍历所有记录
    for (uint16_t id = 0; id < WIFI_STATION_MAX_RECORDS; id++) {
        // 构造key名称
        snprintf(key_ssid, sizeof(key_ssid), "ssid_%u", id);
        snprintf(key_passwd, sizeof(key_passwd), "passwd_%u", id);
        snprintf(key_record, sizeof(key_record), "record_%u", id);

        if (s_wifi_ctx.records[id].valid) {  // 有效记录
            // 保存SSID
            err = nvs_set_str(nvs_handle, key_ssid, s_wifi_ctx.records[id].ssid);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save SSID for id %u: %s", id, esp_err_to_name(err));
                continue;
            }

            // 保存密码
            err = nvs_set_str(nvs_handle, key_passwd, s_wifi_ctx.records[id].password);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save password for id %u: %s", id, esp_err_to_name(err));
                continue;
            }

            // 保存记录状态
            snprintf(record_str, sizeof(record_str), "%d;%" PRIu32, 
                    (int)s_wifi_ctx.records[id].ever_success, s_wifi_ctx.records[id].sequence);
            err = nvs_set_str(nvs_handle, key_record, record_str);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to save record for id %u: %s", id, esp_err_to_name(err));
                continue;
            }
        } else {  // 空记录,删除相关key
            nvs_erase_key(nvs_handle, key_ssid);
            nvs_erase_key(nvs_handle, key_passwd);
            nvs_erase_key(nvs_handle, key_record);
        }
    }

    err = nvs_commit(nvs_handle);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Saved WiFi records to NVS");
    } else {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(err));
    }

    nvs_close(nvs_handle);
    return err;
}

static esp_err_t load_sequence_from_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        s_wifi_ctx.current_sequence = 1;
        return err;
    }

    size_t required_size = sizeof(s_wifi_ctx.current_sequence);
    err = nvs_get_blob(nvs_handle, NVS_SEQUENCE_KEY, &s_wifi_ctx.current_sequence, &required_size);
    if (err != ESP_OK) {
        s_wifi_ctx.current_sequence = 1;
    }

    ESP_LOGI(TAG, "nvs sequence: %" PRIu32, s_wifi_ctx.current_sequence);

    nvs_close(nvs_handle);
    return ESP_OK;
}

static esp_err_t save_sequence_to_nvs(void)
{
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(nvs_handle, NVS_SEQUENCE_KEY, &s_wifi_ctx.current_sequence, sizeof(s_wifi_ctx.current_sequence));
    if (err == ESP_OK) {
        err = nvs_commit(nvs_handle);
    }

    nvs_close(nvs_handle);
    return err;
}

static int find_best_network(wifi_ap_record_t *ap_list, uint16_t ap_count)
{
    int best_index = -1;
    int8_t best_rssi = -127;
    uint32_t best_sequence = 0;
    bool best_has_connected = false;

    // 如果没有连接记录,则直接返回-1
    if (s_wifi_ctx.record_count == 0) {
        ESP_LOGD(TAG, "No WiFi records found");
        return -1;
    }

    for (uint16_t i = 0; i < ap_count; i++) {
        // 查找是否有对应的连接记录
        for (uint8_t j = 0; j < WIFI_STATION_MAX_RECORDS; j++) {
            if (s_wifi_ctx.records[j].valid && 
                strcmp(s_wifi_ctx.records[j].ssid, (char*)ap_list[i].ssid) == 0) {
                
                // 跳过被用户主动断开的网络
                if (s_wifi_ctx.records[j].user_disconnected) {
                    ESP_LOGD(TAG, "Skip user disconnected network: %s", s_wifi_ctx.records[j].ssid);
                    break;
                }
                
                bool current_has_connected = s_wifi_ctx.records[j].ever_success;
                
                // 优先选择曾经连接成功过的网络
                if (!best_has_connected && current_has_connected) {
                    best_index = i;
                    best_rssi = ap_list[i].rssi;
                    best_sequence = s_wifi_ctx.records[j].sequence;
                    best_has_connected = true;

                    ESP_LOGD(TAG, "hit network: %s (rssi: %d, ever_success: %d)", 
                             ap_list[i].ssid, best_rssi, best_has_connected);
                } 
                // 如果都是连接成功过的或都是未连接过的,则比较信号强度和序号
                else if (best_has_connected == current_has_connected) {
                    if (ap_list[i].rssi > best_rssi || 
                        (ap_list[i].rssi == best_rssi && s_wifi_ctx.records[j].sequence > best_sequence)) {
                        best_index = i;
                        best_rssi = ap_list[i].rssi;
                        best_sequence = s_wifi_ctx.records[j].sequence;
                        best_has_connected = current_has_connected;

                        ESP_LOGD(TAG, "hit network: %s (rssi: %d, ever_success: %d)", 
                                 ap_list[i].ssid, best_rssi, best_has_connected);
                    }
                }
                break;
            }
        }
    }

    if (best_index >= 0) {
        ESP_LOGI(TAG, "Selected network: %s (RSSI: %d)", 
                 ap_list[best_index].ssid, best_rssi);
    } 
    return best_index;
}

static void add_or_update_record_internal(const char *ssid, const char *password, bool ever_success)
{
    // 查找是否已存在该SSID的记录
    int existing_index = -1;
    for (uint8_t i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
        if (s_wifi_ctx.records[i].valid && strcmp(s_wifi_ctx.records[i].ssid, ssid) == 0) {
            existing_index = i;
            break;
        }
    }

    if (existing_index >= 0) {
        // 更新现有记录
        strncpy(s_wifi_ctx.records[existing_index].password, password, WIFI_STATION_PASSWORD_LEN - 1);
        s_wifi_ctx.records[existing_index].password[WIFI_STATION_PASSWORD_LEN - 1] = '\0';
        s_wifi_ctx.records[existing_index].sequence = ++s_wifi_ctx.current_sequence;
        s_wifi_ctx.records[existing_index].ever_success = ever_success;
        // 不修改user_disconnected标记，保持其当前状态
        ESP_LOGI(TAG, "Updated WiFi record: %s", ssid);
    } else {
        // 添加新记录
        int free_index = -1;
        
        // 查找空闲位置
        for (uint8_t i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
            if (!s_wifi_ctx.records[i].valid) {
                free_index = i;
                break;
            }
        }
        
        // 如果没有空闲位置，删除序号最小的记录
        if (free_index < 0) {
            uint32_t min_sequence = UINT32_MAX;
            int min_index = 0;
            
            for (uint8_t i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
                if (s_wifi_ctx.records[i].valid && s_wifi_ctx.records[i].sequence < min_sequence) {
                    min_sequence = s_wifi_ctx.records[i].sequence;
                    min_index = i;
                }
            }
            
            free_index = min_index;
            s_wifi_ctx.record_count--;
            ESP_LOGI(TAG, "Removed old WiFi record: %s", s_wifi_ctx.records[free_index].ssid);
        }
        
        // 添加新记录
        memset(&s_wifi_ctx.records[free_index], 0, sizeof(wifi_connection_record_t));
        strncpy(s_wifi_ctx.records[free_index].ssid, ssid, WIFI_STATION_SSID_LEN - 1);
        strncpy(s_wifi_ctx.records[free_index].password, password, WIFI_STATION_PASSWORD_LEN - 1);
        s_wifi_ctx.records[free_index].sequence = ++s_wifi_ctx.current_sequence;
        s_wifi_ctx.records[free_index].ever_success = ever_success;
        s_wifi_ctx.records[free_index].user_disconnected = false;  // 新记录初始化为false
        s_wifi_ctx.records[free_index].valid = true;
        s_wifi_ctx.record_count++;
        
        ESP_LOGI(TAG, "Added new WiFi record: %s", ssid);
    }
    
    save_records_to_nvs();
    save_sequence_to_nvs();
}

esp_err_t wifi_station_set_auto_connect(bool enable)
{
    if (!s_wifi_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    s_wifi_ctx.auto_connect_enabled = enable;
    xSemaphoreGive(s_wifi_ctx.mutex);

    ESP_LOGI(TAG, "Auto connect %s", enable ? "enabled" : "disabled");
    return ESP_OK;
} 

void wifi_station_try_auto_connect_once(void)
{
    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    s_wifi_ctx.auto_connect_one_shot = true;
    xSemaphoreGive(s_wifi_ctx.mutex);
}

esp_err_t wifi_station_start_scan_async(void)
{
    if (!s_wifi_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return start_scan_internal(false);  // false表示用户扫描
}

esp_err_t wifi_station_get_scan_result(wifi_network_info_t *networks, uint16_t *count)
{
    if (!s_wifi_ctx.initialized || !networks || !count || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    
    if (s_wifi_ctx.scan_in_progress) {
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_wifi_ctx.scan_done || !s_wifi_ctx.last_scan_result) {
        xSemaphoreGive(s_wifi_ctx.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Get scan result, found: %d, max return: %d", s_wifi_ctx.last_scan_ap_count, *count);

    uint16_t output_count = (*count < s_wifi_ctx.last_scan_ap_count) ? *count : s_wifi_ctx.last_scan_ap_count;
    
    // 转换为组件格式
    for (uint16_t i = 0; i < output_count; i++) {
        strncpy(networks[i].ssid, (char*)s_wifi_ctx.last_scan_result[i].ssid, WIFI_STATION_SSID_LEN - 1);
        networks[i].ssid[WIFI_STATION_SSID_LEN - 1] = '\0';
        memcpy(networks[i].bssid, s_wifi_ctx.last_scan_result[i].bssid, WIFI_STATION_BSSID_LEN);
        networks[i].rssi = s_wifi_ctx.last_scan_result[i].rssi;
    }

    *count = output_count;
    xSemaphoreGive(s_wifi_ctx.mutex);

    return ESP_OK;
}

bool wifi_station_is_scan_done(void)
{
    if (!s_wifi_ctx.initialized) {
        return false;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    bool scan_done = s_wifi_ctx.scan_done && !s_wifi_ctx.scan_in_progress;
    xSemaphoreGive(s_wifi_ctx.mutex);

    return scan_done;
}

static esp_err_t start_scan_internal(bool is_background_scan)
{
    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    
    // 如果已经有扫描在进行中
    if (s_wifi_ctx.scan_in_progress) {
        if (is_background_scan) {
            // 后台扫描请求，标记并共用结果
            s_wifi_ctx.background_scan_requested = true;
            xSemaphoreGive(s_wifi_ctx.mutex);
            ESP_LOGI(TAG, "Background scan request queued, will share result");
            return ESP_OK;
        } else {
            // 用户扫描请求，标记并共用结果
            s_wifi_ctx.user_scan_requested = true;
            xSemaphoreGive(s_wifi_ctx.mutex);
            ESP_LOGI(TAG, "User scan request queued, will share result");
            return ESP_OK;
        }
    }

    // 配置扫描参数
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    s_wifi_ctx.scan_in_progress = true;
    s_wifi_ctx.scan_done = false;
    s_wifi_ctx.scan_start_time = xTaskGetTickCount();
    
    if (is_background_scan) {
        s_wifi_ctx.background_scan_requested = true;
    } else {
        s_wifi_ctx.user_scan_requested = true;
    }
    
    xSemaphoreGive(s_wifi_ctx.mutex);

    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);  // 统一使用异步扫描
    if (ret != ESP_OK) {
        xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
        s_wifi_ctx.scan_in_progress = false;
        if (is_background_scan) {
            s_wifi_ctx.background_scan_requested = false;
        } else {
            s_wifi_ctx.user_scan_requested = false;
        }
        xSemaphoreGive(s_wifi_ctx.mutex);
        ESP_LOGE(TAG, "Failed to start %s scan: %s", 
                 is_background_scan ? "background" : "user", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Started %s scan", is_background_scan ? "background" : "user");
    }

    return ret;
}

esp_err_t wifi_station_scan_networks_async(wifi_network_info_t *networks, uint16_t *count, int timeout_ms)
{
    if (!s_wifi_ctx.initialized || !networks || !count || *count == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // 启动异步扫描
    esp_err_t ret = start_scan_internal(false);  // false表示用户扫描
    if (ret != ESP_OK) {
        return ret;
    }

    // 计算超时时间
    TickType_t start_time = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    
    // 等待扫描完成或超时
    while (!wifi_station_is_scan_done()) {
        if ((xTaskGetTickCount() - start_time) >= timeout_ticks) {
            ESP_LOGW(TAG, "Scan timeout after %d ms", timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // 100ms检查一次
    }

    // 获取扫描结果
    return wifi_station_get_scan_result(networks, count);
}

esp_err_t wifi_station_reset_network_status(const char *ssid)
{
    if (!s_wifi_ctx.initialized || !ssid) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_wifi_ctx.mutex, portMAX_DELAY);
    
    bool found = false;
    for (uint8_t i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
        if (s_wifi_ctx.records[i].valid && 
            strcmp(s_wifi_ctx.records[i].ssid, ssid) == 0) {
            
            // 重置网络状态
            s_wifi_ctx.records[i].ever_success = true;
            s_wifi_ctx.records[i].user_disconnected = false;
            
            // 如果这是当前重试的网络，也重置重试状态
            if (strcmp(s_wifi_ctx.retry_target_ssid, ssid) == 0) {
                memset(s_wifi_ctx.retry_target_ssid, 0, sizeof(s_wifi_ctx.retry_target_ssid));
                s_wifi_ctx.retry_count = 0;
                s_wifi_ctx.consecutive_failures = 0;
                s_wifi_ctx.use_short_interval = true;
            }
            
            found = true;
            ESP_LOGI(TAG, "Reset network status for: %s", ssid);
            break;
        }
    }
    
    if (found) {
        save_records_to_nvs();
    }
    
    xSemaphoreGive(s_wifi_ctx.mutex);
    
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}