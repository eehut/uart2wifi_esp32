/**
 * @file cli_impl.c
 * @author Samuel (samuel@neptune-robotics.com)
 * @brief 命令行状态机实现
 * @version 0.1
 * @date 2025-05-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "wifi_station.h"
#include "esp_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

const char *TAG = "cli_impl";

#define CLI_ACTIVITY_TIMEOUT_MS (60 * 1000)  // 1分钟超时

typedef enum {
    CLI_STATE_MAIN = 0,
    CLI_STATE_STATUS,
    CLI_STATE_WIFI,
    CLI_STATE_UART,
    CLI_STATE_ABOUT,
} cli_state_t;

typedef enum {
    CLI_SUB_STATE_NONE = 0,
    CLI_SUB_STATE_WIFI_AUTO_CONNECT,
    CLI_SUB_STATE_WIFI_SCAN_CONNECT,
    CLI_SUB_STATE_WIFI_DISCONNECT,
    CLI_SUB_STATE_WIFI_LIST_NETWORKS,
    CLI_SUB_STATE_WIFI_DELETE_NETWORK,
    CLI_SUB_STATE_WIFI_ADD_NETWORK,
} cli_sub_state_t;

typedef struct {
    // 活动状态与超时管理
    bool is_active;
    TimerHandle_t activity_timer;
    // 状态和子状态     
    cli_state_t state;
    cli_sub_state_t sub_state;
    // 扫描结果与连接
    wifi_network_info_t scan_results[32];
    uint8_t scan_count;
    bool scan_success;
    bool connect_done;

    wifi_connection_record_t wifi_records[WIFI_STATION_MAX_RECORDS];
    uint8_t wifi_record_count;

#define _SUB_STEP_CHOOSE_NETWORK 0
#define _SUB_STEP_INPUT_PASSWORD 1
#define _SUB_STEP_CONFIRM_DISCONNECT 2
#define _SUB_STEP_DELETE_NETWORK 3
#define _SUB_STEP_ADD_NETWORK_INPUT_SSID 4
#define _SUB_STEP_ADD_NETWORK_INPUT_PASSWORD 5
    uint8_t sub_step;
    int input_index;
    char input_buffer[128];  // 用于多步骤输入
} cli_state_machine_t;

static const uint32_t s_supported_baudrates[] = {
    9600, 19200, 38400, 57600, 115200, 
    230400, 460800, 921600, 1500000
};

static cli_state_machine_t s_cli_sm = {0};

// 前向声明
static void show_main_menu(void);
static void show_wifi_menu(void);
static void show_about_menu(void);
static void show_uart_baudrate_menu(void);
static void show_status(void);

static void active_auto_connect_once(void);
static void start_wifi_scan_and_connect(void);
static void start_wifi_disconnect(void);

static void show_wifi_networks(void);
static void show_wifi_network_delete_menu(void);
static void show_wifi_network_add_menu(void);

static void handle_main_menu(const char *input);
static void handle_wifi_menu(const char *input);
static void handle_set_uart_baudrate(const char *input);
static void handle_auto_connect_once(const char *input);
static void handle_wifi_scan_and_connect(const char *input);
static void handle_wifi_disconnect(const char *input);

static void handle_wifi_networks(const char *input);
static void handle_wifi_network_delete(const char *input);
static void handle_wifi_network_add(const char *input);

static void return_to_main_menu(const char *input);

static void activity_timer_callback(TimerHandle_t xTimer);
static void set_cli_active(bool active);

// 模拟串口波特率设置
static uint32_t s_current_baudrate = 115200;

uint32_t get_current_baudrate(void)
{
    return s_current_baudrate;
}

void set_uart_baudrate(uint32_t baudrate)
{
    s_current_baudrate = baudrate;
}

void cli_state_machine_init(void)
{
    memset(&s_cli_sm, 0, sizeof(s_cli_sm));
    s_cli_sm.state = CLI_STATE_MAIN;
    s_cli_sm.sub_state = CLI_SUB_STATE_NONE;
    s_cli_sm.scan_count = 0;
    s_cli_sm.is_active = false;

    // 创建活动超时定时器
    s_cli_sm.activity_timer = xTimerCreate(
        "cli_activity_timer",
        pdMS_TO_TICKS(CLI_ACTIVITY_TIMEOUT_MS),
        pdFALSE,  // 单次触发
        NULL,
        activity_timer_callback
    );

    if (s_cli_sm.activity_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create activity timer");
    }

    ESP_LOGI(TAG, "CLI state machine initialized");
}

void cli_state_machine_reset(void)
{
    s_cli_sm.state = CLI_STATE_MAIN;
    s_cli_sm.sub_state = CLI_SUB_STATE_NONE;

    s_cli_sm.scan_count = 0;
    set_cli_active(false);

    ESP_LOGI(TAG, "CLI state machine reset");
}

/**
 * @brief 处理用户输入
 * 
 * @param input 如果为空,表示回车键, 否则是处理输入字串
 */
