#ifndef __LCD_MODEL_TYPE_H__
#define __LCD_MODEL_TYPE_H__

/**
 * @file lcd_model_type.h
 * @author LiuChuansen (1797120666@qq.com)
 * @brief LCD模型头文件
 * @version 0.1
 * @date 2025-05-14
 * 
 * @copyright Copyright (c) 2025
 * 
 */

 #ifdef __cplusplus
 extern "C" {
 #endif

 #include "esp_types.h"

 typedef struct 
 {
    /// 模型ID
    int id;
    /// 名称    
    const char *name;
    /// 大小
    int xsize, ysize;

    /// 指向初始数据
    const uint8_t *init_datas;
    /// 指向初始数据长度
    uint16_t init_data_size;
 }lcd_model_t;


/// 定义一个OLED模型
#define LCD_MODEL_DEFINE(_id, _name, _xsize, _ysize, _init) \
static const uint8_t s_lcd_init_data_##_name[] = _init; \
static const lcd_model_t s_lcd_model_##_name = { \
    .id = _id, .name = #_name,  \
    .xsize = _xsize, .ysize = _ysize, \
    .init_datas = s_lcd_init_data_##_name, \
    .init_data_size = sizeof(s_lcd_init_data_##_name) \
}

/// 引用一个MODEL
#define LCD_MODEL(_name)  &s_lcd_model_##_name

#ifdef __cplusplus
}
#endif

#endif // __LCD_MODEL_TYPE_H__
