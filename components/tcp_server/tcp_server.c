#include "tcp_server.h"
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "hex_dump.h"

static const char *TAG = "tcp_server";

/**
 * @brief TCP服务器内部结构体
 */
struct tcp_server_s {
    int listen_socket;                      // 监听socket
    uint16_t port;                          // 监听端口
    uint32_t max_clients;                   // 最大客户端数
    tcp_client_t *clients;                  // 客户端数组
    uint32_t client_count;                  // 当前客户端数量
    bool running;                           // 服务器运行状态
    TaskHandle_t server_task;               // 服务器任务句柄
    SemaphoreHandle_t clients_mutex;        // 客户端列表互斥锁
    
    // 回调函数
    tcp_recv_callback_t recv_callback;
    tcp_connect_callback_t connect_callback;
    tcp_disconnect_callback_t disconnect_callback;
    void *user_ctx;
    
    // 任务配置
    uint32_t stack_size;
    uint32_t task_priority;
    bool tx_verbose;
    bool rx_verbose;
};

/**
 * @brief 设置socket为非阻塞模式
 */
static esp_err_t set_socket_nonblocking(int socket_fd) {
    int flags = fcntl(socket_fd, F_GETFL, 0);
    if (flags < 0) {
        ESP_LOGE(TAG, "Failed to get socket flags: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    if (fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        ESP_LOGE(TAG, "Failed to set socket to non-blocking mode: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    return ESP_OK;
}

/**
 * @brief 查找空闲的客户端槽位
 */
static int find_free_client_slot(tcp_server_handle_t server) {
    for (int i = 0; i < server->max_clients; i++) {
        if (server->clients[i].socket_fd == -1) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 根据socket查找客户端
 */
// static tcp_client_t* find_client_by_socket(tcp_server_handle_t server, int socket_fd) {
//     for (int i = 0; i < server->max_clients; i++) {
//         if (server->clients[i].socket_fd == socket_fd) {
//             return &server->clients[i];
//         }
//     }
//     return NULL;
// }

/**
 * @brief 移除客户端
 */
static void remove_client(tcp_server_handle_t server, tcp_client_t *client) {
    if (client == NULL) {
        return;
    }
    
    ESP_LOGI(TAG, "client(%s:%d) disconnected", 
             ipaddr_ntoa(&client->ip_addr), client->port);
    
    // 调用断开连接回调
    if (server->disconnect_callback) {
        server->disconnect_callback(client, server->user_ctx);
    }
    
    // 关闭socket
    if (client->socket_fd != -1) {
        close(client->socket_fd);
        client->socket_fd = -1;
    }
    
    // 清除客户端信息
    memset(client, 0, sizeof(tcp_client_t));
    client->socket_fd = -1;
    
    if (server->client_count > 0) {
        server->client_count--;
    }
}

/**
 * @brief 处理新客户端连接
 */
static esp_err_t handle_new_connection(tcp_server_handle_t server) {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);
    
    int client_socket = accept(server->listen_socket, (struct sockaddr*)&client_addr, &addr_len);
    if (client_socket < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return ESP_OK; // No new connection in non-blocking mode
        }
        ESP_LOGE(TAG, "Failed to accept client connection: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    // Check if maximum client limit reached
    if (server->client_count >= server->max_clients) {
        ESP_LOGW(TAG, "Maximum client connections reached: %d", server->max_clients);
        close(client_socket);
        return ESP_OK;
    }
    
    // Find free slot
    int slot = find_free_client_slot(server);
    if (slot < 0) {
        ESP_LOGE(TAG, "No available client slots");
        close(client_socket);
        return ESP_FAIL;
    }
    
    // 设置客户端socket为非阻塞
    if (set_socket_nonblocking(client_socket) != ESP_OK) {
        close(client_socket);
        return ESP_FAIL;
    }
    
    // 初始化客户端信息
    tcp_client_t *client = &server->clients[slot];
    client->socket_fd = client_socket;
    client->port = ntohs(client_addr.sin_port);
    ip_addr_set_ip4_u32(&client->ip_addr, client_addr.sin_addr.s_addr);
    client->user_data = NULL;
    
    server->client_count++;
    
    ESP_LOGI(TAG, "New client(%s:%d) connected (slot: %d, total: %d)", 
             ipaddr_ntoa(&client->ip_addr), client->port, slot, server->client_count);
    
    // 调用连接回调
    if (server->connect_callback) {
        server->connect_callback(client, server->user_ctx);
    }
    
    return ESP_OK;
}

/**
 * @brief 处理客户端数据
 */
static esp_err_t handle_client_data(tcp_server_handle_t server, tcp_client_t *client) {
    uint8_t buffer[1024];
    
    int bytes_received = recv(client->socket_fd, buffer, sizeof(buffer), 0);
    if (bytes_received > 0) {
        
        if (server->rx_verbose) {
            char client_info[64];
            sprintf(client_info, "rx from client(%s:%d)[len=%d]:", ipaddr_ntoa(&client->ip_addr), client->port, bytes_received);
            hex_dump(buffer, bytes_received, client_info);
        }

        // 调用数据接收回调
        if (server->recv_callback) {
            server->recv_callback(client, buffer, bytes_received, server->user_ctx);
        }
    } else if (bytes_received == 0) {
        // 客户端主动断开连接
        remove_client(server, client);
    } else {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            // Error occurred, remove client
            ESP_LOGW(TAG, "Failed to receive data from client: %s", strerror(errno));
            remove_client(server, client);
        }
    }
    
    return ESP_OK;
}

/**
 * @brief TCP服务器主任务
 */
static void tcp_server_task(void *pvParameters) {
    tcp_server_handle_t server = (tcp_server_handle_t)pvParameters;
    fd_set read_fds;
    int max_fd;
    struct timeval timeout;
    
    ESP_LOGI(TAG, "TCP server task started");
    
    while (server->running) {
        FD_ZERO(&read_fds);
        FD_SET(server->listen_socket, &read_fds);
        max_fd = server->listen_socket;
        
        // 加锁保护客户端列表
        if (xSemaphoreTake(server->clients_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
            // 添加所有客户端socket到监听集合
            for (int i = 0; i < server->max_clients; i++) {
                if (server->clients[i].socket_fd != -1) {
                    FD_SET(server->clients[i].socket_fd, &read_fds);
                    if (server->clients[i].socket_fd > max_fd) {
                        max_fd = server->clients[i].socket_fd;
                    }
                }
            }
            xSemaphoreGive(server->clients_mutex);
        }
        
        // 设置超时时间
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000; // 100ms
        
        int activity = select(max_fd + 1, &read_fds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            if (errno != EINTR) {
                ESP_LOGE(TAG, "select failed: %s", strerror(errno));
                break;
            }
            continue;
        }
        
        if (activity == 0) {
            continue; // 超时，继续下一次循环
        }
        
        // 检查监听socket是否有新连接
        if (FD_ISSET(server->listen_socket, &read_fds)) {
            if (xSemaphoreTake(server->clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                handle_new_connection(server);
                xSemaphoreGive(server->clients_mutex);
            }
        }
        
        // 检查客户端socket是否有数据
        if (xSemaphoreTake(server->clients_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (int i = 0; i < server->max_clients; i++) {
                if (server->clients[i].socket_fd != -1 && 
                    FD_ISSET(server->clients[i].socket_fd, &read_fds)) {
                    handle_client_data(server, &server->clients[i]);
                }
            }
            xSemaphoreGive(server->clients_mutex);
        }
    }
    
    ESP_LOGI(TAG, "TCP server task exited");
    vTaskDelete(NULL);
}

tcp_server_handle_t tcp_server_create(const tcp_server_config_t *config, esp_err_t *err) {
    if (config == NULL || err == NULL) {
        if (err) {
            *err = ESP_ERR_INVALID_ARG;
        }
        return NULL;
    }
    
    if (config->port == 0 || config->max_clients == 0 || config->recv_callback == NULL) {
        if (err) {
            *err = ESP_ERR_INVALID_ARG;
        }
        return NULL;
    }
    
    // Allocate server structure memory
    tcp_server_handle_t server = calloc(1, sizeof(struct tcp_server_s));
    if (server == NULL) {
        ESP_LOGE(TAG, "Failed to allocate server memory");
        if (err) {
            *err = ESP_ERR_NO_MEM;
        }
        return NULL;
    }
    
    // Allocate client array memory
    server->clients = calloc(config->max_clients, sizeof(tcp_client_t));
    if (server->clients == NULL) {
        ESP_LOGE(TAG, "Failed to allocate client array memory");
        free(server);
        if (err) {
            *err = ESP_ERR_NO_MEM;
        }
        return NULL;
    }
    
    // Initialize client array
    for (int i = 0; i < config->max_clients; i++) {
        server->clients[i].socket_fd = -1;
    }
    
    // Create mutex
    server->clients_mutex = xSemaphoreCreateMutex();
    if (server->clients_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(server->clients);
        free(server);
        if (err) {
            *err = ESP_ERR_NO_MEM;
        }
        return NULL;
    }
    
    // Initialize server configuration
    server->port = config->port;
    server->max_clients = config->max_clients;
    server->recv_callback = config->recv_callback;
    server->connect_callback = config->connect_callback;
    server->disconnect_callback = config->disconnect_callback;
    server->user_ctx = config->user_ctx;
    server->stack_size = config->stack_size > 0 ? config->stack_size : 4096;
    server->task_priority = config->task_priority > 0 ? config->task_priority : 5;
    server->tx_verbose = config->verbose;
    server->rx_verbose = config->verbose;
    server->listen_socket = -1;
    server->client_count = 0;
    server->running = false;
    server->server_task = NULL;

    if (err) {
        *err = ESP_OK;
    }
    
    ESP_LOGI(TAG, "TCP server created successfully, port: %d, max clients: %d", 
             server->port, server->max_clients);
    return server;
}

esp_err_t tcp_server_start(tcp_server_handle_t server_handle) {
    if (server_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    tcp_server_handle_t server = server_handle;
    
    if (server->running) {
        ESP_LOGW(TAG, "TCP server is already running");
        return ESP_OK;
    }
    
    // Create listening socket
    server->listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->listen_socket < 0) {
        ESP_LOGE(TAG, "Failed to create listening socket: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(server->listen_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        ESP_LOGW(TAG, "Failed to set SO_REUSEADDR: %s", strerror(errno));
    }
    
    // Set to non-blocking mode
    if (set_socket_nonblocking(server->listen_socket) != ESP_OK) {
        close(server->listen_socket);
        server->listen_socket = -1;
        return ESP_FAIL;
    }
    
    // Bind address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(server->port);
    
    if (bind(server->listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        ESP_LOGE(TAG, "Failed to bind address: %s", strerror(errno));
        close(server->listen_socket);
        server->listen_socket = -1;
        return ESP_FAIL;
    }
    
    // Start listening
    if (listen(server->listen_socket, 5) < 0) {
        ESP_LOGE(TAG, "Failed to start listening: %s", strerror(errno));
        close(server->listen_socket);
        server->listen_socket = -1;
        return ESP_FAIL;
    }
    
    // Start server task
    server->running = true;
    BaseType_t result = xTaskCreate(tcp_server_task, "tcp_server", 
                                   server->stack_size, server, 
                                   server->task_priority, &server->server_task);
    
    if (result != pdPASS) {
        ESP_LOGE(TAG, "Failed to create server task");
        server->running = false;
        close(server->listen_socket);
        server->listen_socket = -1;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "TCP server started successfully, listening on port: %d", server->port);
    return ESP_OK;
}

esp_err_t tcp_server_stop(tcp_server_handle_t server_handle) {
    if (server_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    tcp_server_handle_t server = server_handle;
    
    if (!server->running) {
        ESP_LOGW(TAG, "TCP server is already stopped");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Stopping TCP server...");
    
    // Set stop flag
    server->running = false;
    
    // Wait for task to exit
    if (server->server_task != NULL) {
        // Wait for task to exit, up to 5 seconds
        for (int i = 0; i < 50 && eTaskGetState(server->server_task) != eDeleted; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        server->server_task = NULL;
    }
    
    // Close all client connections
    if (xSemaphoreTake(server->clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (int i = 0; i < server->max_clients; i++) {
            if (server->clients[i].socket_fd != -1) {
                remove_client(server, &server->clients[i]);
            }
        }
        xSemaphoreGive(server->clients_mutex);
    }
    
    // Close listening socket
    if (server->listen_socket != -1) {
        close(server->listen_socket);
        server->listen_socket = -1;
    }
    
    ESP_LOGI(TAG, "TCP server stopped");
    return ESP_OK;
}

esp_err_t tcp_server_destroy(tcp_server_handle_t server_handle) {
    if (server_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    tcp_server_handle_t server = server_handle;
    
    // First stop server
    tcp_server_stop(server);
    
    // Destroy mutex
    if (server->clients_mutex != NULL) {
        vSemaphoreDelete(server->clients_mutex);
    }
    
    // Free memory
    if (server->clients != NULL) {
        free(server->clients);
    }
    free(server);
    
    ESP_LOGI(TAG, "TCP server destroyed");
    return ESP_OK;
}

esp_err_t tcp_server_send_to_client(tcp_server_handle_t server_handle, tcp_client_t *client, 
                                   const uint8_t *data, size_t len) {
    if (server_handle == NULL || client == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (client->socket_fd == -1) {
        return ESP_ERR_INVALID_STATE;
    }

    if (server_handle->tx_verbose) {
        char client_info[64];
        sprintf(client_info, "tx to client(%s:%d)[len=%d]:", ipaddr_ntoa(&client->ip_addr), client->port, len);
        hex_dump(data, len, client_info);
    }
    
    int bytes_sent = send(client->socket_fd, data, len, 0);
    if (bytes_sent < 0) {
        ESP_LOGE(TAG, "Failed to send data: %s", strerror(errno));
        return ESP_FAIL;
    }
    
    if (bytes_sent != len) {
        ESP_LOGW(TAG, "Incomplete data sent: %d/%d", bytes_sent, len);
        return ESP_ERR_INVALID_SIZE;
    }
    
    return ESP_OK;
}

esp_err_t tcp_server_broadcast(tcp_server_handle_t server_handle, const uint8_t *data, size_t len) {
    if (server_handle == NULL || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    tcp_server_handle_t server = server_handle;
    esp_err_t result = ESP_OK;
    
    if (xSemaphoreTake(server->clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        for (int i = 0; i < server->max_clients; i++) {
            if (server->clients[i].socket_fd != -1) {
                esp_err_t send_result = tcp_server_send_to_client(server, &server->clients[i], data, len);
                if (send_result != ESP_OK) {
                    result = send_result;
                }
            }
        }
        xSemaphoreGive(server->clients_mutex);
    } else {
        return ESP_ERR_TIMEOUT;
    }
    
    return result;
}

int tcp_server_get_client_count(tcp_server_handle_t server_handle) {
    if (server_handle == NULL) {
        return -1;
    }
    
    return server_handle->client_count;
}

esp_err_t tcp_server_disconnect_client(tcp_server_handle_t server_handle, tcp_client_t *client) {
    if (server_handle == NULL || client == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    
    tcp_server_handle_t server = server_handle;
    
    if (xSemaphoreTake(server->clients_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        remove_client(server, client);
        xSemaphoreGive(server->clients_mutex);
    } else {
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
} 

void tcp_server_set_verbose(tcp_server_handle_t server_handle, bool tx_verbose, bool rx_verbose) {
    if (server_handle == NULL) {
        return;
    }
    
    tcp_server_handle_t server = server_handle;
    server->tx_verbose = tx_verbose;
    server->rx_verbose = rx_verbose;
}