void cli_state_machine_input(const char *input)
{
    cli_state_machine_t *sm = &s_cli_sm;

    // 设置CLI为活动状态
    set_cli_active(true);

    switch (sm->state) {
        case CLI_STATE_MAIN:
            handle_main_menu(input);
            break;
        case CLI_STATE_STATUS:
            return_to_main_menu(input);
            break;
        case CLI_STATE_WIFI:
            if (sm->sub_state == CLI_SUB_STATE_WIFI_AUTO_CONNECT) {
                handle_auto_connect_once(input);
            } else if (sm->sub_state == CLI_SUB_STATE_WIFI_SCAN_CONNECT) {
                handle_wifi_scan_and_connect(input);
            } else if (sm->sub_state == CLI_SUB_STATE_WIFI_DISCONNECT) {
                handle_wifi_disconnect(input);
            } else if (sm->sub_state == CLI_SUB_STATE_WIFI_LIST_NETWORKS) {
                handle_wifi_networks(input);
            } else if (sm->sub_state == CLI_SUB_STATE_WIFI_DELETE_NETWORK) {
                handle_wifi_network_delete(input);
            } else if (sm->sub_state == CLI_SUB_STATE_WIFI_ADD_NETWORK) {
                handle_wifi_network_add(input);
            } else {
                handle_wifi_menu(input);
            }
            break;
        case CLI_STATE_UART:
            handle_set_uart_baudrate(input);
            break;
        case CLI_STATE_ABOUT:
            return_to_main_menu(input);
            break;
        default:
            ESP_LOGE(TAG, "Invalid state: %d", sm->state);
            break;
    }
}

static void activity_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "CLI activity timeout, returning to main menu");
    cli_state_machine_reset();
    show_main_menu();
}

static void set_cli_active(bool active)
{
    bool changed = s_cli_sm.is_active != active;


    // 如果是活动状态, 每次要复位定时器, 以保证活动状态 
    if (active && s_cli_sm.activity_timer) {
        xTimerReset(s_cli_sm.activity_timer, 0);
    }

    s_cli_sm.is_active = active;

    if (changed) {
        // 有改变, 让改动生效 
        wifi_station_set_auto_connect(active ? false : true);

        if (s_cli_sm.activity_timer && !active) {
            xTimerStop(s_cli_sm.activity_timer, 0);
        }
    }
}

static void show_main_menu(void)
{
    printf("\n=== Main Menu ===\n");
    printf("1. Status\n");
    printf("2. WiFi Setting\n");
    printf("3. UART Setting\n");
    printf("4. About\n");
    printf("Please input: ");
    fflush(stdout);
}

static void show_wifi_menu(void)
{
    printf("\n=== WiFi Setting ===\n");
    printf("1. Auto Connect\n");
    printf("2. Scan & Connect\n");    
    printf("3. Disconnect\n");
    printf("4. List Networks\n");
    printf("5. Delete Network\n");
    printf("6. Add Network\n");
    printf("--------\n");    
    printf("0. Exit\n");
    printf("--------\n");
    printf("Please input: ");
    fflush(stdout);
}


