/**
 * @file display.h
 * @brief 显示模块头文件
 * @version 0.1
 * @date 2023-07-14
 */

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_display.h"

/**
 * @brief 初始化显示模块
 * 
 * @return lcd_handle_t 返回显示屏句柄
 */
lcd_handle_t display_init(void);

/**
 * @brief 启动显示任务
 * 
 * @param lcd_handle 显示屏句柄
 * @return esp_err_t 操作结果
 */
esp_err_t display_task_start(lcd_handle_t lcd_handle);

/**
 * @brief 停止显示任务
 * 
 * @return esp_err_t 操作结果
 */
esp_err_t display_task_stop(void);

/**
 * @brief 设置信号等级图标
 * 
 * @param level 信号等级(0-4)，0表示无信号，1-4表示信号等级
 * @return esp_err_t 操作结果
 */
esp_err_t display_signal_level(uint8_t level);

/**
 * @brief 显示IP地址
 * 
 * @param ip_address IP地址
 * @return esp_err_t 操作结果
 */
esp_err_t display_ip_address(const char *ip_address);


#ifdef __cplusplus
}
#endif

#endif // __DISPLAY_H__
