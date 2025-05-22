/**
 * @file display.c
 * @brief 显示模块实现
 * @version 0.1
 * @date 2023-07-14
 */

#include "display.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_log_config.h"
#include "lcd_driver.h"
#include "lcd_display.h"
#include "lcd_models.h"
#include "lcd_fonts.h"
#include "img_icons.h"
#include "bus_manager.h"

static const char *TAG = "display";

// 定义显示任务参数
#define DISPLAY_TASK_STACK_SIZE    2048
#define DISPLAY_TASK_PRIORITY      3
#define DISPLAY_REFRESH_RATE_HZ    10  // 10Hz刷新率

// 定义动画参数
#define ANIMATION_UPDATE_MS        100 // 动画更新周期(ms)
#define ANIMATION_ERASER_WIDTH     4   // 擦除点宽度
#define LINE_Y_POSITION            12  // 水平线的Y坐标
#define LINE_WIDTH                 128 // 水平线宽度

// 定义信号图标位置
#define SIGNAL_ICON_X              114
#define SIGNAL_ICON_Y              1

// 定义一个I2C的OLED驱动
LCD_DEFINE_DRIVER_I2C(i2c, BUS_I2C0, 0x3C);

// 定义一个SSD1312的OLED显示模型
LCD_DEFINE_SSD1312_128X64(ssd1312);

// 显示任务句柄
static TaskHandle_t s_display_task_handle = NULL;

// 显示屏句柄
static lcd_handle_t s_lcd_handle = NULL;

// 动画状态
static struct {
    uint8_t eraser_position;         // 擦除点的当前位置
    uint32_t last_update_time;       // 上次更新的时间戳(ms)
} s_animation_state = {0};

// 显示任务函数
static void display_task(void *arg);

// 绘制带动画效果的水平线
static void draw_animated_line(lcd_handle_t lcd, bool refresh);

/**
 * @brief 初始化显示模块
 * 
 * @return lcd_handle_t 返回显示屏句柄
 */
lcd_handle_t display_init(void)
{
    ESP_LOGI(TAG, "Initializing display...");
    
    // 创建一个OLED显示器
    s_lcd_handle = lcd_display_create(LCD_DRIVER(i2c), LCD_MODEL(ssd1312), LCD_ROTATION_0, NULL, 0);
    if (s_lcd_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create display");
        return NULL;
    }
    
    // 启动显示器
    lcd_startup(s_lcd_handle);
    
    // 清屏
    lcd_fill(s_lcd_handle, 0x00);
    
    // 初始化显示内容
    // 在x=0,y=12处绘制水平线，宽度为1像素，长度为屏幕宽度
    lcd_draw_horizontal_line(s_lcd_handle, 0, LINE_Y_POSITION, LINE_WIDTH, 1, false);
    
    // 在右上角(x=114,y=1)显示信号满格图标
    lcd_display_mono_img(s_lcd_handle, SIGNAL_ICON_X, SIGNAL_ICON_Y, LCD_IMG(signal_4), true);

    // 显示IP地址
    display_ip_address("192.168.1.1");
    
    ESP_LOGI(TAG, "Display module initialized");
    
    return s_lcd_handle;
}

/**
 * @brief 启动显示任务
 * 
 * @param lcd_handle 显示屏句柄
 * @return esp_err_t 操作结果
 */
