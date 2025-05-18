#ifndef __LCD_DISPLAY_H__
#define __LCD_DISPLAY_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lcd_driver.h"
#include "lcd_models.h"
#include "lcd_fonts.h"
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
 * @brief 查找字库并显示，较底层函数，不考虑能否显示。
 * 
 * @param disp disp handle
 * @param x 
 * @param y 
 * @param ch 
 * @param font 
 * @param refresh 
 */
void lcd_display_char(lcd_handle_t disp, int x, int y, int ch, const lcd_font_t *font, bool refresh);

/**
 * @brief 显示一串文本， 这是一个比较底层的函数，如果显示内容超出所在行，不显示, 暂不支持非ASCII字串
 * 
 * @param disp 
 * @param x 显示位置X
 * @param y 显示位置Y
 * @param text 需要显示的文本
 * @param font 字体
 * @param refresh 是否刷新 
 * 
 * @return int 返回显示的数量
 */
int lcd_display_string(lcd_handle_t disp, int x, int y, const char *text, const lcd_font_t *font, bool refresh);

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
int lcd_clear_area(lcd_handle_t disp, int x, int y, int width, int height);

#ifdef __cplusplus
}
#endif

#endif // __LCD_DISPLAY_H__
