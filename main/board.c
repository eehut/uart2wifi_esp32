
/**
 * @file board.c
 * @author LiuChuansen (179712066@qq.com)
 * @brief 板级初始化
 * @version 0.1
 * @date 2025-05-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "driver/gpio.h"
#include "export_ids.h"
#include "ext_gpio.h"
#include "bus_manager.h"

#include "esp_log.h"

static const char *TAG = "board";

/// 系统GPIO配置
static const ext_gpio_config_t s_gpio_configs[] = {
    { .id = GPIO_SYS_LED, .name = "sys_led", .chip = _GPIO_CHIP_SOC, .pin = GPIO_NUM_7, .flags = _GPIO_FLAG_OUTPUT },
    #ifdef CONFIG_IDF_TARGET_ESP32S3
    { .id = GPIO_BUTTON, .name = "test", .chip = _GPIO_CHIP_SOC, .pin = GPIO_NUM_0, .flags = _GPIO_FLAG_BUTTON | _GPIO_FLAG_INPUT | _GPIO_FLAG_ACTIVE_LOW },
    #else 
    { .id = GPIO_BUTTON, .name = "test", .chip = _GPIO_CHIP_SOC, .pin = GPIO_NUM_9, .flags = _GPIO_FLAG_BUTTON | _GPIO_FLAG_INPUT | _GPIO_FLAG_ACTIVE_LOW },
    #endif
};


/// I2C总线 
static const i2c_bus_config_t s_i2c_bus_config = {
    .port = I2C_NUM_0,
    .sda_io_num = GPIO_NUM_5,
    .scl_io_num = GPIO_NUM_6,
    .clk_speed_hz = 400000,
    .internal_pullup = true,
};


static const uart_hw_config_t s_uart_hw_config = {
    .uart_port = 1,
    .rxd_pin = GPIO_NUM_1,
    .txd_pin = GPIO_NUM_0,
};

/**
 * @brief 板级初始化
 * 
 * @return int 0: 成功, 其他: 失败
 */
int board_init(void)
{

    ESP_ERROR_CHECK(ext_gpio_config(s_gpio_configs, sizeof(s_gpio_configs) / sizeof(s_gpio_configs[0])));

    ESP_ERROR_CHECK(i2c_bus_init(BUS_I2C0, &s_i2c_bus_config));

    ESP_ERROR_CHECK(uart_hw_config_add(UART_PRIMARY, &s_uart_hw_config));

    ESP_LOGI(TAG, "Board initialized success");

    return 0;
}