esp_err_t display_task_start(lcd_handle_t lcd_handle)
{
    if (lcd_handle == NULL) {        
        return ESP_ERR_INVALID_ARG;
    }
    
    if (s_display_task_handle != NULL) {
        ESP_LOGW(TAG, "Display task already started");
        return ESP_OK;
    }
    
    s_lcd_handle = lcd_handle;
    
    // 初始化动画状态
    s_animation_state.eraser_position = 0;
    s_animation_state.last_update_time = 0;
    
    // 创建显示任务
    BaseType_t ret = xTaskCreate(
        display_task,
        "display_task",
        DISPLAY_TASK_STACK_SIZE,
        NULL,
        DISPLAY_TASK_PRIORITY,
        &s_display_task_handle
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Display task started successfully");
    return ESP_OK;
}

/**
 * @brief 停止显示任务
 * 
 * @return esp_err_t 操作结果
 */
esp_err_t display_task_stop(void)
{
    if (s_display_task_handle == NULL) {
        ESP_LOGW(TAG, "Display task not started");
        return ESP_OK;
    }
    
    // 删除任务
    vTaskDelete(s_display_task_handle);
    s_display_task_handle = NULL;
    
    ESP_LOGI(TAG, "Display task stopped");
    return ESP_OK;
}

/**
 * @brief 设置信号等级图标
 * 
 * @param level 信号等级(0-4)，0表示无信号，1-4表示信号等级
 * @return esp_err_t 操作结果
 */
esp_err_t display_signal_level(uint8_t level)
{
    if (s_lcd_handle == NULL) {
        ESP_LOGE(TAG, "Display not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    const lcd_mono_img_t *icon = NULL;
    
    // 根据信号等级选择图标
    switch (level) {
        case 0:
            icon = LCD_IMG(no_signal_2);
            break;
        case 1:
            icon = LCD_IMG(signal_1);
            break;
        case 2:
            icon = LCD_IMG(signal_2);
            break;
        case 3:
            icon = LCD_IMG(signal_3);
            break;
        case 4:
        default:
            icon = LCD_IMG(signal_4);
            break;
    }
    
    // 显示选中的图标
    lcd_display_mono_img(s_lcd_handle, SIGNAL_ICON_X, SIGNAL_ICON_Y, icon, false);
    
    ESP_LOGI(TAG, "Signal level set to %d", level);
    return ESP_OK;
}

esp_err_t display_ip_address(const char *ip_address)
{
    if (s_lcd_handle == NULL) {
        ESP_LOGE(TAG, "Display not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    lcd_display_string(s_lcd_handle, 0, 2, ip_address, LCD_FONT(ascii_8x8), false);

    return ESP_OK;
}

/**
 * @brief 绘制带动画效果的水平线
 * 
 * @param lcd LCD句柄
 * @param refresh 是否立即刷新显示
 */
static void draw_animated_line(lcd_handle_t lcd, bool refresh)
{
    // 清除原来的水平线区域
    lcd_clear_area(lcd, 0, LINE_Y_POSITION, LINE_WIDTH, 1);
    
    // 绘制新的水平线，但在擦除点位置不绘制
    for (int x = 0; x < LINE_WIDTH; x++) {
        // 如果当前位置在擦除范围内，则跳过绘制
        if (x >= s_animation_state.eraser_position && 
            x < (s_animation_state.eraser_position + ANIMATION_ERASER_WIDTH) && 
            s_animation_state.eraser_position < LINE_WIDTH) {
            continue;
        }
        
        // 绘制线段上的一个点 - 使用水平线绘制函数替代像素点绘制
        lcd_draw_horizontal_line(lcd, x, LINE_Y_POSITION, 1, 1, false);
    }
    
    // 如果需要，立即刷新
    if (refresh) {
        lcd_refresh(lcd);
    }
}

/**
 * @brief 显示任务函数
 * 
 * @param arg 任务参数
 */
static void display_task(void *arg)
{
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t refresh_period = pdMS_TO_TICKS(1000 / DISPLAY_REFRESH_RATE_HZ); // 计算刷新周期
    
    ESP_LOGI(TAG, "Display task started, refresh rate: %dHz", DISPLAY_REFRESH_RATE_HZ);
    
    while (1) {
        uint32_t current_time = (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
        
        // 检查是否需要更新动画
        if ((current_time - s_animation_state.last_update_time) >= ANIMATION_UPDATE_MS) {
            // 更新擦除点位置
            s_animation_state.eraser_position = (s_animation_state.eraser_position + 1) % (LINE_WIDTH + ANIMATION_ERASER_WIDTH);
            
            // 更新动画时间戳
            s_animation_state.last_update_time = current_time;
            
            // 重新绘制水平线
            draw_animated_line(s_lcd_handle, false);
        }
        
        // 执行刷新操作
        lcd_refresh(s_lcd_handle);
        
        // 每个周期延时，保证固定刷新率
        vTaskDelayUntil(&last_wake_time, refresh_period);
    }
}