static void show_status(void)
{
    wifi_connection_status_t wifi_status;
    esp_err_t ret = wifi_station_get_status(&wifi_status);

    if (ret != ESP_OK) {
        printf("***Failed to get WiFi status: %s\n", esp_err_to_name(ret));
        return;
    }

    // 如果未连接, 清空IP地址信息等
    if (wifi_status.state != WIFI_STATE_CONNECTED) {
        wifi_status.ip_addr = 0;
        wifi_status.netmask = 0;
        wifi_status.gateway = 0;
        wifi_status.dns1 = 0;
        wifi_status.dns2 = 0;
        wifi_status.connected_time = 0;
    }
    
    printf("\n=== Device Status ===\n");
    char status_str[32] = {0};

    switch (wifi_status.state) {
        case WIFI_STATE_DISCONNECTED:
            strcpy(status_str, "Disconnected");
            break;
        case WIFI_STATE_CONNECTING:
            strcpy(status_str, "Connecting...");
            break;
        case WIFI_STATE_CONNECTED:
            strcpy(status_str, "Connected");
            break;
        default:
            strcpy(status_str, "Unknown");
            break;
    }

    printf("WiFi Connection\n");
    printf(" Status   : %s\n", status_str);
    printf(" SSID     : %s\n", wifi_status.ssid);
    printf(" RSSI     : %d dBm\n", wifi_status.rssi);
    printf(" Duration : %" PRIu32 " seconds\n", wifi_status.connected_time);    
    printf("Network Address\n");
    printf(" IP       : %d.%d.%d.%d\n", 
            (int)(wifi_status.ip_addr & 0xFF),
            (int)((wifi_status.ip_addr >> 8) & 0xFF),
            (int)((wifi_status.ip_addr >> 16) & 0xFF),
            (int)((wifi_status.ip_addr >> 24) & 0xFF));
    printf(" Gateway  : %d.%d.%d.%d\n",
            (int)(wifi_status.gateway & 0xFF),
            (int)((wifi_status.gateway >> 8) & 0xFF),
            (int)((wifi_status.gateway >> 16) & 0xFF),
            (int)((wifi_status.gateway >> 24) & 0xFF));
    printf(" DNS      : %d.%d.%d.%d\n",
            (int)(wifi_status.dns1 & 0xFF),
            (int)((wifi_status.dns1 >> 8) & 0xFF),
            (int)((wifi_status.dns1 >> 16) & 0xFF),
            (int)((wifi_status.dns1 >> 24) & 0xFF));    
    printf("UART Settings\n");
    printf(" Baudrate : %" PRIu32 "\n", get_current_baudrate());
    printf("--------\n");
    printf("Input [Enter] to return\n");
    fflush(stdout);
}

static void show_uart_baudrate_menu(void)
{
    printf("\n=== UART Baudrate Setting ===\n");

    uint32_t current = get_current_baudrate();

    for (int i = 0; i < sizeof(s_supported_baudrates) / sizeof(s_supported_baudrates[0]); i++) {
        printf("%d. %" PRIu32 " %s\n", i + 1, s_supported_baudrates[i], current == s_supported_baudrates[i] ? "<" : "");
    }

    printf("--------\n");
    printf("0. Exit\n");
    printf("--------\n");
    printf("Please input: ");
    fflush(stdout);
}

static void show_about_menu(void)
{
    printf("\n=== About ===\n");
    printf("Product  : WiFi-UART Bridge\n");
    printf("Model    : ESP32-C3 Pro\n");
    printf("SN       : SN20250520\n");
    printf("Version  : V1.0.0\n");
    printf("Released : %s %s\n", __DATE__, __TIME__);
    printf("--------\n");
    printf("Copyright (c) 2025 LiuChuansen\n");
    printf("All rights reserved.\n");
    printf("--------\n");
    printf("Input [Enter] to return\n");
}


