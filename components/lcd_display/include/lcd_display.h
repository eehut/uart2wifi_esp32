#ifndef __LCD_DISPLAY_H__
#define __LCD_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file lcd_display.h
 * @author Samuel (179712066@qq.com)
 * @brief LCD显示驱动
 * @version 0.1
 * @date 2025-05-22
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "lcd_driver.h"
#include "lcd_models.h"
#include "lcd_fonts.h"
#include "lcd_img.h"
#include <string.h>

// 旋转角度
typedef enum {
    LCD_ROTATION_0 = 0,
    LCD_ROTATION_90 = 1,
    LCD_ROTATION_180 = 2,
    LCD_ROTATION_270 = 3,
} lcd_rotation_t;

/// @brief lcd显示句柄
typedef void * lcd_handle_t;




/**
 * @brief 创建一个OLED显示屏
 * 
 * @param driver 指向驱动
 * @param model 指向显示模型
 * @param rotation 旋转角度
 * @param static_mem 静态内存，如果为空，使用动态分配的内存
 * @param mem_size 静态内存大小
 * @return void* 
 * 返回一个OLED的HANDLE
 */
lcd_handle_t lcd_display_create(const lcd_driver_ops_t *driver, const lcd_model_t *model, lcd_rotation_t rotation, uint8_t *static_mem, uint32_t mem_size);

/**
 * @brief 销废一个显示器
 * 
 * @param disp 
 */
void lcd_display_destory(lcd_handle_t disp);

/**
 * @brief 刷新屏幕数据
 * 
 * @param disp 
 */
void lcd_refresh(lcd_handle_t disp);

/**
 * @brief 启动显示器
 * 
 * @param disp 
 */
void lcd_startup(lcd_handle_t disp);

/**
 * @brief 填充指定的数据 
 * 
 * @param disp 
 * @param data 
 */
void lcd_fill(lcd_handle_t disp, uint8_t data);

/**
 * @brief 显示单个字符，支持部分显示
 * 
 * @param disp LCD显示句柄
 * @param x X坐标
 * @param y Y坐标
 * @param ch 要显示的字符
 * @param font 字体
 * @param reverse 是否反向显示(黑底白字)
 * @return int 返回实际显示的像素宽度，如果完全不可见则返回0
 */
int lcd_display_char(lcd_handle_t disp, int x, int y, int ch, const lcd_font_t *font, bool reverse);

/**
 * @brief 显示一串文本，支持部分显示。如果字符超出显示区域，会显示能显示的部分
 * 
 * @param disp 
 * @param x 显示位置X, 水平方向, 从左到右
 * @param y 显示位置Y, 垂直方向, 从上到下
 * @param text 需要显示的文本
 * @param font 字体
 * @param reverse 是否反向显示(黑底白字)
 * 
 * @return int 返回显示的字符数量
 */
int lcd_display_string(lcd_handle_t disp, int x, int y, const char *text, const lcd_font_t *font, bool reverse);

/**
 * @brief 显示单色位图，支持部分显示
 * 
 * @param disp 显示对象 
 * @param x 显示位置X
 * @param y 显示位置Y
 * @param img 位图对象
 * @param reverse 是否反向显示(黑底白字)
 * 
 * @return int 返回实际显示的像素宽度，如果完全不可见则返回0
 */
int lcd_display_mono_img(lcd_handle_t disp, int x, int y, const lcd_mono_img_t *img, bool reverse);

/**
 * @brief 清除指定区域的显示内容
 * 
 * @param disp LCD显示句柄
 * @param x 起始x坐标
 * @param y 起始y坐标
 * @param width 要清除的宽度(像素)
 * @param height 要清除的高度(像素)
 * @param value 填充的值: 0 - 清空, 1 - 填充
 * @return int 成功返回0，失败返回-1
 */
int lcd_fill_area(lcd_handle_t disp, int x, int y, int width, int height, uint8_t value);

/**
 * @brief 清除指定区域的显示内容
 * 
 * @param disp LCD显示句柄
 * @param x 起始x坐标
 * @param y 起始y坐标
 * @param width 要清除的宽度(像素)
 * @param height 要清除的高度(像素)
 * @return int 成功返回0，失败返回-1
 */
static inline int lcd_clear_area(lcd_handle_t disp, int x, int y, int width, int height)
{
    return lcd_fill_area(disp, x, y, width, height, 0);
}


/**
 * @brief 绘制垂直线
 * 
 * @param disp 
 * @param x 
 * @param y 
 * @param length 线的长度(垂直方向)
 * @param width 线宽(水平方向)
 * @param reverse 是否反向显示(黑底白字)
 * @return int 成功返回0，失败返回-1
 */
int lcd_draw_vertical_line(lcd_handle_t disp, int x, int y, int length, int width, bool reverse);

/**
 * @brief 绘制水平线
 * 
 * @param disp 
 * @param x 
 * @param y 
 * @param length 线的长度(水平方向)
 * @param width 线宽(垂直方向)
 * @param reverse 是否反向显示(黑底白字)
 * @return int 成功返回0，失败返回-1
 */
int lcd_draw_horizontal_line(lcd_handle_t disp, int x, int y, int length, int width, bool reverse);

/**
 * @brief 绘制矩形
 * 
 * @param disp 
 * @param start_x 起始x坐标
 * @param start_y 起始y坐标
 * @param end_x 结束x坐标
 * @param end_y 结束y坐标
 * @param width 线宽
 * @param reverse 是否反向显示(黑底白字)
 * @return int 成功返回0，失败返回-1
 */
int lcd_draw_rectangle(lcd_handle_t disp, int start_x, int start_y, int end_x, int end_y, int width, bool reverse);

/**
 * @brief 绘制矩形方法2
 * 
 * @param disp LCD显示句柄
 * @param start_x 左上角x坐标
 * @param start_y 左上角y坐标
 * @param x_len 矩形宽度
 * @param y_len 矩形高度
 * @param width 线宽(向内缩进)
 * @param reverse 是否反向显示(黑底白字)
 * @return int 成功返回0，失败返回-1
 */
int lcd_draw_rectangle1(lcd_handle_t disp, int start_x, int start_y, int x_len, int y_len, int width, bool reverse);

#ifdef __cplusplus
}
#endif

#endif // __LCD_DISPLAY_H__
