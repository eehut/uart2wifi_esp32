/**
 * @file app_event_loop.c
 * @author Samuel (179712066@qq.com)
 * @brief 应用事件循环组件实现
 * @version 0.1
 * @date 2025-05-11
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "app_event_loop.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "app-event";
static esp_event_loop_handle_t s_event_loop = NULL;

/**
 * @brief 初始化应用事件循环
 * 
 * @param queue_size 事件队列大小
 * @param task_priority 事件处理任务优先级
 * @return esp_err_t 
 */
esp_err_t app_event_loop_init(int queue_size, int task_priority)
{
    if (s_event_loop != NULL) {
        ESP_LOGW(TAG, "app_event_loop already initialized");
        return ESP_OK; // 已经初始化过了
    }

    esp_event_loop_args_t loop_args = {
        .queue_size = queue_size,
        .task_name = "app_events",
        .task_priority = task_priority,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY
    };

    esp_err_t ret = esp_event_loop_create(&loop_args, &s_event_loop);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "create event loop failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "app_event_loop initialized, queue size: %d", queue_size);
    return ESP_OK;
}

/**
 * @brief 注册事件处理器
 */
esp_err_t app_event_handler_register(esp_event_base_t event_base,
                                    int32_t event_id,
                                    esp_event_handler_t event_handler,
                                    void *event_handler_arg)
{
    if (s_event_loop == NULL) {
        ESP_LOGE(TAG, "event loop not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_event_handler_register_with(s_event_loop,
                                                  event_base,
                                                  event_id,
                                                  event_handler,
                                                  event_handler_arg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "register event handler failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief 注销事件处理器
 */
esp_err_t app_event_handler_unregister(esp_event_base_t event_base,
                                      int32_t event_id,
                                      esp_event_handler_t event_handler)
{
    if (s_event_loop == NULL) {
        ESP_LOGE(TAG, "event loop not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_event_handler_unregister_with(s_event_loop,
                                                    event_base,
                                                    event_id,
                                                    event_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "unregister event handler failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

/**
 * @brief 发送事件
 */
esp_err_t app_event_post(esp_event_base_t event_base,
                        int32_t event_id,
                        void *event_data,
                        size_t event_data_size,
                        TickType_t ticks_to_wait)
{
    if (s_event_loop == NULL) {
        ESP_LOGE(TAG, "event loop not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = esp_event_post_to(s_event_loop,
                                     event_base,
                                     event_id,
                                     event_data,
                                     event_data_size,
                                     ticks_to_wait);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "post event failed: %s", esp_err_to_name(ret));
    }
    return ret;
} 