static void start_wifi_scan_and_connect(void)
{
    printf("\n=== WiFi Scan & Connect ===\n");
    printf("Scanning...\n");

    // 获取当前连接的SSID
    wifi_connection_status_t wifi_status;
    wifi_status.state = WIFI_STATE_DISCONNECTED;
    wifi_station_get_status(&wifi_status);

    s_cli_sm.scan_success = false;
    s_cli_sm.scan_count = 0;
    s_cli_sm.connect_done = false;


    uint16_t count = sizeof(s_cli_sm.scan_results) / sizeof(s_cli_sm.scan_results[0]);
    
    esp_err_t ret = wifi_station_scan_networks_async(s_cli_sm.scan_results, &count, 10000);
    if (ret == ESP_OK && (count > 0)) {
        s_cli_sm.scan_success = true;
        s_cli_sm.scan_count = count;
        s_cli_sm.connect_done = false;
        s_cli_sm.sub_step = _SUB_STEP_CHOOSE_NETWORK;

        printf("Found %d networks:\n", count);
        for (uint16_t i = 0; i < count; i++) {

            bool is_current = false;
            if ((wifi_status.state == WIFI_STATE_CONNECTED) && (strcmp(wifi_status.ssid, s_cli_sm.scan_results[i].ssid) == 0)) {
                is_current = true;
            }

            printf("%d. %-32s RSSI: %d %s\n", i + 1, s_cli_sm.scan_results[i].ssid, s_cli_sm.scan_results[i].rssi, is_current ? "<" : "");
        }
        printf("--------\n");
        printf("0. Exit\n");
        printf("--------\n");
        printf("Please input network index: ");
        fflush(stdout);
        return;
    } else if (ret == ESP_OK) {
        printf("Scan success, but no network found\n");
    } else {
        printf("***Scan failed: %s\n", esp_err_to_name(ret));
    }
    printf("--------\n");
    printf("Input [Enter] to return\n");
}


static void active_auto_connect_once(void)
{
    printf("\n=== WiFi Auto Connect ===\n");
    wifi_station_try_auto_connect_once();
    printf("Auto connect request submitted, please wait...\n");
    printf("--------\n");
    printf("Input [Enter] to return\n");
}


static void start_wifi_disconnect(void)
{
/*
  -- 处理以下逻辑 
  获取当前连接状态, 如果有, 输入命令断开连接
  如果没有连接, 提示没有连接, 任意键返回 
 */
    printf("\n=== WiFi Disconnect ===\n");

    wifi_connection_status_t wifi_status;
    wifi_station_get_status(&wifi_status);

    if (wifi_status.state == WIFI_STATE_CONNECTED) {
        printf("WiFi is connected to [%s]\n", wifi_status.ssid);
        printf("--------\n");
        printf("Are to sure to disconnect? (Y/n): ");
        fflush(stdout);
        s_cli_sm.sub_step = _SUB_STEP_CONFIRM_DISCONNECT;
        return;
    } else {
        printf("WiFi is not connected\n");
        printf("--------\n");
        printf("Input [Enter] to return\n");
    }
}


static void show_wifi_networks(void)
{    
    cli_state_machine_t *sm = &s_cli_sm;
    
    sm->wifi_record_count = 0;
    uint8_t count = sizeof(sm->wifi_records) / sizeof(sm->wifi_records[0]);
    
    esp_err_t ret = wifi_station_get_records(sm->wifi_records, &count);
    if (ret == ESP_OK) {
        sm->wifi_record_count = count;
        printf("\n=== WiFi Networks ===\n");
        if (count == 0) {
            printf("No WiFi networks found\n");
        } else {
            printf("Found %d networks:\n", count);
            for (uint8_t i = 0; i < count; i++) {
                printf("%d. %-32s seq:%" PRIu32 "\n", i + 1, sm->wifi_records[i].ssid, sm->wifi_records[i].sequence);
            }
        }
    } else {
        printf("***Failed to get WiFi networks: %s\n", esp_err_to_name(ret));
    }
    printf("--------\n");
    printf("Input [Enter] to return\n");
}

static void show_wifi_network_delete_menu(void)
{
    cli_state_machine_t *sm = &s_cli_sm;
    
    sm->wifi_record_count = 0;
    uint8_t count = sizeof(sm->wifi_records) / sizeof(sm->wifi_records[0]);
    
    esp_err_t ret = wifi_station_get_records(sm->wifi_records, &count);    
    if (ret != ESP_OK) {
        printf("***Failed to get WiFi networks: %s\n", esp_err_to_name(ret));
        printf("--------\n");
        printf("Input [Enter] to return\n");        
        return;
    }

    sm->wifi_record_count = count;
    printf("\n=== Delete WiFi Network ===\n");

    if (count == 0) {
        printf("No WiFi network found\n");
        printf("--------\n");
        printf("Input [Enter] to return\n");     
        return ;        
    } else {
        printf("Found %d networks:\n", count);
        for (uint8_t i = 0; i < count; i++) {
            printf("%d. %-32s\n", i + 1, sm->wifi_records[i].ssid);
        }
        printf("--------\n");
        printf("0. Exit\n");
        printf("--------\n");
        printf("Please input network index: ");
        fflush(stdout);
        s_cli_sm.sub_step = _SUB_STEP_DELETE_NETWORK;
        return;
    }
}

