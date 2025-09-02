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
#include "uart_bridge.h"
#include "lcd_models.h"
#include "lcd_fonts.h"
#include "img_icons.h"
#include "ext_gpio.h"
#include "export_ids.h"
#include "app_event_loop.h"
#include "time.h"
#include "uptime.h"
#include "wifi_station.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "version.h"

static const char *TAG = "display";

// 定义显示任务参数
#define DISPLAY_TASK_STACK_SIZE    4096
#define DISPLAY_TASK_PRIORITY      3
#define DISPLAY_REFRESH_RATE_HZ    20  // 10Hz刷新率

// 定义动画参数
#define ANIMATION_UPDATE_MS        50 // 动画更新周期(ms)

// 定义内部按键事件队列
#define DISPLAY_BUTTON_QUEUE_SIZE  8

// 内部按键事件结构
typedef struct {
    ext_gpio_event_data_t gpio_event;
    int32_t event_id;
} display_button_event_t;

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
    MSG_ID_STATISTICS_CLEARED,
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
    uint8_t client_num;
    uint16_t ip_port;
    uint8_t cpu_usaged;

    sys_tick_t cpu_usage_update_time;

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

typedef struct {
#define PAGE_STATE_ENTER_NETWORK_PAGE 0
#define PAGE_STATE_CHECK_SCAN_RESULT 1

    uint8_t state;
    uint8_t selected_index;
    uint8_t display_num;
    uint8_t network_num;
    
    // 添加保存的网络记录
    wifi_connection_record_t saved_networks[WIFI_STATION_MAX_RECORDS];
    uint8_t saved_network_count;
    
    // 添加网络信号等级信息 
    int8_t network_signal_levels[WIFI_STATION_MAX_RECORDS];  // 0表示无信号，1-4表示信号等级
}page_network_data_t;


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
    bool cpu_usage_enabled;
    bool force_help_mode;  // 强制帮助页模式，没有配网时启用

    TaskHandle_t task_handle;
    lcd_handle_t lcd_handle;
    
    // 添加按键事件队列
    QueueHandle_t button_queue;
    
    struct {
        bool dirty;
        bool page_changed;
        display_page_t current_page;    
        display_page_t previous_page;
        sys_tick_t page_expried_time;
        page_home_data_t home;
        page_uart_data_t uart;
        page_network_data_t network;  // 添加网络页面数据
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

// 支持的波特率列表, 已在cli_impl.c中定义
extern const uint32_t g_supported_baudrates[];
extern const int g_supported_baudrates_count;

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

// 处理按键事件（在显示任务中执行）
static void handle_button_event(display_context_t* ctx, const display_button_event_t* button_event);

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

    ESP_LOGD(TAG, "switch page from %d to %d", ctx->page.current_page, target);

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
    
    // 创建内部事件并发送到队列
    display_button_event_t button_event = {
        .gpio_event = *data,
        .event_id = id
    };
    
    // 非阻塞发送到队列
    if (ctx->button_queue != NULL) {
        BaseType_t result = xQueueSend(ctx->button_queue, &button_event, 0);
        if (result != pdTRUE) {
            ESP_LOGW(TAG, "Button event queue full, dropping event");
        }
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
    
    // 创建按键事件队列
    s_display_context.button_queue = xQueueCreate(DISPLAY_BUTTON_QUEUE_SIZE, sizeof(display_button_event_t));
    if (s_display_context.button_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create button event queue");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建一个OLED显示器
    s_display_context.lcd_handle = lcd_display_create(LCD_DRIVER(i2c), LCD_MODEL(ssd1312), LCD_ROTATION_0, NULL, 0);
    if (s_display_context.lcd_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create display");
        vQueueDelete(s_display_context.button_queue);
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

    // 检测保存的网络数量，决定是否启用强制帮助页模式
    wifi_connection_record_t saved_networks[WIFI_STATION_MAX_RECORDS];
    uint8_t saved_network_count = WIFI_STATION_MAX_RECORDS;
    esp_err_t ret = wifi_station_get_records(saved_networks, &saved_network_count);
    
    if (ret != ESP_OK || saved_network_count == 0) {
        // 没有保存的网络，启用强制帮助页模式
        ctx->force_help_mode = true;
        ESP_LOGI(TAG, "No saved networks found, entering force help mode");
    } else {
        ctx->force_help_mode = false;
        ESP_LOGI(TAG, "Found %d saved networks, normal mode", saved_network_count);
    }

    // 初始化页面数据
    ctx->page.dirty = true;
    if (ctx->force_help_mode) {
        ctx->page.current_page = PAGE_HELP;
        ctx->page.previous_page = PAGE_HELP;
        ctx->page.page_expried_time = 0; // 不设置超时，永久显示
    } else {
        ctx->page.current_page = PAGE_HOME;
        ctx->page.previous_page = PAGE_HOME;
        ctx->page.page_expried_time = 0;
    }
    ctx->page.home.wifi_state = WIFI_STATE_DISCONNECTED;
    ctx->page.home.signal_level = 0;
    strcpy(ctx->page.home.ssid, "N/A");
    strcpy(ctx->page.home.ip_address, "0.0.0.0");
    ctx->page.home.baudrate = 0;
    ctx->page.home.rx_bytes = 0;
    ctx->page.home.tx_bytes = 0;
    ctx->page.home.client_num = 0;
    ctx->page.home.ip_port = 0;
    ctx->cpu_usage_enabled = false;
    ctx->page.home.cpu_usaged = 0;
    ctx->page.home.cpu_usage_update_time = uptime();

    ctx->page.uart.selected_index = 0;
    ctx->page.uart.display_num =4;
    ctx->page.uart.baudrate_num = g_supported_baudrates_count;

    // 创建显示任务
    BaseType_t task_ret = xTaskCreate(
        display_task,
        "display_task",
        DISPLAY_TASK_STACK_SIZE,
        NULL,
        DISPLAY_TASK_PRIORITY,
        &ctx->task_handle
    );
    
    if (task_ret != pdPASS) {
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

        // 处理按键事件队列
        display_button_event_t button_event;
        while (xQueueReceive(ctx->button_queue, &button_event, 0) == pdTRUE) {
            handle_button_event(ctx, &button_event);
        }

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

        // 检查页面是否超时（强制帮助页模式下不检查超时）
        if (!ctx->force_help_mode && 
            ctx->page.current_page != PAGE_HOME && 
            uptime_after(now, ctx->page.page_expried_time)) {
            switch_page(ctx, PAGE_HOME);
        }

        // 检查是否发生了切页 
        if (ctx->page.page_changed)
        {
            //ESP_LOGI(TAG, "page changed from %d to %d", ctx->page.previous_page, ctx->page.current_page);
            ctx->page.page_changed = false;

            if (ctx->page.current_page == PAGE_UART) 
            {
                // 计算当前波特率在支持的波特率列表中的索引
                for (int i = 0; i < ctx->page.uart.baudrate_num; i++) {
                    if (ctx->page.home.baudrate == g_supported_baudrates[i]) {
                        ctx->page.uart.selected_index = i;
                        break;
                    }
                }
            }
            // 进入网络页面时,触发网络相关逻辑启动 
            else if (ctx->page.current_page == PAGE_NETWORK) 
            {
                
                // 初始化网络页面状态
                ctx->page.network.state = PAGE_STATE_ENTER_NETWORK_PAGE;
                ctx->page.network.selected_index = 0;
                ctx->page.network.display_num = 4;  // 每页显示4个网络
                
                // 获取保存的网络记录
                ctx->page.network.saved_network_count = WIFI_STATION_MAX_RECORDS;
                esp_err_t ret = wifi_station_get_records(ctx->page.network.saved_networks, &ctx->page.network.saved_network_count);
                
                if (ret != ESP_OK || ctx->page.network.saved_network_count == 0) {
                    // 没有保存的网络，显示消息并返回主页
                    active_popup_msg(ctx, MSG_ID_NO_SAVED_NETWORK);
                    switch_page(ctx, PAGE_HOME);
                } else {
                    // 获取当前WiFi状态
                    wifi_connection_status_t wifi_status = {0};
                    if (wifi_station_get_status(&wifi_status) == ESP_OK && 
                        wifi_status.state == WIFI_STATE_CONNECTED) {
                        // 如果已连接,查找是否是已保存的网络
                        for (int i = 0; i < ctx->page.network.saved_network_count; i++) {
                            if (strcmp(wifi_status.ssid, ctx->page.network.saved_networks[i].ssid) == 0) {
                                // 找到匹配的网络,将选择器指向该网络
                                ctx->page.network.selected_index = i;
                                break;
                            }
                        }
                    }
                    
                    // 有保存的网络，启动扫描
                    ctx->page.network.network_num = ctx->page.network.saved_network_count;
                    
                    // 初始化信号等级为0
                    for (int i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
                        ctx->page.network.network_signal_levels[i] = 0;
                    }
                    
                    // 显示扫描消息并启动异步扫描
                    active_popup_msg(ctx, MSG_ID_START_SCANING_NETWORK);
                    wifi_station_start_scan_async();
                    ctx->page.network.state = PAGE_STATE_CHECK_SCAN_RESULT;
                }
            }
        }

        // 检查网络页面的扫描结果
        if (ctx->page.current_page == PAGE_NETWORK && 
            ctx->page.network.state == PAGE_STATE_CHECK_SCAN_RESULT) {
            
            if (wifi_station_is_scan_done()) {
                // 扫描完成，获取扫描结果并更新信号等级
                wifi_network_info_t scan_results[16];
                uint16_t scan_count = 16;
                
                esp_err_t ret = wifi_station_get_scan_result(scan_results, &scan_count);
                if (ret == ESP_OK) {
                    // 更新保存网络的信号等级
                    for (uint8_t i = 0; i < ctx->page.network.saved_network_count; i++) {
                        ctx->page.network.network_signal_levels[i] = 0;  // 默认无信号
                        
                        // 在扫描结果中查找匹配的网络
                        for (uint16_t j = 0; j < scan_count; j++) {
                            if (strcmp(ctx->page.network.saved_networks[i].ssid, scan_results[j].ssid) == 0) {
                                // 计算信号等级
                                if (scan_results[j].rssi >= -55) {
                                    ctx->page.network.network_signal_levels[i] = 4;
                                } else if (scan_results[j].rssi >= -66) {
                                    ctx->page.network.network_signal_levels[i] = 3;
                                } else if (scan_results[j].rssi >= -77) {
                                    ctx->page.network.network_signal_levels[i] = 2;
                                } else {
                                    ctx->page.network.network_signal_levels[i] = 1;
                                }
                                break;
                            }
                        }
                    }
                    
                    ESP_LOGI(TAG, "Network scan completed, found %d APs, updated signal levels", scan_count);
                } else {
                    ESP_LOGW(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
                }
                
                // 取消扫描消息，更新页面显示
                ctx->popup.current_popup = POPUP_NONE;
                ctx->popup.dirty = true;
                ctx->page.dirty = true;
                ctx->page.network.state = PAGE_STATE_ENTER_NETWORK_PAGE;  // 回到正常显示状态
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

#if !defined(CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS)
#error "Please enable CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS in sdkconfig"
#endif

// CPU使用率统计静态变量
static struct {
    configRUN_TIME_COUNTER_TYPE last_run_time;
    configRUN_TIME_COUNTER_TYPE last_idle_time;
    bool initialized;
} cpu_usage_context = {0};

// CPU使用率统计函数
static uint8_t get_cpu_usage(void) {
    TaskStatus_t *pxTaskStatusArray = NULL;
    UBaseType_t uxArraySize;
    configRUN_TIME_COUNTER_TYPE current_run_time, current_idle_time = 0;
    uint8_t cpu_usage = 0;
    
    // 获取任务数量，添加一些缓冲
    uxArraySize = uxTaskGetNumberOfTasks() + 4;
    
    // 分配内存存储任务状态
    pxTaskStatusArray = pvPortMalloc(uxArraySize * sizeof(TaskStatus_t));
    if (pxTaskStatusArray == NULL) {
        ESP_LOGW(TAG, "Failed to allocate memory for CPU usage calculation");
        return 0;
    }
    
    // 获取当前系统状态
    uxArraySize = uxTaskGetSystemState(pxTaskStatusArray, uxArraySize, &current_run_time);
    if (uxArraySize == 0) {
        vPortFree(pxTaskStatusArray);
        return 0;
    }
    
    // 计算IDLE任务的运行时间（通常IDLE任务名为"IDLE"或"IDLE0"、"IDLE1"等）
    ESP_LOGD(TAG, "Task list (%d tasks):", uxArraySize);
    for (UBaseType_t i = 0; i < uxArraySize; i++) {
        ESP_LOGD(TAG, "  %s: runtime=%"PRIu32, pxTaskStatusArray[i].pcTaskName, pxTaskStatusArray[i].ulRunTimeCounter);
        if (strncmp(pxTaskStatusArray[i].pcTaskName, "IDLE", 4) == 0) {
            current_idle_time += pxTaskStatusArray[i].ulRunTimeCounter;
        }
    }
    ESP_LOGD(TAG, "Current total runtime: %"PRIu32", idle time: %"PRIu32, current_run_time, current_idle_time);
    
    // 如果是第一次调用，只保存当前值，不计算CPU使用率
    if (!cpu_usage_context.initialized) {
        cpu_usage_context.last_run_time = current_run_time;
        cpu_usage_context.last_idle_time = current_idle_time;
        cpu_usage_context.initialized = true;
        cpu_usage = 0;
        ESP_LOGD(TAG, "First call, initializing with total=%"PRIu32", idle=%"PRIu32, current_run_time, current_idle_time);
    } else {
        // 计算时间差
        configRUN_TIME_COUNTER_TYPE total_elapsed_time = current_run_time - cpu_usage_context.last_run_time;
        configRUN_TIME_COUNTER_TYPE idle_elapsed_time = current_idle_time - cpu_usage_context.last_idle_time;
        
        ESP_LOGD(TAG, "Time diff: total=%"PRIu32", idle=%"PRIu32, total_elapsed_time, idle_elapsed_time);
        
        if (total_elapsed_time > 0) {
            // CPU使用率 = (总时间 - 空闲时间) / 总时间 * 100
            // 考虑多核情况，除以核心数
            uint32_t used_time = total_elapsed_time - idle_elapsed_time;
            uint32_t adjusted_total = total_elapsed_time / CONFIG_FREERTOS_NUMBER_OF_CORES;
            uint32_t usage_percentage = (used_time * 100) / adjusted_total;
            
            ESP_LOGD(TAG, "CPU calc: used=%"PRIu32", adjusted_total=%"PRIu32", percentage=%"PRIu32"%%", 
                used_time, adjusted_total, usage_percentage);
            
            // 限制在0-100%范围内
            if (usage_percentage > 100) {
                usage_percentage = 100;
            }
            
            cpu_usage = (uint8_t)usage_percentage;
        }
        
        // 更新上一次的值
        cpu_usage_context.last_run_time = current_run_time;
        cpu_usage_context.last_idle_time = current_idle_time;
    }
    
    // 释放内存
    vPortFree(pxTaskStatusArray);
    
    return cpu_usage;
}

static void display_update_data(display_context_t* ctx)
{
    page_home_data_t* home = &ctx->page.home;
    wifi_connection_status_t wifi_status = {0};
    bool need_refresh = false;
    sys_tick_t now = uptime();

    // 在强制帮助页模式下，定期检查网络数量变化
    if (ctx->force_help_mode) {
        wifi_connection_record_t saved_networks[WIFI_STATION_MAX_RECORDS];
        uint8_t saved_network_count = WIFI_STATION_MAX_RECORDS;
        esp_err_t ret = wifi_station_get_records(saved_networks, &saved_network_count);
        
        if (ret == ESP_OK && saved_network_count > 0) {
            // 检测到有保存的网络，退出强制帮助页模式
            ESP_LOGI(TAG, "Network configuration detected, exiting force help mode");
            ctx->force_help_mode = false;
            switch_page(ctx, PAGE_HOME);
            return; // 退出强制帮助页模式后，本次不进行其他数据更新
        } else {
            // 仍然没有网络配置，在强制帮助页模式下不更新其他数据
            return;
        }
    }

    // 每隔一秒更新CPU使用率
    if (ctx->cpu_usage_enabled) {
        if (uptime_after(now, home->cpu_usage_update_time)) {
            home->cpu_usage_update_time = now + 1000;
            uint8_t cpu_usaged = get_cpu_usage();
            if (cpu_usaged != home->cpu_usaged) {
                home->cpu_usaged = cpu_usaged;
                need_refresh = true;
            }
        }
    }

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

    // 更新UART状态
    uart_bridge_status_t uart_status = {0};
    if (uart_bridge_get_status(&uart_status) == ESP_OK) {

        if ((home->client_num != uart_status.tcp_client_num)
            || (home->ip_port != uart_status.tcp_port)
            || (home->baudrate != uart_status.uart_baudrate)) {
            home->client_num = uart_status.tcp_client_num;
            home->ip_port = uart_status.tcp_port;
            home->baudrate = uart_status.uart_baudrate;
            need_refresh = true;
        }        
    }

    // 更新RX/TX字节数
    uart_bridge_stats_t stats = {0};
    if (uart_bridge_get_stats(&stats) == ESP_OK) {
        if ((home->rx_bytes != stats.uart_rx_bytes)
            || (home->tx_bytes != stats.uart_tx_bytes)) {
            home->rx_bytes = stats.uart_rx_bytes;
            home->tx_bytes = stats.uart_tx_bytes;
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
            signal_img = LCD_IMG(signal_big_1);
            break;
        case 2:
            signal_img = LCD_IMG(signal_big_2);
            break;
        case 3:
            signal_img = LCD_IMG(signal_big_3);
            break;
        case 4:
            signal_img = LCD_IMG(signal_big_4);
            break;
        default:
            signal_img = LCD_IMG(no_signal_big);
            break;                
    }

    // 显示信号图标
    lcd_display_mono_img(ctx->lcd_handle, 0, 0, signal_img, false);

    #define LINE1_TEXT_X 20
    #define LINE1_TEXT_Y 0

    #define LINE2_TEXT_X 20
    #define LINE2_TEXT_Y 18

    // 显示SSID
    if (home->wifi_state == WIFI_STATE_CONNECTED || home->wifi_state == WIFI_STATE_CONNECTING) {
        lcd_display_string(ctx->lcd_handle, LINE1_TEXT_X, LINE1_TEXT_Y, home->ssid, LCD_FONT(ascii_8x16), false);
    } /* else {
        lcd_display_string(ctx->lcd_handle, LINE1_TEXT_X, LINE1_TEXT_Y, "N/A", LCD_FONT(ascii_8x16), false);
    }*/

    // 显示状态信息, 如果是离线,显示OFFLINE, 如果是已连接,显示IP地址.

    if (home->wifi_state == WIFI_STATE_CONNECTED) {
        // 水平居中显示, 需要根据长度计算X坐标
        int text_width = strlen(home->ip_address) * 8; // ascii_8x16字体宽度为8
        int x = (128 - text_width) / 2;
        lcd_display_string(ctx->lcd_handle, x, LINE2_TEXT_Y, home->ip_address, LCD_FONT(ascii_8x8), false);
    } else if (home->wifi_state == WIFI_STATE_CONNECTING) {
        lcd_display_string(ctx->lcd_handle, 0, LINE2_TEXT_Y, "CONNECTING...", LCD_FONT(ascii_8x8), false);
    } else {
        lcd_display_string(ctx->lcd_handle, 0, LINE2_TEXT_Y, "NO NETWORK", LCD_FONT(ascii_8x8), false);
    }


    // 在最后一行显示 收发字节数
    #define LINE4_TOP_Y  (64 - 8) - 2
    #define LINE4_TEXT_Y  LINE4_TOP_Y + 3 // 2 pixes space
    // 显示一个行, 行高为1
    lcd_draw_horizontal_line(ctx->lcd_handle, 0, LINE4_TOP_Y, 128, 1, false);

    char stat_str[32];
    snprintf(stat_str, sizeof(stat_str), "%" PRIu32 "/%" PRIu32, home->rx_bytes, home->tx_bytes);
    // 右对齐显示, 需要根据长度计算X坐标
    int text_width = strlen(stat_str) * 8; // ascii_8x16字体宽度为8
    int x = (text_width > 128) ? 0 : (128 - text_width);
    lcd_display_string(ctx->lcd_handle, x, LINE4_TEXT_Y, stat_str, LCD_FONT(ascii_8x8), false);

    // 最后一行, 右对齐显示统计信息, 左侧有空间才显示标题 
    const char *stats_title = "R/T";
    text_width = strlen(stats_title) * 8;
    if (x >= text_width) {
        lcd_display_string(ctx->lcd_handle, 0, LINE4_TEXT_Y, stats_title, LCD_FONT(ascii_8x8), false);
    }


    // 如果动画位置大于0，擦除对应位置的像素
    if (home->animation_line.eraser_position > 0) {
        // 擦除左边
        if (64 - home->animation_line.eraser_position >= 0) {
            lcd_clear_area(ctx->lcd_handle, 
                64 - home->animation_line.eraser_position - 1, 
                LINE4_TOP_Y, 
                4, 
                1);
        }
        // 擦除右边
        if (64 + home->animation_line.eraser_position < 128) {
            lcd_clear_area(ctx->lcd_handle, 
                64 + home->animation_line.eraser_position - 1, 
                LINE4_TOP_Y, 
                4, 
                1);
        }
    }

    // 显示客户端数量, 端口号, 波特率 
    // 可用区域, Y=[26,53] 总共27个像素(上下预留一个空像素, 所以可用区域为25个像素), x=0-127
    #define INFO_AREA_START_Y 29
    #define INFO_AREA_HEIGHT 22
    #define INFO_AREA_PADDING 1  // 上下预留1像素

    // 定义三个显示区域的宽度
    #define AREA1_WIDTH 12   // 客户端数量区域
    #define AREA2_WIDTH 36   // 端口号区域
    #define AREA3_WIDTH 60   // 波特率区域
    #define SPACE_WIDTH 10   // 间隔区域宽度

    // 计算三个区域的起始X坐标
    const int area1_x = 0;
    const int area2_x = AREA1_WIDTH + SPACE_WIDTH;
    const int area3_x = area2_x + AREA2_WIDTH + SPACE_WIDTH;

    // 绘制三个反色背景区域
    lcd_fill_area(ctx->lcd_handle, area1_x, INFO_AREA_START_Y, AREA1_WIDTH, INFO_AREA_HEIGHT, true);
    lcd_fill_area(ctx->lcd_handle, area2_x, INFO_AREA_START_Y, AREA2_WIDTH, INFO_AREA_HEIGHT, true);
    lcd_fill_area(ctx->lcd_handle, area3_x, INFO_AREA_START_Y, AREA3_WIDTH, INFO_AREA_HEIGHT, true);

    // 准备显示文本
    char client_num_str[2];
    char port_str[5];
    char baudrate_str[8];
    
    snprintf(client_num_str, sizeof(client_num_str), "%" PRIu8, home->client_num);
    snprintf(port_str, sizeof(port_str), "%" PRIu16, home->ip_port);
    snprintf(baudrate_str, sizeof(baudrate_str), "%" PRIu32, home->baudrate);

    // 计算文本垂直居中的Y坐标
    int text_y;
    int text_x;

    // 显示客户端数量(区域1) - 居中显示
    // 8位字体左侧区域少,所以+1
    text_width = strlen(client_num_str) * 8;  // 8是字体宽度
    text_x = area1_x + (AREA1_WIDTH - text_width) / 2;
    text_y = INFO_AREA_START_Y + (INFO_AREA_HEIGHT - 16) / 2;
    lcd_display_string(ctx->lcd_handle, text_x + 1, text_y, client_num_str, LCD_FONT(ascii_8x16), true);

    #if 0

    // 显示端口号(区域2) - 居中显示
    text_width = strlen(port_str) * 8;
    text_x = area2_x + (AREA2_WIDTH - text_width) / 2;
    lcd_display_string(ctx->lcd_handle, text_x + 1, text_y, port_str, LCD_FONT(ascii_8x16), true);

    // 显示波特率(区域3) - 居中显示
    text_width = strlen(baudrate_str) * 8;
    text_x = area3_x + (AREA3_WIDTH - text_width) / 2;
    lcd_display_string(ctx->lcd_handle, text_x + 1, text_y, baudrate_str, LCD_FONT(ascii_8x16), true);

    #else 
    // 方式2, 使用8X8字体, 区域二增加显示 PORT字样, 区域三增加显示 RATE字样, 需要居中显示 
    text_y = INFO_AREA_START_Y + 2;

    const char* port_title = "PORT";
    const char* rate_title = "UART";

    text_width = strlen(port_title) * 8;
    text_x = area2_x + (AREA2_WIDTH - text_width) / 2;
    lcd_display_string(ctx->lcd_handle, text_x + 1, text_y, port_title, LCD_FONT(ascii_8x8), true);
    
    text_width = strlen(rate_title) * 8;
    text_x = area3_x + (AREA3_WIDTH - text_width) / 2;
    lcd_display_string(ctx->lcd_handle, text_x + 1, text_y, rate_title, LCD_FONT(ascii_8x8), true);

    // 计算第二行文字显示的Y坐标
    text_y = INFO_AREA_START_Y + 10 + (INFO_AREA_HEIGHT - 10 - 8) / 2;

    // 显示端口号(区域2) - 居中显示
    text_width = strlen(port_str) * 8;
    text_x = area2_x + (AREA2_WIDTH - text_width) / 2;
    lcd_display_string(ctx->lcd_handle, text_x + 1, text_y, port_str, LCD_FONT(ascii_8x8), true);

    // 显示波特率(区域3) - 居中显示
    text_width = strlen(baudrate_str) * 8;
    text_x = area3_x + (AREA3_WIDTH - text_width) / 2;
    lcd_display_string(ctx->lcd_handle, text_x + 1, text_y, baudrate_str, LCD_FONT(ascii_8x8), true);

    #endif  

    // 显示CPU使用率
    if (ctx->cpu_usage_enabled) {
        char cpu_usage_str[8];
        snprintf(cpu_usage_str, sizeof(cpu_usage_str), "%" PRIu8, home->cpu_usaged);
        text_width = strlen(cpu_usage_str) * 8;
        // 显示在右上角, 需要根据长度计算X坐标
        int x = (text_width > 128) ? 0 : (128 - text_width);
        lcd_display_string(ctx->lcd_handle, x, 0, cpu_usage_str, LCD_FONT(ascii_8x8), false);
    }
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
    } else if (msg_id == MSG_ID_STATISTICS_CLEARED) {
        line1 = "Statistics";
        line2 = "Cleared";
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
        snprintf(buffer, sizeof(buffer), "%u", (unsigned int)g_supported_baudrates[start_index + i]);
        
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
    page_network_data_t* network = &ctx->page.network;
    
    // 如果没有保存的网络，显示提示信息
    if (network->saved_network_count == 0) {
        lcd_display_string(ctx->lcd_handle, 0, 24, "No Networks", LCD_FONT(ascii_8x16), false);
        return;
    }
    
    // 定义布局参数
    const int line_height = 16;        // 每行高度(8x16字体)
    const int selector_width = 16;     // 选择符号区域宽度  
    const int signal_icon_width = 16;  // 信号图标宽度
    const int ssid_start_x = selector_width + signal_icon_width;  // SSID起始x坐标
    const int start_y = 0;             // 从顶部开始显示
    
    // 计算显示的网络起始索引
    int start_index = 0;
    if (network->selected_index >= network->display_num) {
        start_index = network->selected_index - network->display_num + 1;
    }

    // 显示网络列表
    for (int i = 0; i < network->display_num; i++) 
    {
        int network_index = start_index + i;
        if (network_index >= network->saved_network_count) {
            break;  // 超出网络数量，停止显示
        }

        int y_pos = start_y + i * line_height;
        wifi_connection_record_t *saved_network = &network->saved_networks[network_index];
        int8_t signal_level = network->network_signal_levels[network_index];
        
        // 如果是当前选中的网络，显示'>'符号
        if (network_index == network->selected_index) {
            lcd_display_string(ctx->lcd_handle, 0, y_pos, ">", LCD_FONT(ascii_8x16), false);
        }
        
        // 根据信号等级选择合适的图标
        const lcd_mono_img_t* signal_img = NULL;
        switch(signal_level) {
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
            default:  // 0 或其他值
                signal_img = LCD_IMG(no_signal_2);
                break;                
        }
        
        // 显示信号图标
        if (signal_img) {
            lcd_display_mono_img(ctx->lcd_handle, selector_width, y_pos, signal_img, false);
        }
        
        // 显示SSID，截断过长的名称
        char ssid_display[20];  // 限制显示长度
        strncpy(ssid_display, saved_network->ssid, sizeof(ssid_display) - 1);
        ssid_display[sizeof(ssid_display) - 1] = '\0';
        
        lcd_display_string(ctx->lcd_handle, ssid_start_x, y_pos, ssid_display, LCD_FONT(ascii_8x16), false);
    }
}

/**
 * @brief 从完整版本号中提取简短版本（如从"v1.0-g0786e3d"提取"v1.0"）
 * 
 * @param full_version 完整版本号
 * @param short_version 输出的简短版本号缓冲区
 * @param buffer_size 缓冲区大小
 */
static void extract_short_version(const char* full_version, char* short_version, size_t buffer_size)
{
    if (full_version == NULL || short_version == NULL || buffer_size == 0) {
        return;
    }
    
    // 找到'-'字符的位置
    const char* dash_pos = strchr(full_version, '-');
    if (dash_pos != NULL) {
        // 计算要复制的长度（不包括'-'及其后面的内容）
        size_t copy_length = dash_pos - full_version;
        if (copy_length >= buffer_size) {
            copy_length = buffer_size - 1;
        }
        strncpy(short_version, full_version, copy_length);
        short_version[copy_length] = '\0';
    } else {
        // 没有找到'-'，直接复制整个字符串
        strncpy(short_version, full_version, buffer_size - 1);
        short_version[buffer_size - 1] = '\0';
    }

    // 找到第一个‘v’，将它转成大写 
    char* v_pos = strchr(short_version, 'v');
    if (v_pos != NULL) {
        *v_pos = 'V';
    }
}

static void draw_help_page(display_context_t* ctx)
{

    // 屏幕尺寸: 128x64
    // 二维码尺寸: 64x64，放在右侧对齐
    const int qr_size = 64;
    const int qr_x = 128 - qr_size;  // 右对齐
    const int qr_y = 0;
    
    // 绘制二维码（右对齐）
    lcd_display_mono_img(ctx->lcd_handle, qr_x, qr_y, LCD_IMG(qrcode), false);
    
    // 在左侧显示版本信息
    // 行1: 版本号（8x16字体）
    char short_version[16];
    extract_short_version(APP_VERSION, short_version, sizeof(short_version));
    lcd_display_string(ctx->lcd_handle, 0, 0, short_version, LCD_FONT(ascii_8x16), false);
    
    // 行2: 编译日期（8x8字体，紧凑显示）
    lcd_display_string(ctx->lcd_handle, 0, 18, BUILD_DATE, LCD_FONT(ascii_8x8), false);

    // 行3: 显示文本“Scan for”， 8x8字体
    lcd_display_string(ctx->lcd_handle, 0, 40, "SCAN FOR", LCD_FONT(ascii_8x8), false);

    // 行4: 显示文本“help”， 8x8字体
    lcd_display_string(ctx->lcd_handle, 0, 50, "  HELP", LCD_FONT(ascii_8x8), false);
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

// 处理按键事件（在显示任务中执行）
static void handle_button_event(display_context_t* ctx, const display_button_event_t* button_event)
{
    const ext_gpio_event_data_t* data = &button_event->gpio_event;
    int32_t id = button_event->event_id;

    // 在强制帮助页模式下，禁用所有按键响应
    if (ctx->force_help_mode) {
        ESP_LOGD(TAG, "Button event ignored in force help mode");
        return;
    }

    // 根据事件类型进行不同处理
    switch(id) {
        case EXT_GPIO_EVENT_BUTTON_PRESSED:
            ESP_LOGD(TAG, "button event: [%s] pressed, click_count: %d", data->gpio_name, data->data.button.click_count);
            break; 
            
        case EXT_GPIO_EVENT_BUTTON_RELEASED:
            ESP_LOGD(TAG, "button event: [%s] released", data->gpio_name);         
            break;
            
        case EXT_GPIO_EVENT_BUTTON_LONG_PRESSED:
            ESP_LOGD(TAG, "button event: [%s] long pressed up to %d seconds", data->gpio_name, data->data.button.long_pressed);  
            // 长按3秒, 清除统计信息, 并显示弹出框, 显示已清除.
            if (data->data.button.long_pressed >= 3) {
                if (ctx->page.current_page == PAGE_HOME && ctx->popup.current_popup != POPUP_MENU) 
                {
                    // 清除统计信息
                    uart_bridge_reset_stats();
                    // 显示弹出框, 显示已清除.
                    active_popup_msg(ctx, MSG_ID_STATISTICS_CLEARED);
                }
            }

            break;
            
        case EXT_GPIO_EVENT_BUTTON_CONTINUE_CLICK:
            ESP_LOGD(TAG, "button event: [%s] continue click stopped, click count: %d", data->gpio_name, data->data.button.click_count);

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
                else if (ctx->page.current_page == PAGE_NETWORK) 
                {
                    // 单击切换选择的网络
                    if (ctx->page.network.saved_network_count > 0) {
                        ctx->page.network.selected_index = (ctx->page.network.selected_index + 1) % ctx->page.network.saved_network_count;
                        ctx->page.dirty = true;
                        // 延长页面显示时间
                        ctx->page.page_expried_time = uptime() + 60000; // 60秒后自动返回主页
                    }
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
                    ctx->page.home.baudrate = g_supported_baudrates[ctx->page.uart.selected_index];

                    // 设置波特率
                    uart_bridge_set_baudrate(ctx->page.home.baudrate);

                    // 返回主页
                    switch_page(ctx, PAGE_HOME);
                }
                else if (ctx->page.current_page == PAGE_NETWORK) 
                {
                    // 双击确认选择网络并尝试连接
                    if (ctx->page.network.saved_network_count > 0 && 
                        ctx->page.network.selected_index < ctx->page.network.saved_network_count) {
                        
                        uint8_t selected_idx = ctx->page.network.selected_index;
                        wifi_connection_record_t *selected_network = &ctx->page.network.saved_networks[selected_idx];
                        int8_t signal_level = ctx->page.network.network_signal_levels[selected_idx];
                        
                        // 获取当前WiFi状态
                        wifi_connection_status_t wifi_status = {0};
                        wifi_station_get_status(&wifi_status);
                        
                        if (signal_level == 0) {
                            // 信号等级为0，网络不存在
                            active_popup_msg(ctx, MSG_ID_NETWORK_NOT_AVAILABLE);
                        } else if (wifi_status.state == WIFI_STATE_CONNECTED && 
                                   strcmp(wifi_status.ssid, selected_network->ssid) == 0) {
                            // 当前已连接到选中的网络
                            active_popup_msg(ctx, MSG_ID_NETWORK_ALREADY_CONNECTED);
                        } else {
                            // 可用但非当前连接的网络，开始连接
                            active_popup_msg(ctx, MSG_ID_START_CONNECTING_NETWORK);
                            
                            // 异步连接WiFi
                            ESP_LOGI(TAG, "User selected network: %s, starting connection", selected_network->ssid);
                            int retry = 2;
                            while (retry > 0) {
                                esp_err_t ret = wifi_station_connect(selected_network->ssid, selected_network->password);
                                if (ret == ESP_OK) {
                                    break;
                                }
                                retry--;
                            }
                        }
                    }
                    
                    // 返回主页
                    switch_page(ctx, PAGE_HOME);
                }
            } else if (data->data.button.click_count == 3) {
                // 三击切换CPU使用率显示
                ctx->cpu_usage_enabled = !ctx->cpu_usage_enabled;

                if (ctx->cpu_usage_enabled) {
                    ctx->page.home.cpu_usaged = 0;
                    ctx->page.home.cpu_usage_update_time = uptime();
                } 

                ESP_LOGI(TAG, "CPU usage display %s", ctx->cpu_usage_enabled ? "enabled" : "disabled");
            }
            break;
    }
}
