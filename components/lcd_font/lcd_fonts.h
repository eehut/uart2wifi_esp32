
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

#include "lcd_font_type.h"

/// 默认启用Acorn8x8字体
#ifndef CONFIG_LCD_FONT_ACORN_8X8
#define CONFIG_LCD_FONT_ACORN_8X8 1
#endif

/// 默认不启用console_number_32x48字体
#ifndef CONFIG_LCD_FONT_CONSOLE_NUMBER_32X48
#define CONFIG_LCD_FONT_CONSOLE_NUMBER_32X48 0
#endif


/// ASCII ACORN 8X8
LCD_FONT_DECLARE(acorn8x8);

/// 大号的数字 32*48 
LCD_FONT_DECLARE(console_number_32x48);