static void show_wifi_network_add_menu(void)
{
    printf("\n=== Add WiFi Network ===\n");
    printf("Please input SSID: ");
    fflush(stdout);
    s_cli_sm.sub_step = _SUB_STEP_ADD_NETWORK_INPUT_SSID;
}


static void handle_main_menu(const char *input)
{
    cli_state_machine_t *sm = &s_cli_sm;

    if (!input) {
        show_main_menu();
        return;
    }

    int menu_id = atoi(input);
    switch (menu_id) {
        case 1:
            sm->state = CLI_STATE_STATUS;
            show_status();
            break;
        case 2:
            sm->state = CLI_STATE_WIFI;
            sm->sub_state = CLI_SUB_STATE_NONE;
            show_wifi_menu();
            break;
        case 3:
            sm->state = CLI_STATE_UART;
            show_uart_baudrate_menu();
            break;
        case 4:
            sm->state = CLI_STATE_ABOUT;
            show_about_menu();
            break;
        default:
            printf("***Invalid input: %s\n", input);
            show_main_menu();
            break;
    }
}

static void handle_wifi_menu(const char *input)
{

/*
sub_state == NONE:
=== WiFi Setting ===
1. Scan
2. Connect
3. Disconnect
4. List Records
5. Delete Record
6. Add Record
0. Exit
--------
处理逻辑:
- 0 退出当前菜单,回到主菜单
- 1-6, 根据输入的数字, 执行相应的操作
- 输入非0数字, 则提示输入错误, 并回到当前菜单
*/
    cli_state_machine_t *sm = &s_cli_sm;

    if (!input) {
        show_wifi_menu();
        return;
    }

    int menu_id = atoi(input);
    switch (menu_id) {
        case 0:
            return_to_main_menu(input);
            break;
        case 1:
            sm->sub_state = CLI_SUB_STATE_WIFI_AUTO_CONNECT;
            active_auto_connect_once();
            break;
        case 2:       
            sm->sub_state = CLI_SUB_STATE_WIFI_SCAN_CONNECT;
            start_wifi_scan_and_connect();
            break;
        case 3:
            sm->sub_state = CLI_SUB_STATE_WIFI_DISCONNECT;
            start_wifi_disconnect();
            break;
        case 4:
            sm->sub_state = CLI_SUB_STATE_WIFI_LIST_NETWORKS;
            show_wifi_networks();
            break;
        case 5:
            sm->sub_state = CLI_SUB_STATE_WIFI_DELETE_NETWORK;
            show_wifi_network_delete_menu();
            break;
        case 6:
            sm->sub_state = CLI_SUB_STATE_WIFI_ADD_NETWORK;
            show_wifi_network_add_menu();
            break;
        default:
            printf("***Invalid input: %s\n", input);
            show_wifi_menu();
            break;
    }
}

