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
#include "time.h"
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
    PAGE_UART,
    PAGE_NETWORK,
    PAGE_HELP,
    PAGE_MAX
} display_page_t;


// 定义一个弹出框
typedef enum {
    POPUP_NONE = 0,
    POPUP_MENU,
    POPUP_MSG,
    POPUP_MAX
}display_popup_t;


typedef enum {
    MSG_ID_NETWORK_ALREADY_CONNECTED = 0,
    MSG_ID_NETWORK_NOT_AVAILABLE,
    MSG_ID_START_CONNECTING_NETWORK,
    MSG_ID_NO_SAVED_NETWORK,
    MSG_ID_START_SCANING_NETWORK,
}popup_msg_id_t;


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


typedef struct {
    uint8_t selected_index;
    uint8_t display_num;
    uint8_t baudrate_num;
}page_uart_data_t;


// 消息框数据结构
typedef struct {
    popup_msg_id_t msg_id;
}popup_msg_data_t;

// 菜单框数据结构       
typedef struct {
#define _MENU_ENTRY_UART 0
#define _MENU_ENTRY_NETWORK 1
#define _MENU_ENTRY_HELP 2
#define _MENU_ENTRY_MAX 3    
    uint8_t selected_index;
}popup_menu_data_t;

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
        bool page_changed;
        display_page_t current_page;    
        display_page_t previous_page;
        sys_tick_t page_expried_time;
        page_home_data_t home;
        page_uart_data_t uart;
    } page;

    struct {
        bool dirty;
        display_popup_t current_popup;
        sys_tick_t popup_expried_time;
        popup_menu_data_t menu;
        popup_msg_data_t msg;
    } popup;


    sys_tick_t data_update_time;
}display_context_t;


static const uint32_t s_supported_baudrates[] = {
    9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
};

// 显示上下文全局变量
static display_context_t s_display_context = { 0 };

// 更新显示数据
static void display_update_data(display_context_t* ctx);

// 绘制主页
static void draw_home_page(display_context_t* ctx);

// 绘制串口页面
static void draw_uart_page(display_context_t* ctx);

// 绘制网络页面
static void draw_network_page(display_context_t* ctx);

// 绘制帮助页面
static void draw_help_page(display_context_t* ctx);

// 绘制弹出框
static void draw_popup(display_context_t* ctx);

// 显示弹出消息
static void active_popup_msg(display_context_t* ctx, popup_msg_id_t msg_id);

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

