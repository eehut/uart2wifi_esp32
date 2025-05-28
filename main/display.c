/**
 * @file display.c
 * @brief 显示模块实现
 * @version 0.1
 * @date 2023-07-14
 */

#include "display.h"
#include "esp_log.h"
#include "esp_err.h"
#include "lcd_driver.h"
#include "lcd_display.h"
#include "lcd_models.h"
#include "lcd_fonts.h"
#include "img_icons.h"
#include "ext_gpio.h"
#include "export_ids.h"
#include "app_event_loop.h"
#include "uptime.h"
#include "wifi_station.h"
#include <stdint.h>
#include <inttypes.h>

static const char *TAG = "display";

// 定义显示任务参数
#define DISPLAY_TASK_STACK_SIZE    2048
#define DISPLAY_TASK_PRIORITY      3
#define DISPLAY_REFRESH_RATE_HZ    20  // 10Hz刷新率

// 定义动画参数
#define ANIMATION_UPDATE_MS        50 // 动画更新周期(ms)

// 定义一个I2C的OLED驱动
LCD_DEFINE_DRIVER_I2C(i2c, BUS_I2C0, 0x3C);

// 定义一个SSD1312的OLED显示模型
LCD_DEFINE_SSD1312_128X64(ssd1312);

// 页面标识
typedef enum {
    PAGE_HOME = 0,
    PAGE_WIFI,
    PAGE_UART,
    PAGE_HELP,
    PAGE_MAX
} display_page_t;


// 主页数据结构
typedef struct 
{
    wifi_station_state_t wifi_state;
    int8_t signal_level;
    char ssid[32];
    char ip_address[16];
    uint32_t baudrate;
    uint32_t rx_bytes;
    uint32_t tx_bytes;

    struct {
        uint8_t eraser_position;
        sys_tick_t last_update_time;
    }animation_line;
}page_home_data_t;


/**
 * @brief 显示上下文
 * 
 * @param task_handle 显示任务句柄
 * @param lcd_handle 显示屏句柄
 * @param page 显示页面数据
 * 
 */
typedef struct {
    bool initialized;

    TaskHandle_t task_handle;
    lcd_handle_t lcd_handle;
    
    struct {
        bool dirty;
        display_page_t current_page;        
        sys_tick_t page_expried_time;
        page_home_data_t home;
    } page;

    sys_tick_t data_update_time;


}display_context_t;


// 显示上下文全局变量
static display_context_t s_display_context = { 0 };

// 更新显示数据
static void display_update_data(display_context_t* ctx);

// 绘制主页
static void draw_home_page(display_context_t* ctx);

// 更新主页动画
static bool update_home_animation(display_context_t* ctx);

// 显示任务函数
static void display_task(void *arg);

/**
 * @brief Get the context object
 *  
 * @return display_context_t* 
 */
static inline display_context_t* get_context(void)
{
    return &s_display_context;
}




/**s
 * @brief 按键事件处理函数
 * 
 * @param handler_args 处理函数参数
 * @param base 事件基础
 * @param id 事件ID
 * @param event_data 事件数据
 */
static void button_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    ext_gpio_event_data_t* data = (ext_gpio_event_data_t*)event_data;

    if (data->gpio_id != GPIO_BUTTON) {
        return;
    }
    
    // 根据事件类型进行不同处理
    // switch(id) {
    //     case EXT_GPIO_EVENT_BUTTON_PRESSED:
    //         ESP_LOGI(TAG, "button event: [%s] pressed, click_count: %d", data->gpio_name, data->data.button.click_count);
    //         break; 
    //     case EXT_GPIO_EVENT_BUTTON_RELEASED:
    //         ESP_LOGI(TAG, "button event: [%s] released", data->gpio_name);
            
    //         // 轮流显示不同的信号图标
    //         s_current_signal_level = (s_current_signal_level + 1) % 5; // 0-4循环
    //         display_signal_level(s_current_signal_level);
    //         break;
            
    //     case EXT_GPIO_EVENT_BUTTON_LONG_PRESSED:
    //         ESP_LOGI(TAG, "button event: [%s] long pressed up to %d seconds", data->gpio_name, data->data.button.long_pressed);
            
    //         // 长按时设置LED闪烁
    //         if (data->gpio_id == GPIO_BUTTON && data->data.button.long_pressed == 3) {
    //             ext_led_flash(GPIO_SYS_LED, 0x0003, 0xFFFF);
    //         }
    //         break;
            
    //     case EXT_GPIO_EVENT_BUTTON_CONTINUE_CLICK:
    //         ESP_LOGI(TAG, "button event: [%s] continue click stopped, click count: %d", data->gpio_name, data->data.button.click_count);

    //         // 双击时改变LED闪烁模式
    //         if (data->gpio_id == GPIO_BUTTON && data->data.button.click_count == 2) {
    //             ext_led_flash(GPIO_SYS_LED, 0x333, 0xFFF);
    //         }
    //         // 三击时改变LED闪烁模式
    //         else if (data->gpio_id == GPIO_BUTTON && data->data.button.click_count == 3) {
    //             ext_led_flash(GPIO_SYS_LED, 0x03F, 0xFFF);
    //         }
    //         break;
    // }
}

