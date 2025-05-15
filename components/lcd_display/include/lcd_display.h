#ifndef __LCD_DISPLAY_H__
#define __LCD_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lcd_driver.h"
#include "lcd_models.h"

// 旋转角度
typedef enum {
    LCD_ROTATION_0 = 0,
    LCD_ROTATION_90 = 1,
    LCD_ROTATION_180 = 2,
    LCD_ROTATION_270 = 3,
} lcd_rotation_t;

/// @brief lcd显示句柄
typedef void * lcd_handle_t;




#ifdef __cplusplus
}
#endif

#endif // __LCD_DISPLAY_H__
