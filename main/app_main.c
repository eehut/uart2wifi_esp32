/**
 * @file app_main.c
 * @author LiuChuansen (179712066@qq.com)
 * @brief 主函数
 * @version 0.1
 * @date 2025-05-10
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "uptime.h"
#include "app_event_loop.h"
#include "ext_gpio.h"
#include "board.h"
#include "export_ids.h"
#include "display.h" 
#include "esp_log.h"

static const char *TAG = "app_main";


void app_main(void)
{
    int ret = 0;
    
    // 设置日志级别
    esp_log_level_set("*", ESP_LOG_INFO);    // 设置所有模块的默认日志级别
    esp_log_level_set("ext_gpio", ESP_LOG_DEBUG);  // 设置ext_gpio模块的日志级别
    esp_log_level_set("app_event", ESP_LOG_DEBUG); // 设置app_event模块的日志级别
    esp_log_level_set("app_main", ESP_LOG_DEBUG);      // 设置main模块的日志级别

    // 初始化APP事件循环, GPIO按键事件处理
    app_event_loop_init(32, 5);  // 增加队列大小到32
    
    // 初始化板级
    board_init();

    // 启动GPIO任务
    ext_gpio_start();

    // 设置LED闪烁模式
    // 0x33 = 00110011，表示LED会以更快的速度闪烁
    ext_led_flash(GPIO_SYS_LED, 0x33, 0xFF);

    // 初始化显示模块
    lcd_handle_t lcd = display_init();
    if (lcd == NULL) {
        ESP_LOGE(TAG, "Failed to initialize display");
        return;
    }

    // 启动显示任务
    ret = display_task_start(lcd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start display task: %s", esp_err_to_name(ret));
        return;
    }

    while (1) {
        mdelay(1000);
    }
}