static void handle_wifi_scan_and_connect(const char *input)
{
    /*
    进入这里,以下逻辑:
    扫描不成功, 任意输入, 回到上一级菜单, wifi menu
    扫描成功, 但输入0, 回到上一级菜单, wifi menu
    扫描成功, 输入有效数数, 开始连接, 
    扫描成功, 输入无效数据, 回到上一级菜单, wifi menu
    只有回车, 重新提示输入:
    */

    cli_state_machine_t *sm = &s_cli_sm;

    if (!sm->scan_success || sm->connect_done) {
        sm->sub_state = CLI_SUB_STATE_NONE;
        show_wifi_menu();
        return;
    }

    if (!input) {
        if (sm->sub_step == _SUB_STEP_CHOOSE_NETWORK) {
            printf("Please input network index: ");
        } else if (sm->sub_step == _SUB_STEP_INPUT_PASSWORD) {
            printf("Please input password: ");
        }
        fflush(stdout);
        return;
    }

    if (sm->sub_step == _SUB_STEP_CHOOSE_NETWORK) 
    {
        int index = atoi(input);
        if (index == 0) {
            sm->sub_state = CLI_SUB_STATE_NONE;
            show_wifi_menu();
            return;
        } else if (index < 0 || index > sm->scan_count) {
            printf("***Invalid input: %s\n", input);
            printf("Please input network index: ");
            fflush(stdout);
            return;
        } else {
            sm->input_index = index - 1;
            printf("\nSelected network: %s\n", sm->scan_results[sm->input_index].ssid);
            printf("Please input password: ");
            fflush(stdout);
            sm->sub_step = _SUB_STEP_INPUT_PASSWORD;
            return;
        }
    }
    else if (sm->sub_step == _SUB_STEP_INPUT_PASSWORD) {
        strncpy(sm->input_buffer, input, sizeof(sm->input_buffer) - 1);
        sm->input_buffer[sizeof(sm->input_buffer) - 1] = '\0';

        printf("\nStart connecting to %s...\n", sm->scan_results[sm->input_index].ssid);
        esp_err_t ret = wifi_station_connect(sm->scan_results[sm->input_index].ssid, sm->input_buffer);
        if (ret == ESP_OK) {
            printf("Connected to %s successfully\n", sm->scan_results[sm->input_index].ssid);
        }else {
            printf("***Failed to connect to %s: %s\n", sm->scan_results[sm->input_index].ssid, esp_err_to_name(ret));
        }
        printf("--------\n");
        printf("Input [Enter] to return\n");
        sm->connect_done = true;
        return;
    }

}

static void handle_auto_connect_once(const char *input)
{
    // 任意输入,回到上一级菜单 
    s_cli_sm.sub_state = CLI_SUB_STATE_NONE;
    show_wifi_menu();
}

static void handle_wifi_disconnect(const char *input)
{
    cli_state_machine_t *sm = &s_cli_sm;

    if (sm->sub_step == _SUB_STEP_CONFIRM_DISCONNECT) {
        // 输入Y, 断开连接
        if (input && (strcmp(input, "Y") == 0 || strcmp(input, "y") == 0)) {
            esp_err_t ret = wifi_station_disconnect();
            if (ret == ESP_OK) {
                printf("WiFi disconnected successfully\n");
            } else {
                printf("***Failed to disconnect WiFi: %s\n", esp_err_to_name(ret));
            }
        } else {
            printf("You are not sure to disconnect\n");
        }
    }

    sm->sub_state = CLI_SUB_STATE_NONE;
    show_wifi_menu();
}


static void handle_wifi_networks(const char *input)
{
    // 任意输入,回到上一级菜单 
    s_cli_sm.sub_state = CLI_SUB_STATE_NONE;
    show_wifi_menu();
}


static void handle_wifi_network_delete(const char *input)
{
    cli_state_machine_t *sm = &s_cli_sm;

    if (!input) {
        if (sm->sub_step == _SUB_STEP_DELETE_NETWORK) {
            printf("Please input network index: ");
            fflush(stdout);
        } else {
            sm->sub_state = CLI_SUB_STATE_NONE;
            show_wifi_menu();
        }
        return;
    }

    if (sm->sub_step == _SUB_STEP_DELETE_NETWORK) {
        int index = atoi(input);
        if (index == 0) {
            sm->sub_state = CLI_SUB_STATE_NONE;
            show_wifi_menu();
            return;
        } else if (index < 0 || index > sm->wifi_record_count) {
            printf("***Invalid input: %s\n", input);
            printf("Please input network index: ");
            fflush(stdout);
            return;
        } else {
            esp_err_t ret = wifi_station_delete_record(sm->wifi_records[index - 1].ssid);
            if (ret == ESP_OK) {
                printf("Deleted WiFi network: %s\n", sm->wifi_records[index - 1].ssid);
            } else {
                printf("***Failed to delete WiFi network: %s\n", esp_err_to_name(ret));
            }
            printf("--------\n");
            printf("Input [Enter] to return\n");
            sm->sub_step = 0;  // 清除子步骤,等待回车返回
            return;
        }
    }
    
    // 如果已经删除完成,任意输入返回上级菜单
    sm->sub_state = CLI_SUB_STATE_NONE;
    show_wifi_menu();
}

