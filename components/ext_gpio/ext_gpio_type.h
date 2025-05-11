
/**
 * @brief  EXT GPIO 类型定义
 * 
 */

#pragma once


#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 定义GPIO的标志位
 * 
 */
#define _GPIO_FLAG_INPUT       (0 << 0)
#define _GPIO_FLAG_OUTPUT      (1 << 0)
#define _GPIO_FLAG_ACTIVE_LOW  (1 << 1)
#define _GPIO_FLAG_PULLUP      (1 << 2)
#define _GPIO_FLAG_PULLDOWN    (1 << 3)
#define _GPIO_FLAG_INIT_ACTIVE (1 << 4)
#define _GPIO_FLAG_BUTTON      (1 << 5)

/**
 * @brief 定义支持的GPIO的芯片类型
 * 
 */
typedef enum {
    _GPIO_CHIP_SOC = 0,
    // 由PWM驱动的IO，支持呼吸效果
    _GPIO_CHIP_PWM,
    _GPIO_CHIP_END,
    _GPIO_CHIP_NUM = _GPIO_CHIP_END
}ext_gpio_chip_t;


/**
 * @brief 设备GPIO的定义
 * 
 */
typedef struct {
    uint16_t id;
    const char *name;
    ext_gpio_chip_t chip;
    uint16_t pin;
    uint16_t flags;
}ext_gpio_config_t;



#ifdef __cplusplus
}
#endif
