/**
 * @file fonts.c
 * @author Liu Chuansen (179712066@qq.com)
 * @brief 放置字体
 * @version 0.1
 * @date 2022-10-09
 * 
 * @copyright Copyright (c) 2022
 * 
 */

#include "lcd_fonts.h"

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
const uint8_t *lcd_font_get_ascii_code(const void *self, uint32_t ch)
{
    const lcd_font_t *f = (const lcd_font_t *)self;

#ifdef CONFIG_LCD_FONT_EXTENDED_ASCII
    // 大于，非法的码
    if (ch > 255)
    {
        return NULL;
    }
#else 
    // 大于，非法的码
    if (ch > 127)
    {
        return NULL;
    }
#endif 

    // 看看结束数据是否还在数据内
    int end = ((ch + 1) * f->code_size) - 1;
    // 结束大于可用字体
    if (end > f->data_size)
    {
        return NULL;
    }

    //求出数据
    return &f->data[f->code_size * ch];
}



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
const uint8_t *lcd_font_get_ascii_number_code(const void *self, uint32_t ch)
{
    const lcd_font_t *f = (const lcd_font_t *)self;

    // 大于，非法的码
    if ((ch > '9') || (ch < '0'))
    {
        return NULL;
    }

    int index = ch - '0';
    // 看看结束数据是否还在数据内
    int end = ((index + 1) * f->code_size) - 1;
    // 结束大于可用字体
    if (end > f->data_size)
    {
        return NULL;
    }

    //求出数据
    return &f->data[f->code_size * index];
}


#if CONFIG_LCD_FONT_ASCII_8X8
#include "fonts/font_8x8.c"
#endif // CONFIG_LCD_FONT_ASCII_8X8

#if CONFIG_LCD_FONT_ASCII_8X16
#include "fonts/font_8x16.c"
#endif // CONFIG_LCD_FONT_ASCII_8X16

#if CONFIG_LCD_FONT_ASCII_10X18
#include "fonts/font_10x18.c"
#endif // CONFIG_LCD_FONT_ASCII_10X18

#if CONFIG_LCD_FONT_SUN_ASCII_12X22
#include "fonts/font_sun_12x22.c"
#endif // CONFIG_LCD_FONT_SUN_ASCII_12X22   

#if CONFIG_LCD_FONT_TER_ASCII_16X32
#include "fonts/font_ter_16x32.c"
#endif // CONFIG_LCD_FONT_TER_ASCII_16X32

#if CONFIG_LCD_FONT_ACORN_ASCII_8X8
#include "fonts/font_acorn_8x8.c"
#endif // CONFIG_LCD_FONT_ACORN_ASCII_8X8

#if CONFIG_LCD_FONT_CONSOLE_NUMBER_32X48
#include "fonts/font_console_number_32x48.c"
#endif // CONFIG_LCD_FONT_CONSOLE_NUMBER_32X48

