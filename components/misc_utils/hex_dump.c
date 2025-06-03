#include "hex_dump.h"
#include <stdio.h>
#include <ctype.h>

void hex_dump(const void *data, size_t len, const char *prefix)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t i, j;
    
    if (!data) {
        printf("hex_dump: NULL data pointer\n");
        return;
    }

    // 打印可选前缀
    if (prefix) {
        printf("%s\n", prefix);
    }

    for (i = 0; i < len; i += 16) {

        // 打印偏移量 (8位十六进制，前导零)
        printf("%08zx  ", i);
        
        // 打印十六进制字节 (前8个字节)
        for (j = 0; j < 8; j++) {
            if (i + j < len) {
                printf("%02x ", bytes[i + j]);
            } else {
                printf("   ");  // 空白填充
            }
        }
        
        // 中间分隔空格
        printf(" ");
        
        // 打印十六进制字节 (后8个字节)
        for (j = 8; j < 16; j++) {
            if (i + j < len) {
                printf("%02x ", bytes[i + j]);
            } else {
                printf("   ");  // 空白填充
            }
        }
        
        // 打印ASCII字符部分
        printf(" |");
        for (j = 0; j < 16; j++) {
            if (i + j < len) {
                uint8_t byte = bytes[i + j];
                if (isprint(byte)) {
                    printf("%c", byte);
                } else {
                    printf(".");  // 非可打印字符用点号代替
                }
            } else {
                printf(" ");  // 空白填充
            }
        }
        printf("|\n");
    }
}
