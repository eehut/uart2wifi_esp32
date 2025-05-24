/**
 * @file ext_gpio_events.c
 * @brief 扩展GPIO事件实现
 */
#include "ext_gpio_event_type.h"
#include "app_event_loop.h"
#include "esp_log.h"

static const char *TAG = "ext_gpio_event";

// 定义事件基础
ESP_EVENT_DEFINE_BASE(EXT_GPIO_EVENTS);


/**
 * @brief 发送按键事件
 * 
 * @param gpio_id 按键ID
 * @param gpio_name 按键名称
 * @param event 事件类型
 * @param click_count 连击次数
 * @param long_pressed 长按时间
 * @return esp_err_t 
 */
esp_err_t ext_gpio_send_button_event(uint16_t gpio_id, 
                                   const char *gpio_name,
                                   ext_gpio_event_t event, 
                                   uint8_t click_count, 
                                   uint16_t long_pressed)
{
    ext_gpio_event_data_t event_data = {
        .gpio_id = gpio_id,
        .gpio_name = gpio_name,
        .event = event,
        .data.button.click_count = click_count,
        .data.button.long_pressed = long_pressed
    };

    ESP_LOGD(TAG, "Send event: gpio<%s>, event=%d, click_count=%d, long_pressed=%d", 
             gpio_name, event, click_count, long_pressed);
    
    // 使用应用事件循环发送事件
    return app_event_post(EXT_GPIO_EVENTS, event, &event_data, sizeof(event_data), pdMS_TO_TICKS(100));
} 
