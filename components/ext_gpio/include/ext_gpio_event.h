/**
 * @file ext_gpio_events.h
 * @brief 扩展GPIO事件定义
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "esp_err.h"

#include "ext_gpio_event_type.h"

/**
 * @brief 发送按键事件
 * 
 * @param button_id 按键ID
 * @param button_name 按键名称
 * @param event 事件类型
 * @param click_count 连击次数
 * @param long_pressed 长按时间
 * @return esp_err_t 
 */
esp_err_t ext_gpio_send_button_event(uint16_t gpio_id, 
                                   const char *gpio_name,
                                   ext_gpio_event_t event, 
                                   uint8_t click_count, 
                                   uint16_t long_pressed);

#ifdef __cplusplus
}
#endif 