/**
 * @brief 初始化显示模块
 * 
 * @return lcd_handle_t 返回显示屏句柄
 */
esp_err_t display_init(void)
{
    if (s_display_context.initialized) {
        ESP_LOGW(TAG, "Display already initialized");
        return ESP_OK;
    }

    // 清空显示上下文
    memset(&s_display_context, 0, sizeof(s_display_context));

    ESP_LOGI(TAG, "Initializing display...");
    
    // 创建一个OLED显示器
    s_display_context.lcd_handle = lcd_display_create(LCD_DRIVER(i2c), LCD_MODEL(ssd1312), LCD_ROTATION_0, NULL, 0);
    if (s_display_context.lcd_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create display");
        return ESP_ERR_NO_MEM;
    }
    
    // 启动显示器
    lcd_startup(s_display_context.lcd_handle);
    
    // 清屏
    lcd_fill(s_display_context.lcd_handle, 0x00);

    // 注册按键事件处理函数
    app_event_handler_register(EXT_GPIO_EVENTS, ESP_EVENT_ANY_ID, button_event_handler, NULL);
    
    ESP_LOGI(TAG, "Display module initialized");

    s_display_context.initialized = true;
    
    return ESP_OK;
}

/**
 * @brief 启动显示任务
 * 
 * @return esp_err_t 操作结果
 */