static void handle_wifi_network_add(const char *input)
{
    cli_state_machine_t *sm = &s_cli_sm;

    if (!input) {
        if (sm->sub_step == _SUB_STEP_ADD_NETWORK_INPUT_SSID) {
            printf("Please input SSID: ");
            fflush(stdout);
        } else if (sm->sub_step == _SUB_STEP_ADD_NETWORK_INPUT_PASSWORD) {
            printf("Please input password (or press Enter for none): ");
            fflush(stdout);
        } else {
            sm->sub_state = CLI_SUB_STATE_NONE;
            show_wifi_menu();
        }
        return;
    }

    if (sm->sub_step == _SUB_STEP_ADD_NETWORK_INPUT_SSID) {
        if (strlen(input) >= WIFI_STATION_SSID_LEN) {
            printf("***SSID too long (max %d characters)\n", WIFI_STATION_SSID_LEN - 1);
            printf("Please input SSID: ");
            fflush(stdout);
            return;
        }
        // 保存SSID
        strncpy(sm->input_buffer, input, sizeof(sm->input_buffer) - 1);
        sm->input_buffer[sizeof(sm->input_buffer) - 1] = '\0';
        
        // 进入密码输入阶段
        printf("Please input password (or press Enter for none): ");
        fflush(stdout);
        sm->sub_step = _SUB_STEP_ADD_NETWORK_INPUT_PASSWORD;
        return;
    } 
    else if (sm->sub_step == _SUB_STEP_ADD_NETWORK_INPUT_PASSWORD) 
    {
        char password[WIFI_STATION_PASSWORD_LEN] = {0};
        if (strlen(input) > 0) {
            if (strlen(input) >= WIFI_STATION_PASSWORD_LEN) {
                printf("***Password too long (max %d characters)\n", WIFI_STATION_PASSWORD_LEN - 1);
                printf("Please input password (or press Enter for none): ");
                fflush(stdout);
                return;
            }
            strncpy(password, input, sizeof(password) - 1);
        }
        
        // 添加WiFi记录
        esp_err_t ret = wifi_station_add_record(sm->input_buffer, password);
        if (ret == ESP_OK) {
            printf("Added WiFi network: %s\n", sm->input_buffer);
        } else {
            printf("***Failed to add WiFi network: %s\n", esp_err_to_name(ret));
        }
        printf("--------\n");
        printf("Input [Enter] to return\n");
        sm->sub_step = 0;  // 清除子步骤,等待回车返回
        return;
    }
    
    // 如果已经添加完成,任意输入返回上级菜单
    sm->sub_state = CLI_SUB_STATE_NONE;
    show_wifi_menu();
}

static void handle_set_uart_baudrate(const char *input)
{
    /*
=== UART Baudrate Setting ===
1. 9600
2. 19200
3. 38400
4. 57600
5. 115200
6. 230400
7. 460800
8. 921600 <
9. 1500000
0. Exit
--------
 - 0 退出当前菜单,回到主菜单 
 - 1-9 设置波特率, 设置后回到主菜单
 - 输入非0数字, 则提示输入错误, 并回到当前菜单
     */
    cli_state_machine_t *sm = &s_cli_sm;

    if (!input) {
        show_uart_baudrate_menu();
        return;
    }

    int menu_id = atoi(input);

    if (menu_id == 0) {
        sm->state = CLI_STATE_MAIN;
        show_main_menu();
        return;
    }

    if (menu_id > 0 && menu_id <= sizeof(s_supported_baudrates) / sizeof(s_supported_baudrates[0])) {
        set_uart_baudrate(s_supported_baudrates[menu_id - 1]);
        printf("Set baudrate to %" PRIu32 " success\n", s_supported_baudrates[menu_id - 1]);
        sm->state = CLI_STATE_MAIN;
        show_main_menu();
        return;
    }

    printf("***Invalid input: %s\n", input);
    show_uart_baudrate_menu();
}

static void return_to_main_menu(const char *input)
{
    cli_state_machine_t *sm = &s_cli_sm;

    sm->state = CLI_STATE_MAIN;
    sm->sub_state = CLI_SUB_STATE_NONE;
    show_main_menu();
}
