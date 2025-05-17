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
static i2c_master_bus_handle_t s_i2c_bus_handles[BUS_I2C_MAX] = {NULL};

esp_err_t i2c_bus_init(i2c_bus_t bus_id, const i2c_bus_config_t *config)
{
    if (bus_id >= BUS_I2C_MAX || config == NULL) {
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
    if (bus_id >= BUS_I2C_MAX) {
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
    if (bus_id >= BUS_I2C_MAX) {
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