esp_err_t display_task_start(void)
{
    display_context_t* ctx = get_context();

    if (!ctx->initialized) {        
        ESP_LOGE(TAG, "Display not initialized");
        return ESP_ERR_INVALID_ARG;
    }
    
    if (ctx->task_handle != NULL) {
        ESP_LOGW(TAG, "Display task already started");
        return ESP_OK;
    }

    // 初始化页面数据
    ctx->page.dirty = true;
    ctx->page.current_page = PAGE_HOME;
    ctx->page.page_expried_time = 0;
    ctx->page.home.wifi_state = WIFI_STATE_DISCONNECTED;
    ctx->page.home.signal_level = 0;
    strcpy(ctx->page.home.ssid, "N/A");
    strcpy(ctx->page.home.ip_address, "0.0.0.0");
    ctx->page.home.baudrate = 115200;
    ctx->page.home.rx_bytes = 0;
    ctx->page.home.tx_bytes = 0;

    // 创建显示任务
    BaseType_t ret = xTaskCreate(
        display_task,
        "display_task",
        DISPLAY_TASK_STACK_SIZE,
        NULL,
        DISPLAY_TASK_PRIORITY,
        &ctx->task_handle
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
    display_context_t* ctx = get_context();

    if (ctx->task_handle == NULL) {
        ESP_LOGW(TAG, "Display task not started");
        return ESP_OK;
    }
    
    // 删除任务
    vTaskDelete(ctx->task_handle);
    ctx->task_handle = NULL;
    
    ESP_LOGI(TAG, "Display task stopped");
    return ESP_OK;
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

    display_context_t* ctx = get_context();
    ctx->data_update_time = uptime();

    while (1) {
        sys_tick_t now = uptime();
        bool refresh = false;

        // 刷新数据, 500ms 刷新一次
        if (uptime_after(now, ctx->data_update_time)) {
            display_update_data(ctx);
            ctx->data_update_time = now + 250;
        }

        // 更新动画状态
        if (update_home_animation(ctx)) {
            ctx->page.dirty = true;
        }

        if (ctx->page.dirty) 
        {            
            // 清屏
            lcd_fill(ctx->lcd_handle, 0x00);

            switch (ctx->page.current_page) {
                case PAGE_HOME:
                    draw_home_page(ctx);
                    break;
                default:
                    break;
            }

            ctx->page.dirty = false;
            refresh = true;
        }

        // 执行刷新操作
        if (refresh) {
            lcd_refresh(ctx->lcd_handle);
        }
        
        // 每个周期延时，保证固定刷新率
        vTaskDelayUntil(&last_wake_time, refresh_period);
    }
}



static void display_update_data(display_context_t* ctx)
{
    page_home_data_t* home = &ctx->page.home;
    wifi_connection_status_t wifi_status = {0};
    bool need_refresh = false;

    // 获取WiFi状态
    if (wifi_station_get_status(&wifi_status) == ESP_OK) {

        // ESP_LOGD(TAG, "WiFi status: %d, SSID: %s, RSSI: %d", 
        //     wifi_status.state, wifi_status.ssid, wifi_status.rssi);

        // 更新WiFi连接状态
        if (home->wifi_state != wifi_status.state) {
            home->wifi_state = wifi_status.state;
            need_refresh = true;

            // set sysled 闪烁
            if (wifi_status.state == WIFI_STATE_CONNECTED) {
                // 连接成功, 常亮
                ext_led_set(GPIO_SYS_LED, 0x01);
            } else if (wifi_status.state == WIFI_STATE_CONNECTING) {
                // 连接中, 快闪
                ext_led_flash(GPIO_SYS_LED, 0xAA, 0xFF);
            } else {
                // 离线, 慢闪
                ext_led_flash(GPIO_SYS_LED, 0x01, 0xFFFFFFFF);
            }
        }

        // 更新SSID
        if (strcmp(home->ssid, wifi_status.ssid) != 0) {
            strncpy(home->ssid, wifi_status.ssid, sizeof(home->ssid) - 1);
            need_refresh = true;
        }

        // 更新IP地址
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", 
            (int)(wifi_status.ip_addr >> 0) & 0xFF,
            (int)(wifi_status.ip_addr >> 8) & 0xFF,
            (int)(wifi_status.ip_addr >> 16) & 0xFF,
            (int)(wifi_status.ip_addr >> 24) & 0xFF
        );
        if (strcmp(home->ip_address, ip_str) != 0) {
            strncpy(home->ip_address, ip_str, sizeof(home->ip_address) - 1);
            need_refresh = true;
        }

        // 更新信号强度等级
        int8_t signal_level;
        if (wifi_status.state != WIFI_STATE_CONNECTED) {
            signal_level = 0;
        } else if (wifi_status.rssi >= -55) {
            signal_level = 4;
        } else if (wifi_status.rssi >= -66) {
            signal_level = 3;
        } else if (wifi_status.rssi >= -77) {
            signal_level = 2;
        } else {
            signal_level = 1;
        }

        if (home->signal_level != signal_level) {
            home->signal_level = signal_level;
            need_refresh = true;
        }
    }

    // 如果有数据更新，设置页面需要刷新
    if (need_refresh) {
        ctx->page.dirty = true;
    }
}

/**
 * @brief 更新主页动画状态
 * 
 * @param ctx 显示上下文
 * @return true 如果动画状态发生改变
 * @return false 如果动画状态未改变
 */
static bool update_home_animation(display_context_t* ctx)
{
    page_home_data_t* home = &ctx->page.home;
    sys_tick_t now = uptime();

    // 只有在主页时才更新动画
    if (ctx->page.current_page != PAGE_HOME) {
        return false;
    }

    // 检查是否需要更新动画
    if (uptime_after(now, home->animation_line.last_update_time)) {
        home->animation_line.last_update_time = now + ANIMATION_UPDATE_MS;
        home->animation_line.eraser_position++;
        if (home->animation_line.eraser_position >= 64) {
            home->animation_line.eraser_position = 0;
        }
        return true;
    }

    return false;
}

