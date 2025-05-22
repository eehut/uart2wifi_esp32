#ifndef __LCD_IMG_H__
#define __LCD_IMG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>


typedef struct 
{
    const char *name;
    uint16_t width;
    uint16_t height;
    uint16_t data_size;
    const uint8_t *data;
}lcd_mono_img_t;


/// 定义一个单色图片
#define LCD_MONO_IMG_DEFINE(_name, _width, _height, ...) \
static const uint8_t s_lcd_img_data_##_name[] = { __VA_ARGS__ }; \
const lcd_mono_img_t g_lcd_img_##_name = { \
    .name = #_name, \
    .width = _width, \
    .height = _height, \
    .data_size = sizeof(s_lcd_img_data_##_name), \
    .data = s_lcd_img_data_##_name, \
}

/// 声明一个图片
#define LCD_MONO_IMG_DECLARE(_name) \
extern const lcd_mono_img_t g_lcd_img_##_name

/// 引用一个图片
#define LCD_IMG(_name) &g_lcd_img_##_name



#ifdef __cplusplus
}
#endif

#endif // __LCD_IMG_H__
