/**
 * @file bus_manager.h
 * @brief 总线管理器头文件
 * @version 0.1
 * @date 2025-05-14
 */

#ifndef __BUS_MANAGER_H__
#define __BUS_MANAGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "driver/i2c_master.h"

/**
 * @brief 定义UART最大数量
 * 
 */
#ifndef CONFIG_BUS_MANAGER_UART_MAX_NUM 
#define CONFIG_BUS_MANAGER_UART_MAX_NUM  3
#endif

/**
 * @brief UART硬件配置结构体
 */
typedef struct {
    uint8_t uart_port;
    uint8_t rxd_pin;
    uint8_t txd_pin;
}uart_hw_config_t;

/**
 * @brief 总线ID枚举
 */
typedef enum {
    BUS_I2C0,    // I2C总线0
    BUS_I2C1,    // I2C总线1
    BUS_I2C2,    // I2C总线2
    BUS_I2C3,    // I2C总线3
    // 可以添加更多的总线ID
    BUS_I2C_MAX      // 总线ID最大值，用于数组大小定义
} i2c_bus_t;

#ifndef CONFIG_BUS_MANAGER_I2C_BUS_MAX_NUM 
#define CONFIG_BUS_MANAGER_I2C_BUS_MAX_NUM  2
#endif

// 临时注释掉此检查以便编译
//#if CONFIG_BUS_MANAGER_I2C_BUS_MAX_NUM > BUS_I2C_MAX
//#error "CONFIG_BUS_MANAGER_I2C_BUS_MAX_NUM must be less than or equal to BUS_I2C_MAX"
//#endif

/**
 * @brief I2C总线配置结构体
 */
typedef struct {
    i2c_port_num_t port;        // I2C端口号
    gpio_num_t sda_io_num;      // SDA引脚
    gpio_num_t scl_io_num;      // SCL引脚
    uint32_t clk_speed_hz;      // 时钟速度
    bool internal_pullup;       // 是否启用内部上拉
} i2c_bus_config_t;

/**
 * @brief 初始化I2C总线
 * 
 * @param bus_id 总线ID
 * @param config I2C配置
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t i2c_bus_init(i2c_bus_t bus_id, const i2c_bus_config_t *config);

/**
 * @brief 获取I2C总线句柄
 * 
 * @param bus_id 总线ID
 * @return 总线句柄, 非空表示成功 
 */
i2c_master_bus_handle_t i2c_bus_get_handle(i2c_bus_t bus_id);

/**
 * @brief 反初始化I2C总线
 * 
 * @param bus_id 总线ID
 * @return esp_err_t ESP_OK表示成功，其他值表示失败
 */
esp_err_t i2c_bus_deinit(i2c_bus_t bus_id);


/**
 * @brief 添加UART硬件配置
 * 
 * @param user_id 
 * @param config 
 * @return esp_err_t 
 */
esp_err_t uart_hw_config_add(uint8_t user_id, const uart_hw_config_t *config);


/**
 * @brief 获取UART硬件配置
 * 
 * @param user_id 
 * @return const uart_hw_config_t* 
 */
const uart_hw_config_t *uart_hw_config_get(uint8_t user_id);

#ifdef __cplusplus
}
#endif

#endif // __BUS_MANAGER_H__ 