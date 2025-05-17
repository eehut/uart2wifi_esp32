/**
 * @file serial2ip_esp32.c
 * @author Samuel (samuel@neptune-robotics.com)
 * @brief ESP32C3 串口转IP
 * @version 0.1
 * @date 2025-05-10
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

#include "uptime.h"
#include "ext_gpio.h"

#include "app_event_loop.h"

#include "bus_manager.h"

#include "lcd_driver.h"
#include "lcd_display.h"
#include "lcd_models.h"
#include "lcd_fonts.h"

#include "esp_log.h"

static const char *TAG = "main";

enum GPIO_IDS {
    GPIO_SYS_LED = 0,
    GPIO_BUTTON,
};

/// 系统GPIO配置
static ext_gpio_config_t s_gpio_configs[] = {
    { .id = GPIO_SYS_LED, .name = "sys_led", .chip = _GPIO_CHIP_SOC, .pin = GPIO_NUM_7, .flags = _GPIO_FLAG_OUTPUT },
    #ifdef CONFIG_IDF_TARGET_ESP32S3
    { .id = GPIO_BUTTON, .name = "test", .chip = _GPIO_CHIP_SOC, .pin = GPIO_NUM_0, .flags = _GPIO_FLAG_BUTTON | _GPIO_FLAG_INPUT | _GPIO_FLAG_ACTIVE_LOW },
    #else 
    { .id = GPIO_BUTTON, .name = "test", .chip = _GPIO_CHIP_SOC, .pin = GPIO_NUM_9, .flags = _GPIO_FLAG_BUTTON | _GPIO_FLAG_INPUT | _GPIO_FLAG_ACTIVE_LOW },
    #endif
};

/**s
 * @brief 按键事件处理函数
 * 
 * @param handler_args 处理函数参数
 * @param base 事件基础
 * @param id 事件ID
 * @param event_data 事件数据
 */
static void button_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    ext_gpio_event_data_t* data = (ext_gpio_event_data_t*)event_data;
    
    // 根据事件类型进行不同处理
    switch(id) {
        case EXT_GPIO_EVENT_BUTTON_PRESSED:
            ESP_LOGI(TAG, "button event: [%s] pressed, click_count: %d", data->gpio_name, data->data.button.click_count);
            break;            
        case EXT_GPIO_EVENT_BUTTON_RELEASED:
            ESP_LOGI(TAG, "button event: [%s] released", data->gpio_name);
            break;
            
        case EXT_GPIO_EVENT_BUTTON_LONG_PRESSED:
            ESP_LOGI(TAG, "button event: [%s] long pressed up to %d seconds", data->gpio_name, data->data.button.long_pressed);
            
            // 长按时设置LED闪烁
            if (data->gpio_id == GPIO_BUTTON && data->data.button.long_pressed == 3) {
                ext_led_flash(GPIO_SYS_LED, 0x0003, 0xFFFF);
            }
            break;
            
        case EXT_GPIO_EVENT_BUTTON_CONTINUE_CLICK:
            ESP_LOGI(TAG, "button event: [%s] continue click stopped, click count: %d", data->gpio_name, data->data.button.click_count);

            // 双击时改变LED闪烁模式
            if (data->gpio_id == GPIO_BUTTON && data->data.button.click_count == 2) {
                ext_led_flash(GPIO_SYS_LED, 0x333, 0xFFF);
            }
            // 三击时改变LED闪烁模式
            else if (data->gpio_id == GPIO_BUTTON && data->data.button.click_count == 3) {
                ext_led_flash(GPIO_SYS_LED, 0x03F, 0xFFF);
            }
            break;
    }
}

// 定义一个SPI的OLED驱动
//LCD_DEFINE_DRIVER_GPIO_SPI(gpio_spi, GPIO_NUM_10, GPIO_NUM_9, GPIO_NUM_12, GPIO_NUM_11, -1);

// 定义一个I2C的OLED驱动
LCD_DEFINE_DRIVER_I2C(i2c, BUS_I2C0, 0x3C);

// 定义一个SSD1312的OLED显示模型
LCD_DEFINE_SSD1312_128X64(ssd1312);



void app_main(void)
{
    // 设置日志级别
    esp_log_level_set("*", ESP_LOG_INFO);    // 设置所有模块的默认日志级别
    esp_log_level_set("ext-gpio", ESP_LOG_DEBUG);  // 设置ext_gpio模块的日志级别
    esp_log_level_set("app-event", ESP_LOG_DEBUG); // 设置app_event模块的日志级别
    esp_log_level_set("main", ESP_LOG_DEBUG);      // 设置main模块的日志级别

    ESP_LOGI(TAG, "Starting GPIO test...");

    // 初始化事件循环
    app_event_loop_init(32, 5);  // 增加队列大小到32
    
    // 注册按键事件处理函数
    esp_err_t event_ret = app_event_handler_register(EXT_GPIO_EVENTS, ESP_EVENT_ANY_ID, button_event_handler, NULL);
    if (event_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event handler: %s", esp_err_to_name(event_ret));
        return;
    }

    // 初始化GPIO
    int ret = ext_gpio_config(s_gpio_configs, sizeof(s_gpio_configs) / sizeof(s_gpio_configs[0]));
    if (ret != 0) {
        ESP_LOGE(TAG, "init gpio failed!");
        return;
    }

    // 启动GPIO任务
    ext_gpio_start();
    
    // 设置LED闪烁模式
    // 0x33 = 00110011，表示LED会以更快的速度闪烁
    ext_led_flash(GPIO_SYS_LED, 0x33, 0xFF);

    ESP_LOGI(TAG, "GPIO initialized, press the button to test the event loop");
    ESP_LOGI(TAG, "- long press button: LED fast flash");
    ESP_LOGI(TAG, "- double click button: LED slow flash");
    ESP_LOGI(TAG, "- triple click button: LED very slow flash");

    // 初始化I2C总线
    i2c_bus_config_t i2c_bus_config = {
        .port = I2C_NUM_0,
        .sda_io_num = GPIO_NUM_5,
        .scl_io_num = GPIO_NUM_6,
        .clk_speed_hz = 400000, // 400KHz 不知道为什么当前版本ESPIDF 并不是在master中配置速率.而是在设备中配置.
        .internal_pullup = true,
    };
    ret = i2c_bus_init(BUS_I2C0, &i2c_bus_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return;
    }

    // 创建一个OLED显示器
    lcd_handle_t lcd = lcd_display_create(LCD_DRIVER(i2c), LCD_MODEL(ssd1312), LCD_ROTATION_0, NULL, 0);

    lcd_startup(lcd);

    //lcd_fill(lcd, 0x00);

    // 显示一个字符串
    lcd_display_string(lcd, 0, 0, "Hello, World!", LCD_FONT(acorn_ascii_8x8), true);

    // 显示一个数字
    lcd_display_string(lcd, 0, 16, "1809", LCD_FONT(console_number_32x48), true);

    while (1) {
        // 主循环中不需要做任何事情，因为LED闪烁和按键检测都在GPIO任务中处理
        // 事件处理在事件循环任务中进行
        mdelay(1000);
    }
}
