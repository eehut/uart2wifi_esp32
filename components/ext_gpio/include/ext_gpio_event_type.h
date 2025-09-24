/**
 * @file ext_gpio_event_type.h
 * @author Samuel (179712066@qq.com)
 * @brief 扩展GPIO事件类型定义
 * @version 0.1
 * @date 2025-05-11
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_event.h"


// 声明GPIO事件基础
ESP_EVENT_DECLARE_BASE(EXT_GPIO_EVENTS);

// 定义事件类型
typedef enum {
    EXT_GPIO_EVENT_NONE = 0,
    EXT_GPIO_EVENT_BUTTON_PRESSED,    // 按键按下
    EXT_GPIO_EVENT_BUTTON_RELEASED,       // 按键释放  
    EXT_GPIO_EVENT_BUTTON_LONG_PRESSED,   // 长按
    EXT_GPIO_EVENT_BUTTON_CONTINUE_CLICK, // 连击停止
} ext_gpio_event_t;

// GPIO事件数据结构
typedef struct {
    uint16_t gpio_id;      // GPIO ID
    const char *gpio_name; // GPIO名称
    ext_gpio_event_t event;  // 事件类型
    union {
        struct {
            uint8_t click_count;     // 连击次数
            uint16_t long_pressed;   // 长按时间(秒)
        } button;        
    } data;
} ext_gpio_event_data_t;


#ifdef __cplusplus
}
#endif
