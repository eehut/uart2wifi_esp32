/**
 * @file bus_manager.c
 * @brief 总线管理器实现
 * @version 0.1
 * @date 2025-05-14
 */

#include "bus_manager.h"
#include "esp_log.h"

static const char *TAG = "bus-manager";

// I2C总线句柄数组
static i2c_master_bus_handle_t s_i2c_bus_handles[CONFIG_BUS_MANAGER_I2C_BUS_MAX_NUM] = {NULL};
// UART硬件配置数组
typedef struct {
    bool is_used;
    uint8_t user_id;
    uart_hw_config_t hw_config;
}uart_hw_config_item_t;
static uart_hw_config_item_t s_uart_hw_configs[CONFIG_BUS_MANAGER_UART_MAX_NUM] = {0};

esp_err_t i2c_bus_init(i2c_bus_t bus_id, const i2c_bus_config_t *config)
{
    if (bus_id >= CONFIG_BUS_MANAGER_I2C_BUS_MAX_NUM || config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // 检查是否已经初始化
    if (s_i2c_bus_handles[bus_id] != NULL) {
        ESP_LOGW(TAG, "I2C bus %d already initialized", bus_id);
        return ESP_OK;
    }

    // 配置I2C总线
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = config->port,
        .sda_io_num = config->sda_io_num,
        .scl_io_num = config->scl_io_num,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = config->internal_pullup,
    };

    // 创建I2C总线
    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_i2c_bus_handles[bus_id]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "I2C bus %d initialized success", bus_id);
    return ESP_OK;
}

i2c_master_bus_handle_t i2c_bus_get_handle(i2c_bus_t bus_id)
{
    if (bus_id >= CONFIG_BUS_MANAGER_I2C_BUS_MAX_NUM) {
        return NULL;
    }

    if (s_i2c_bus_handles[bus_id] == NULL) {
        ESP_LOGE(TAG, "I2C bus %d not initialized", bus_id);
        return NULL;
    }

    return s_i2c_bus_handles[bus_id];
}

esp_err_t i2c_bus_deinit(i2c_bus_t bus_id)
{
    if (bus_id >= CONFIG_BUS_MANAGER_I2C_BUS_MAX_NUM) {
        return ESP_ERR_INVALID_ARG;
    }

    if (s_i2c_bus_handles[bus_id] == NULL) {
        ESP_LOGW(TAG, "I2C bus %d not initialized", bus_id);
        return ESP_OK;
    }

    esp_err_t ret = i2c_del_master_bus(s_i2c_bus_handles[bus_id]);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_del_master_bus failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_i2c_bus_handles[bus_id] = NULL;
    ESP_LOGI(TAG, "I2C bus %d deinitialized success", bus_id);
    return ESP_OK;
} 


esp_err_t uart_hw_config_add(uint8_t user_id, const uart_hw_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (int i = 0; i < CONFIG_BUS_MANAGER_UART_MAX_NUM; i++) {
        if (!s_uart_hw_configs[i].is_used) {
            s_uart_hw_configs[i].is_used = true;
            s_uart_hw_configs[i].user_id = user_id;
            s_uart_hw_configs[i].hw_config = *config;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

const uart_hw_config_t *uart_hw_config_get(uint8_t user_id)
{
    if (user_id >= CONFIG_BUS_MANAGER_UART_MAX_NUM) {
        return NULL;
    }

    for (int i = 0; i < CONFIG_BUS_MANAGER_UART_MAX_NUM; i++) {
        if (s_uart_hw_configs[i].is_used && s_uart_hw_configs[i].user_id == user_id) {
            return &s_uart_hw_configs[i].hw_config;
        }
    }
    return NULL;
}
