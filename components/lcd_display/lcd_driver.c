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
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "bus_manager.h"
#include <stdint.h>

static const char *TAG = "lcd-driver";

// 定义最大支持的LCD实例数
#ifndef CONFIG_LCD_MAX_I2C_DRIVER_NUM
#define CONFIG_LCD_MAX_I2C_DRIVER_NUM  1
#endif

// LCD设备句柄结构体
typedef struct {
    i2c_bus_t bus;           // 总线ID
    uint16_t address;         // 设备地址
    i2c_master_dev_handle_t handle;   // 设备句柄
    bool in_use;              // 是否在使用
} lcd_i2c_device_t;

// LCD设备数组
static lcd_i2c_device_t s_lcd_i2c_devices[CONFIG_LCD_MAX_I2C_DRIVER_NUM] = {0};

// 根据总线ID和地址查找设备
static lcd_i2c_device_t* lcd_find_device(i2c_bus_t bus, uint16_t address)
{
    for (int i = 0; i < CONFIG_LCD_MAX_I2C_DRIVER_NUM; i++) {
        if (s_lcd_i2c_devices[i].in_use && 
            s_lcd_i2c_devices[i].bus == bus && 
            s_lcd_i2c_devices[i].address == address) {
            return &s_lcd_i2c_devices[i];
        }
    }
    return NULL;
}

// 分配新设备
static lcd_i2c_device_t* lcd_allocate_device(i2c_bus_t bus, uint16_t address)
{
    // 先查找是否已存在
    lcd_i2c_device_t* device = lcd_find_device(bus, address);
    if (device) {
        return device;
    }
    
    // 查找空闲槽位
    for (int i = 0; i < CONFIG_LCD_MAX_I2C_DRIVER_NUM; i++) {
        if (!s_lcd_i2c_devices[i].in_use) {
            s_lcd_i2c_devices[i].bus = bus;
            s_lcd_i2c_devices[i].address = address;
            s_lcd_i2c_devices[i].in_use = true;
            return &s_lcd_i2c_devices[i];
        }
    }
    return NULL;
}

/**
 * @brief I2C port initial, if pin is valid, then try to bind pin to i2c port
 * 
 * @param drv 
 */
void lcd_ops_i2c_init(const void *drv)
{
    const lcd_i2c_data_t *i2c = (const lcd_i2c_data_t *)drv;
    
    // 查找或分配设备
    lcd_i2c_device_t *device = lcd_allocate_device(i2c->bus, i2c->address);
    if (!device) {
        ESP_LOGE(TAG, "Failed to allocate LCD device, max devices reached");
        return;
    }
    
    // 如果设备已初始化，直接返回
    if (device->handle != NULL) {
        ESP_LOGW(TAG, "LCD device already initialized");
        return;
    }
    
    // 获取总线句柄
    i2c_master_bus_handle_t bus_handle = i2c_bus_get_handle(i2c->bus);
    if (bus_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C bus handle");
        return;
    }
    
    // 配置I2C设备
    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c->address,
        .scl_speed_hz = 400000, // 默认400KHz
    };
    
    // 添加I2C设备
    esp_err_t ret = i2c_master_bus_add_device(bus_handle, &dev_config, &device->handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_bus_add_device failed");
        return;
    }
    
    ESP_LOGI(TAG, "LCD device (bus=%d, addr=0x%02X) initialized success", i2c->bus, i2c->address);
}

/// I2C写
static void lcd_ops_i2c_write(lcd_i2c_device_t *device, uint8_t cmd, const uint8_t *data, uint16_t size)
{
    const uint16_t max_packet_size = 32;
    uint16_t remaining = size;
    const uint8_t *ptr = data;

    while (remaining > 0) {
        uint16_t packet_size = (remaining > max_packet_size) ? max_packet_size : remaining;

        i2c_master_transmit_multi_buffer_info_t infos[2] = {0};
        infos[0].write_buffer = &cmd;
        infos[0].buffer_size = 1;

        infos[1].write_buffer = (uint8_t *)ptr;
        infos[1].buffer_size = packet_size;
        
        esp_err_t ret = i2c_master_multi_buffer_transmit(device->handle, infos, 2, pdMS_TO_TICKS(100));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2c_master_multi_buffer_transmit failed: %s", esp_err_to_name(ret));
            return;
        }

        ptr += packet_size;
        remaining -= packet_size;

        // 延时1ms
        //vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/// I2C写
void lcd_ops_i2c_write_command(const void *drv, const uint8_t *data, uint16_t size)
{
    const lcd_i2c_data_t *i2c = (const lcd_i2c_data_t *)drv;
    
    // 查找设备
    lcd_i2c_device_t *device = lcd_find_device(i2c->bus, i2c->address);
    if (!device || !device->handle) {
        ESP_LOGE(TAG, "LCD device not initialized");
        return;
    }

    lcd_ops_i2c_write(device, 0x00, data, size);
}

/// I2C写数据
void lcd_ops_i2c_write_dram_data(const void *drv, const uint8_t *data, uint16_t size)
{
    const lcd_i2c_data_t *i2c = (const lcd_i2c_data_t *)drv;
    
    // 查找设备
    lcd_i2c_device_t *device = lcd_find_device(i2c->bus, i2c->address);
    if (!device || !device->handle) {
        ESP_LOGE(TAG, "LCD device not initialized");
        return;
    }

    lcd_ops_i2c_write(device, 0x40, data, size);
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
static void lcd_ops_gpio_spi_write(const lcd_spi_data_t *spi, bool cmd, uint8_t data)
{
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

void lcd_ops_gpio_spi_write_command(const void *drv, const uint8_t *data, uint16_t size)
{
    const lcd_spi_data_t *spi = (const lcd_spi_data_t *)drv;
    for (int i = 0; i < size; i++)
    {
        lcd_ops_gpio_spi_write(spi, true, data[i]);
    }
}

void lcd_ops_gpio_spi_write_dram_data(const void *drv, const uint8_t *data, uint16_t size)
{
    const lcd_spi_data_t *spi = (const lcd_spi_data_t *)drv;
    for (int i = 0; i < size; i++)
    {
        lcd_ops_gpio_spi_write(spi, false, data[i]);
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
