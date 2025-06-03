# TCP服务器组件

这是一个用于ESP32的独立TCP服务器组件，提供了简单易用的TCP服务器功能。

## 特性

- 支持多客户端连接
- 非阻塞I/O操作
- 线程安全
- 回调函数机制处理客户端事件
- 支持单播和广播数据发送
- 完整的客户端生命周期管理

## API接口

### 基本类型

```c
// 客户端对象结构体
typedef struct {
    int socket_fd;           // 客户端socket文件描述符
    ip_addr_t ip_addr;       // 客户端IP地址
    uint16_t port;           // 客户端端口号
    void *user_data;         // 用户自定义数据
} tcp_client_t;

// 服务器句柄
typedef struct tcp_server_s *tcp_server_handle_t;
```

### 回调函数

```c
// 数据接收回调
typedef void (*tcp_recv_callback_t)(tcp_client_t *client, const uint8_t *data, size_t len, void *user_ctx);

// 客户端连接回调（可选）
typedef void (*tcp_connect_callback_t)(tcp_client_t *client, void *user_ctx);

// 客户端断开连接回调（可选）
typedef void (*tcp_disconnect_callback_t)(tcp_client_t *client, void *user_ctx);
```

### 配置结构体

```c
typedef struct {
    uint16_t port;                              // 监听端口
    uint32_t max_clients;                       // 最大客户端连接数
    tcp_recv_callback_t recv_callback;          // 数据接收回调函数（必需）
    tcp_connect_callback_t connect_callback;    // 客户端连接回调函数（可选）
    tcp_disconnect_callback_t disconnect_callback; // 客户端断开连接回调函数（可选）
    void *user_ctx;                             // 用户上下文
    uint32_t stack_size;                        // 任务栈大小，默认4096
    uint32_t task_priority;                     // 任务优先级，默认5
} tcp_server_config_t;
```

### 主要函数

```c
// 创建TCP服务器
esp_err_t tcp_server_create(const tcp_server_config_t *config, tcp_server_handle_t *server_handle);

// 启动TCP服务器
esp_err_t tcp_server_start(tcp_server_handle_t server_handle);

// 停止TCP服务器
esp_err_t tcp_server_stop(tcp_server_handle_t server_handle);

// 销毁TCP服务器
esp_err_t tcp_server_destroy(tcp_server_handle_t server_handle);

// 向指定客户端发送数据
esp_err_t tcp_server_send_to_client(tcp_server_handle_t server_handle, tcp_client_t *client, 
                                   const uint8_t *data, size_t len);

// 向所有客户端广播数据
esp_err_t tcp_server_broadcast(tcp_server_handle_t server_handle, const uint8_t *data, size_t len);

// 获取当前连接的客户端数量
int tcp_server_get_client_count(tcp_server_handle_t server_handle);

// 断开指定客户端连接
esp_err_t tcp_server_disconnect_client(tcp_server_handle_t server_handle, tcp_client_t *client);
```

## 使用示例

```c
#include "tcp_server.h"
#include "esp_log.h"

static const char *TAG = "tcp_example";
static tcp_server_handle_t server = NULL;

// 数据接收回调函数
void on_data_received(tcp_client_t *client, const uint8_t *data, size_t len, void *user_ctx) {
    ESP_LOGI(TAG, "从客户端 %s:%d 接收到数据，长度: %d", 
             ipaddr_ntoa(&client->ip_addr), client->port, len);
    
    // 回显数据给客户端
    tcp_server_send_to_client(server, client, data, len);
}

// 客户端连接回调函数
void on_client_connected(tcp_client_t *client, void *user_ctx) {
    ESP_LOGI(TAG, "新客户端连接: %s:%d", 
             ipaddr_ntoa(&client->ip_addr), client->port);
    
    // 发送欢迎消息
    const char *welcome = "欢迎连接到TCP服务器!\r\n";
    tcp_server_send_to_client(server, client, (uint8_t*)welcome, strlen(welcome));
}

// 客户端断开连接回调函数
void on_client_disconnected(tcp_client_t *client, void *user_ctx) {
    ESP_LOGI(TAG, "客户端断开连接: %s:%d", 
             ipaddr_ntoa(&client->ip_addr), client->port);
}

void app_main(void) {
    // 初始化WiFi等网络配置...
    
    // 配置TCP服务器
    tcp_server_config_t config = {
        .port = 8080,
        .max_clients = 5,
        .recv_callback = on_data_received,
        .connect_callback = on_client_connected,
        .disconnect_callback = on_client_disconnected,
        .user_ctx = NULL,
        .stack_size = 4096,
        .task_priority = 5
    };
    
    // 创建TCP服务器
    esp_err_t ret = tcp_server_create(&config, &server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建TCP服务器失败: %s", esp_err_to_name(ret));
        return;
    }
    
    // 启动TCP服务器
    ret = tcp_server_start(server);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "启动TCP服务器失败: %s", esp_err_to_name(ret));
        tcp_server_destroy(server);
        return;
    }
    
    ESP_LOGI(TAG, "TCP服务器已启动，监听端口: %d", config.port);
    
    // 主循环
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        int client_count = tcp_server_get_client_count(server);
        ESP_LOGI(TAG, "当前连接客户端数: %d", client_count);
        
        // 可以在这里添加定时广播等功能
        // const char *broadcast_msg = "定时广播消息\r\n";
        // tcp_server_broadcast(server, (uint8_t*)broadcast_msg, strlen(broadcast_msg));
    }
}
```

## 在CMakeLists.txt中使用

在主项目的CMakeLists.txt中添加对tcp_server组件的依赖：

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES tcp_server wifi nvs_flash
)
```

## 注意事项

1. 使用前需要确保WiFi或以太网已正确初始化并连接到网络
2. 回调函数在TCP服务器任务中执行，避免在回调中执行耗时操作
3. 如果需要在回调中执行耗时操作，建议使用队列或事件组与其他任务通信
4. 服务器会自动管理客户端连接，无需手动清理已断开的连接
5. 发送数据时要检查返回值，网络错误可能导致发送失败

## 错误处理

所有API函数都返回esp_err_t类型的错误码：

- `ESP_OK`: 操作成功
- `ESP_ERR_INVALID_ARG`: 参数无效
- `ESP_ERR_NO_MEM`: 内存不足
- `ESP_ERR_INVALID_STATE`: 状态无效
- `ESP_ERR_TIMEOUT`: 操作超时
- `ESP_FAIL`: 操作失败 