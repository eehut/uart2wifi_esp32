#include "cli_menu.h"
#include "wifi_station.h"
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
#define UART_NUM UART_NUM_0
#define BUF_SIZE 1024
#define INPUT_MAX_LEN 128

// 菜单状态
typedef enum {
    MENU_STATE_MAIN = 0,
    MENU_STATE_WIFI,
    MENU_STATE_WIFI_CONNECT,
    MENU_STATE_WIFI_DELETE,
} menu_state_t;

// 全局状态
static struct {
    bool initialized;
    bool running;
    TaskHandle_t menu_task_handle;
    menu_state_t current_state;
    char input_buffer[INPUT_MAX_LEN];
    uint8_t input_pos;
    wifi_network_info_t *scan_results;
    uint16_t scan_count;
} menu_ctx = {0};

// 前向声明
static void menu_task(void *pvParameters);
static void handle_user_input(char c);
static void process_command(void);
static void show_main_menu(void);
static void show_wifi_menu(void);
static void handle_main_menu_command(const char *cmd);
static void handle_wifi_menu_command(const char *cmd);
static void handle_wifi_connect_command(const char *cmd);
static void handle_wifi_delete_command(const char *cmd);
static void wifi_show_status(void);
static void wifi_scan_networks(void);
static void wifi_connect_interactive(void);
static void wifi_disconnect(void);
static void wifi_list_records(void);
static void wifi_delete_record_interactive(void);
static void wifi_add_record_interactive(void);
static void show_help(void);
static void show_about(void);
static void clear_input_buffer(void);
static void print_prompt(void);

esp_err_t command_menu_init(void)
{
    if (menu_ctx.initialized) {
        ESP_LOGW(TAG, "Command menu already initialized");
        return ESP_OK;
    }

    // 初始化上下文
    memset(&menu_ctx, 0, sizeof(menu_ctx));
    menu_ctx.current_state = MENU_STATE_MAIN;
    menu_ctx.initialized = true;

    ESP_LOGI(TAG, "Command menu initialized");
    return ESP_OK;
}

