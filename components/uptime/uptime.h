
/**
 * @brief 系统时间抽象
 * 
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if configTICK_RATE_HZ != 1000
#error "configTICK_RATE_HZ must be 1000"
#endif

/// 一个sys_tick的抽象
typedef uint32_t sys_tick_t;

// test if a up to b
#define uptime_after(a, b) ((int32_t)((int32_t)b - (int32_t)a) < 0)

/**
 * @brief 获取当前TICK
 * 
 */
static inline sys_tick_t uptime(void)
{
    return (sys_tick_t)xTaskGetTickCount();
}

/**
 * @brief 延迟us
 * 
 * @param us 
 */
static inline void udelay(uint32_t us)
{
    esp_rom_delay_us(us);
}

/**
 * @brief 延迟ms
 * 
 * @param ms 
 */
static inline void mdelay(uint32_t ms)
{
    vTaskDelay(ms / portTICK_PERIOD_MS);
}


#ifdef __cplusplus
}
#endif

