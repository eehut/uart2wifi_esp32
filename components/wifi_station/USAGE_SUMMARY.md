# WiFi Station 组件使用总结

## 快速开始

### 1. 添加到项目依赖

在您的主组件的 `CMakeLists.txt` 中添加：

```cmake
idf_component_register(SRCS "your_source_files.c"
                    INCLUDE_DIRS "."
                    REQUIRES wifi_station)
```

### 2. 基本初始化

```c
#include "wifi_station.h"

void app_main(void)
{
    // 必需的初始化步骤
    nvs_flash_init();
    esp_netif_init();
    esp_event_loop_create_default();
    
    // 初始化WiFi Station组件
    wifi_station_init();
    
    // 您的应用代码...
}
```

### 3. 常用API调用

```c
// 获取连接状态
wifi_connection_status_t status;
wifi_station_get_status(&status);

// 手动连接
wifi_station_connect("MyWiFi", "password");

// 扫描网络
wifi_network_info_t networks[20];
uint16_t count = 20;
wifi_station_scan_networks(networks, &count);

// 管理连接记录
wifi_station_add_record("WiFiName", "password");
wifi_station_delete_record("WiFiName");
```

## 核心特性

✅ **自动连接**: 初始化后自动连接到最优网络  
✅ **后台扫描**: 断线后自动扫描和重连  
✅ **记录管理**: 最多保存8个连接记录  
✅ **NVS存储**: 重启后记录依然有效  
✅ **线程安全**: 所有API都是线程安全的  

## 组件结构

```
components/wifi_station/
├── include/
│   ├── wifi_station.h          # 主要API头文件
│   └── wifi_station_example.h  # 示例代码头文件
├── wifi_station.c              # 主要实现文件
├── wifi_station_example.c      # 使用示例
├── CMakeLists.txt              # 构建配置
└── README.md                   # 详细文档
```

## 注意事项

1. 确保NVS分区有足够空间
2. 组件会创建后台任务，消耗约4KB栈空间
3. 密码明文存储，注意安全风险
4. 连接函数最多等待15秒

## 错误处理

所有API函数返回`esp_err_t`：
- `ESP_OK`: 成功
- `ESP_ERR_INVALID_ARG`: 参数错误  
- `ESP_ERR_INVALID_STATE`: 未初始化
- `ESP_FAIL`: 连接失败

## 获取帮助

查看详细文档：`components/wifi_station/README.md`  
参考示例代码：`components/wifi_station/wifi_station_example.c`  
集成示例：`main/wifi_integration_example.c` 