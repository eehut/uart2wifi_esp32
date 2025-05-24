#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化命令菜单系统
 * 
 * 注意：此函数应在所有其他组件初始化完成后调用
 * 
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t command_menu_init(void);

/**
 * @brief 启动命令菜单任务
 * 
 * 这会创建一个独立的任务来处理用户输入和菜单显示
 * 
 * @return ESP_OK 成功
 *         其他错误码 失败
 */
esp_err_t command_menu_start(void);

/**
 * @brief 停止命令菜单系统
 * 
 * @return ESP_OK 成功
 */
esp_err_t command_menu_stop(void);

#ifdef __cplusplus
}
#endif 