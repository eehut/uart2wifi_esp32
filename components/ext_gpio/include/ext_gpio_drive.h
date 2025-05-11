
/**
 * @brief GPIO low level function
 * 
 */
#pragma once

#include "ext_gpio_type.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief 初始化GPIO
 * 
 * @param gpio 
 * @return int 
 */
int ext_gpio_low_level_config(const ext_gpio_config_t *gpio);

/**
 * @brief 设置GPIO的输出值
 * 
 * @param gpio 
 * @param value 
 */
int ext_gpio_low_level_set(const ext_gpio_config_t *gpio, int value);

/**
 * @brief 获取GPIO的输入值
 * 
 * @param gpio 
 * @return int 
 */
int ext_gpio_low_level_get(const ext_gpio_config_t *gpio, int *value);


#ifdef __cplusplus
}
#endif
