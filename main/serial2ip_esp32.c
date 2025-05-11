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

#include "esp_log.h"

static const char *TAG = "main";

enum GPIO_IDS {
    GPIO_SYS_LED = 0,
    GPIO_BUTTON,
};

/// 系统GPIO配置
static ext_gpio_config_t s_gpio_configs[] = {
    { .id = GPIO_SYS_LED, .name = "sys_led", .chip = _GPIO_CHIP_SOC, .pin = GPIO_NUM_7, .flags = _GPIO_FLAG_OUTPUT },
    { .id = GPIO_BUTTON, .name = "test", .chip = _GPIO_CHIP_SOC, .pin = GPIO_NUM_9, .flags = _GPIO_FLAG_BUTTON | _GPIO_FLAG_INPUT | _GPIO_FLAG_ACTIVE_LOW },
};

void app_main(void)
{
    // 设置日志级别
    esp_log_level_set("*", ESP_LOG_INFO);    // 设置所有模块的默认日志级别
    esp_log_level_set("ext_gpio", ESP_LOG_DEBUG);  // 设置ext_gpio模块的日志级别
    esp_log_level_set("main", ESP_LOG_DEBUG);      // 设置main模块的日志级别

    ESP_LOGD(TAG, "Starting GPIO test...");

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

    ESP_LOGI(TAG, "gpio init done, please press button for button test");

    while (1) {
        // 主循环中不需要做任何事情，因为LED闪烁和按键检测都在GPIO任务中处理
        mdelay(1000);
    }
}
