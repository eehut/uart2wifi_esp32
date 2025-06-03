#ifndef HEX_DUMP_H
#define HEX_DUMP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 以十六进制格式打印数据
 * 
 * 输出格式:
 * 偏移量  十六进制字节(16个/行)  ASCII字符
 * 例如: 00000000  48 65 6c 6c 6f 20 57 6f  72 6c 64 21 0a 00 00 00  |Hello World!....|
 * 
 * @param data 要打印的数据指针
 * @param len 数据长度
 * @param prefix 可选的行前缀字符串，可以为NULL
 */
void hex_dump(const void *data, size_t len, const char *prefix);


#ifdef __cplusplus
}
#endif

#endif // HEX_DUMP_H
