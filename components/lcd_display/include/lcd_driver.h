#ifndef __LCD_DRIVER_H__
#define __LCD_DRIVER_H__

/**
 * @file lcd_driver.h
 * @author LiuChuansen (1797120666@qq.com)
 * @brief LCD驱动头文件
 * @version 0.1
 * @date 2025-05-14
 * 
 * @copyright Copyright (c) 2025
 * 
 */

 #ifdef __cplusplus
 extern "C" {
 #endif

 #include "esp_types.h"
 #include "driver/i2c_master.h"
 #include "driver/gpio.h"
 #include "bus_manager.h"


/**
 * @brief I2C驱动总线接口
 * 
 */
typedef struct 
{
    i2c_bus_t bus;       // 总线ID
    uint16_t address;      // 设备地址
}lcd_i2c_data_t;


/**
 * @brief SPI驱动总线接口
 * 
 */
typedef struct 
{
    int16_t sda;
    int16_t scl;
    int16_t dc;
    int16_t rst;
    int16_t cs;
}lcd_spi_data_t;


/**
 * @brief LCD驱动操作接口
 * 
 */
typedef struct 
{
    /// 驱动数据 
    const void *data;
    /// 初始化
    void (*init)(const void *);
    /// 写数据
    void (*write)(const void *, bool, uint8_t);
    /// 复位
    void (*reset)(const void *);
}lcd_driver_ops_t;


///声明快速OPS操作函数
static inline void _lcd_reset(const lcd_driver_ops_t *ops)
{
    ops->reset(ops->data);
}

static inline void _lcd_write_command(const lcd_driver_ops_t *ops, uint8_t data)
{
    ops->write(ops->data, true, data);
}

static inline void _lcd_write_data(const lcd_driver_ops_t *ops, uint8_t data)
{
    ops->write(ops->data, false, data);
}

static inline void _lcd_init(const lcd_driver_ops_t *ops)
{
    ops->init(ops->data);
}

/// 声明驱动接口函数

/**
 * @brief 空操作
 * 
 * @param data 
 */
static inline void lcd_ops_dummy(const void *drv)
{
    // DO NOTHING
}

/// I2C 初始化
extern void lcd_ops_i2c_init(const void *drv);
/// I2C写
extern void lcd_ops_i2c_write(const void *drv, bool cmd, uint8_t data);
/// I2C反初始化
extern void lcd_ops_i2c_deinit(const void *drv);


/// SPI 初始化
extern void lcd_ops_gpio_spi_init(const void *drv);
/// SPI写
extern void lcd_ops_gpio_spi_write(const void *drv, bool cmd, uint8_t data);
/// SPI 复位
extern void lcd_ops_gpio_spi_reset(const void *drv);


/// 声明一个I2C的驱动 
#define LCD_DEFINE_DRIVER_I2C(_name, _bus, _addr) \
static const lcd_i2c_data_t s_lcd_data_##_name = {.bus = _bus, .address = _addr}; \
static const lcd_driver_ops_t s_lcd_driver_##_name = \
{ \
    .data = &s_lcd_data_##_name, \
    .init = lcd_ops_i2c_init, \
    .write = lcd_ops_i2c_write, \
    .reset = lcd_ops_dummy \
}

///声明一个SPI的OLED驱动 
#define LCD_DEFINE_DRIVER_GPIO_SPI(_name, _data, _clk, _dc, _rst, _cs) \
static const lcd_spi_data_t s_lcd_data_##_name = {_data, _clk, _dc, _rst, _cs}; \
static const lcd_driver_ops_t s_lcd_driver_##_name = \
{ \
    .data = &s_lcd_data_##_name, \
    .init = lcd_ops_gpio_spi_init, \
    .write = lcd_ops_gpio_spi_write, \
    .reset = lcd_ops_gpio_spi_reset \
}

/// 引用一个驱动 
#define LCD_DRIVER(_name) &s_lcd_driver_##_name


 #ifdef __cplusplus
 }
 #endif

#endif // __LCD_DRIVER_H__