esp_err_t command_menu_start(void)
{
    if (!menu_ctx.initialized) {
        ESP_LOGE(TAG, "Command menu not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (menu_ctx.running) {
        ESP_LOGW(TAG, "Command menu already running");
        return ESP_OK;
    }

    menu_ctx.running = true;

    // 创建菜单任务
    BaseType_t ret = xTaskCreate(menu_task, "command_menu", 4096, NULL, 5, &menu_ctx.menu_task_handle);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create menu task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Command menu started");
    
    return ESP_OK;
}

esp_err_t command_menu_stop(void)
{
    if (!menu_ctx.running) {
        return ESP_OK;
    }

    menu_ctx.running = false;
    
    if (menu_ctx.menu_task_handle) {
        vTaskDelete(menu_ctx.menu_task_handle);
        menu_ctx.menu_task_handle = NULL;
    }

    if (menu_ctx.scan_results) {
        free(menu_ctx.scan_results);
        menu_ctx.scan_results = NULL;
    }

    ESP_LOGI(TAG, "Command menu stopped");
    return ESP_OK;
}

// 任务主函数
static void menu_task(void *pvParameters)
{
    char data[BUF_SIZE];
    
    ESP_LOGI(TAG, "Command menu task started");
    ESP_LOGI(TAG, "按回车键显示菜单...");
    
    while (menu_ctx.running) {
        // 从标准输入读取
        if (fgets(data, BUF_SIZE, stdin) != NULL) {
            // 处理每个字符
            for (int i = 0; data[i] != '\0'; i++) {
                handle_user_input(data[i]);
            }
        }

        // 读取UART数据
        // int len = uart_read_bytes(UART_NUM_0, data, BUF_SIZE - 1, 500 / portTICK_PERIOD_MS);
        // if (len > 0) {
        //     data[len] = '\0'; // 添加字符串结束符
        //     printf("Received: %s\n", data);
        // } else {
        //     printf("Rx nothing\n");
        // }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
    
    ESP_LOGI(TAG, "Command menu task ended");
    vTaskDelete(NULL);
}

// 处理用户输入字符
static void handle_user_input(char c)
{
    // 处理特殊字符
    if (c == '\r' || c == '\n') {
        if (menu_ctx.input_pos > 0) {
            // 有输入内容，处理命令
            menu_ctx.input_buffer[menu_ctx.input_pos] = '\0';
            process_command();
        } else {
            // 空输入，显示当前菜单
            switch (menu_ctx.current_state) {
                case MENU_STATE_MAIN:
                    show_main_menu();
                    break;
                case MENU_STATE_WIFI:
                    show_wifi_menu();
                    break;
                default:
                    show_main_menu();
                    menu_ctx.current_state = MENU_STATE_MAIN;
                    break;
            }
        }
        clear_input_buffer();
        print_prompt();
        return;
    }
    
    // 处理退格键
    if (c == '\b' || c == 127) {
        if (menu_ctx.input_pos > 0) {
            menu_ctx.input_pos--;
            printf("\b \b"); // 删除屏幕上的字符
            fflush(stdout);
        }
        return;
    }
    
    // 处理普通字符
    if (c >= 32 && c <= 126 && menu_ctx.input_pos < INPUT_MAX_LEN - 1) {
        menu_ctx.input_buffer[menu_ctx.input_pos++] = c;
        printf("%c", c); // 回显字符
        fflush(stdout);
    }
}

// 处理完整的命令
static void process_command(void)
{
    printf("\n"); // 换行
    
    switch (menu_ctx.current_state) {
        case MENU_STATE_MAIN:
            handle_main_menu_command(menu_ctx.input_buffer);
            break;
        case MENU_STATE_WIFI:
            handle_wifi_menu_command(menu_ctx.input_buffer);
            break;
        case MENU_STATE_WIFI_CONNECT:
            handle_wifi_connect_command(menu_ctx.input_buffer);
            break;
        case MENU_STATE_WIFI_DELETE:
            handle_wifi_delete_command(menu_ctx.input_buffer);
            break;
        default:
            printf("未知状态\n");
            menu_ctx.current_state = MENU_STATE_MAIN;
            break;
    }
}

// 显示主菜单
static void show_main_menu(void)
{
    printf("\n=== 主菜单 ===\n");
    printf("Please choose function:\n");
    printf("1. WIFI Setting\n");
    printf("2. Help\n");
    printf("3. About\n");
    printf("请输入选择: ");
    fflush(stdout);
}

// 显示WiFi菜单
static void show_wifi_menu(void)
{
    printf("\n=== WiFi 设置菜单 ===\n");
    printf("1. Show Status\n");
    printf("2. Scan\n");
    printf("3. Connect\n");
    printf("4. Disconnect\n");
    printf("5. List Records\n");
    printf("6. Delete Record\n");
    printf("7. Add Record\n");
    printf("0. Exit\n");
    printf("请输入选择: ");
    fflush(stdout);
}

// 处理主菜单命令
static void handle_main_menu_command(const char *cmd)
{
    int choice = atoi(cmd);
    
    switch (choice) {
        case 1:
            menu_ctx.current_state = MENU_STATE_WIFI;
            show_wifi_menu();
            break;
        case 2:
            show_help();
            break;
        case 3:
            show_about();
            break;
        default:
            printf("无效选择，请重新输入\n");
            break;
    }
}

// 处理WiFi菜单命令
static void handle_wifi_menu_command(const char *cmd)
{
    int choice = atoi(cmd);
    
    switch (choice) {
        case 1:
            wifi_show_status();
            break;
        case 2:
            wifi_scan_networks();
            break;
        case 3:
            wifi_connect_interactive();
            break;
        case 4:
            wifi_disconnect();
            break;
        case 5:
            wifi_list_records();
            break;
        case 6:
            wifi_delete_record_interactive();
            break;
        case 7:
            wifi_add_record_interactive();
            break;
        case 0:
            menu_ctx.current_state = MENU_STATE_MAIN;
            printf("返回主菜单\n");
            show_main_menu();
            break;
        default:
            printf("无效选择，请重新输入\n");
            break;
    }
}

// WiFi功能实现

// 显示WiFi状态
static void wifi_show_status(void)
{
    printf("\n=== WiFi 状态 ===\n");
    
    wifi_connection_status_t status;
    esp_err_t ret = wifi_station_get_status(&status);
    
    if (ret != ESP_OK) {
        printf("获取WiFi状态失败: %s\n", esp_err_to_name(ret));
        return;
    }
    
    switch (status.status) {
        case WIFI_STATION_STATUS_DISCONNECTED:
            printf("连接状态: 未连接\n");
            break;
        case WIFI_STATION_STATUS_CONNECTING:
            printf("连接状态: 连接中...\n");
            break;
        case WIFI_STATION_STATUS_CONNECTED:
            printf("连接状态: 已连接\n");
            printf("SSID: %s\n", status.ssid);
            printf("BSSID: %02x:%02x:%02x:%02x:%02x:%02x\n",
                   status.bssid[0], status.bssid[1], status.bssid[2],
                   status.bssid[3], status.bssid[4], status.bssid[5]);
            printf("信号强度: %d dBm\n", status.rssi);
            printf("IP地址: " IPSTR "\n", IP2STR((esp_ip4_addr_t*)&status.ip_addr));
            printf("子网掩码: " IPSTR "\n", IP2STR((esp_ip4_addr_t*)&status.netmask));
            printf("网关: " IPSTR "\n", IP2STR((esp_ip4_addr_t*)&status.gateway));
            printf("DNS: " IPSTR "\n", IP2STR((esp_ip4_addr_t*)&status.dns));
            printf("连接时长: %" PRIu32 " 秒\n", status.connected_time);
            break;
    }
    printf("\n");
}

// 扫描WiFi网络
static void wifi_scan_networks(void)
{
    printf("\n=== 扫描 WiFi 网络 ===\n");
    printf("正在扫描...\n");
    
    // 释放之前的扫描结果
    if (menu_ctx.scan_results) {
        free(menu_ctx.scan_results);
        menu_ctx.scan_results = NULL;
    }
    
    // 分配扫描结果缓冲区
    uint16_t max_count = 30;
    menu_ctx.scan_results = malloc(sizeof(wifi_network_info_t) * max_count);
    if (!menu_ctx.scan_results) {
        printf("内存分配失败\n");
        return;
    }
    
    menu_ctx.scan_count = max_count;
    esp_err_t ret = wifi_station_scan_networks(menu_ctx.scan_results, &menu_ctx.scan_count);
    
    if (ret != ESP_OK) {
        printf("WiFi扫描失败: %s\n", esp_err_to_name(ret));
        free(menu_ctx.scan_results);
        menu_ctx.scan_results = NULL;
        return;
    }
    
    printf("发现 %d 个网络:\n", menu_ctx.scan_count);
    for (uint16_t i = 0; i < menu_ctx.scan_count; i++) {
        printf("%2d. %-32s (信号: %d dBm)\n", 
               i + 1, menu_ctx.scan_results[i].ssid, menu_ctx.scan_results[i].rssi);
    }
    printf("\n");
}

// 交互式连接WiFi
static void wifi_connect_interactive(void)
{
    printf("\n=== 连接 WiFi ===\n");
    
    // 首先执行扫描
    wifi_scan_networks();
    
    if (menu_ctx.scan_count == 0) {
        printf("没有发现可用网络\n");
        return;
    }
    
    printf("请输入要连接的网络序号 (1-%d): ", menu_ctx.scan_count);
    fflush(stdout);
    
    menu_ctx.current_state = MENU_STATE_WIFI_CONNECT;
}

// 处理WiFi连接命令
static void handle_wifi_connect_command(const char *cmd)
{
    static int selected_index = -1;
    
    if (selected_index == -1) {
        // 第一步：选择SSID
        int choice = atoi(cmd);
        if (choice < 1 || choice > menu_ctx.scan_count) {
            printf("无效选择，请重新输入 (1-%d): ", menu_ctx.scan_count);
            fflush(stdout);
            return;
        }
        
        selected_index = choice - 1;
        printf("选择的网络: %s\n", menu_ctx.scan_results[selected_index].ssid);
        printf("请输入密码 (如果是开放网络请直接按回车): ");
        fflush(stdout);
    } else {
        // 第二步：输入密码并连接
        const char *ssid = menu_ctx.scan_results[selected_index].ssid;
        const char *password = (strlen(cmd) > 0) ? cmd : NULL;
        
        printf("正在连接到 %s...\n", ssid);
        
        esp_err_t ret = wifi_station_connect(ssid, password);
        if (ret == ESP_OK) {
            printf("连接成功!\n");
        } else {
            printf("连接失败: %s\n", esp_err_to_name(ret));
        }
        
        // 重置状态
        selected_index = -1;
        menu_ctx.current_state = MENU_STATE_WIFI;
        printf("\n");
        show_wifi_menu();
    }
}

// 断开WiFi连接
static void wifi_disconnect(void)
{
    printf("\n=== 断开 WiFi ===\n");
    
    esp_err_t ret = wifi_station_disconnect();
    if (ret == ESP_OK) {
        printf("WiFi已断开\n");
    } else {
        printf("断开失败: %s\n", esp_err_to_name(ret));
    }
    printf("\n");
}

// 列出连接记录
static void wifi_list_records(void)
{
    printf("\n=== 连接记录 ===\n");
    
    wifi_connection_record_t records[WIFI_STATION_MAX_RECORDS];
    uint8_t count;
    
    esp_err_t ret = wifi_station_get_records(records, &count);
    if (ret != ESP_OK) {
        printf("获取记录失败: %s\n", esp_err_to_name(ret));
        return;
    }
    
    if (count == 0) {
        printf("没有保存的连接记录\n");
    } else {
        printf("共有 %d 条记录:\n", count);
        for (uint8_t i = 0; i < count; i++) {
            if (records[i].valid) {
                printf("%2d. %-32s (序号: %" PRIu32 ")\n", 
                       i + 1, records[i].ssid, records[i].sequence);
            }
        }
    }
    printf("\n");
}

// 交互式删除记录
static void wifi_delete_record_interactive(void)
{
    printf("\n=== 删除连接记录 ===\n");
    
    // 先显示记录列表
    wifi_list_records();
    
    wifi_connection_record_t records[WIFI_STATION_MAX_RECORDS];
    uint8_t count;
    
    esp_err_t ret = wifi_station_get_records(records, &count);
    if (ret != ESP_OK || count == 0) {
        printf("没有可删除的记录\n");
        return;
    }
    
    printf("请输入要删除的记录序号 (1-%d): ", count);
    fflush(stdout);
    
    menu_ctx.current_state = MENU_STATE_WIFI_DELETE;
}

// 处理删除记录命令
static void handle_wifi_delete_command(const char *cmd)
{
    wifi_connection_record_t records[WIFI_STATION_MAX_RECORDS];
    uint8_t count;
    
    esp_err_t ret = wifi_station_get_records(records, &count);
    if (ret != ESP_OK) {
        printf("获取记录失败\n");
        menu_ctx.current_state = MENU_STATE_WIFI;
        show_wifi_menu();
        return;
    }
    
    int choice = atoi(cmd);
    if (choice < 1 || choice > count) {
        printf("无效选择，请重新输入 (1-%d): ", count);
        fflush(stdout);
        return;
    }
    
    // 找到对应的有效记录
    int valid_index = 0;
    for (uint8_t i = 0; i < WIFI_STATION_MAX_RECORDS; i++) {
        if (records[i].valid) {
            valid_index++;
            if (valid_index == choice) {
                ret = wifi_station_delete_record(records[i].ssid);
                if (ret == ESP_OK) {
                    printf("已删除记录: %s\n", records[i].ssid);
                } else {
                    printf("删除失败: %s\n", esp_err_to_name(ret));
                }
                break;
            }
        }
    }
    
    menu_ctx.current_state = MENU_STATE_WIFI;
    printf("\n");
    show_wifi_menu();
}

// 交互式添加记录
static void wifi_add_record_interactive(void)
{
    printf("\n=== 添加连接记录 ===\n");
    printf("此功能将添加一个新的WiFi连接记录\n");
    printf("注意：通过 'Connect' 功能连接成功的网络会自动添加记录\n");
    printf("请输入 SSID: ");
    fflush(stdout);
    
    // 这里简化处理，实际可以实现一个多步骤的输入流程
    // 由于当前架构限制，我们显示说明后返回菜单
    printf("\n提示：请使用 'Connect' 功能连接WiFi，成功后会自动添加记录\n");
    printf("\n");
}

// 显示帮助信息
static void show_help(void)
{
    printf("\n=== 帮助信息 ===\n");
    printf("这是一个ESP32 WiFi管理系统的命令行界面。\n");
    printf("\n功能说明：\n");
    printf("• WiFi Setting: 管理WiFi连接，包括扫描、连接、断开等操作\n");
    printf("• 自动保存: 成功连接的WiFi会自动保存，最多保存8个\n");
    printf("• 自动连接: 设备启动后会自动连接到信号最强的已保存网络\n");
    printf("• 后台重连: 断线后会自动尝试重新连接\n");
    printf("\n操作提示：\n");
    printf("• 按回车键显示当前菜单\n");
    printf("• 输入数字选择菜单项\n");
    printf("• 支持退格键编辑输入\n");
    printf("• 在WiFi菜单中输入0返回主菜单\n");
    printf("\n注意事项：\n");
    printf("• WiFi密码会明文保存在设备中\n");
    printf("• 连接超时时间为15秒\n");
    printf("• 系统日志和命令界面共用同一串口\n");
    printf("\n");
}

// 显示关于信息
static void show_about(void)
{
    printf("\n=== 关于设备 ===\n");
    printf("设备型号: ESP32-C3 开发板\n");
    printf("序列号: ESP32C3-SERIAL2IP-00000000\n");
    printf("固件版本: v1.0.0\n");
    printf("编译时间: %s %s\n", __DATE__, __TIME__);
    printf("\n功能简介:\n");
    printf("这是一个基于ESP32-C3的串口转IP网络设备，主要功能包括：\n");
    printf("• WiFi Station 模式连接，支持自动重连\n");
    printf("• 串口数据转发到网络\n");
    printf("• 网络数据转发到串口\n");
    printf("• 命令行界面配置和管理\n");
    printf("• 支持多种网络协议转换\n");
    printf("\n技术规格:\n");
    printf("• CPU: RISC-V 32位单核，最高160MHz\n");
    printf("• RAM: 400KB\n");
    printf("• Flash: 根据模组配置\n");
    printf("• WiFi: 802.11 b/g/n (2.4 GHz)\n");
    printf("• 串口: UART0/1/2 支持多种波特率\n");
    printf("\n开发信息:\n");
    printf("公司名称: Your Company Name\n");
    printf("技术支持: support@yourcompany.com\n");
    printf("开发框架: ESP-IDF v5.x\n");
    printf("开源许可: MIT License\n");
    printf("\n");
}

// 辅助函数
static void clear_input_buffer(void)
{
    memset(menu_ctx.input_buffer, 0, sizeof(menu_ctx.input_buffer));
    menu_ctx.input_pos = 0;
}

static void print_prompt(void)
{
    switch (menu_ctx.current_state) {
        case MENU_STATE_MAIN:
            printf("主菜单> ");
            break;
        case MENU_STATE_WIFI:
            printf("WiFi> ");
            break;
        case MENU_STATE_WIFI_CONNECT:
            printf("连接> ");
            break;
        case MENU_STATE_WIFI_DELETE:
            printf("删除> ");
            break;
        default:
            printf("> ");
            break;
    }
    fflush(stdout);
} 