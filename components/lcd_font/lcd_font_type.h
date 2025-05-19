
#ifndef __LCD_FONT_TYPE_H__
#define __LCD_FONT_TYPE_H__

/**
 * @file lcd_font_type.h
 * @author Liu Chuansen (179712066@qq.com)
 * @brief 实现一个字体函数
 * @version 0.1
 * @date 2022-08-30
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_types.h"


/**
 * @brief 是否启用扩展ASCII字符
 * 
 * @note 
 *   如果启用，则字符集为ASCII + 扩展ASCII
 */
#ifndef CONFIG_LCD_FONT_EXTENDED_ASCII 
#define CONFIG_LCD_FONT_EXTENDED_ASCII 0
#endif

/**
 * @brief 定义一个字体
 * 
 */
typedef struct 
{
    /// 字体名称
    const char *name;
    /// 字体宽度
    uint16_t width;
    /// 字体高度
    uint16_t height;
    /// 单个字体编码大小
    uint16_t code_size;
    /// 编码数据总大小
    uint32_t data_size;
    /// 字体编码库
    const uint8_t *data;
    /// 获取数据编码数据的起始地址
    const uint8_t * (*get_code_data)(const void *self, uint32_t ch);
}lcd_font_t;



/**
 * @brief 一般获取ASCII字库数据的方法
 * 
 * @param self 字体对象
 * @param ch ASCII编码
 * @return const uint8_t * 返回编码地址，使用f->codeSize长度的数据即可
 * 
 * @note 
 *   如果是8*8的， 返回连续的8个数据，
 *   如果是8*16的，返回连续的16个数据，
 *   如果是16*16的，返回连续的32个数据 
 */
const uint8_t *lcd_font_get_ascii_code(const void *self, uint32_t ch);

/**
 * @brief 仅提取数字0-9的编码
 * 
 * @param self 字体对象
 * @param ch ASCII 0 - 9 
 * @return const uint8_t * 返回编码地址，使用f->codeSize长度的数据即可
 * 
 * @note 
 *   如果是8*8的， 返回连续的8个数据，
 *   如果是8*16的，返回连续的16个数据，
 *   如果是16*16的，返回连续的32个数据 
 */
const uint8_t *lcd_font_get_ascii_number_code(const void *self, uint32_t ch);




/**
 * @brief 定义一个字体数据 
 * 
 * 
 */

#define LCD_FONT_DATA_DEFINE(_name) \
static const uint8_t s_lcd_font_data_##_name[] = 

/**
 * @brief 定义一个字体
 * 
 * codeSize： width 8个位为一个字节，不足8位也算一字节
 */
#define LCD_FONT_DEFINE(_name, _width, _height, _func) \
const lcd_font_t g_lcd_font_##_name = { \
    .name = #_name,  \
    .width = _width, .height = _height, \
    .code_size = ((((_width) >> 3) + (((_width) & 0x7) ? 1 : 0)) * (_height)), \
    .data_size = sizeof(s_lcd_font_data_##_name), \
    .data = s_lcd_font_data_##_name,  \
    .get_code_data = _func, \
}

/**
 * @brief 声明一个字体
 * 
 */
#define LCD_FONT_DECLARE(_name) \
extern const lcd_font_t g_lcd_font_##_name 

/**
 * @brief 引用一个字体
 * 
 */
#define LCD_FONT(_name) &g_lcd_font_##_name 

/// 定义一个ASCII字体
#define LCD_ASCII_FONT_DEFINE(_name, _width, _height) LCD_FONT_DEFINE(_name, _width, _height, lcd_font_get_ascii_code)

/// 定义一个ASCII-数字only字体
#define LCD_ASCII_NUMBER_FONT_DEFINE(_name, _width, _height) LCD_FONT_DEFINE(_name, _width, _height, lcd_font_get_ascii_number_code)


#ifdef __cplusplus
}
#endif

#endif // __LCD_FONT_TYPE_H__


