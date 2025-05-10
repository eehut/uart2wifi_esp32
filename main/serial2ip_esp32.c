
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

#include "esp_log.h"

#define LED_PIN GPIO_NUM_7
#define BLINK_DELAY_MS 1000

static const char *TAG = "main";

void app_main(void)
{
    sys_tick_t now = uptime();
    sys_tick_t next_blink = now + 200;
    int led_state = 0;


    // 配置GPIO
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

    ESP_LOGI(TAG, "LED初始化完成");

    while (1) {
        now = uptime();

        //ESP_LOGI(TAG, "now: %u, next_blink: %u", now, next_blink);

        if (uptime_after(now, next_blink)) {
            ESP_LOGI(TAG, "set led %s", led_state ? "on" : "off");
            gpio_set_level(LED_PIN, led_state);
            led_state = !led_state;
            next_blink = now + 200;
        }

        mdelay(10);
    }
}
