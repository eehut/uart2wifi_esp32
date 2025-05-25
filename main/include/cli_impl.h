#pragma once

/**
 * @file cli_menu.h
 * @author LiuChuansen (179712066@qq.com)
 * @brief 命令行菜单系统，用于在终端中显示菜单并处理用户输入
 * @version 0.1
 * @date 2025-05-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化命令行状态机
 * 
 */
void cli_state_machine_init(void);

/**
 * @brief 重置命令行状态机
 * 
 */
void cli_state_machine_reset(void);

/**
 * @brief 处理用户输入
 * 
 * @param input 
 */
void cli_state_machine_input(const char *input);

#ifdef __cplusplus
}
#endif 