static void draw_home_page(display_context_t* ctx)
{
    page_home_data_t* home = &ctx->page.home;

    // 初始化显示内容
    const lcd_mono_img_t* signal_img = NULL;

    // 在右上角(x=114,y=1)显示信号满格图标
    switch(home->signal_level) {
        case 1:
            signal_img = LCD_IMG(signal_1);
            break;
        case 2:
            signal_img = LCD_IMG(signal_2);
            break;
        case 3:
            signal_img = LCD_IMG(signal_3);
            break;
        case 4:
            signal_img = LCD_IMG(signal_4);
            break;
        default:
            signal_img = LCD_IMG(no_signal_2);
            break;                
    }

#define LINE1_BOTTOM_Y    12    
#define LINE1_BOTTOM_WIDTH 128
#define LINE2_TEXT_Y LINE1_BOTTOM_Y + 3
#define LINE3_TEXT_Y LINE2_TEXT_Y + 3 + 16
#define LINE4_TOP_Y LINE3_TEXT_Y + 2 + 16
#define LINE4_TEXT_Y LINE4_TOP_Y + 3

    if (signal_img) {   
        lcd_display_mono_img(ctx->lcd_handle, 114, 1, signal_img, false);
    }

    lcd_display_string(ctx->lcd_handle, 0, 2, home->ip_address, LCD_FONT(ascii_8x8), false);

    // 显示状态信息, 如果是离线,显示OFFLINE, 如果是已连接,显示IP地址.
    if (home->wifi_state == WIFI_STATE_CONNECTED) {
        lcd_display_string(ctx->lcd_handle, 0, 2, home->ip_address, LCD_FONT(ascii_8x8), false);
    } else if (home->wifi_state == WIFI_STATE_CONNECTING) {
        lcd_display_string(ctx->lcd_handle, 0, 2, "CONNECT...", LCD_FONT(ascii_8x8), false);
    } else {
        lcd_display_string(ctx->lcd_handle, 0, 2, "OFFLINE", LCD_FONT(ascii_8x8), false);
    }

    // 绘制基础线条
    lcd_draw_horizontal_line(ctx->lcd_handle, 0, LINE1_BOTTOM_Y, LINE1_BOTTOM_WIDTH, 2, false);
    
    // 如果动画位置大于0，擦除对应位置的像素
    if (home->animation_line.eraser_position > 0) {
        // 擦除左边
        if (64 - home->animation_line.eraser_position >= 0) {
            lcd_clear_area(ctx->lcd_handle, 
                64 - home->animation_line.eraser_position - 1, 
                LINE1_BOTTOM_Y, 
                4, 
                2);
        }
        // 擦除右边
        if (64 + home->animation_line.eraser_position < 128) {
            lcd_clear_area(ctx->lcd_handle, 
                64 + home->animation_line.eraser_position - 1, 
                LINE1_BOTTOM_Y, 
                4, 
                2);
        }
    }

    // 第二行：显示网络图标和SSID (y=20)
    lcd_display_mono_img(ctx->lcd_handle, 0, LINE2_TEXT_Y, LCD_IMG(network), false);
    if (home->wifi_state == WIFI_STATE_CONNECTED) {
        lcd_display_string(ctx->lcd_handle, 20, LINE2_TEXT_Y + 1, home->ssid, LCD_FONT(ascii_8x16), false);
    } else if (home->wifi_state == WIFI_STATE_CONNECTING) {
        lcd_display_string(ctx->lcd_handle, 20, LINE2_TEXT_Y + 1, home->ssid, LCD_FONT(ascii_8x16), false);
    } else {
        lcd_display_string(ctx->lcd_handle, 20, LINE2_TEXT_Y + 1, "N/A", LCD_FONT(ascii_8x16), false);
    }

    // 第三行：显示串口图标和波特率 (y=36)
    char baudrate_str[16];
    snprintf(baudrate_str, sizeof(baudrate_str), "%" PRIu32, home->baudrate);
    lcd_display_mono_img(ctx->lcd_handle, 0, LINE3_TEXT_Y - 1, LCD_IMG(serial), false);
    lcd_display_string(ctx->lcd_handle, 20, LINE3_TEXT_Y, baudrate_str, LCD_FONT(ascii_8x16), false);

    // 第四行：显示收发字节数 (y=52)
    char rx_str[16], tx_str[16];
    snprintf(rx_str, sizeof(rx_str), "R:%" PRIu32, home->rx_bytes);
    snprintf(tx_str, sizeof(tx_str), "T:%" PRIu32, home->tx_bytes);
    
    // 绘制行四上边界
    lcd_draw_horizontal_line(ctx->lcd_handle, 0, LINE4_TOP_Y, 128, 1, false);

    // 绘制中间的竖线
    lcd_draw_vertical_line(ctx->lcd_handle, 64, LINE4_TOP_Y, 64 - LINE4_TOP_Y, 1, false);
    
    // 显示接收和发送字节数
    lcd_display_string(ctx->lcd_handle, 2, LINE4_TEXT_Y, rx_str, LCD_FONT(ascii_8x8), false);
    lcd_display_string(ctx->lcd_handle, 66, LINE4_TEXT_Y, tx_str, LCD_FONT(ascii_8x8), false);
}