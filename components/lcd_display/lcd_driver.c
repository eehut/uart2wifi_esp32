/**
 * @file lcd_driver.c
 * @author LiuChuansen (1797120666@qq.com)
 * @brief LCD底层驱动实现
 * @version 0.1
 * @date 2025-05-14
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "lcd_driver.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "lcd-driver";


/**
 * @brief I2C port initial, if pin is valid, then try to bind pin to i2c port
 * 
 * @param drv 
 */
void lcd_ops_i2c_init(const void *drv)
{
    const lcd_i2c_data_t *i2c = (const lcd_i2c_data_t *)drv;

    if (i2c->sda < 0 || i2c->scl < 0) {
        ESP_LOGE(TAG, "Invalid I2C pins: sda=%d, scl=%d", i2c->sda, i2c->scl);
        return;
    }
    
    i2c_config_t config = {0};
    config.mode = I2C_MODE_MASTER;
    config.sda_io_num = i2c->sda;
    config.scl_io_num = i2c->scl;
    config.sda_pullup_en = GPIO_PULLUP_ENABLE;
    config.scl_pullup_en = GPIO_PULLUP_ENABLE;
    config.master.clk_speed = i2c->rate ? i2c->rate : 400000; // 400KHz

    esp_err_t ret = i2c_param_config(i2c->port, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = i2c_driver_install(i2c->port, config.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "i2c driver init success");
}

/// I2C写
void lcd_ops_i2c_write(const void *drv, bool cmd, uint8_t data)
{
    const lcd_i2c_data_t *i2c = (const lcd_i2c_data_t *)drv;
    uint8_t val[2];

    val[0] = cmd ? 0x00 : 0x40;
    val[1] = data;

    i2c_master_write_to_device(i2c->port, i2c->address, val, 2, pdMS_TO_TICKS(100));
}


/// SPI 初始化
void lcd_ops_gpio_spi_init(const void *drv)
{
    const lcd_spi_data_t *gpio_spi = (const lcd_spi_data_t *)drv;
    bool has_cs = gpio_spi->cs >= 0;
    bool has_rst = gpio_spi->rst >= 0;

    // sda, scl, dc, 这三个引脚不能为无效引脚
    if (gpio_spi->sda < 0 || gpio_spi->scl < 0 || gpio_spi->dc < 0) {
        ESP_LOGE(TAG, "Invalid SPI pins: sda=%d, scl=%d, dc=%d", gpio_spi->sda, gpio_spi->scl, gpio_spi->dc);
        return;
    }
    
    gpio_config_t cfg = {0};

    cfg.pin_bit_mask = (1ULL << gpio_spi->sda) | (1ULL << gpio_spi->scl) | (1ULL << gpio_spi->dc);

    if (has_cs) {
        cfg.pin_bit_mask |= (1ULL << gpio_spi->cs);
    }

    if (has_rst) {
        cfg.pin_bit_mask |= (1ULL << gpio_spi->rst);
    }

    
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;

    esp_err_t ret = gpio_config(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "gpio-spi driver init success");
}
/// SPI写
void lcd_ops_gpio_spi_write(const void *drv, bool cmd, uint8_t data)
{
    const lcd_spi_data_t *spi = (const lcd_spi_data_t *)drv;

    // 决定写的是命令还是数据 
    gpio_set_level(spi->dc, cmd ? 0 : 1);

    for (int i = 0; i < 8; i ++)
    {
        if (data & 0x80)
        {
            gpio_set_level(spi->scl, 0);
            gpio_set_level(spi->sda, 1);
            gpio_set_level(spi->scl, 1);
            gpio_set_level(spi->sda, 1);
        }
        else 
        {
            gpio_set_level(spi->scl, 0);
            gpio_set_level(spi->sda, 0);
            gpio_set_level(spi->scl, 1);
            gpio_set_level(spi->sda, 0);
        }
        data <<= 1;
    }
}
/// SPI 复位
void lcd_ops_gpio_spi_reset(const void *drv)
{
    const lcd_spi_data_t *spi = (const lcd_spi_data_t *)drv;

    if (spi->rst >= 0)
    {
        gpio_set_level(spi->rst, 0);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level(spi->rst, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(spi->rst, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (spi->cs >= 0)
    {
        gpio_set_level(spi->cs, 0);
    }
}