// 切换页面
static void switch_page(display_context_t* ctx, display_page_t target)
{
    if (ctx->page.current_page == target) {
        return;
    }

    ctx->page.previous_page = ctx->page.current_page;
    ctx->page.current_page = target;    
    ctx->page.dirty = true;
    ctx->page.page_changed = true;
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
    display_context_t* ctx = get_context();
    ext_gpio_event_data_t* data = (ext_gpio_event_data_t*)event_data;

    if (data->gpio_id != GPIO_BUTTON) {
        return;
    }
    
    // 根据事件类型进行不同处理
    switch(id) {
        case EXT_GPIO_EVENT_BUTTON_PRESSED:
            ESP_LOGI(TAG, "button event: [%s] pressed, click_count: %d", data->gpio_name, data->data.button.click_count);
            break; 
            
        case EXT_GPIO_EVENT_BUTTON_RELEASED:
            ESP_LOGI(TAG, "button event: [%s] released", data->gpio_name);         
            break;
            
        case EXT_GPIO_EVENT_BUTTON_LONG_PRESSED:
            ESP_LOGI(TAG, "button event: [%s] long pressed up to %d seconds", data->gpio_name, data->data.button.long_pressed);            
            break;
            
        case EXT_GPIO_EVENT_BUTTON_CONTINUE_CLICK:
            ESP_LOGI(TAG, "button event: [%s] continue click stopped, click count: %d", data->gpio_name, data->data.button.click_count);

            /*
            主页:
               单击显示菜单, 切换选项, 双击进入选择的子菜单, 超时关闭菜单 

            帮助页:
                任意按键退出, 超时(60秒)退出. 
                不会唤起弹出菜单. 

            串口页:
                单击切换选项, 双击确认选项,并回到 home 
                超时关闭菜单 
                不会唤起弹出菜单. 

            网络页:
                单击切换选项, 双击确认选项,并回到 home 
                超时关闭菜单 
                不会唤起弹出菜单. 
            */


            if (data->data.button.click_count == 1) 
            {
                if (ctx->page.current_page == PAGE_HOME) 
                {
                    if (ctx->popup.current_popup == POPUP_MENU) {
                        // 如果菜单已经显示, 则切换菜单 
                        ctx->popup.menu.selected_index = (ctx->popup.menu.selected_index + 1) % _MENU_ENTRY_MAX;
                        ctx->popup.dirty = true;
                        ctx->popup.popup_expried_time = uptime() + 10000; // 10秒后自动关闭
                    } else {
                        // 显示菜单
                        ctx->popup.current_popup = POPUP_MENU;
                        ctx->popup.menu.selected_index = 0;
                        ctx->popup.dirty = true;
                        ctx->popup.popup_expried_time = uptime() + 10000;
                    }                       
                } 
                else if (ctx->page.current_page == PAGE_UART)
                {
                    // 定义支持的波特率列表
                    ctx->page.uart.selected_index = (ctx->page.uart.selected_index + 1) % ctx->page.uart.baudrate_num;
                    ctx->page.dirty = true;
                    // 延长页面显示时间
                    ctx->page.page_expried_time = uptime() + 60000; // 60秒后自动返回主页
                }
                else // 其他子页,单击 可以延长时间, 超时返回主页
                {
                    ctx->page.page_expried_time = uptime() + 60000; // 60秒后自动返回主页
                }
            }
            // 双击显示菜单, 或确认选项, 或关闭菜单 
            else if (data->data.button.click_count == 2) 
            {
                if (ctx->page.current_page == PAGE_HOME) 
                {
                    if (ctx->popup.current_popup == POPUP_MENU) {    
                        // 双击进入选择的子菜单 
                        switch (ctx->popup.menu.selected_index) {
                            case _MENU_ENTRY_UART:
                                ESP_LOGI(TAG, "enter uart menu");
                                switch_page(ctx, PAGE_UART);
                                break;
                            case _MENU_ENTRY_NETWORK:
                                ESP_LOGI(TAG, "enter network menu");
                                switch_page(ctx, PAGE_NETWORK);
                                break;
                            case _MENU_ENTRY_HELP:
                                ESP_LOGI(TAG, "enter help menu");
                                switch_page(ctx, PAGE_HELP);                                
                                break;
                            default:
                                break;
                        }
                        
                        ctx->page.page_expried_time = uptime() + 60000; // 60秒后自动返回主页
                        ctx->popup.current_popup = POPUP_NONE;
                        ctx->popup.dirty = true;
                        
                    } else {
                        // 显示菜单
                        ctx->popup.current_popup = POPUP_MENU;
                        ctx->popup.menu.selected_index = 0;
                        ctx->popup.dirty = true;
                        ctx->popup.popup_expried_time = uptime() + 10000;
                    }
                } 
                else if (ctx->page.current_page == PAGE_HELP) 
                {
                    switch_page(ctx, PAGE_HOME);
                }
                else if (ctx->page.current_page == PAGE_UART) 
                {
                    // 获取选中的波特率
                    ctx->page.home.baudrate = s_supported_baudrates[ctx->page.uart.selected_index];
                    // TODO: 保存到配置
                    ESP_LOGI(TAG, "TODO: apply baudrate: %u", ctx->page.home.baudrate);
                    // 返回主页
                    switch_page(ctx, PAGE_HOME);
                }
                else if (ctx->page.current_page == PAGE_NETWORK) 
                {
                    switch_page(ctx, PAGE_HOME);
                }
            }
            // 三击显示消息
            // else if (data->data.button.click_count == 3) {
            //     ctx->popup.current_popup = POPUP_MSG;
            //     //snprintf(ctx->popup.msg.message, sizeof(ctx->popup.msg.message), "TODO");
            //     ctx->popup.dirty = true;
            //     ctx->popup.popup_expried_time = uptime() + 5000; // 5秒后自动关闭
            // }
            break;
    }
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
    ctx->page.previous_page = PAGE_HOME;
    ctx->page.page_expried_time = 0;
    ctx->page.home.wifi_state = WIFI_STATE_DISCONNECTED;
    ctx->page.home.signal_level = 0;
    strcpy(ctx->page.home.ssid, "N/A");
    strcpy(ctx->page.home.ip_address, "0.0.0.0");
    ctx->page.home.baudrate = 115200;
    ctx->page.home.rx_bytes = 0;
    ctx->page.home.tx_bytes = 0;

    ctx->page.uart.selected_index = 0;
    ctx->page.uart.display_num =4;
    ctx->page.uart.baudrate_num = sizeof(s_supported_baudrates) / sizeof(s_supported_baudrates[0]);
    for (int i = 0; i < ctx->page.uart.baudrate_num; i++) {
        if (ctx->page.home.baudrate == s_supported_baudrates[i]) {
            ctx->page.uart.selected_index = i;
            break;
        }
    }

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
    const TickType_t refresh_period = pdMS_TO_TICKS(1000 / DISPLAY_REFRESH_RATE_HZ);
    
    ESP_LOGI(TAG, "Display task started, refresh rate: %dHz", DISPLAY_REFRESH_RATE_HZ);

    display_context_t* ctx = get_context();
    ctx->data_update_time = uptime();

    while (1) {
        sys_tick_t now = uptime();
        bool refresh = false;

        // 刷新数据
        if (uptime_after(now, ctx->data_update_time)) {
            display_update_data(ctx);
            ctx->data_update_time = now + 250;
        }

        // 更新动画状态
        if (update_home_animation(ctx)) {
            ctx->page.dirty = true;
        }

        // 检查弹出框是否超时
        if (ctx->popup.current_popup != POPUP_NONE && 
            uptime_after(now, ctx->popup.popup_expried_time)) {
            ctx->popup.current_popup = POPUP_NONE;
            ctx->popup.dirty = true;
        }

        // 检查页面是否超时
        if (ctx->page.current_page != PAGE_HOME && 
            uptime_after(now, ctx->page.page_expried_time)) {
            switch_page(ctx, PAGE_HOME);
        }

        // 检查是否发生了切页 
        if (ctx->page.page_changed)
        {
            ESP_LOGI(TAG, "page changed from %d to %d", ctx->page.previous_page, ctx->page.current_page);
            ctx->page.page_changed = false;

            if (ctx->page.current_page == PAGE_NETWORK) {
                // 执行相关任务,如果有的话 
                if (ctx->popup.msg.msg_id == MSG_ID_NETWORK_ALREADY_CONNECTED) {
                    active_popup_msg(ctx, MSG_ID_NETWORK_NOT_AVAILABLE);
                } else if (ctx->popup.msg.msg_id == MSG_ID_NETWORK_NOT_AVAILABLE) {
                    active_popup_msg(ctx, MSG_ID_START_CONNECTING_NETWORK);
                } else if (ctx->popup.msg.msg_id == MSG_ID_START_CONNECTING_NETWORK) {
                    active_popup_msg(ctx, MSG_ID_NETWORK_ALREADY_CONNECTED);
                }
            }
        }

        // 刷新显示
        if (ctx->page.dirty || ctx->popup.dirty) 
        {            
            // 清屏
            lcd_fill(ctx->lcd_handle, 0x00);

            // 绘制当前页面
            switch (ctx->page.current_page) {
                case PAGE_HOME:
                    draw_home_page(ctx);
                    break;
                case PAGE_UART:
                    draw_uart_page(ctx);
                    break;
                case PAGE_NETWORK:
                    draw_network_page(ctx);
                    break;
                case PAGE_HELP: 
                    draw_help_page(ctx);
                    break;
                default:    
                    draw_home_page(ctx);                
                    break;
            }

            // 绘制弹出框
            if (ctx->popup.current_popup != POPUP_NONE) {
                draw_popup(ctx);
            }

            ctx->page.dirty = false;
            ctx->popup.dirty = false;
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

#define POPUP_WIDTH     108
#define POPUP_HEIGHT    44
#define POPUP_MARGIN    3
#define POPUP_PADDING   2
#define POPUP_X         ((128 - POPUP_WIDTH) / 2)
#define POPUP_Y         14

#define POPUP_FRAME_WIDTH  (POPUP_WIDTH - POPUP_MARGIN * 2)
#define POPUP_FRAME_HEIGHT (POPUP_HEIGHT - POPUP_MARGIN * 2)
#define POPUP_FRAME_X      (POPUP_X + POPUP_MARGIN)
#define POPUP_FRAME_Y      (POPUP_Y + POPUP_MARGIN)


static void draw_popup_msg(display_context_t* ctx, popup_msg_id_t msg_id)
{
    const char *line1 = NULL;
    const char *line2 = NULL;

    if (msg_id == MSG_ID_NETWORK_ALREADY_CONNECTED) {
        line1 = "Network";
        line2 = "Connected";
    } else if (msg_id == MSG_ID_NETWORK_NOT_AVAILABLE) {
        line1 = "Network";
        line2 = "Not Exist";
    } else if (msg_id == MSG_ID_START_CONNECTING_NETWORK) {
        line1 = "Start";
        line2 = "Connecting";
    } else if (msg_id == MSG_ID_NO_SAVED_NETWORK) {
        line1 = "No Saved";
        line2 = "Network";
    } else if (msg_id == MSG_ID_START_SCANING_NETWORK) {
        line1 = "Start";
        line2 = "Scanning";
    } else {
        ESP_LOGE(TAG, "invalid popup message id: %d", msg_id);
        return;
    }

    if (line1) {
        // 计算第一行文本宽度并居中显示
        int text_width = strlen(line1) * 8; // ascii_8x16字体宽度为8
        int x = POPUP_FRAME_X + (POPUP_FRAME_WIDTH - text_width) / 2;
        lcd_display_string(ctx->lcd_handle,
            x,
            POPUP_FRAME_Y + POPUP_PADDING,
            line1,
            LCD_FONT(ascii_8x16), false);
    }

    if (line2) {
        // 计算第二行文本宽度并居中显示
        int text_width = strlen(line2) * 8; // ascii_8x16字体宽度为8
        int x = POPUP_FRAME_X + (POPUP_FRAME_WIDTH - text_width) / 2;
        lcd_display_string(ctx->lcd_handle,
            x,
            POPUP_FRAME_Y + POPUP_PADDING + 16, // 第二行Y坐标增加16(字体高度)
            line2,
            LCD_FONT(ascii_8x16), false);
   }
}


/**
 * @brief 绘制弹出框
 * 
 * @param ctx 显示上下文
 */
static void draw_popup(display_context_t* ctx)
{
    if (ctx->popup.current_popup == POPUP_NONE) {
        return;
    }

    // 清除弹出框区域
    lcd_clear_area(ctx->lcd_handle, 
        POPUP_X, 
        POPUP_Y, 
        POPUP_WIDTH,
        POPUP_HEIGHT
    );

    // 绘制边框
    // x+m, y+m, x+w-m, y+h-m
    lcd_draw_rectangle1(ctx->lcd_handle,
        POPUP_FRAME_X,
        POPUP_FRAME_Y,
        POPUP_FRAME_WIDTH,
        POPUP_FRAME_HEIGHT,
        1,
        false
    );

    // 根据弹出框类型绘制内容
    switch (ctx->popup.current_popup) {
        case POPUP_MENU:
        {
            const int icon_y = POPUP_FRAME_Y + 8;
            const int img_width = 16;
            const int icon_spacing = (POPUP_FRAME_WIDTH - img_width * 3) / 4;
            
            // 绘制三个图标
            lcd_display_mono_img(ctx->lcd_handle, 
                POPUP_FRAME_X + icon_spacing, 
                icon_y, 
                LCD_IMG(serial), 
                false);

            lcd_display_mono_img(ctx->lcd_handle, 
                POPUP_FRAME_X + icon_spacing * 2 + img_width, 
                icon_y, 
                LCD_IMG(network), 
                false);

            lcd_display_mono_img(ctx->lcd_handle, 
                POPUP_FRAME_X + icon_spacing * 3 + img_width * 2, 
                icon_y, 
                LCD_IMG(help), 
                false);

            // 在选中的图标下方绘制下划线
            const int underline_y = icon_y + img_width + 2; // 图标下方2像素的间隙
            const int underline_width = 12;  // 下划线宽度略小于图标
            const int underline_height = 3;  // 下划线高度

            // 根据选中的菜单项计算下划线位置
            int underline_x = POPUP_FRAME_X + icon_spacing + (img_width - underline_width) / 2; // 默认在第一个图标下
            switch(ctx->popup.menu.selected_index) {
                case _MENU_ENTRY_UART:
                    // 使用默认值
                    break;
                case _MENU_ENTRY_NETWORK:
                    underline_x = POPUP_FRAME_X + icon_spacing * 2 + img_width + (img_width - underline_width) / 2;
                    break;
                case _MENU_ENTRY_HELP:
                    underline_x = POPUP_FRAME_X + icon_spacing * 3 + img_width * 2 + (img_width - underline_width) / 2;
                    break;
                default:
                    break;
            }

            // 绘制下划线
            lcd_draw_horizontal_line(ctx->lcd_handle,
                underline_x,
                underline_y,
                underline_width,
                underline_height,
                false
            );
            break;
        }
        case POPUP_MSG:
        {
            draw_popup_msg(ctx, ctx->popup.msg.msg_id);
            break;
        }
        default:
            break;
    }
}


static void draw_uart_page(display_context_t* ctx)
{
    // 定义布局参数
    const int left_width = 20;  // 左侧区域宽度
    const int divider_width = 2;  // 分隔线宽度
    const int line_height = 16;  // 每行高度(8x16字体)
    const int right_start_x = left_width + divider_width + 10;  // 右侧内容起始x坐标(加10像素间距)
    const int right_icon_width = 16;
    const int start_y = 0;  // 从顶部开始显示
    
    // 绘制左侧串口图标
    const int icon_x = (left_width - 16) / 2;  // 16是图标宽度，水平居中
    const int icon_y = (64 - 16) / 2;  // 垂直居中显示图标
    lcd_display_mono_img(ctx->lcd_handle, icon_x, icon_y, LCD_IMG(serial), false);
    
    // 绘制分隔线
    lcd_draw_vertical_line(ctx->lcd_handle, left_width, 0, 64, divider_width, false);
    
    // 计算显示的波特率起始索引
    int start_index = 0;
    if (ctx->page.uart.selected_index >= ctx->page.uart.display_num) {
        start_index = ctx->page.uart.selected_index - ctx->page.uart.display_num + 1;
    }

    // 显示波特率列表
    for (int i = 0; i < ctx->page.uart.display_num; i++) 
    {
        if ((start_index + i) >= ctx->page.uart.baudrate_num) {
            ESP_LOGE(TAG, "uart baudrate index out of range");
            return;
        }

        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%u", (unsigned int)s_supported_baudrates[start_index + i]);
        
        // 如果是当前波特率，在行首显示'>'符号
        if ((start_index + i) == ctx->page.uart.selected_index) {
            lcd_display_string(ctx->lcd_handle, right_start_x, start_y + i * line_height, ">", LCD_FONT(ascii_8x16), false);
        }
        
        // 显示波特率值
        lcd_display_string(ctx->lcd_handle, right_start_x + right_icon_width, start_y + i * line_height, buffer, LCD_FONT(ascii_8x16), false);
    }
}

static void draw_network_page(display_context_t* ctx)
{
    // 定义布局参数
    const int left_width = 20;  // 左侧区域宽度
    const int divider_width = 2;  // 分隔线宽度
    // const int line_height = 16;  // 每行高度(8x16字体)
    // const int right_start_x = left_width + divider_width + 10;  // 右侧内容起始x坐标(加10像素间距)
    // const int right_icon_width = 16;
    // const int start_y = 0;  // 从顶部开始显示
    
    // 绘制左侧串口图标
    const int icon_x = (left_width - 16) / 2;  // 16是图标宽度，水平居中
    const int icon_y = (64 - 16) / 2;  // 垂直居中显示图标
    lcd_display_mono_img(ctx->lcd_handle, icon_x, icon_y, LCD_IMG(network), false);
    
    // 绘制分隔线
    lcd_draw_vertical_line(ctx->lcd_handle, left_width, 0, 64, divider_width, false);
}

static void draw_help_page(display_context_t* ctx)
{
    // 绘制帮助页面
    lcd_display_mono_img(ctx->lcd_handle, 32, 0, LCD_IMG(qrcode), false);
}


/// 显示弹出消息, 消息显示3秒后自动消失
/// 有条件显示, 当前不在菜单栏才可以显示消息.
static void active_popup_msg(display_context_t* ctx, popup_msg_id_t msg_id)
{
    if (ctx->popup.current_popup == POPUP_MENU) {
        return;
    }

    ctx->popup.current_popup = POPUP_MSG;
    ctx->popup.msg.msg_id = msg_id;
    ctx->popup.popup_expried_time = uptime() + 3000;
}
