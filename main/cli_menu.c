
/**
 * @file cli_menu.c
 * @author LiuChuansen (179712066@qq.com)
 * @brief 命令行菜单系统，用于在终端中显示菜单并处理用户输入
 * @version 0.1
 * @date 2025-05-24
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "cli_menu.h"
#include "cli_impl.h"
#include "wifi_station.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_system.h"
#include <inttypes.h>  // 添加这个头文件以支持 PRIu32 宏

static const char *TAG = "cli_menu";

// UART配置
#define BUF_SIZE      256
#define INPUT_MAX_LEN 128

// 全局状态
typedef struct {
    bool initialized;
    bool running;
    bool show_welcome;
    TaskHandle_t task_handle;
    char input_buffer[INPUT_MAX_LEN];
    uint8_t input_pos;
} cli_ctx_t;

static cli_ctx_t s_cli_ctx = {0};

// 前向声明
static void command_line_task(void *pvParameters);
static void command_line_input(char c);


/**
 * @brief 初始化命令行菜单
 * 
 * @return esp_err_t 
 */
esp_err_t cli_menu_init(void)
{
    if (s_cli_ctx.initialized) {
        ESP_LOGW(TAG, "Command line menu already initialized");
        return ESP_OK;
    }

    // 初始化上下文
    memset(&s_cli_ctx, 0, sizeof(s_cli_ctx));
    s_cli_ctx.show_welcome = true;
    s_cli_ctx.initialized = true;

    // 初始化命令行状态机
    cli_state_machine_init();

    ESP_LOGI(TAG, "Command line menu initialized");
    return ESP_OK;
}

/**
 * @brief 启动命令行菜单
 * 
 * @return esp_err_t 
 */
esp_err_t cli_menu_start(void)
{
    if (!s_cli_ctx.initialized) {
        ESP_LOGE(TAG, "Command line menu not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_cli_ctx.running) {
        ESP_LOGW(TAG, "Command line menu already running");
        return ESP_OK;
    }

    s_cli_ctx.running = true;

    // 创建菜单任务
    BaseType_t ret = xTaskCreate(command_line_task, "command_menu", 4096, NULL, 5, &s_cli_ctx.task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create command line menu task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Command line task started");
    
    return ESP_OK;
}

/**
 * @brief 停止命令行菜单
 * 
 * @return esp_err_t 
 */
esp_err_t cli_menu_stop(void)
{
    if (!s_cli_ctx.running) {
        return ESP_OK;
    }

    s_cli_ctx.running = false;
    
    if (s_cli_ctx.task_handle) {
        vTaskDelete(s_cli_ctx.task_handle);
        s_cli_ctx.task_handle = NULL;
    }

    ESP_LOGI(TAG, "Command line task stopped");
    return ESP_OK;
}



// 任务主函数
static void command_line_task(void *pvParameters)
{
    char data[BUF_SIZE];

    // 重置命令行状态机
    cli_state_machine_reset();

    while (s_cli_ctx.running) {
        // 从标准输入读取
        if (fgets(data, BUF_SIZE, stdin) != NULL) {
            // 处理每个字符
            for (int i = 0; data[i] != '\0'; i++) {
                command_line_input(data[i]);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Command menu task ended");
    vTaskDelete(NULL);
}

// 处理用户输入字符
// 主要逻辑:
// 1. 首次收到任意消息,显示欢迎信息
// 2. 回显可打印字符,并保存到输入buffer
// 3. 如果是退格键,删除一个字符
// 4. 如果是回车键, 如果buffer 不为空,则处理命令
// 7. 如果是其他字符,忽略,并回显
static void command_line_input(char c)
{
    cli_ctx_t *ctx = &s_cli_ctx;

    // 首次收到任意消息,显示欢迎信息
    if (ctx->show_welcome) {
        ctx->show_welcome = false;
        printf("Welcome to the command line interface\n");
        printf("Press 'ENTER' to show main menu\n");
        return;
    }

    // 处理特殊字符
    if (c == '\r' || c == '\n') {
        // 先回显一个回车
        printf("\n");

        // 处理输入内容
        if (ctx->input_pos > 0) {
            // 有输入内容，处理命令
            ctx->input_buffer[ctx->input_pos] = '\0';
            cli_state_machine_input(ctx->input_buffer);
        } else {
            cli_state_machine_input(NULL);
        }
        // 清空输入缓冲区
        ctx->input_pos = 0; 
        return;
    }
    
    // 处理退格键
    if (c == '\b' || c == 127) {
        if (ctx->input_pos > 0) {
            ctx->input_pos--;
            ctx->input_buffer[ctx->input_pos] = '\0';
            printf("\b \b"); // 删除屏幕上的字符
            fflush(stdout);
        }
        return;
    }

    // 处理普通字符
    if (c >= 32 && c <= 126 && ctx->input_pos < INPUT_MAX_LEN - 1) {
        ctx->input_buffer[ctx->input_pos++] = c;
        printf("%c", c); // 回显字符
        fflush(stdout);
    }
}

