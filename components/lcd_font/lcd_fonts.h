/**
 * @file lcd_fonts.h
 * @author Liu Chuansen (179712066@qq.com)
 * @brief LCD字体定义
 * @version 0.1
 * @date 2025-05-12
 * @note 
 * 1. CONFIG_LCD_FONT_ACORN_8X8 是否启用Acorn8x8字体
 * 2. CONFIG_LCD_FONT_CONSOLE_NUMBER_32X48 是否启用console_number_32x48字体
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lcd_font_type.h"


/// 默认启用扩展基本ASCII字符
#ifndef CONFIG_LCD_FONT_ASCII_8X8
#define CONFIG_LCD_FONT_ASCII_8X8   1
#endif

#ifndef CONFIG_LCD_FONT_ASCII_8X16
#define CONFIG_LCD_FONT_ASCII_8X16 1
#endif 

#ifndef CONFIG_LCD_FONT_ASCII_10X18
#define CONFIG_LCD_FONT_ASCII_10X18 1
#endif

#ifndef CONFIG_LCD_FONT_SUN_ASCII_12X22
#define CONFIG_LCD_FONT_SUN_ASCII_12X22 1
#endif

#ifndef CONFIG_LCD_FONT_TER_ASCII_16X32
#define CONFIG_LCD_FONT_TER_ASCII_16X32 1
#endif

/// 默认启用Acorn8x8字体
#ifndef CONFIG_LCD_FONT_ACORN_ASCII_8X8
#define CONFIG_LCD_FONT_ACORN_ASCII_8X8 0
#endif

/// 默认不启用console_number_32x48字体
#ifndef CONFIG_LCD_FONT_CONSOLE_NUMBER_32X48
#define CONFIG_LCD_FONT_CONSOLE_NUMBER_32X48 0
#endif


/// ASCII字符 8*8
LCD_FONT_DECLARE(ascii_8x8);

/// ASCII字符 8*16
LCD_FONT_DECLARE(ascii_8x16);

/// ASCII字符 10*18
LCD_FONT_DECLARE(ascii_10x18);

/// ASCII字符 12*22
LCD_FONT_DECLARE(sun_ascii_12x22);

/// ASCII字符 16*32
LCD_FONT_DECLARE(ter_ascii_16x32);

/// ASCII ACORN 8X8
LCD_FONT_DECLARE(acorn_ascii_8x8);

/// 大号的数字 32*48 
LCD_FONT_DECLARE(console_number_32x48);


#ifdef __cplusplus
}
#endif

