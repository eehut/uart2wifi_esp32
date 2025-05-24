# WiFi Station 组件

这是一个功能完整的 ESP32 WiFi Station 独立组件，提供了完整的 WiFi 客户端功能。

## 功能特点

1. **NVS 存储**: 支持最多 8 个 WiFi 连接记录
2. **自动连接**: 组件初始化时自动连接到信号最强的已保存网络  
3. **后台扫描**: 独立任务实现后台网络扫描和自动重连
4. **连接管理**: 自动添加、更新和删除连接记录
5. **完整的API**: 提供丰富的 API 接口供外部使用

## API 接口

### 核心接口
- `wifi_station_init()` - 初始化组件
- `wifi_station_deinit()` - 反初始化组件

### 连接管理  
- `wifi_station_connect(ssid, password)` - 连接到指定网络
- `wifi_station_disconnect()` - 断开当前连接

### 状态查询
- `wifi_station_get_status(status)` - 获取连接状态
- `wifi_station_scan_networks(networks, count)` - 扫描网络

### 记录管理
- `wifi_station_get_records(records, count)` - 获取连接记录
- `wifi_station_add_record(ssid, password)` - 添加记录
- `wifi_station_delete_record(ssid)` - 删除记录

## 使用方法

```c
#include "wifi_station.h"

// 初始化
nvs_flash_init();
esp_event_loop_create_default();
wifi_station_init();

// 获取状态
wifi_connection_status_t status;
wifi_station_get_status(&status);

// 手动连接
wifi_station_connect("MyWiFi", "password");
```

详细使用示例请参考 `wifi_station_example.c` 文件。 