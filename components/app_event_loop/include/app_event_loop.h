/**
 * @file app_event_loop.h
 * @brief 通用事件循环组件接口
 */
#pragma once

#include "esp_err.h"
#include "esp_event.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化应用事件循环
 * 
 * @param queue_size 事件队列大小
 * @param task_priority 事件处理任务优先级
 * @return esp_err_t 
 */
esp_err_t app_event_loop_init(int queue_size, int task_priority);

/**
 * @brief 注册事件处理器
 * 
 * @param event_base 事件基础
 * @param event_id 事件ID
 * @param event_handler 事件处理函数
 * @param event_handler_arg 事件处理函数参数
 * @return esp_err_t 
 */
esp_err_t app_event_handler_register(esp_event_base_t event_base,
                                    int32_t event_id,
                                    esp_event_handler_t event_handler,
                                    void *event_handler_arg);

/**
 * @brief 注销事件处理器
 * 
 * @param event_base 事件基础
 * @param event_id 事件ID
 * @param event_handler 事件处理函数
 * @return esp_err_t 
 */
esp_err_t app_event_handler_unregister(esp_event_base_t event_base,
                                      int32_t event_id,
                                      esp_event_handler_t event_handler);

/**
 * @brief 发送事件
 * 
 * @param event_base 事件基础
 * @param event_id 事件ID
 * @param event_data 事件数据
 * @param event_data_size 事件数据大小
 * @param ticks_to_wait 等待时间
 * @return esp_err_t 
 */
esp_err_t app_event_post(esp_event_base_t event_base,
                        int32_t event_id,
                        void *event_data,
                        size_t event_data_size,
                        TickType_t ticks_to_wait);

#ifdef __cplusplus
}
#endif 