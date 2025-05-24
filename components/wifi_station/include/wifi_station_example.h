#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi Station组件使用示例函数
 */
void wifi_station_example(void);

/**
 * @brief WiFi示例任务函数
 * 
 * @param pvParameters 任务参数
 */
void wifi_example_task(void *pvParameters);

#ifdef __cplusplus
}
#endif 