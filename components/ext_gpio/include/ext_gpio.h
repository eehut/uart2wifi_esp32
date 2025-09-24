/**
 * @file ext_gpio.h
 * @author Samuel (179712066@qq.com)
 * @brief 扩展的GPIO驱动
 * @version 0.1
 * @date 2025-05-10
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ext_gpio_type.h"
#include "ext_gpio_event_type.h"

/**
 * @brief 当前使用了的最大GPIO的数量，可在外部配置
 * 
 */
#ifndef CONFIG_EXT_GPIO_MAX_NUM
#define CONFIG_EXT_GPIO_MAX_NUM     8
#endif 

/**
 * @brief 当前使用了最大按键数量，可以外部配置
 * 
 */
#ifndef CONFIG_EXT_BUTTON_MAX_NUM   
#define CONFIG_EXT_BUTTON_MAX_NUM   2
#endif 


/**
 * @brief 当前使用了最大输入数量，可以外部配置
 * 
 */
#ifndef CONFIG_EXT_GPIO_CACHE_SIZE
#define CONFIG_EXT_GPIO_CACHE_SIZE  8
#endif


/**
 * @brief configure the ext_gpios, can be called in multiple times
 * 
 * @param configs configs array of ext_gpio_config_t
 * @param num number of configs
 * @return int 
 */
int ext_gpio_config(const ext_gpio_config_t *configs, int num);

/**
 * @brief 获取GPIO的名称
 * 
 * @param id 
 * @return const char* 
 */
const char * ext_gpio_name(uint16_t id);

/**
 * @brief 设定GPIO为指定的状态, 0 - 低电平, 1 - 高电平, 将自动关闭GPIO的自动控制
 * 
 * @param id GPIO逻辑索引
 * @param value 设置的值
 * @return int 
 */
int ext_gpio_set(uint16_t id, int value);

/**
 * @brief 反转GPIO的状态
 * 
 * @param id GPIO逻辑索引
 * @return int 
 */
int ext_gpio_revert(uint16_t id);

/** 
 * @brief GPIO顶层控制函数
 * 
 * @param id GPIO逻辑索引
 * @param control  GPIO控制位，与GPIO控制的SLOT时间一起作用
 * @param bits  GPIO有效控制位，0-1， 所有位有效，2 - control[0:1]有效，3 - control[0:2]有效 ...
 * @param cycle GPIO控制循环次数， 0 - 一直循环，1 - 循环一次，2 - 循环两次 ...
 * @return int 
 */
int ext_gpio_control(uint16_t id, uint32_t control, uint8_t bits, uint8_t cycle);

/**
 * @brief 获取GPIO的值, 为了更方便使用,它不返回错误值, 所有错误都返回0
 * 
 * @param id GPIO逻辑索引
 * @return int GPIO的值
 */
int ext_gpio_get(uint16_t id);

/**
 * @brief 设置LED为指定状态
 * 
 * @param id LED的逻辑索引
 * @param on true - 开启, false - 关闭
 * @return int 
 */
int ext_led_set(uint16_t id, bool on);

/**
 * @brief 设置LED为闪烁状态
 * 
 * @param id LED的逻辑索引
 * @param control 控制位
 * @param mask 从0位开始算, 至少两个1起作用,否则当作所有control位有效
 * @return int 
 */
int ext_led_flash(uint16_t id, uint32_t control, uint32_t mask);

/**
 * @brief 启动GPIO 任务
 * 
 * @return int 
 */
int ext_gpio_start(void);

#ifdef __cplusplus
}
#endif

