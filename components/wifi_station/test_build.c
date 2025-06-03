// 这是一个简单的编译测试文件
// 用于验证 WiFi Station 组件是否可以正确编译

#include "wifi_station.h"

// 编译测试函数
void test_wifi_station_compilation(void)
{
    // 这个函数只是为了测试编译，不会被实际调用
    wifi_connection_status_t status;
    wifi_network_info_t networks[10];
    uint16_t count = 10;
    wifi_connection_record_t records[WIFI_STATION_MAX_RECORDS];
    uint8_t record_count;
    
    // 测试所有API函数是否能正确编译
    (void)wifi_station_init(NULL, NULL);
    (void)wifi_station_get_status(&status);
    (void)wifi_station_scan_networks(networks, &count);
    (void)wifi_station_connect("test", "test");
    (void)wifi_station_disconnect();
    (void)wifi_station_get_records(records, &record_count);
    (void)wifi_station_add_record("test", "test");
    (void)wifi_station_delete_record("test");
    (void)wifi_station_deinit();
} 