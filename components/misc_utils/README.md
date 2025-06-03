# misc_utils 组件

这是一个包含各种实用工具函数的ESP32组件。

## 功能特性

### hex_dump - 十六进制数据打印

提供了标准的十六进制数据打印功能，支持：

- 任意数据的十六进制显示
- 每行显示16个字节
- 显示偏移量（8位十六进制）
- ASCII字符显示（可打印字符正常显示，非可打印字符用'.'代替）
- 支持可选的行前缀

## API 参考

### hex_dump()

```c
void hex_dump(const void *data, size_t len, const char *prefix);
```

**参数：**
- `data`: 要打印的数据指针
- `len`: 数据长度
- `prefix`: 可选的行前缀字符串，可以为NULL


## 使用示例

```c
#include "hex_dump.h"

void app_main(void)
{
    const char test_data[] = "Hello World!\x01\x02\x03\x04";

    // 带前缀使用
    hex_dump(test_data, sizeof(test_data) - 1, "DEBUG: ");
}
```

## 输出示例

```
00000000  48 65 6c 6c 6f 20 57 6f  72 6c 64 21 01 02 03 04  |Hello World!....|
DEBUG: 00000000  48 65 6c 6c 6f 20 57 6f  72 6c 64 21 01 02 03 04  |Hello World!....|
```

## 集成方法

1. 将此组件添加到你的ESP32项目的`components`目录中
2. 在需要使用此组件的组件的`CMakeLists.txt`中添加依赖：

```cmake
idf_component_register(
    SRCS "your_source.c"
    INCLUDE_DIRS "include"
    REQUIRES misc_utils  # 添加这一行
)
```

3. 在源码中包含头文件：

```c
#include "hex_dump.h"
```

## 技术细节

- 使用标准C库的`printf()`函数输出
- 使用`isprint()`函数判断可打印字符
- 每行输出格式：`偏移量  hex_bytes  |ascii_chars|`
- 内存安全：检查空指针并安全处理边界情况
- 兼容所有ESP32系列芯片

## 许可证

本组件遵循与ESP-IDF相同的许可证。
