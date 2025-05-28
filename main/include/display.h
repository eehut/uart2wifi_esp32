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
#include "esp_err.h"

/**
 * @brief 初始化显示模块
 * 
 * @return lcd_handle_t 返回显示屏句柄
 */
esp_err_t display_init(void);

/**
 * @brief 启动显示任务
 * 
 * @param lcd_handle 显示屏句柄
 * @return esp_err_t 操作结果
 */
esp_err_t display_task_start(void);

/**
 * @brief 停止显示任务
 * 
 * @return esp_err_t 操作结果
 */
esp_err_t display_task_stop(void);

#ifdef __cplusplus
}
#endif

#endif // __DISPLAY_H__
